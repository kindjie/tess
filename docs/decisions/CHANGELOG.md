# Design Changelog

Records meaningful design changes from the original TDDs.

## Template

```md
## YYYY-MM-DD - Title

- Changed:
- Reason:
- Affected docs:
- Affected code:
```

## 2026-07-08 - NodeIndexSpace Trait for A* Node Storage

- Changed: New internal `tess::detail::NodeIndexSpace<World>`
  (`include/tess/path/node_index_space.h`) maps a global tile index to a
  node-array offset and reports the array capacity a search must allocate.
  `astar_path` and `weighted_astar_path` now size and index their
  `PathScratch` node arrays through this trait instead of `tile_count<World>()`
  and raw `static_cast<std::size_t>(index)`. The `AlwaysResident`
  specialization is the identity (`offset(i) == i`, capacity == the whole tile
  count), so the dense search is byte-identical and stays allocation-free after
  warmup. `PathScratch::touch_node` now takes the pre-computed offset alongside
  the global index.
- Reason: Slice 2 of the full-sparse stage. Decoupling A* node storage from the
  global tile count is the prerequisite for running A* over sparse worlds,
  whose global tile index can reach ~1e17 and cannot size a dense array. The
  sparse `NodeIndexSpace` specialization (resident-slot remap, bounded by the
  residency budget) and the missing-chunk path policy land in the next slice;
  this slice ships only the trait and the dense identity so the change is a
  provably behavior-preserving refactor.
- Affected docs: `docs/decisions/CHANGELOG.md`.
- Affected code: `include/tess/path/node_index_space.h` (new),
  `include/tess/path/path.h` (astar_path/weighted_astar_path offset threading,
  PathScratch::touch_node signature), `include/tess/tess.h`, `CMakeLists.txt`.

## 2026-07-08 - Sparse-Resident World (Storage Core)

- Changed: New `tess::World<Shape, Schema, tess::SparseResident>` (alias
  `SparseResidentWorld`) materializes only a byte-budgeted, least-recently-used
  subset of a bounded shape, so worlds spanning trillions of chunks cost only
  their residency budget. New public `tess/storage/residency.h`
  (`SparseResident`, `ResidencyConfig`, `ResidencyHandle`) and
  `tess/storage/sparse_world.h` (the specialization plus an internal
  fixed-capacity `ChunkDirectory` with backward-shift deletion). `ChunkMeta`,
  `ChunkState`, `DirtyObservation`, and every `ChunkMeta` mutation
  (dirty/active/version) moved to a new shared `tess/storage/chunk_meta.h`, so
  the dense and sparse worlds share one metadata implementation. Residency is
  explicit (`ensure_resident`/`touch`/`evict`) with world-monotonic per-load
  generations that invalidate stale handles across eviction; `is_resident`
  distinguishes `Missing` in-bounds chunks from out-of-bounds; `try_chunk`/
  `try_meta`/`try_field` are the residency-tolerant readers. All iteration is
  bounded by the resident set or fixed slot capacity, never `chunk_count`.
- Reason: Milestone M2 requires sparse residency early because it changes the
  storage API that topology, pathing, queued ops, and block views all consume.
  This is Slice 1 of the full-sparse stage: the self-contained storage core the
  later topology/path/consumer slices build on. Extracting the shared metadata
  ops keeps dirty/active/version semantics identical across both worlds
  (guarded by the unchanged dense storage tests).
- Affected docs: `docs/architecture/storage.md` (new Sparse-Resident World
  section, revised Out Of Scope), `docs/architecture/surface.json`,
  `tests/AGENTS.md`.
- Affected code: `include/tess/storage/chunk_meta.h` (new),
  `include/tess/storage/residency.h` (new),
  `include/tess/storage/sparse_world.h` (new),
  `include/tess/storage/world.h` (delegates to shared meta ops),
  `include/tess/tess.h`, `CMakeLists.txt`, `tests/tess_residency_test.cc` (new),
  `tests/CMakeLists.txt`.

## 2026-07-07 - Parallel Benchmark Family and Concurrency Plan

- Changed: New `bench/tess_parallel_bench.cc` compares serial,
  scoped-thread, and worker-pool phase executors on identical partitioned
  one-op-per-chunk phases (dispatch-bound, memory-bound, and compute-bound
  workloads, fixed worker counts, worker/chunk counters). Parallel cases
  are deliberately ungated pending CI baselines. New
  `docs/planning/concurrency-plan.md` records the S1 concurrency-stream
  decisions: planner-anchored write-policy enforcement in v1 (claim-table
  checking deferred to the scheduler stage), internal backends before
  external `work_contract` evaluation, ungated parallel benchmarks, and the
  non-atomic `ChunkMeta` invariant, plus measured dispatch-cost evidence.
- Reason: The concurrent tile-world addendum requires baseline benchmark
  data before thresholds and requires Tess-owned backends to establish
  correctness and performance before any external scheduler dependency is
  considered.
- Affected docs: `docs/planning/concurrency-plan.md`,
  `docs/planning/README.md`
- Affected code: `bench/tess_parallel_bench.cc`, `bench/CMakeLists.txt`

## 2026-07-07 - Persistent Worker-Pool Phase Executor Prototype

- Changed: `tess/ops/phase_executor.h` gains `WorkerPoolPhaseExecutor`, a
  persistent-pool prototype satisfying the `PhaseExecutor` concept: workers
  are created once and reused across phases (no per-phase thread creation),
  jobs are published type-erased under the pool mutex, completion blocks
  until all claimed operations finish and all adopted workers leave the
  claim loop, failures report in operation order, and
  `reserve_operations(count)` makes warm dispatch allocation-free. It does
  not declare `serial_execution_tag` and pairs only with
  `execute_phase_partitioned_dirty_with`.
- Reason: The concurrent tile-world addendum requires a Tess-owned
  persistent pool prototype behind the executor interface before any
  external backend is evaluated; this is the S1 slice of the v1 concurrency
  stream, providing the comparison point for the parallel benchmarks and
  the candidate promoted to production in the scheduler stage (S7).
- Affected docs: `docs/architecture/queued-operations.md`,
  `docs/architecture/surface.json`
- Affected code: `include/tess/ops/phase_executor.h`,
  `tests/tess_phase_executor_test.cc`, `tests/tess_queued_test.cc`,
  `tests/AGENTS.md`

## 2026-07-07 - Phase Executor Contract Promoted to Its Own Public Header

- Changed: The backend-facing executor pieces (`PlannedExecutionStatus`,
  `PlannedExecutionResult`, `ExecutorPhaseRange`, `SerialPhaseExecutor`, the
  `SerialExecutor` concept, `ScopedThreadPhaseExecutor`, and
  `execute_operation_index_range`) moved from `tess/ops/queued.h` into a new
  public header `tess/ops/phase_executor.h`, which also adds a structural
  `PhaseExecutor` concept stating the
  `for_each_operation(first, count, fn) -> PlannedExecutionResult` contract
  and documents the executor thread contract (non-atomic world metadata,
  planner-proven disjoint mutable ownership, caller-owned dirty partitions
  reduced in plan order). `executor_phase_range(ExecutionPhase)` stays in
  `queued.h` as the plan-side bridge. No symbol was renamed.
- Reason: The v1 concurrency stream (plan S1) lands API-shaping work first;
  worker backends must be writable and testable against a small stable
  header without pulling in the planner, and later stages (result channels,
  scheduler auto-exec, production pool) build on the same concept.
- Affected docs: `docs/architecture/queued-operations.md`
- Affected code: `include/tess/ops/phase_executor.h`,
  `include/tess/ops/queued.h`, `include/tess/tess.h`, `CMakeLists.txt`,
  `tests/tess_phase_executor_test.cc`, `tests/CMakeLists.txt`,
  `tests/AGENTS.md`

## 2026-07-07 - Generation-Stamped Dirty Observe/Clear Protocol

- Changed: `World` gains `observe_dirty(key, flags)` returning a
  `DirtyObservation` (observed dirty subset, dirty bounds, chunk version)
  and `clear_dirty_observed(key, observation)`, which clears exactly the
  observed flags only while the chunk's dirty generation still matches and
  otherwise preserves all flags/bounds and returns `false`.
- Reason: The concurrent tile-world addendum requires a generation-aware
  observe/clear protocol before budgeted or concurrent maintenance may clear
  dirty metadata, so rebuilds cannot lose marks that land mid-rebuild. This
  lands the API early in the v1 completion plan (S1) because later scheduler
  and maintenance stages build on it.
- Affected docs: `docs/architecture/storage.md`,
  `docs/architecture/surface.json`
- Affected code: `include/tess/storage/world.h`,
  `tests/tess_storage_test.cc`, `tests/AGENTS.md`

## 2026-07-06 - Docs Audit Sweep Closes Drift and Adds Surface Manifest

- Changed: Final workstream of the 29-part audit remediation; documentation
  drift found "at the seams" is closed and a prevention gate is added.
  `docs/architecture/simulation.md` now documents the full current
  `include/tess/sim/` surface: `SimSchedulerStats`,
  `run_queued_operations`, the four `tick_*_path_agents` /
  `tick_*_path_agents_with_movement` wrappers and their state/options/stats
  types, the path-agent batch helpers, `MovementResult`,
  `MovementVersionCheck`, `MovementFailureCounts` /
  `record_movement_failure` / `is_transient_movement_failure` /
  `movement_versions_match`, the `PathAgentPhase` lifecycle interplay
  across layers, and the fixed-step time types including
  `FixedStepFrame::dropped_seconds`. New maintained docs
  `docs/architecture/shape.md` (Shape/ShapeTraits, coordinate types, key
  packing including the portable `detail::UInt128` and the 64-bit boundary
  guards, the default-member-initializer zero-init guarantee, `contains` /
  `manhattan_distance`, and the `TESS_ASSERT` policy) and
  `docs/architecture/diagnostics.md` (`TESS_ENABLE_DIAGNOSTICS`, event
  macros, scoped counters, and the `thread_local` scope limitation:
  counters do not aggregate across worker threads) join the maintained
  list. `docs/architecture/queued-operations.md` no longer contradicts
  itself about phase sharing: both prose passages now match
  `detail::parallel_phase_conflict` (a mutable operation conflicts with
  any overlapping operation, read-only included), and the
  `OperationFailure` enumerator list is complete (including `None`) and
  well-formed. `docs/tdd/README.md` indexes the Work Contracts and tile
  layout addenda; `docs/README.md` frames the TDD archive as original TDDs
  plus proposed addenda, non-authoritative. `docs/performance.md` marks
  the snapshot stale relative to the 2026-07-06 harness rework and
  threshold changes with an explicit regeneration TODO (no numbers
  fabricated). `README.md` lists both examples and the hooks-backstop CI
  job. Historical corrections: the 2026-06-08 "Concurrent Tile-World TDD
  Split" entry's "Affected code: none" gained a bracketed correction
  (commit 2e22c05 changed public headers), and the 2026-06-05 "One
  Millisecond Benchmark Investigation Gate" entry gained a superseded
  annotation pointing at calibrated per-benchmark ceilings. Prevention
  gate: `docs/architecture/surface.json` maps each maintained doc to the
  public symbol names it documents (233 symbols across 9 docs), and
  `tools/check_public_surface.py` extracts type and free-function names
  from every `TESS_PUBLIC_HEADERS` header (24 headers; simple line-based
  parser with a `detail`/`internal` namespace allowlist) and fails when a
  symbol is missing from the manifest. It runs as an advisory
  (`::warning`) step in the CI hooks-backstop job, with pytest coverage in
  `tests/test_check_public_surface.py` including a completeness test
  against the real tree.
