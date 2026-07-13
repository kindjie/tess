# Audit 2026-07-11 — Remediation Plan

Executes the findings in `audit-2026-07-11.md`. Six workstreams, each a
feature branch off `main`, merged sequentially via PR after review.
Finding IDs (H*/M*/C*) refer to the audit report. Unlike audit2 this is
a performance-dominant audit, so the discipline differs in one way:
every perf workstream must post before/after numbers from the owning
bench family in its PR description, and a perf change without a
measurable win is reverted and logged in
`docs/planning/optimization-log.md` instead of merged.

| Branch | Findings | Scope |
| --- | --- | --- |
| `bench/audit3-integrity` | H1 M11a + bench lows | De-elide the five dead benches, recalibrate their caps, gate `parallel/`, median-of-reps gating, new sparse-residency bench family |
| `perf/audit3-batch-residency` | M1 M2 M3 | Batch member scatter, fingerprint probes + per-batch hoist, topology freshness probes, field early-termination spike. C4 is resolved by decision only (deferred/logged, see below) |
| `perf/audit3-tick-engine` | M4 M5 M6 M7 C1 C2 + popcount low | Queued planning scratch, ChunkMeta/flag layout, schedule phase partition, movement resolve-once; shared saturating axis_end; in_run_ guard |
| `perf/audit3-path-micro` | M9 M10 C3 + path lows | A* node-state layout experiment, touched_ counter, saturating f, entry-cost reorder, sparse-neighbor slot threading, portal-segment index |
| `perf/audit3-executor` | M8 | Worker-pool counter padding, chunked claiming, bounded wakeups, last-worker notify |
| `perf/audit3-storage-ecs` | M11b + ecs lows | Intrusive O(1) LRU eviction, occupancy hash cheapening, EnTT re-get elimination |

M11 is split into explicit halves: **M11a** = the sparse-residency
bench family (owned by integrity), **M11b** = the LRU eviction fix
(owned by storage-ecs, prerequisite M11a). Each half has exactly one
owner; M11 is done only when M11b merges.

## Fix approaches (decisions)

- **H1 (bench integrity first):** `ClobberMemory()` after the write
  loops and `DoNotOptimize` on the loop-variant object (page/scratch),
  per bench. The five de-elided benches then get fresh bootstrap
  ceilings (2x locally observed, 25 ns floor rule unchanged) and rejoin
  the 10-artifact recalibration queue — same policy as S11.3. This
  workstream merges first: every later perf claim is measured against
  these gates. `diagnostics/record_timing` is checked by disassembly
  and left alone if it is a genuine single-add fold.
- **M11a/M11b:** the bench family (M11a, integrity) lands first so the
  O(capacity) eviction scan is *measured ungated* before the intrusive
  LRU list (M11b, storage-ecs) uses it as before/after evidence. No fix
  without a failing-first-style baseline number.
- **M4 in full:** (a) reusable planning scratch AND a reusable
  `ExecutionReport` (per-op chunk vectors recycled across runs); (b)
  whole-domain ops consume a `ChunkDomain` range instead of
  materialized key vectors where executors permit; (c) the O(ops^2 x
  chunks) hazard/phase scan is explicitly **deferred by decision** —
  acceptable at current op counts; a chunk/field ownership index is the
  recorded upgrade path, logged in the optimization log with a
  trigger condition (op counts per frame reaching the hundreds).
  Sub-items (a)/(b) are in scope for tick-engine; (c) is not.
- **M1:** copy the counting-sort bucket pattern verbatim from
  `path_runtime.h:451-467` into `weighted_path_batch`; the scratch
  gains `group_offsets_`/`group_members_` vectors.
- **M2 (minimal, no semantic change):** slot-direct iteration inside
  `residency_fingerprint` (kill the 3-per-chunk probes), a
  slot-returning accessor for the topology freshness checks, and one
  fingerprint hoisted per batch/tick in the batch and runtime paths.
  **C4 (deferred/logged — no code lands for it in any workstream):**
  the 64-bit fingerprint stays; the monotonic per-world
  residency-epoch alternative is recorded in the optimization log as
  the upgrade path if collision risk ever matters. Rationale: epoch
  changes the scratch-reuse contract across worlds and deserves its
  own design round. Branch completion of batch-residency does NOT
  imply C4 remediation.
- **M3 is a spike, not a commitment:** implement early termination
  behind the existing fields/batch bench families (open-map shared-goal
  scenario sized like the S11.4 soak), batch-local scratch only — the
  retained-field caching path keeps full floods so `has_goal_`
  consumers are untouched. Accept on evidence; either way the outcome
  becomes an optimization-log entry. Composes with the recorded
  per-agent pathing-dirty backlog item but does not depend on it.
- **M5:** staged migration, not two independent flips — `ChunkMeta` is
  public API and `dirty_bounds`/flag words are exposed through
  `meta()`/`DirtyObservation`: step 0 routes all dirty-bounds and
  flag-word reads through world accessors so `ChunkMeta`'s layout stops
  being load-bearing (pure refactor, no behavior change); then (a) the
  hot flag words become the accessor-backed SoA column consulted by
  `collect_matching_chunks` (single source of truth — the struct field
  is removed, not duplicated); then (b) `dirty_bounds` moves to the
  cold parallel array. The storage/block bench families gate (a)/(b).
  If step 0's caller migration grows beyond tick-engine's budget, M5
  splits out into its own `perf/audit3-chunk-meta` branch between
  batch-residency and tick-engine rather than bloating the branch.
  The deeper design-level fix
  (a maintained dirty-chunk set killing the O(chunk_count) tick floor,
  which would also serve the render/delta scans) is *documented as the
  ceiling* and deferred to the future backlog next to
  per-agent-dirty — it is an architecture change, not a remediation.
- **M6:** stable-partition tasks by phase at `seal()` with per-phase
  offset ranges; dirty masks accumulate into a frame-level mask plus an
  OnDirty-only task index. `notify_dirty` keeps its public semantics.
- **M7:** introduce a resolved-endpoint struct threaded through
  validate/version-check/commit/dirty-mark;
  `movement_versions_match` early-returns `Moved` when the intent
  carries no version expectations. Golden-seam tests (movement DSL
  suite) must pass byte-identical.
- **M8 (own branch because concurrency):** `alignas(64)` the two hot
  counters, claim short runs per `fetch_add` (run length chosen by
  bench), wake `min(workers, ops)` threads, and only the last finishing
  worker notifies completion. Verified under the tsan preset plus the
  existing worker-pool stress tests; `parallel/` gates (new in WS1)
  provide the before/after.
- **M9 is an experiment with a kill-switch:** interleave
  `{generation, state, g}` into one node record behind the existing
  path bench family; `parent_` stays separate initially. If the family
  does not show a clear win (epoch-wrap bulk reset is the counter-
  pressure), revert and log — the audit explicitly marks this
  measure-first.
- **M10/C3 (same lines, one commit):** `PathScratch::touched_` becomes
  a counter (`DistanceFieldScratch` keeps its vector — it is iterated
  for dependency capture); the unweighted core's `f` arithmetic and
  dial step adopt `saturating_add`, restoring symmetry with the
  weighted core.
- **C1:** the saturating axis-end helper moves to one shared detail
  header (chunk_meta's semantics win); `queued.h` and `render_delta.h`
  call it. Cheap guard, pathological-input class.
- **C2:** RAII scope guard for `in_run_`, plus one line in the header
  docs stating task callables should not throw (matching the
  executors' documented contract).
- **Deferred by decision, logged not fixed:** `FieldProductCache`
  linear scans (fine at tens of entries; index when entry counts grow),
  route-cache suffix-insert gating (workload-dependent, needs a
  suffix-miss-heavy bench to justify), render/delta full-scan ceiling
  (see M5), EnTT per-tick sort + write-back copies (documented
  determinism seam).

Remaining lows land with the workstream that owns their files:
`std::popcount` with tick-engine (chunk_meta); entry-cost reorder,
node_index_space slot threading, and the portal-segment-cache
open-addressed index with path-micro; occupancy hash single-round mix
and EnTT `PathState*` caching with storage-ecs; the `parallel/`
threshold JSON and `--benchmark_repetitions` gating with integrity.

## Order and verification

Merge order: integrity → batch-residency → tick-engine → path-micro →
executor → storage-ecs (rebasing each on the previous merge; CHANGELOG
conflicts resolved at rebase). Integrity goes first so gates are
trustworthy; batch-residency before tick-engine because it owns the
product's stated hot path; storage-ecs last because its LRU fix
consumes the bench integrity workstream's new family.

Every branch: tests first where behavior changes (correctness items
C1-C3 get failing-first tests; pure perf changes get before/after bench
numbers in the PR plus unchanged golden/behavioral suites), full suite
+ ASan preset green before commit, tsan additionally for
`perf/audit3-executor`, codex review before merge plus the reserved
Fable pass on the final integrated set (advisor when available),
CHANGELOG entry per branch, `tests/AGENTS.md` updated for any new test
target, optimization-log entries for every accepted or rejected
experiment (M3, M9, and the M11 LRU are experiments by definition).
Bench-affecting merges regenerate `docs/performance.md` +
`docs/assets/benchmark-trends.svg` per the regeneration policy once the
five recalibrated caps have real numbers.