- Reason: A docs audit found the architecture docs had drifted where
  subsystems meet (scheduler/tick/movement seams, phase-conflict rules,
  unindexed addenda, stale benchmark framing), and nothing prevented new
  public symbols from landing undocumented.
- Affected docs: `docs/architecture/simulation.md`,
  `docs/architecture/shape.md` (new), `docs/architecture/diagnostics.md`
  (new), `docs/architecture/queued-operations.md`,
  `docs/architecture/path.md`, `docs/architecture/topology.md`,
  `docs/architecture/block.md`, `docs/architecture/README.md`,
  `docs/architecture/surface.json` (new), `docs/tdd/README.md`,
  `docs/README.md`, `docs/performance.md`, `docs/decisions/CHANGELOG.md`,
  `README.md`, `tests/AGENTS.md`
- Affected code: `tools/check_public_surface.py` (new),
  `tests/test_check_public_surface.py` (new), `.github/workflows/ci.yml`

## 2026-07-06 - CI Gains TSan, macOS, and Advisory MSVC Platform Gates

- Changed: The sanitizer toggle is generalized — `TESS_ENABLE_SANITIZERS`
  now applies the comma-separated `TESS_SANITIZERS` cache string (default
  `address,undefined`, so existing presets are unchanged), with a
  configure-time `FATAL_ERROR` when the list combines `address` and
  `thread` (they cannot link into one binary) and
  `-fno-sanitize-recover=undefined` still applied whenever `undefined` is
  in the list. New `dev-tsan` configure/build/test presets build the suite
  with `-fsanitize=thread` and run ctest with
  `TSAN_OPTIONS=halt_on_error=1 second_deadlock_stack=1`; the preset joins
  the CI quality matrix (no extra apt packages needed). A new `macos` CI
  job on `macos-15` runs the `dev` and `dev-asan` presets plus the install
  smoke test, with no benchmark gates because bench thresholds are
  calibrated on the Linux runner family. A new advisory `windows` CI job
  on `windows-2025` (`continue-on-error: true` during shake-out; flip to
  required after two consecutive green runs on main) builds the
  condition-gated `windows-msvc` preset, runs ctest, and runs the install
  smoke under `shell: bash`. `tools/install_smoke.sh` gained
  `TESS_INSTALL_SMOKE_CONFIG` for multi-config generators: it forwards
  `--config` to `cmake --install` and the consumer build and looks for
  the consumer binary in the per-config subdirectory.
- Reason: The threaded queued executor and scheduler paths had no data
  race gate (a full local dev-tsan run passed 351/351, and a deliberate
  unsynchronized-counter race was verified to fail the gate before being
  discarded), and the library claimed macOS/Windows portability that CI
  never exercised. Platform pinning (`macos-15`, `windows-2025`) follows
  the existing anti-drift policy for `ubuntu-24.04`.
- Affected docs: `README.md`, `docs/dependencies.md`.
- Affected code: `CMakeLists.txt`, `cmake/TessProjectOptions.cmake`,
  `CMakePresets.json`, `.github/workflows/ci.yml`,
  `tools/install_smoke.sh`.
## 2026-07-06 - A* Heap Loops Gain Oracle-Backed Coverage and Mutation Guards

- Changed: Added `tests/tess_path_search_test.cc` (new binary) and
  `tests/path_test_util.h` with serpentine maze fixtures that defeat every
  pre-A* fast path (two parallel two-gap walls with gaps at opposite ends,
  start/goal displaced on both non-degenerate axes), plus independent BFS
  and Dijkstra oracles. An audit had found the unit A* heap loop was never
  reached by any Found-status test — every maze was answered by fast paths
  (heap_pushes = 0 under diagnostics), and pervasive
  `expanded_nodes == path.size()` assertions structurally required this.
  The new tests pin exact optimal costs against the oracles across
  top-down 2D, vertical 2D, and multi-chunk 3D shapes and assert
  `expanded_nodes > path.size()`; `tess_diagnostics_enabled_test` gains
  heap-push mutation guards on the same fixtures. Mutation validation
  (single-gap walls) confirmed the discriminators fail when a fast path
  answers. The heap loops themselves were verified correct: unit banded
  search and weighted heap search both matched the oracles exactly, so no
  library fix was needed. Start == goal now has pinned semantics (Found,
  single-node path, cost 0) across all public path entry points, all of
  which already behaved correctly. The flagship
  `FindsTopDown2DPathAroundBlockedTiles` weak assert
  (`EXPECT_GT(cost, 3u)`) was tightened to the exact optimal cost 9 and
  documented as plane-gap-fast-path coverage.
- Reason: Closes the audit finding that a tie-breaking, band-stride, or
  parent-reconstruction bug in the real search would ship green, and pins
  start == goal semantics (audit H4) against regression.
- Affected docs: `tests/AGENTS.md`
- Affected code: `tests/path_test_util.h`, `tests/tess_path_search_test.cc`,
  `tests/tess_diagnostics_enabled_test.cc`, `tests/tess_path_test.cc`,
  `tests/CMakeLists.txt`
## 2026-07-06 - Benchmark Harness Measures What It Claims and Checks Results

- Changed: Fixed five measurement bugs in the benchmark suite.
  `storage/world_dirty_chunks_iteration` collects into a hoisted reserved
  vector via `collect_dirty_chunks` and iterates the keys (it previously
  timed only the by-value vector allocation and never iterated; 295 ns
  before, 126 ns after on an M-series dev box). `key/coord_from_tile_key_*`
  precompute encoded keys in setup so the loop measures decode only
  (encode was double-counted; 2.67/2.84/2.76 ns before vs 0.68/0.57/0.64 ns
  after for 2d/3d/u128). The field-product cache stale-rejection and LRU
  eviction benchmarks replace per-iteration `PauseTiming`/`ResumeTiming`
  (timer overhead comparable to the measured op) with manual timing plus
  pinned iterations; their gated names gained `/iterations:N/manual_time`
  suffixes and their thresholds moved from `max_cpu_time_ns` to
  `max_real_time_ns` at the 1 ms investigation ceiling because cpu_time now
  includes the untimed cache refill (measured op: ~313 ns stale lookup,
  ~119 ns evicting store, vs ~760/~596 ns reported before). The agent
  clean-tick benchmark keeps its cheap agent reset inside the timed region
  instead of hiding it behind a Pause/Resume pair that cost as much as the
  tick. `path/weighted_chunk_portal_candidates_*` publishes the measured
  scan-tile/waypoint totals instead of a hardcoded formula, and all batch
  benchmarks publish `batch.cost_total`/`batch.expanded_total` aggregates
  instead of the last request's values under single-query counter names
  (which made per-node math ~100x off). Per benchmark-plan sections 14/20,
  benchmark families now validate the last result outside timed regions
  (aborting `bench_check`: Found status, endpoints, legal unit steps onto
  passable tiles, setup-run expected costs, agent frame stats, cache
  outcomes), and `tess_bench_diagnostics` asserts the warm
  `path/astar_open_2d` iteration allocates nothing.
  `tools/benchmark_thresholds.py` now rejects duplicate benchmark names
  (previously a dict comprehension silently kept the last duplicate),
  prefers `--benchmark_repetitions` aggregates keyed by `run_name` (median
  default, `--aggregate` flag), and reports missing/malformed files
  without tracebacks; `tools/benchmark_baseline_summary.py` filters
  aggregates by `run_type` instead of name suffixes and writes real CSV.
  New `tests/test_benchmark_tools.py` covers both tools and runs in the CI
  hooks-backstop job.
- Reason: Q10 audit remediation — several benchmarks measured allocation or
  encode instead of the named operation, per-iteration Google Benchmark
  timer overhead dominated microsecond cache ops, batch counters
  misrepresented totals, thresholds could silently gate on the wrong
  duplicate entry, and the plan-mandated correctness/allocation assertions
  were missing.
- Affected docs: `docs/planning/benchmark-plan.md`, `tests/AGENTS.md`.
- Affected code: `bench/tess_bench.cc`, `bench/tess_path_agent_bench.cc`,
  `bench/tess_path_product_bench.cc`, `bench/tess_path_weighted_bench.cc`,
  `bench/thresholds/path.json`, `tools/benchmark_thresholds.py`,
  `tools/benchmark_baseline_summary.py`, `tests/test_benchmark_tools.py`,
  `.github/workflows/ci.yml`.
## 2026-07-06 - Flat-Hash Path Grouping, Batch Status Fix, Scan Cost Model

- Changed: `PathRequestRuntime::process_repeated_goal_fields` no longer
  rescans the request list per request (O(n^2) with an O(n^3)-worst inner
  start-chunk dedup); one O(n) pass over a reusable open-addressed flat
  map (goal -> group id, first-occurrence order, matching the route
  cache's hashing style) buckets members via counting sort and counts
  distinct start chunks with sort+unique over runtime-owned scratch.
  `weighted_path_batch` likewise builds its goal -> request count map once
  per batch instead of rescanning all requests per request; the map
  scratch lives in `WeightedPathBatchScratch`. Group processing order and
  every stats counter (`field_product_candidate_groups`/`used`/`skipped`,
  `WeightedPathBatchStats`) are pinned unchanged by seeded randomized
  equivalence tests against per-request A* oracles, and warm reruns of
  both passes are allocation-free. Behavior fix (red evidence first):
  members of a failed shared-goal weighted batch group were blanket
  labeled with the goal's failure status even when their own start was
  invalid; `detail::weighted_group_member_failure` now mirrors
  `weighted_astar_path`'s exact endpoint-validation precedence
  (start containment/passability before goal status, start entry cost
  after goal containment/passability). Added the pre-A* scan cost model
  to `docs/architecture/path.md` with two accepted-with-evidence
  worst-case benchmarks (`path/astar_plane_gap_miss_512x512`,
  `path/astar_plane_gap_miss_3d_64x64x16`) under deliberately generous
  10x-measured ceilings, plus new coverage: portal-route corner cases
  (invalid endpoints, no-candidate NoPath, segment failure clearing the
  assembled path, a layout where the greedy candidate is the only winner,
  multi-seam L routes), weighted batch edges (empty batch, all-distinct
  goals, duplicate requests, >MaxCost fallback engagement evidence,
  bounded-vs-unbounded random-cost equivalence), and runtime lifecycle
  (clear_requests regeneration, empty frames, failure counters, byte
  budget driving real eviction through `process_unit_cached`).
- Reason: Audit flagged the grouping hot paths as quadratic-plus in
  request count, the batch fan-out as mislabeling per-member statuses,
  and the pre-A* scans as an undocumented O(world-slice) cost accepted
  without evidence. The rewrite keeps deterministic first-occurrence
  semantics while making grouping linear, and the new tests pin the
  previously untested corners.
- Affected docs: `docs/architecture/path.md`,
  `docs/planning/optimization-log.md`, `tests/AGENTS.md`.
- Affected code: `include/tess/path/path_runtime.h`,
  `include/tess/path/path.h`, `include/tess/path/detail/weighted_batch.h`,
  `tests/tess_path_runtime_test.cc`, `tests/tess_path_weighted_batch_test.cc`,
  `tests/tess_path_portal_route_test.cc`, `tests/CMakeLists.txt`,
  `bench/tess_path_scan_bench.cc`, `bench/CMakeLists.txt`,
  `bench/thresholds/path.json`.

## 2026-07-06 - Path Caches Gain Eviction Budgets and Hash-Indexed Lookups

- Changed: `WeightedPortalSegmentCache` is now bounded by a segment budget
  (default 256 entries, `set_segment_budget`, plumbed through
  `PathRuntimeCachePolicy::portal_segment_budget`). A store at budget first
  sweeps stale entries in one compaction pass that rebuilds both the entry
  list and the path-node append arena (piecemeal erase would leak the
  arena), then evicts the oldest live entries in insertion order if the
  sweep cannot make room; a zero budget stores nothing. This fixes two
  audit findings: unbounded growth in any edited world (50 edit/rebuild
  cycles grew the cache to 100 entries pre-fix) and the compounding
  duplicate bug where `store()` deduped through a stale-rejecting lookup,
  so re-storing the same request after a world change appended an identical
  entry forever (2 entries for one request pre-fix). The cache reports
  `PortalSegmentCacheStats` (entries, path_nodes, sweeps, evictions,
  stale_rejections — mirroring `FieldProductCacheStats` conventions) via
  `stats()`/`reset_stats()`; `PathRuntimeStats` now embeds the full struct
  as `portal_segment_cache`, replacing the old
  `portal_segment_cache_entries` field. `RouteCacheScratch` replaced its
  O(entries) exact scan and O(entries × path nodes) suffix scan with two
  open-addressed flat hash indexes (power-of-two capacity, linear probing,
  plain vectors): exact (start, goal) lookups, and a suffix index keyed by
  hash(node, goal) populated per stored Found-path node with
  first-write-wins, preserving the linear scan's earliest-stored-entry
  determinism (pinned by test before the change). Route storage is capped
  (`max_route_entries` default 512, `max_route_path_nodes` default 2^20,
  `set_caps`, plumbed through `PathRuntimeCachePolicy`); an insert that
  would exceed either cap invalidates the whole cache first — the same
  lifecycle as world-change invalidation — and counts
  `RouteCacheStats::cap_invalidations`. `ChunkVersionDependencies::
  add_chunk` checks the most recent entry before its linear scan, since
  path nodes are chunk-coherent during product builds.
- Reason: Both caches grew without bound and rescanned dead or unrelated
  entries on every lookup; an edited long-lived world leaked entries and
  path storage forever, and route lookups degraded linearly with cache
  population. Budgeted compaction plus cheap cap-triggered invalidation
  match the library's existing world-change lifecycle, and the flat hash
  indexes keep the no-std-map style while making exact/suffix lookups
  O(1) expected. Bench evidence: cached_astar_batch_100_suffix_open
  17.27us -> 16.94us mean, cached_astar_batch_100_mixed_repeated_room_
  portals 13.13ms -> 12.89ms mean; all path thresholds green.
- Affected docs: `docs/architecture/path.md`, `tests/AGENTS.md`.
- Affected code: `include/tess/path/portal_segment_cache.h`,
  `include/tess/path/route_cache.h`, `include/tess/path/path.h`,
  `include/tess/path/path_runtime.h`, `tests/tess_path_cache_test.cc`,
  `tests/tess_path_runtime_test.cc`, `tests/CMakeLists.txt`.

## 2026-07-06 - Path Caches Stop Handing Out Views Into Reallocatable Storage

- Changed: All three path caches now follow one lifetime policy — no
  returned view may point into storage that a later cache write can
  reallocate. `cached_astar_path` hits (exact and suffix) copy the cached
  route into the caller's `PathScratch` and return a span into that scratch,
  giving hits the identical lifetime contract as misses (valid until the
  next call using the same scratch); warm hits stay allocation-free with
  pre-reserved scratch. `WeightedPortalSegmentCache` deleted its public
  `find()`/`path()` (a held span dangled across `store()` growth) in favor
  of `lookup_append(world, request, out_path) -> SegmentHit`, which appends
  the cached path into caller storage and deduplicates the shared junction
  node when stitching consecutive segments; `Entry` went private.
  `FieldProductCache` entries now hold products behind
  `std::unique_ptr<DistanceFieldProduct>`, so `lookup()` pointers survive
  stores/evictions of other entries (borrow window: until a store/eviction
  touching that key, or `clear()`), and `store()` takes
  `DistanceFieldProduct&&`, moving instead of double-copying world-sized
  field data (the runtime rebuilds its staging product on the next build).
  `RouteCacheScratch` + `cached_astar_path` moved to the new public header
  `include/tess/path/route_cache.h` (included by `path.h`, listed in
  `TESS_PUBLIC_HEADERS`) to keep `path.h` under the token budget. Audit
  gaps pinned: oversized-store clears the entire cache, zero byte budget
  stores nothing, same-key replacement byte accounting, LRU eviction by
  recency with 3+ entries (verified correct, pinned), distance-field
  error-status families (InvalidGoal/InvalidStart/empty `GoalSet`) across
  plain/weighted/product builds and path reconstruction. The placebo
  portal-segment-cache assertions in the runtime weighted-batch test
  (asserting 0 entries in a cache the runtime never writes) were replaced
  by a real contract test: entries stored through the runtime accessor are
  reported by `stats()` and dropped by `clear_caches()` — the runtime
  itself intentionally does not populate that cache today.
- Reason: A lifetime audit reproduced heap-use-after-free under ASan for
  all three caches: a route-cache hit span dangled after a later miss
  reallocated `paths_`, a segment-cache `path()` span dangled across
  `store()`, and a field-product-cache `lookup()` pointer dangled after
  `entries_` growth. `store()` also copied world-sized products twice
  (measured 8,800 bytes allocated for a 4,376-byte product store; now a
  move). Fixes chosen per size class: small routes copy into scratch;
  world-sized products get stable heap slots plus move-in ownership.
- Affected docs: `docs/architecture/path.md`, `tests/AGENTS.md`
- Affected code: `include/tess/path/route_cache.h` (new),
  `include/tess/path/path.h`, `include/tess/path/portal_segment_cache.h`,
  `include/tess/path/field_product_cache.h`,
  `include/tess/path/path_runtime.h`, `include/tess/tess.h`,
  `CMakeLists.txt`, `bench/tess_path_product_bench.cc`,
  `tests/tess_path_test.cc`, `tests/tess_path_runtime_test.cc`

## 2026-07-06 - Byte-Array Backing For BlockScratch Object Lifetimes

- Changed: `BlockScratch` now owns heap `std::byte[]` storage
  (`std::unique_ptr<std::byte[]>` + explicit capacity accounting) instead
  of `std::vector<std::max_align_t>`, and `allocate<T>` returns
  `std::launder`ed pointers into that buffer. Growth allocates a fresh
  buffer without preserving contents (previously returned spans were
  already invalidated; byte accounting still carries over), a wrapped
  capacity computation now throws `std::bad_alloc` instead of silently
  wrapping, and a `static_assert` pins
  `__STDCPP_DEFAULT_NEW_ALIGNMENT__ >= alignof(std::max_align_t)` so the
  buffer base keeps the documented max-align guarantee. The class is now
  move-only; moved-from scratches read as empty. Tests pin zero-count
  allocation, byte-count overflow rejection, mixed-alignment disjoint
  spans, growth accounting, and runtime `WritePolicy` dispatch for
  `UniquePerTile`, `UniquePerChunk`, and the first-ever `Unsafe`
  instantiation.
- Reason: Reinterpreting `std::vector<std::max_align_t>` storage as `T*`
  never created `T` objects, which is undefined behavior under the C++20
  object model even for trivial `T` (and per-element pointer arithmetic is
  per-object UB before C++23 `start_lifetime_as_array`). A `std::byte`
  array-new implicitly creates implicit-lifetime objects in its storage
  ([intro.object]/13), and `allocate<T>` already requires trivially
  default-constructible, trivially destructible `T`, making the returned
  spans well-defined. The defect was formal-model-only and not observable
  under ASan/UBSan.
- Affected docs: `docs/architecture/block.md`, `tests/AGENTS.md`
- Affected code: `include/tess/block/block.h`, `tests/tess_block_test.cc`

## 2026-07-06 - Structural Serial-Executor Guard For Shared-Dirty Phase Execution

- Changed: `execute_phase_deferred_dirty_with<Policy>` now requires the new
  `tess::SerialExecutor` concept (a nested `serial_execution_tag` type
  alias declared by the executor), so passing a concurrent executor such as
  `ScopedThreadPhaseExecutor` to the shared-`PlannedDirtyAccumulator`
  helper is a compile error instead of a data race on the shared
  accumulator and non-atomic chunk counter. `SerialPhaseExecutor` declares
  the tag; `ScopedThreadPhaseExecutor` is documented as a prototype that
  pairs only with `execute_phase_partitioned_dirty_with<Policy>`, which
  stays unconstrained (per-operation dirty partitions and result slots) as
  does the `execute_operation_index_range` adapter it uses. Also:
  `OpId`/`OpHandle` gained `= 0` default member initializers so
  default-constructed values compare equal to zero instead of reading
  indeterminate storage; `DomainDesc::explicit_chunks` now deduplicates
  keys after sorting so a planned operation never visits one chunk twice
  (repeated keys under `UniquePerChunk` would break the per-chunk
  ownership rule phase planning relies on); `FrameOps::clear()` resets a
  frame for reuse while keeping vector capacity (handles invalidated,
  handle/id assignment restarts at zero, warm re-enqueue is
  allocation-free); and the queued replay stress world comparison now
  checks every tile of every chunk via field spans instead of tile 0 only.
- Reason: A concurrency audit flagged the shared-accumulator deferred-dirty
  helper as its top finding: the helper hands a callback capturing shared
  mutable state to any executor's `for_each_operation`, and the public
  threaded executor satisfied that contract silently. The serial contract
  existed only as documentation, so the unsafe pairing compiled cleanly.
  The remaining changes close audit test gaps (write-then-read hazards,
  phase-range boundaries, explicit-domain edge cases, executor clamps) and
  align `OpId`/`OpHandle` with the codebase default-init convention.
- Affected docs: `docs/architecture/queued-operations.md`,
  `docs/decisions/CHANGELOG.md`, `tests/AGENTS.md`
- Affected code: `include/tess/ops/queued.h`, `tests/tess_queued_test.cc`

## 2026-07-06 - RAII Allocation Counting And Complete New/Delete Coverage

- Changed: Replaced the raw `set_allocation_counting(bool)` /
  `reset_allocation_count()` test API with an RAII-only
  `tess_test::ScopedAllocationCounter` (constructor resets and enables,
  destructor disables; `count()`/`bytes()` accessors) and migrated all 23
  call sites across seven test binaries. The non-sanitizer backend of
  `tests/allocation_counter.cc` no longer dlsym-chains to Itanium-mangled
  `_Znwm`/`_Znam` symbols; it now replaces the complete standard
  `operator new`/`delete` overload set (plain, array, aligned, nothrow)
  backed by `std::malloc`/`posix_memalign` (`_aligned_malloc`/
  `_aligned_free` behind a `_WIN32` guard for MSVC), so aligned and
  nothrow allocations are counted. The sanitizer-hook route now also
  covers ThreadSanitizer. `bench/tess_diagnostics_alloc_hooks.cc` gained
  the same ASan/TSan guard: under a sanitizer it forwards
  `__sanitizer_malloc_hook`/`__sanitizer_free_hook` to the diagnostics
  counters (free-hook bytes are best-effort zero, matching unsized
  delete) behind a pthread-specific re-entrancy guard, because first
  `thread_local` access can malloc and re-enter the hook; without a
  sanitizer it now defines the missing aligned-nothrow new and
  aligned/nothrow delete counterparts. Added
  `tests/tess_allocation_counter_test.cc` covering every allocation form
  and RAII unwind on fatal gtest failures.
- Reason: The old dlsym chaining breaks on MSVC and silently missed
  aligned and nothrow allocations, and a failed `ASSERT_*` between
  `set_allocation_counting(true/false)` leaked counting enabled,
  poisoning every later allocation assertion in the binary. The
  unguarded global `new`/`delete` replacement in the bench hooks blinded
  ASan's new/delete-mismatch and alloc-dealloc-size checks in
  `tess_diagnostics_enabled_test`, violating the sanitizer rule in
  `tests/AGENTS.md`.
- Affected docs: `docs/decisions/CHANGELOG.md`, `tests/AGENTS.md`
- Affected code: `tests/allocation_counter.{h,cc}`,
  `tests/tess_allocation_counter_test.cc`, `tests/CMakeLists.txt`,
  `bench/tess_diagnostics_alloc_hooks.cc`, `tests/tess_block_test.cc`,
  `tests/tess_shape_test.cc`, `tests/tess_storage_test.cc`,
  `tests/tess_path_test.cc`, `tests/tess_queued_test.cc`,
  `tests/tess_path_agent_test.cc`, `tests/tess_path_agent_tick_test.cc`

## 2026-07-06 - Path Agent Phase Lifecycle And Bounded Re-Pathing

- Changed: `PathAgentState` gained an explicit `PathAgentPhase` lifecycle
  (`Idle`, `NeedsPath`, `Following`, `Blocked`, `Unreachable`) plus a
  `blocked_retries` budget. Transient movement failures (occupied,
  reserved, blocked, stale version/topology) no longer set `NoPath`; the
  agent enters `Blocked`, keeps its `Found` route, and the tick drivers
  schedule bounded re-paths (`PathAgentTickOptions::max_blocked_retries`,
  default 8) before turning terminally `Unreachable`. Goals assigned via
  the two-argument `set_path_agent_goal` are now picked up on the next
  tick without a manual dirty mark (`NeedsPath` requests processing
  structurally). `PathTicket` carries a generation stamped at submission
  and invalidated by `clear_requests()`; stale tickets assert in debug
  and return `NoPath` in release. `manhattan_distance` moved to
  `core/shape.h` built on overflow-safe `abs_delta` with a saturating
  sum; the signed implementation in `sim/movement.h` was removed.
- Reason: an audit found transient movement failures permanently
  stranded agents (crossing agents froze forever in a static world), the
  two-argument goal overload silently never scheduled pathing, stale
  tickets indexed out of bounds, and the signed Manhattan distance was
  UB at coordinate extremes.
- Affected docs: `docs/architecture/path.md`, `tests/AGENTS.md`
- Affected code: `include/tess/sim/path_agent.h`,
  `include/tess/sim/path_agent_tick.h`, `include/tess/sim/movement.h`,
  `include/tess/core/shape.h`, `include/tess/path/path.h`,
  `include/tess/path/path_runtime.h`, `tests/tess_path_agent_test.cc`,
  `tests/tess_path_agent_tick_test.cc`, `tests/tess_assert_test.cc`

## 2026-07-06 - Portable UInt128 Key Storage And Boundary Shift Guards

- Changed: Replaced the unconditional `unsigned __int128` alias in
  `include/tess/core/shape.h` with a hand-rolled constexpr
  `tess::detail::UInt128` struct (`include/tess/core/uint128.h`) used
  unconditionally on every compiler, so Clang and GCC CI exercise exactly
  the code MSVC will compile. The struct provides only the operations
  shape.h needs (wrap-around multiply, subtract with borrow, and/or,
  boundary-safe shifts, defaulted comparisons, explicit narrowing).
  `tile_key<Shape>(Coord3)` and `chunk_key(TileKey<Shape>)` gained
  `if constexpr (chunk_bits == 0)` short-circuits so single-chunk shapes
  never shift `std::uint64_t` storage by `local_bits == 64` (undefined
  behavior), mirroring the existing `local_bits == 64` mask guard in
  `local_tile_id(TileKey<Shape>)`. Added a Windows MSVC configure/build/
  test preset (condition-gated to Windows hosts) and `/permissive-`
  `/EHsc` to the MSVC warning options.
- Reason: MSVC has no `unsigned __int128`, so >64-bit tile keys could not
  compile on Windows; a conditional typedef would leave the MSVC path
  untested by Clang CI. The builtin also rejects shift counts >= 128 in
  constant expressions, while the portable type defines them as zero.
  Note: with power-of-two chunk dims and the 64-bit local-tile-count fit
  static_assert, `local_bits` maxes out at 63, so the shift-by-64 guards
  are defensive hardening for the 2^63-tile single-chunk boundary shape
  rather than a reachable-bug fix.
- Affected docs: `docs/decisions/CHANGELOG.md`, `tests/AGENTS.md`
- Affected code: `include/tess/core/uint128.h`,
  `include/tess/core/shape.h`, `tests/tess_shape_test.cc`,
  `CMakeLists.txt`, `CMakePresets.json`, `cmake/TessProjectOptions.cmake`

## 2026-07-06 - Debug Assertion Policy For Unchecked APIs

- Changed: Added `TESS_ASSERT`/`TESS_ASSERT_MSG` in
  `include/tess/core/assert.h`, enabled by default outside `NDEBUG` and
  overridable via `TESS_ENABLE_ASSERTS`. Unchecked fast-path accessors now
  assert their preconditions in debug builds: `World::chunk(ChunkKey)`,
  `World::meta(ChunkKey)`, `World::resolve`, `tile_key<Shape>(Coord3)`, and
  `PathRequestRuntime::result(PathTicket)`. Checked `try_*` entry points
  remain the runtime-validated API and are unchanged.
- Reason: Out-of-shape coordinates previously wrapped through
  `static_cast<std::uint64_t>` into out-of-bounds indexing with no debug
  diagnostic anywhere in the library. Release and bench builds define
  `NDEBUG`, so the asserts have zero cost in gated performance paths.
- Affected docs: `docs/decisions/CHANGELOG.md`
- Affected code: `include/tess/core/assert.h`, `include/tess/core/shape.h`,
  `include/tess/storage/world.h`, `include/tess/path/path_runtime.h`,
  `tests/tess_assert_test.cc`, `tests/CMakeLists.txt`, `CMakeLists.txt`
## 2026-07-06 - Topology CSR Reachability and Incremental Region Graph

- Changed: `RegionGraph` now maintains a dense global region index
  (per-chunk prefix sums over 1-based local region ids, exposed through
  `region_count()` / `region_index(RegionRef)` with an
  `invalid_region_index` sentinel) and a CSR portal adjacency rebuilt after
  portal pairing. `reachable()` traverses the CSR adjacency with
  epoch-stamped visited marks in `RegionGraphScratch`, replacing the
  per-frontier-pop portal rescan and linear visited scan (O(V*P + V^2))
  with O(V + P) while preserving the public signature, statuses, and
  visited-region counts. Added the checked
  `LocalChunkTopology::region(LocalRegionId)` accessor for the 1-based id
  convention, defaulted equality for `LocalRegion`, `LocalBoundaryExit`,
  and `RegionPortal`, and `update_region_graph<World, PassableTag>(world,
  scratch, graph, dirty_chunks)` which incrementally rebuilds dirty chunk
  topologies, patches portals, and restores canonical portal order so the
  patched graph is identical to a full rebuild. Incremental portal patching
  drops and re-derives all portals originating from dirty chunks and their
  face neighbors (a superset of the strictly invalidated portals), then
  stable-sorts by from-chunk; this keeps the portal array byte-identical to
  a fresh build, which a minimal to-dirty-only filter cannot guarantee.
- Reason: Reachability queries scaled quadratically with region count, the
  1-based `LocalRegionId` convention had no checked accessor (naive
  `regions()[id.value]` indexing reads the wrong region or out of bounds),
  and consumers such as a sibling game had to run a full
  `build_region_graph` per tile edit because graph internals are private.
- Affected docs: `docs/architecture/topology.md`, `tests/AGENTS.md`
- Affected code: `include/tess/topology/topology.h`,
  `tests/tess_topology_test.cc`
## 2026-07-06 - Sim Scheduler Coverage, Delta Clipping, Query Out-Params

- Changed: Deduplicated the four `tick_*_scheduler` bodies behind a shared
  `detail::tick_scheduler_core` helper; public signatures are unchanged.
  Render-delta collection now clips each chunk's dirty bounds to the chunk's
  own world-space box before the per-tile loop instead of filtering every
  tile through shape containment and coordinate resolution
  (behavior-identical, strictly less work, pinned by new border/clip tests).
  Added appending `collect_dirty_chunks` / `collect_active_chunks`
  out-parameter variants on `World`; the by-value queries became thin
  wrappers. Closed audit coverage gaps with behavioral tests for
  `tick_weighted_movement_scheduler` and
  `tick_weighted_path_agents_with_movement` (cheap cost-band detours,
  occupancy commits, movement dirty-mask interplay), the rejected-plan early
  return, scheduler-driven render-dirty clearing, cross-chunk and
  out-of-shape dirty bounds, fresh-world zero-initialization, and dirty
  bounds `union_box` orientations. Strengthened tautological scheduler test
  assertions (exact accumulator alpha values, reroute-to-arrival with
  visited-tile tracking).
- Reason: A coverage audit found an entirely untested public scheduler
  variant, untested failure and clearing paths, tautological assertions, and
  copy-pasted scheduler bodies; pinning behavior first made the mechanical
  cleanups safe. All new tests passed against the old code, confirming pins
  rather than latent bugs.
- Affected docs: `docs/architecture/simulation.md`,
  `docs/architecture/storage.md`, `tests/AGENTS.md`
- Affected code: `include/tess/sim/scheduler.h`,
  `include/tess/sim/render_delta.h`, `include/tess/storage/world.h`,
  `tests/tess_sim_scheduler_test.cc`, `tests/tess_storage_test.cc`

## 2026-06-08 - Concurrent Tile-World TDD Split

- Changed: Added a concurrent tile-world TDD addendum that separates scoped
  phase execution from coalesced maintenance scheduling. Scoped phase
  execution remains Tess-owned through deterministic phase barriers,
  partitioned dirty records, and ordered reduction. Work Contracts and Signal
  Tree remain deferred candidate infrastructure for backend experiments, not
  adopted dependencies or direct storage integrations.
- Reason: Parallel execution and coalesced maintenance have different
  correctness contracts. Keeping them separate prevents maintenance
  coalescing semantics from leaking into authoritative simulation events or
  planned phase execution.
- Affected docs: `docs/tdd/tdd_addendum_concurrent_tile_world.md`,
  `docs/tdd/tdd_addendum_work_contracts.md`, `docs/tdd/README.md`,
  `docs/dependencies.md`, `docs/architecture/queued-operations.md`
- Affected code: none [Correction 2026-07-06: inaccurate. The commit that
  landed this entry (2e22c05) also changed public headers: it added default
  member initializers to the core value types in
  `include/tess/core/shape.h` and `include/tess/topology/topology.h` (with
  matching zero-init tests in `tests/tess_shape_test.cc` and
  `tests/tess_topology_test.cc`).]

## 2026-06-06 - Queued Parallel Phase Planning

- Changed: Added a conservative `ExecutionPhasePlan` over successful queued
  operation plans. Phase planning accepts only `ReadOnly` and
  `UniquePerChunk` work, preserves deterministic operation order, groups
  disjoint mutable chunk operations into the same phase, separates same-chunk
  mutation behind a phase boundary, and rejects `UniquePerTile` until tile
  subdomains exist. Added `PlannedDirtyAccumulator` and deferred planned
  execution helpers so dirty metadata can be recorded during work and merged
  deterministically after a phase. Added a serial
  `execute_phase_deferred_dirty` helper that validates and executes one phase
  range as the future worker-backend handoff, plus `SerialPhaseExecutor` and
  `execute_phase_deferred_dirty_with` so backend dispatch can be tested without
  owning threads yet. Added `PlannedDirtyPartitions`,
  `PlannedPhaseExecutionScratch`, deterministic partition merge helpers, and
  `execute_phase_partitioned_dirty_with` so future worker backends can avoid
  sharing dirty buffers or result counters during phase dispatch. Added
  test-only threaded executor coverage for disjoint mutable chunk phases and
  overlapping read-only chunk phases with const-view enforcement. Added
  `ExecutorPhaseRange` and `execute_operation_index_range` as the explicit
  operation-index adapter contract for later backend integrations. Added
  `ScopedThreadPhaseExecutor` as a production scoped-thread prototype that
  joins before returning and preserves deterministic result reduction.
  Documented partitioned failure semantics and covered completed dirty
  partitions after a threaded policy mismatch.
- Reason: Concurrent tile-world execution needs a testable ownership and
  barrier contract before worker scheduling or external task systems are
  introduced.
- Affected docs: `docs/architecture/block.md`,
  `docs/architecture/queued-operations.md`, `tests/AGENTS.md`
- Affected code: `include/tess/ops/queued.h`,
  `tests/tess_queued_test.cc`

## 2026-06-06 - Simulation Integration MVP

- Changed: Added movement intent validation/commit helpers, render tile
  deltas, and synchronous unit/weighted scheduler ticks that execute queued
  operations, mark path agents dirty from configured field masks, run path
  ticks, and emit presentation deltas.
- Reason: Colony-sim consumers need a public integration flow instead of
  manually coupling queued edits, path dirtying, occupancy checks, and render
  refreshes.
- Affected docs: `docs/architecture/README.md`,
  `docs/architecture/path.md`, `docs/architecture/simulation.md`
- Affected code: `CMakeLists.txt`, `include/tess/sim/movement.h`,
  `include/tess/sim/render_delta.h`, `include/tess/sim/scheduler.h`,
  `include/tess/tess.h`, `tests/CMakeLists.txt`,
  `tests/tess_sim_scheduler_test.cc`

## 2026-06-06 - Path Agent Runtime MVP

- Changed: Added a public path-agent wrapper over `PathRequestRuntime` for
  goal assignment, ticketed request processing, result application,
  tile-by-tile advancement, conservative replanning after world edits, and
  warm allocation-free unit and weighted agent batches.
- Reason: Mainline implementation needs a testable simulation-facing path loop
  before adding a scheduler, ECS adapter, reservations, or local avoidance.
- Affected docs: `docs/architecture/path.md`,
  `docs/planning/benchmark-plan.md`, `tests/AGENTS.md`
- Affected code: `bench/CMakeLists.txt`, `bench/tess_path_agent_bench.cc`,
  `examples/path_agents.cc`, `examples/CMakeLists.txt`,
  `include/tess/sim/path_agent.h`, `include/tess/tess.h`,
  `tests/CMakeLists.txt`, `tests/tess_path_agent_test.cc`

## 2026-06-06 - Quality Gate Ratchet

- Changed: Fixed installed package header drift, added a public-header file-set
  test, added install smoke to pre-push, made low-noise clang-tidy checks a
  warnings-as-errors gate with a separate advisory preset, added path-agent
  benchmark thresholds, consolidated allocation-sensitive test hooks, made the
  tick dirty contract explicit in tests and docs, and split bounded weighted
  batch implementation into a path detail header.
- Reason: A tech-debt review found that installed consumers were broken and
  that several quality expectations were documented but not enforced.
- Affected docs: `README.md`, `docs/architecture/path.md`,
  `docs/dependencies.md`, `docs/planning/benchmark-plan.md`,
  `tests/AGENTS.md`
- Affected code: `.clang-tidy`, `.clang-tidy-advisory`,
  `.github/workflows/ci.yml`, `CMakeLists.txt`, `CMakePresets.json`,
  `bench/thresholds/path.json`, `cmake/TessProjectOptions.cmake`,
  `cmake/check-public-headers.cmake`, `include/tess/ops/queued.h`,
  `include/tess/path/path.h`, `include/tess/path/detail/weighted_batch.h`,
  `tests/CMakeLists.txt`, `tests/allocation_counter.*`,
  `tests/tess_path_agent_test.cc`, `tests/tess_path_agent_tick_test.cc`,
  `tools/git_hooks.py`

## 2026-06-06 - Path Agent Tick MVP

- Changed: Added a minimal synchronous path-agent tick wrapper with a
  simulation clock, dirty-gated path processing, per-tick movement advancement,
  a tick-state goal-assignment helper, focused tests, example coverage, and
  clean/dirty tick benchmarks.
- Reason: Mainline implementation needs a testable scheduler-adjacent path loop
  before adding cadence policies, ECS adapters, reservations, local avoidance,
  or async planning.
- Affected docs: `docs/architecture/path.md`,
  `docs/planning/benchmark-plan.md`, `tests/AGENTS.md`
- Affected code: `bench/tess_path_agent_bench.cc`,
  `examples/path_agents.cc`, `include/tess/sim/path_agent_tick.h`,
  `include/tess/tess.h`, `tests/CMakeLists.txt`,
  `tests/tess_path_agent_tick_test.cc`

## 2026-06-06 - Follow-Up Review Cache And Coverage Tightening

- Changed: Added chunk-version validation to weighted portal segment cache
  hits, stopped caching failed portal segments, expanded topology reachability
  coverage, labeled clang-tidy as advisory, removed a redundant block context
  alias, and narrowed the queued cppcheck false-positive suppression inline.
- Reason: Follow-up review identified the remaining weighted segment-cache
  correctness hazard, high-value topology test gaps, and minor quality-gate
  clarity issues after the first remediation commit.
- Affected docs: `docs/architecture/path.md`, `docs/dependencies.md`,
  `tests/AGENTS.md`
- Affected code: `.github/workflows/ci.yml`,
  `cmake/TessProjectOptions.cmake`, `include/tess/block/block.h`,
  `include/tess/ops/queued.h`, `include/tess/path/portal_segment_cache.h`,
  `tests/tess_path_test.cc`, `tests/tess_topology_test.cc`

## 2026-06-06 - Follow-Up API Fail-Fast And Cache Retention Contract

- Changed: Made the runtime block `for_each_chunk` overload fail fast for
  invalid `WritePolicy` values in release builds, and covered the weighted
  portal segment-cache stale-entry retention plus `clear()` reclamation
  contract in tests and docs.
- Reason: Follow-up review identified a release-only silent no-op for invalid
  runtime write policies and called out the caller-managed memory implications
  of retained stale segment-cache entries.
- Affected docs: `docs/architecture/block.md`, `docs/architecture/path.md`,
  `tests/AGENTS.md`
- Affected code: `include/tess/block/block.h`, `tests/tess_block_test.cc`,
  `tests/tess_path_test.cc`

## 2026-06-06 - Path Request Runtime MVP

- Changed: Added a small path request runtime that owns ticketed requests,
  stable copied result paths, reusable unit A* scratch, the unit route cache,
  weighted batch scratch, and a caller-managed weighted portal segment cache.
  Runtime processing supports cached unit paths and many-request weighted batch
  paths with an explicit world-edit cache clear cadence.
- Reason: Mainline simulation work needs a testable request/result lifecycle
  and cache ownership boundary before higher-level scheduling or agent systems.
- Affected docs: `docs/architecture/path.md`, `tests/AGENTS.md`
- Affected code: `include/tess/path/path_runtime.h`, `include/tess/tess.h`,
  `tests/CMakeLists.txt`, `tests/tess_path_runtime_test.cc`

## 2026-06-06 - Runtime Block Read-Only Enforcement

- Changed: Made the runtime `for_each_chunk(world, domain, policy, fn)`
  overload dispatch policy-selected chunk view types, including
  `ChunkView<const World>` for `ReadOnly` on mutable worlds. Fixed-policy
  mutable callers now use the compile-time policy overload.
- Reason: Follow-up review flagged the previous runtime policy argument as a
  footgun because it accepted `ReadOnly` without enforcing const callback
  views.
- Affected docs: `docs/architecture/block.md`, `tests/AGENTS.md`
- Affected code: `bench/tess_bench.cc`, `include/tess/block/block.h`,
  `tests/tess_block_test.cc`

## 2026-06-06 - Review Remediation Gates And API Safety

- Changed: Fixed installed public header coverage, added stricter CI quality
  gates and install smoke coverage, made block read-only context world access
  const-correct, documented the runtime block dispatch limitation, added owning
  chunk domains for allocating domain builders, advanced topology versions
  through explicit world methods, and tightened tooling diagnostics.
- Reason: A read-only external review identified package install breakage,
  unexercised quality gates, and API safety gaps that could hide regressions.
- Affected docs: `docs/architecture/block.md`,
  `docs/architecture/storage.md`, `docs/dependencies.md`, `tests/AGENTS.md`
- Affected code: `.github/workflows/ci.yml`, `CMakeLists.txt`,
  `cmake/TessProjectOptions.cmake`, `include/tess/block/block.h`,
  `include/tess/core/shape.h`, `include/tess/storage/world.h`,
  `include/tess/topology/topology.h`, `tests/tess_block_test.cc`,
  `tests/tess_storage_test.cc`, `tests/tess_topology_test.cc`,
  `tools/benchmark_trends.py`, `tools/git_hooks.py`,
  `tools/install_smoke.sh`

## 2026-06-05 - Local Topology Foundation

- Changed: Added local chunk topology that labels passable connected regions
  inside one chunk, records boundary exits to adjacent chunks, pairs boundary
  exits into directed portals, and checks reachability over the resulting
  region graph.
- Reason: Path products now need topology-owned domains and portal facts before
  further portal route quality work can be made exact and reusable.
- Affected docs: `docs/architecture/README.md`,
  `docs/architecture/topology.md`, `tests/AGENTS.md`
- Affected code: `include/tess/topology/topology.h`, `include/tess/tess.h`,
  `tests/tess_topology_test.cc`, `tests/CMakeLists.txt`

## 2026-06-05 - Portal Route Products

- Changed: Added weighted portal route products that stitch exact weighted A*
  segments through caller-supplied or chunk-boundary-derived portal waypoints,
  six-order plus greedy chunk-boundary candidate selection, warmed
  portal-segment reuse, local-domain weighted fields, and room-portal
  build/replay/candidate/cache/endpoint benchmarks.
- Reason: The remaining weighted room-portal bottleneck is search volume.
  Portal waypoints give measurable product primitives before the repository
  owns a full topology graph builder, while candidate and cost-ratio counters
  keep route quality and selection overhead visible.
- Affected docs: `docs/architecture/path.md`,
  `docs/planning/benchmark-plan.md`,
  `docs/planning/optimization-log.md`, `tests/AGENTS.md`
- Affected code: `include/tess/path/path.h`,
  `include/tess/path/distance_field_box.h`,
  `include/tess/path/portal_route.h`,
  `include/tess/path/portal_segment_cache.h`, `include/tess/tess.h`,
  `tests/tess_path_test.cc`, `bench/tess_path_weighted_bench.cc`,
  `bench/thresholds/path.json`

## 2026-06-05 - Weighted Batch Planner and Route Products

- Changed: Added a weighted batch planner API, exact weighted route products
  with chunk-version dependencies, and a narrow blocked-axis weighted detour
  fast path.
- Reason: Current weighted reuse needs a public batching surface, route-product
  dependency wiring needs a concrete exact product primitive, and blocked
  unit-cost weighted detours can avoid A* without weakening optimality.
- Affected docs: `docs/architecture/path.md`,
  `docs/planning/benchmark-plan.md`,
  `docs/planning/optimization-log.md`, `tests/AGENTS.md`
- Affected code: `include/tess/path/path.h`, `tests/tess_path_test.cc`,
  `bench/tess_path_weighted_bench.cc`, `bench/thresholds/path.json`

## 2026-06-05 - Weighted Multi-Goal and Bounded Field Reuse

- Changed: Added weighted multi-goal batch benchmarks, bounded-cost weighted
  distance-field construction, explicit chunk-version dependency helpers, and
  threshold gates for the accepted bounded weighted benchmarks.
- Reason: Weighted many-agent workloads need exact reuse beyond one shared
  goal, and small integral terrain costs can avoid binary heap field-building
  overhead while preserving optimal weighted paths.
- Affected docs: `docs/architecture/path.md`,
  `docs/planning/benchmark-plan.md`,
  `docs/planning/optimization-log.md`, `tests/AGENTS.md`
- Affected code: `include/tess/path/path.h`, `tests/tess_path_test.cc`,
  `bench/tess_path_weighted_bench.cc`, `bench/thresholds/path.json`

## 2026-06-05 - Route Cache Invalidation Hook

- Changed: Added `RouteCacheScratch::invalidate()` to drop cached route data
  without resetting cache hit/miss counters.
- Reason: Stable-map route reuse needs an explicit hook for passability or
  movement-rule changes, while benchmark and diagnostic callers may want to
  preserve accumulated cache stats across invalidations.
- Affected docs: `docs/architecture/path.md`,
  `docs/planning/optimization-log.md`, `tests/AGENTS.md`
- Affected code: `include/tess/path/path.h`, `tests/tess_path_test.cc`

## 2026-06-05 - Weighted Path Reuse and Fast Path

- Changed: Added weighted shared-goal distance fields, an exact weighted
  unit-cost direct fast path, opt-in route-cache world-version fingerprints,
  and weighted shared-goal path benchmarks.
- Reason: Weighted terrain must remain correct, but repeated weighted
  point-to-point A* is too slow for many-agent shared-goal workloads and
  unit-cost maps should not pay the general weighted heap cost.
- Affected docs: `docs/architecture/path.md`,
  `docs/planning/optimization-log.md`, `tests/AGENTS.md`
- Affected code: `include/tess/path/path.h`, `tests/tess_path_test.cc`,
  `bench/CMakeLists.txt`, `bench/tess_path_weighted_bench.cc`,
  `bench/thresholds/path.json`

## 2026-06-05 - Weighted A* Stress Diagnostics

- Changed: Added weighted sparse-blocker, room-portal, and mixed-batch path
  benchmarks, weighted path threshold gates, and a path diagnostic counter for
  weighted entry-cost reads.
- Reason: Weighted terrain needs regression coverage that exercises realistic
  search volume and exposes whether time is spent in heap churn, neighbor
  expansion, passability reads, cost reads, or allocations.
- Affected docs: `docs/planning/benchmark-plan.md`,
  `docs/planning/optimization-log.md`, `tests/AGENTS.md`
- Affected code: `include/tess/diagnostics/diagnostics.h`,
  `include/tess/path/path.h`, `tests/tess_diagnostics_enabled_test.cc`,
  `bench/tess_bench.cc`, `bench/thresholds/path.json`

## 2026-06-05 - Weighted A* Entry Costs

- Changed: Added a separate `weighted_astar_path` API for positive integral
  entry costs, plus weighted correctness tests and weighted path benchmarks.
- Reason: Weighted terrain is likely to be important, but the existing
  unit-cost fast paths and route reuse proofs do not apply to arbitrary entry
  costs. The weighted API preserves optimal weighted paths while leaving the
  optimized unit-cost path unchanged.
- Affected docs: `docs/architecture/path.md`,
  `docs/planning/benchmark-plan.md`,
  `docs/planning/optimization-log.md`, `tests/AGENTS.md`
- Affected code: `include/tess/path/path.h`, `tests/tess_path_test.cc`,
  `bench/tess_bench.cc`, `bench/thresholds/path.json`

## 2026-06-05 - Symbolicated Benchmark Profiling Workflow

- Changed: Added a `bench-profile` preset and a `tools/profile_benchmark.sh`
  command helper for non-interactive `samply` captures with debug information,
  frame pointers, and presymbolication.
- Reason: Release benchmark profiles were saved without usable symbols and
  `samply record` launched a local viewer process that could outlive the
  profiling run. The profiling workflow now emits a direct shell command to run
  as a separate capture step, producing repeatable saved profiles outside the
  repository and loading them explicitly only when needed.
- Affected docs: `docs/planning/benchmark-plan.md`
- Affected code: `CMakePresets.json`, `tools/profile_benchmark.sh`

## 2026-06-05 - Route Cache Path Reuse

- Changed: Added reusable exact route and same-goal suffix caching for the
  current unit-cost A* path model. Added cache-hit benchmark counters and
  monitored batch benchmarks for repeated route and suffix reuse cases.
- Reason: Many-agent batches can repeat identical routes or ask for suffixes
  of an already-computed route. Reusing cached optimal path spans avoids
  rerunning A* while preserving the general fallback.
- Affected docs: `docs/architecture/path.md`,
  `docs/planning/benchmark-plan.md`,
  `docs/planning/optimization-log.md`, `tests/AGENTS.md`
- Affected code: `include/tess/path/path.h`, `tests/tess_path_test.cc`,
  `bench/tess_bench.cc`, `bench/thresholds/path.json`

## 2026-06-05 - Shared-Goal Distance Fields

- Changed: Added reusable reverse distance-field scratch and shared-goal path
  reconstruction for the current unit-cost passability path model. Added
  many-agent batch benchmarks and reuse counters for unique starts, goals, and
  chunks.
- Reason: Independent A* repeats substantial work for agents sharing goals.
  Reverse distance fields amortize the search across all starts for a goal and
  are a better fit for 100-agent shared-destination workloads.
- Affected docs: `docs/architecture/path.md`,
  `docs/planning/benchmark-plan.md`,
  `docs/planning/optimization-log.md`, `tests/AGENTS.md`
- Affected code: `include/tess/path/path.h`, `tests/tess_path_test.cc`,
  `bench/tess_bench.cc`, `bench/thresholds/path.json`

## 2026-06-05 - Unit-Cost A* Bucket Open Set

- Changed: Replaced the general fallback A* binary heap with a two-bucket
  monotone open set for the current unit-cost Manhattan heuristic path model.
- Reason: With unit-cost axis-adjacent movement and a consistent Manhattan
  heuristic, generated fallback nodes have the current `f` score or `f + 2`.
  The bucket queue removes binary heap maintenance while preserving optimal
  path ordering for this MVP path model.
- Affected docs: `docs/architecture/path.md`,
  `docs/planning/optimization-log.md`
- Affected code: `include/tess/path/path.h`

## 2026-06-05 - Path Direct Fast-Path Prechecks

- Changed: Pathfinding now tries shape-relevant direct Manhattan axis orders,
  simple axis-aligned detours, verified 2D and 3D plane-gap routes, and 2D
  forced-gap sequences before fallback A*, and rejects full axis-plane
  barriers before expanding A* nodes.
- Reason: Uniform-cost direct paths and fully separating blocked planes can be
  resolved exactly without general A* search. Axis-aligned one-tile parallel
  detours and verified routes through a passable plane gap are also optimal
  under the current unit-cost movement model. Forced single-gap barriers have
  fixed crossings, while non-matching cases preserve normal A* fallback
  behavior.
- Affected docs: `docs/architecture/path.md`,
  `docs/planning/benchmark-plan.md`,
  `docs/planning/optimization-log.md`
- Affected code: `include/tess/path/path.h`, `tests/tess_path_test.cc`,
  `bench/tess_bench.cc`

## 2026-06-05 - Testable MVP Scope

- Changed: Added an explicit MVP checkpoint that narrows the first end-to-end
  prototype to always-resident queued execution plus minimal unit-cost A*
  pathfinding.
- Reason: The full v1 milestone remains useful design intent, but it is too
  broad to serve as the first testable implementation target.
- Affected docs: `docs/planning/v1-milestone-plan.md`
- Affected code: none

## 2026-06-05 - One Millisecond Benchmark Investigation Gate

[Superseded 2026-07-06: the flat 1 ms ceiling is no longer the gate policy.
Thresholds now carry calibrated per-benchmark CPU-time ceilings per
`bench/thresholds/*.json` and `docs/planning/benchmark-plan.md` (the path
suite carries values both below and far above 1 ms for investigated batch
profiles, and two manual-time cache benchmarks gate on real time instead).
The 1 ms value survives only as the default investigation ceiling for
benchmarks that have not yet been individually calibrated.]

- Changed: Current benchmark threshold scaffolds now enforce a 1 ms CPU-time
  ceiling for each named benchmark while leaving real-time limits unset. The
  path threshold set includes 64x64, 512x512, and 1024x1024 open-world A*
  benchmarks in addition to the cheap smoke path.
- Reason: Any operation taking longer than 1 ms should be investigated, and the
  benchmark policy should encode that expectation directly.
- Affected docs: `docs/planning/benchmark-plan.md`
- Affected code: `bench/thresholds/key-conversions.json`,
  `bench/thresholds/storage.json`, `bench/thresholds/block.json`,
  `bench/thresholds/queued.json`, `bench/thresholds/path.json`

## 2026-06-05 - Path Benchmark Profiling Counters

- Changed: `PathResult` now reports expanded and reached node counts, and path
  benchmarks publish cost, path-node, expanded-node, and reached-node counters.
- Reason: Large-world path timing needs to distinguish graph-work growth from
  per-node overhead. The 1024x1024 open-grid profile currently points to heap
  maintenance, passability/world lookup, and 2D use of six-axis neighbor
  generation as the first bottlenecks.
- Affected docs: `docs/architecture/path.md`,
  `docs/planning/benchmark-plan.md`
- Affected code: `include/tess/path/path.h`, `bench/tess_bench.cc`,
  `tests/tess_path_test.cc`

## 2026-06-05 - Queued Execution Bridge

- Changed: Added explicit execution helpers for planned queued operations and
  plans. Execution runs caller callbacks through policy-typed serial block
  contexts and marks visited chunks dirty from declared dirty masks.
- Reason: The queued planner needed a minimal synchronous execution bridge
  before scheduler-owned execution, barriers, result channels, or async work.
- Affected docs: `docs/architecture/queued-operations.md`,
  `docs/planning/benchmark-plan.md`, `tests/AGENTS.md`
- Affected code: `include/tess/ops/queued.h`, `tests/tess_queued_test.cc`,
  `bench/tess_bench.cc`, `bench/CMakeLists.txt`,
  `bench/thresholds/queued.json`

## 2026-06-05 - MVP Path Foundation

- Changed: Added a minimal always-resident A* path API over boolean-like typed
  passability fields, reusable `PathScratch`, path tests, benchmark coverage,
  and equal-score tie-breaking that prefers deeper paths to avoid open-grid
  wavefront expansion. Path scratch now clears only nodes touched by the prior
  query instead of resetting dense arrays for the whole world on every query.
- Reason: The first MVP needs a concrete path query that proves top-down 2D,
  vertical 2D, and shallow 3D share the existing coordinate/storage model
  before topology prechecks, portal graphs, weighted movement, or distance
  fields are introduced.
- Affected docs: `docs/architecture/path.md`, `docs/architecture/README.md`,
  `docs/planning/benchmark-plan.md`, `tests/AGENTS.md`
- Affected code: `include/tess/path/path.h`, `include/tess/tess.h`,
  `CMakeLists.txt`, `tests/tess_path_test.cc`, `tests/CMakeLists.txt`,
  `examples/mvp_path.cc`, `examples/CMakeLists.txt`, `bench/tess_bench.cc`,
  `bench/CMakeLists.txt`, `bench/thresholds/path.json`

## 2026-06-05 - Queued Operations Foundation

- Changed: Added a public `FrameOps` queue, minimal chunk-domain descriptors,
  deterministic operation handles/ids, and a planner scaffold that validates
  write policies/domains and expands domains into ordered chunk-key vectors.
- Reason: M4 needs a stable queued-intent foundation before adding executors,
  scheduler integration, topology/path systems, richer diagnostics, result
  channels, or work-contract-style maintenance.
- Affected docs: `docs/architecture/queued-operations.md`,
  `docs/architecture/README.md`, `tests/AGENTS.md`
- Affected code: `include/tess/ops/queued.h`, `include/tess/tess.h`,
  `tests/tess_queued_test.cc`, `tests/CMakeLists.txt`

## 2026-06-05 - Queued Operation Diagnostics

- Changed: Added structured operation failure reasons, diagnostic access
  metadata, invalid explicit-chunk detail, and deterministic report lookup and
  count helpers.
- Reason: The queued planner needs testable diagnostics and a hazard-metadata
  foothold before adding executors, barriers, batching, or scheduler behavior.
- Affected docs: `docs/architecture/queued-operations.md`, `tests/AGENTS.md`
- Affected code: `include/tess/ops/queued.h`, `tests/tess_queued_test.cc`

## 2026-06-05 - Planned Operation Block Adapter

- Changed: Added non-owning adapters from successful planned operations to
  `ChunkDomain` and policy-typed `BlockCtx` instances.
- Reason: Planned chunk work needs a practical bridge to the existing serial
  block API before introducing executors, queued callbacks, barriers, or
  scheduler behavior.
- Affected docs: `docs/architecture/queued-operations.md`, `tests/AGENTS.md`
- Affected code: `include/tess/ops/queued.h`, `tests/tess_queued_test.cc`

## 2026-06-05 - Queued Field Access Metadata

- Changed: Added untyped field access masks to queued, planned, and reported
  operations, plus validation rejecting read-only operations with write masks.
- Reason: Planner diagnostics and future hazard checks need explicit
  read/write metadata before typed field binding, callbacks, barriers, or
  executors are introduced.
- Affected docs: `docs/architecture/queued-operations.md`, `tests/AGENTS.md`
- Affected code: `include/tess/ops/queued.h`, `tests/tess_queued_test.cc`

## 2026-06-05 - Basic Queued Hazard Validation

- Changed: Added deterministic field-mask hazard validation across overlapping
  planned chunk domains. Later conflicting operations are rejected with
  conflict handle/id and conflict-mask diagnostics.
- Reason: The queued planner should catch obvious write/write and read/write
  conflicts before adding barriers, reordering, batching, or execution.
- Affected docs: `docs/architecture/queued-operations.md`, `tests/AGENTS.md`
- Affected code: `include/tess/ops/queued.h`, `tests/tess_queued_test.cc`

## 2026-06-01 - Documentation Model

- Changed: TDDs are treated as historical design intent, while maintained
  architecture docs track current implementation behavior.
- Reason: TDDs are likely to drift as implementation validates and revises the
  original design.
- Affected docs: `docs/README.md`, `docs/architecture/README.md`,
  `docs/tdd/README.md`
- Affected code: none

## 2026-06-04 - Initial Chunk Key Ordering

- Changed: The first implemented `ChunkKey` layout uses row-major chunk
  ordering instead of the Morton ordering preferred by the historical TDD.
- Reason: Row-major ordering keeps the M1 coordinate/key API simple and
  testable while chunk-order benchmarks are still pending.
- Affected docs: `docs/tdd/core-shape-coordinate-key-system.md`
- Affected code: `include/tess/core/shape.h`, `tests/tess_shape_test.cc`

## 2026-06-04 - Key Conversion Performance Gates

- Changed: Added key conversion benchmarks, zero-allocation/noexcept tests, and
  disabled benchmark threshold scaffolding for future regression gates.
- Reason: M1 key conversion is hot-path foundation, but wall-clock thresholds
  should wait until same-machine baselines are stable.
- Affected docs: `docs/planning/benchmark-plan.md`
- Affected code: `bench/tess_bench.cc`, `bench/CMakeLists.txt`,
  `tools/benchmark_thresholds.py`, `tests/tess_shape_test.cc`

## 2026-06-04 - Storage Performance Gate Scaffold

- Changed: Added disabled threshold scaffolding for storage benchmarks,
  including chunk page access, field iteration, and always-resident world
  lookup/iteration benchmarks.
- Reason: Storage hot paths should have named regression gates, but hard
  wall-clock limits should wait for stable same-machine baselines.
- Affected docs: `README.md`, `docs/planning/benchmark-plan.md`
- Affected code: `bench/CMakeLists.txt`, `bench/thresholds/storage.json`

## 2026-06-04 - Always-Resident Chunk Metadata

- Changed: Always-resident worlds now own per-chunk metadata with sleeping and
  active states plus raw dirty/active flag tracking.
- Reason: Planner and block domains need chunk-level dirty/active discovery
  without scanning tile fields.
- Affected docs: `docs/architecture/storage.md`
- Affected code: `include/tess/storage/world.h`, `tests/tess_storage_test.cc`,
  `bench/tess_bench.cc`

## 2026-06-04 - Minimal Serial Block Domains

- Changed: Added a public `tess::block` foundation with chunk-domain builders,
  const-correct `ChunkView`, and serial `for_each_chunk` execution over
  always-resident worlds.
- Reason: M3 needs deterministic block/domain execution before adding planner
  integration, diagnostics, scratch storage, or external scheduler backends.
- Affected docs: `docs/architecture/block.md`, `docs/dependencies.md`
- Affected code: `include/tess/block/block.h`, `include/tess/tess.h`,
  `tests/tess_block_test.cc`, `bench/tess_bench.cc`

## 2026-06-04 - Chunk-Local Tile Iteration

- Changed: `ChunkView` now exposes local tile coordinate/id conversion,
  current-chunk world coordinate conversion, and allocation-free serial
  `for_each_tile` traversal in ascending `LocalTileId` order.
- Reason: Block executors need deterministic chunk-local tile traversal before
  adding planners, parallel scheduling, scratch storage, or diagnostics.
- Affected docs: `docs/architecture/block.md`, `tests/AGENTS.md`
- Affected code: `include/tess/block/block.h`, `tests/tess_block_test.cc`,
  `bench/tess_bench.cc`, `bench/CMakeLists.txt`

## 2026-06-04 - Chunk Boundary Helpers

- Changed: `ChunkView` now exposes local bounds, signed local-candidate
  validation/conversion, non-degenerate boundary/interior predicates, and
  signed local-candidate world coordinate conversion.
- Reason: Topology and path systems need allocation-free helpers to identify
  current-chunk candidates before movement rules, transitions, halos, scratch
  storage, diagnostics, or parallel execution are introduced.
- Affected docs: `docs/architecture/block.md`, `tests/AGENTS.md`
- Affected code: `include/tess/block/block.h`, `tests/tess_block_test.cc`,
  `bench/tess_bench.cc`, `bench/CMakeLists.txt`

## 2026-06-04 - BlockCtx Foundation

- Changed: Added `BlockCtx` as a non-owning serial block execution context over
  a world, chunk domain, and write policy. Existing `for_each_chunk` now
  delegates through the context.
- Reason: M3 needs an explicit context object before adding planner phases,
  scratch storage, diagnostics, scheduling, or policy enforcement.
- Affected docs: `docs/architecture/block.md`, `tests/AGENTS.md`
- Affected code: `include/tess/block/block.h`, `tests/tess_block_test.cc`,
  `bench/tess_bench.cc`, `bench/CMakeLists.txt`

## 2026-06-04 - CI Benchmark Baseline Collection

- Changed: Added block benchmark threshold scaffolding, non-gating CI baseline
  JSON collection for key, storage, and block benchmark groups, a baseline
  summary helper for threshold calibration, and README-visible benchmark trend
  snapshot generation.
- Reason: Timing limits should be calibrated from repeated samples on the
  pinned CI runner family that will enforce them, not from developer machines.
- Affected docs: `README.md`, `docs/dependencies.md`, `docs/performance.md`,
  `docs/planning/benchmark-plan.md`
- Affected code: `.github/workflows/ci.yml`, `bench/CMakeLists.txt`,
  `bench/thresholds/block.json`, `tools/benchmark_artifact_metadata.py`,
  `tools/benchmark_baseline_summary.py`, `tools/benchmark_trends.py`

## 2026-06-04 - ReadOnly Block Policy Enforcement

- Changed: `BlockCtx` is now policy-typed, and `ReadOnly` contexts expose
  const chunk views even for mutable worlds. Added `for_each_chunk<Policy>` for
  policy-typed serial execution while keeping the runtime-policy overload as a
  compatibility path.
- Reason: M3 write policies need a real enforcement foothold without adding
  planner phases, scratch storage, diagnostics, or parallel scheduling.
- Affected docs: `docs/architecture/block.md`, `tests/AGENTS.md`
- Affected code: `include/tess/block/block.h`, `tests/tess_block_test.cc`,
  `bench/tess_bench.cc`

## 2026-06-04 - Caller-Owned Block Scratch

- Changed: Added `BlockScratch` as reusable caller-owned bump storage and
  allowed policy-typed `BlockCtx` instances to carry an optional non-owning
  scratch pointer.
- Reason: M3 block algorithms need allocation-free temporary storage during
  serial chunk and tile iteration before planner-owned arenas, diagnostics,
  worker pools, or parallel scheduling exist.
- Affected docs: `docs/architecture/block.md`, `tests/AGENTS.md`
- Affected code: `include/tess/block/block.h`, `tests/tess_block_test.cc`

## 2026-06-05 - Block Scratch Benchmark Scaffold

- Changed: Added disabled block benchmark threshold entries and benchmark
  workloads for scratch allocation/reset and context scratch tile iteration.
- Reason: Caller-owned scratch needs CI baseline visibility before real
  wall-clock gates can be calibrated.
- Affected docs: `docs/performance.md`, `docs/planning/benchmark-plan.md`
- Affected code: `bench/tess_bench.cc`, `bench/thresholds/block.json`

## 2026-06-05 - Caller-Owned Block Diagnostics

- Changed: Added `BlockDiagnostics` with a scratch allocation failure counter
  and allowed policy-typed `BlockCtx` instances to carry an optional
  non-owning diagnostics pointer.
- Reason: Scratch exhaustion needs an explicit reporting path before
  planner-owned arenas, rich diagnostics, worker pools, or parallel scheduling
  exist.
- Affected docs: `docs/architecture/block.md`, `tests/AGENTS.md`
- Affected code: `include/tess/block/block.h`, `tests/tess_block_test.cc`

## 2026-06-04 - Local Warning and Analysis Presets

- Changed: Added project-local warning flags, warnings-as-errors,
  clang-tidy, and ASan/UBSan CMake options plus presets for tests and
  benchmarks.
- Reason: M0 scaffolding needs repeatable compiler diagnostics and dynamic
  analysis without exporting Tess warning policy to downstream consumers.
- Affected docs: `docs/dependencies.md`, `docs/style.md`
- Affected code: `.clang-tidy`, `CMakeLists.txt`, `CMakePresets.json`,
  `cmake/TessProjectOptions.cmake`, `tests/CMakeLists.txt`,
  `bench/CMakeLists.txt`

## 2026-06-04 - clangd Project Configuration

- Changed: Added `.clangd` to point editor tooling at the default developer
  compilation database and keep clang-tidy checks on clangd's strict fast-check
  filter.
- Reason: clangd needs project configuration plus editor startup flags to
  provide consistent clang-tidy diagnostics while editing.
- Affected docs: `docs/dependencies.md`, `docs/style.md`
- Affected code: `.clangd`

## 2026-06-04 - Opt-In Cppcheck Preset

- Changed: Added a project-local cppcheck CMake option and `dev-cppcheck`
  preset for local test and benchmark targets.
- Reason: cppcheck provides a complementary static-analysis pass without
  exporting project analysis policy to downstream consumers.
- Affected docs: `docs/dependencies.md`, `docs/style.md`
- Affected code: `CMakeLists.txt`, `CMakePresets.json`,
  `cmake/TessProjectOptions.cmake`
