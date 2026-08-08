# Design Changelog

Records meaningful design changes from the original TDDs. Entries from
2026-07-09 through 2026-07-10 that postdate the earlier archive are in
[`CHANGELOG-archive-2026-07-09-10.md`](CHANGELOG-archive-2026-07-09-10.md);
entries from 2026-07-11 through 2026-07-28 are in
[`CHANGELOG-archive-2026-07-11-28.md`](CHANGELOG-archive-2026-07-11-28.md);
older entries are in [`CHANGELOG-archive.md`](CHANGELOG-archive.md) and
[`CHANGELOG-archive-2026-06.md`](CHANGELOG-archive-2026-06.md).

## 2026-08-07 - Counter, format, and cost-arithmetic hardening

- Fixed: `EventStream::retire_batch` clamps its outstanding decrement.
  Every other terminalization site routes through
  `FlowAccounting::record_left_outstanding`, which floors at zero;
  `EventStream` subtracted its batch size directly, so any state where the
  accountant reports fewer outstanding than the batch holds wrapped the
  counter. Measured with a `reset()` between publish and retire:
  `outstanding_current` became 18446744073709551614, which
  `inventory_tick_weighted` then multiplies by. Diagnostics corruption
  only, but the corrupted values are exactly the ones a health view
  reports.
- Fixed: the archive writes `lattice_version` through an explicit
  `uint32_t` cast, and both save and load static_assert that the value
  fits the header's 32-bit field. `append_unsigned_le` emits
  `sizeof(UInt)` bytes and `lattice_version` is `static constexpr auto`,
  while `LatticeType` requires only convertibility to `uint32`, so a
  custom lattice -- a documented extension point -- with a `uint64`
  version wrote a 125-byte header against the fixed-width reader.
- Recorded: the cast alone closed only half of that, which a review pass
  proved by execution. With a version ABOVE the 32-bit range, save still
  returned `Ok` and load returned `LatticeMismatch`, because the truncated
  stored value can never equal the full-width trait the load compares it
  against -- the same "saves Ok, never loads" class the cast was meant to
  remove. A lattice that cannot be represented simply cannot be persisted
  in format v1, so rejecting it at compile time is both honest and cheaper
  than a runtime status a caller cannot act on. The `WorldArchiveInfo`
  assignment and the load comparison are narrowed explicitly too, so a
  `-Werror` build does not trip on the conversion.
- Recorded: a provider transition whose source lies outside the enumerated
  chunk is dropped silently in every build. A debug assertion would abort
  the tests that deliberately violate the contract to pin the drop, and a
  diagnostics counter for a caller bug would be public surface, so the
  contract now states the silence and tells a provider author what to
  check when edges go missing. Revisit if third-party providers become
  common.
- Recorded: making terminal agents immovable is a throughput trade. A
  one-wide corridor that used to clear because a passing agent shoved an
  arrived agent aside now stays blocked. That is the intended behavior --
  the shove corrupted a documented-terminal lifecycle -- but it is a real
  change on corridor maps, so the simulation note now says so and lists
  what a caller can do about it.

- Changed: the A* plane-gap and axis-detour fast paths use
  `detail::saturating_add`. `detail::manhattan` clamps each term at
  `uint32` max and the sums then wrapped, asymmetric with
  `best_chunk_portal`, which already saturated the identical expression.
  Not constructible today -- it needs a span beyond 2^32 tiles, which
  cannot be allocated -- so this is consistency, not a live defect, and it
  carries no test for that reason.
- Changed: `DeltaCollector` derives its coalesce slot count from
  `pending_entities_.capacity()` rather than the requested
  `entity_capacity`. `append_entity` admits while `size() != capacity()`
  and `reserve(n)` guarantees only `capacity() >= n`, so an implementation
  rounding past `2 * entity_capacity` could fill every slot and leave both
  unbounded probe loops spinning. Not reproducible on libstdc++, libc++, or
  MSVC, all of which allocate exactly -- which is precisely why the
  invariant is now enforced structurally instead of resting on an
  allocator's behaviour.
## 2026-08-07 - Graph freshness detects stamps, not field edits

- Recorded: `precheck.h` claimed that "a graph that no longer matches the
  world is GraphStale". Freshness compares recorded chunk topology
  versions, residency generations, the shape, and the class and provider
  stamps — and a raw field write advances none of them, since only
  `mark_topology_dirty` and `mark_topology_rebuilt` move a topology
  version. Editing a field a movement class or its provider reads leaves a
  built graph reporting fresh, and the failure is not conservative:
  `precheck_path` returns a definitive `Unreachable`,
  `precheck_rules_out_path` is true, and `precheck_prepass` records
  `NoPath` and marks the request processed, so the search never runs. The
  built-in `StairTransitions` cannot compensate — it is an empty type, so
  its instance identity is permanently null and its revision permanently
  zero, and both stamps compare equal across any edit.
- Decided: this stays a caller obligation rather than becoming automatic.
  Bumping a topology version on every field write would put that cost on
  the hot write path, and the explicit dirty set is already the contract
  for incremental rebuilding. The obligation is now stated where an edit
  is written (`StairTransitions`), where the answer is consumed
  (`precheck_path`), and in the topology architecture note, instead of
  being implied by a claim that overstated the check.
- Changed: `append_provider_portals` drops a transition whose source lies
  outside the enumerated chunk instead of only asserting it. Incremental
  removal keys on `portal.from.chunk`, so such a portal was never erased
  while every update touching that chunk appended it again — unbounded
  growth and divergence from a full rebuild, live only where assertions
  are compiled out. Measured on a release build with a deliberately
  misowning provider: the portal set grew 35 to 36 over repeated updates
  and stopped matching a full rebuild; with the drop it is stable. The
  face-neighbor assertion is unchanged.
- Changed: `for_each_dependency_chunk` rejects an out-of-world origin, as
  the forward and reverse probes already did. `chunk_coord` casts a
  negative component to unsigned, so the sink received an arbitrary key —
  measured at 4611686018427387903 against a chunk count of 4 — which
  `capture_field_product_dependencies` uses to index an unchecked array.
  Latent, since the only in-tree caller passes in-world tiles.
## 2026-08-07 - Terminal agents are immovable under priority inheritance

- Fixed: `start_deciding` refuses an agent with no goal or a phase of
  `Unreachable`, the way it already refuses one standing on impassable
  terrain — it claims the tile so later deciders are vertex-rejected, and
  returns failure so the inheriting agent backtracks. The exclusion existed
  in two of the three places that needed it: the priority loop skips such
  agents, and the apply pass tests `has_goal && phase != Unreachable`
  before touching a stay-put agent. Only inheritance omitted it, and
  ordering made that reachable rather than theoretical — inactive agents
  are forced to `elapsed = 0`, so the descending sort always leaves them
  undecided when an active neighbour decides. The consequences were an
  `Unreachable` agent resurrected into `Blocked` and re-entering the retry
  budget, a second `fail_path_agent_flow` for a single admission (which
  breaks `FlowCounters::retention_identity_holds` permanently, since
  `failed` increments twice against one `record_left_outstanding`), and
  arrived agents being shoved around by passing traffic while counted in
  `stats.frame.advanced`.
- Fixed: the arrival check is gated on `has_goal`. `clear_path_agent_goal`
  zeroes `goal`, so a goalless agent on the origin tile compared equal to
  it and registered an arrival for a journey that was never admitted.
  Every other arrival site in the library reaches its check behind that
  gate. With the inheritance fix a goalless agent no longer moves, so this
  path is no longer reachable through PIBT; the guard is kept because the
  invariant belongs at the check, not in an argument about which callers
  can reach it, and the test pins the invariant rather than the path.
## 2026-08-07 - Residency intervals scope dirty observations

- Fixed: `DirtyObservation` carries the residency generation it was taken
  in, and `clear_dirty_observed` refuses one from an earlier interval.
  Reloading a sparse chunk assigns a fresh `ChunkMeta`, restarting `version`
  at zero while the slot generation stays monotonic, so an observation taken
  before an eviction could compare equal to a mark made after the reload and
  clear a dirty flag it never saw. `chunk_meta.h` states that a clear
  "succeeds only if no later dirty mark changed the version, so a
  maintenance pass cannot erase intervening marks"; across an eviction that
  did not hold, and the consequence was silent — derived state stayed stale
  with no error anywhere. The dense world reaches the same shape only
  through 2^32 version wraparound; sparse needed one eviction.
  `route_cache.h` already folded the residency generation into its
  fingerprint for exactly this reason, so the precedent set the approach.
  The observation gains a trailing defaulted member, which keeps existing
  aggregate initialization valid; always-resident worlds pass zero on both
  sides and are unaffected.
- Changed: `ensure_resident` is total rather than asserted. It is the only
  residency entry point with no checked counterpart, and `ChunkDirectory`'s
  direct-slot mode indexes its slot table by the key itself, so an
  out-of-range key wrote one past the end in any build with assertions
  compiled out — while `find` deliberately bounds-checked the read path.
  It now returns an invalid handle, matching `try_chunk`, `try_meta`, and
  `residency_generation`. The assertion was removed rather than kept
  alongside the check: a debug abort would have made the defined behaviour
  untestable, and the read paths set the precedent of refusing quietly.
- Changed: `dirty_chunk_domain` and `active_chunk_domain` sort. A sparse
  world enumerates matches in residency order, a function of load and
  eviction history rather than of content, so two histories reaching the
  same resident set visited chunks in different orders — while
  `block.h` calls a `ChunkDomain` an "ordered view" and the block
  architecture note guarantees deterministic iteration for domains from the
  provided builders. The builders already allocate a vector, so they absorb
  the sort; the scans stay unordered. Note that `dirty_chunks()` and
  `active_chunks()` allocate too -- only the caller-owned
  `collect_dirty_chunks()`/`collect_active_chunks()` can avoid it, and only
  with a pre-sized output vector.

## 2026-08-07 - Gate lists derive from the tree, not from hand-kept copies

- Changed: `bench/CMakeLists.txt` derives the gated family set from its own
  `BUILDSYSTEM_TARGETS` and exposes `tess_bench_all_thresholds`; the
  workflow builds that one target. The eighteen family names previously
  lived in a workflow loop, in CMake, in `CONTRIBUTING.md`, and in a test
  that checked five of them, so a nineteenth family could ship a manifest
  and a target and gate nowhere with every test green. The aggregate is
  built with `--parallel 1`: dependency edges do not order custom targets,
  and timing gates must not run concurrently with one another.
- Changed: the gate-integrity test enumerates the workflow's job keys
  instead of restating them. `CI Gate` is one of two required checks, so a
  job added later was non-blocking by default and nothing said so. Jobs
  must now be gated or listed in an explicit advisory waiver carrying a
  reason, which also records why each advisory job is advisory.
- Changed: the advisory clang-tidy profile pins `clang-tidy-18` through
  `TESS_CLANG_TIDY_EXE`, the same mechanism the required gate uses. It
  installed the unversioned package, so its meaning tracked the runner
  image; being schedule-only, no pull request would have surfaced a drift.
- Added: a test tying `ci_changes.EXECUTABLE_OUTPUT_DOCS` to the documents
  `check_doc_outputs.py` actually finds output blocks in. The two were
  hand-maintained against different scopes — the scanner reads
  `CONTRIBUTING.md` and all of `docs/**`, the classifier knew three files —
  so an output block added anywhere else would classify its page as
  documentation-only, skip the `dev` job, and never be checked against the
  compiled binary.
- Added: `timeout-minutes` on every job, at roughly two to three times the
  measured median. The 360-minute default is charged at the runner's
  multiplier, which is two on Windows and ten on macOS.
- Changed: the exception-free job falls back to the matching quality
  preset's cache. It compiled a second copy of objects the sibling job
  already had; ccache keys on flags, so the fallback cannot yield a stale
  hit.

## 2026-08-07 - Benchmark and cache gates measure what they claim

- Fixed: every ccache namespace is terminated with `--`. GitHub restore
  keys match by prefix, so `ccache-dev-` also matched `ccache-dev-asan-`,
  `ccache-dev-cppcheck-`, and `ccache-dev-clang-tidy-`, and the `dev` job
  restored whichever sibling preset was written most recently — usually
  one built with different sanitizer flags, so almost every object missed.
  The foreign objects were then saved back under the `dev` key, which is
  why the ambiguous namespaces grew several times larger per entry than
  the unambiguous ones. Two tests pin the invariant, and a third pins its
  premise: no preset name may contain `--`.
- Changed: `include/tess/ops/` is mapped to a sentinel instead of being
  declared unrepresented. The recorded reason — nanosecond micro-benches
  below the paired materiality floor — describes the queued and scheduler
  families, but the directory also owns the pool executor, whose
  `parallel/*_pool_w4` benchmarks are gated near a millisecond. A pull
  request touching only the pool therefore skipped the paired run, skipped
  the threshold gates (which do not run on pull requests), and had no
  sentinel, so a regression between the paired effect floor and the
  main-branch ceilings was invisible on both. An unrepresented reason must
  now hold for the whole directory, not for its best-known family.
- Changed: `CCACHE_MAXSIZE` is set. Nine namespaces at ccache's 5 GiB
  default overrun the 10 GB repository cache quota, so each run evicted
  the previous run's caches.
- Added: every cache-using job reports its hit rate. Caching was
  configured in nine steps and measured in one, which is why the restore
  key collision went unnoticed for as long as it did.

## 2026-08-06 - Bounded Steam Deck benchmark builds

- Fixed: the Steam Deck benchmark workflow builds only the selected benchmark
  target with a configurable positive job bound, defaulting to one for the
  memory-limited emulated amd64 Docker path instead of invoking an unbounded
  build that can be OOM-killed.
- Fixed: governor-pinned runs now honor `BENCH_BIN`, so the main, diagnostics,
  and thread-scaling binaries share the same accurate on-device path.
- Affected docs: Steam Deck workflow and test guarantees.

## 2026-08-06 - Handheld baseline and qualified PMU events

- Published: the controlled Steam Deck timing, scaling, and PMU baseline with
  its power, topology, variance, and interpretation limits.
- Corrected: the redesign status no longer says that the already-completed
  cloud and handheld campaigns are unrun or deferred.
- Fixed: PMU parsing accepts documented `perf` event modifiers such as the
  `:u` suffix observed under SteamOS access restrictions, without accepting
  prefix collisions or unknown modifiers.
- Affected docs: performance, dependency, campaign-plan, optimization-log,
  and Steam Deck workflow documentation.

## 2026-08-06 - Post-v0.12 audit corrections

- Recorded: the 2026-08-04 exception-free entry described the planned-dirty
  capacity work as purely additive, which understated it. `collect_planned_dirty`
  and both partition-collecting `merge_planned_dirty` overloads stopped
  throwing `std::length_error` and now return
  `PlannedDirtyCollectStatus::CapacityExceeded` /
  `PlannedDirtyMergeStatus::CapacityExceeded` in exception-enabled builds too,
  and both enums gained a value that exhaustive `switch` statements must
  handle. Unlike the block-scratch and portal-cache checks, no throwing
  wrapper was kept. This shipped in `v0.12.0` unannounced; the release notes
  and the architecture note now state it.
- Confirmed: `AutoExecTask` absorbing that status is deliberate, not an
  oversight. The allocation-free fallback merge publishes every started
  callback's dirty metadata, so the run's observable result is complete;
  `TessNoExceptions.AutoExecRunsOrdinaryKernelThroughPool` pins exactly that
  under a zero capacity limit. Behavior is unchanged; only the contract is now
  written down.
- Changed: the at-budget `WeightedPortalSegmentCache` store path validated
  capacity with a pre-pass that repeated the transactional compaction's
  dependency-validity sweep, doubling that work in the steady state for any
  budgeted cache. Both store branches already reject before mutating live
  state, so the pre-pass is replaced by a constant-time precheck that keeps
  rejecting impossible stores before dependency capture allocates. Benchmark
  evidence and the follow-up condition are in
  [`optimization-log.md`](../planning/optimization-log.md).
- Changed: exception-free subsystem classification is derived from the
  directories under `include/tess` instead of a list pinned in the checker. A
  new subsystem now fails validation until the manifest records it as
  affected, with runtime coverage, or unaffected, with a written reason.
- Clarified: mixed-exception-mode detection is uneven. GCC and Clang cannot
  diagnose it at all; MSVC gets a partial `detect_mismatch` check that misses
  the `_HAS_EXCEPTIONS` axis. `tess/core/capacity.h` now carries the same
  MSVC check for its internal capacity-testing hook, which is an installed
  public header whose inline definition that macro changes.

## 2026-08-05 - Exception-free validation hardening

- Fixed: native MSVC abort-contract tests quote CRT spawn arguments and force
  a marker path containing a space, so space-bearing checkout paths remain
  covered without another CI job.
- Changed: opting into exception-free testing now adds every registered test
  executable and macro cell to the default build. The standalone-header
  verifier remains explicit. Ordinary builds remain unchanged because the
  option is off; focused CI jobs keep their existing targets and runtime.
- Clarified: AppleClang-specific exception-free CI is intentionally deferred
  to contain CI time, the historical TDD points to the focused Windows job,
  and the MSVC STL switch is undocumented and unsupported upstream.
- Affected docs: integration policy, exception-free architecture note, and
  the historical exception-free TDD implementation status.

## 2026-08-04 - Native MSVC exception-free consumer mode

- Added: native MSVC exception-free support using `/EHs-c-` and the matching
  MSVC STL `_HAS_EXCEPTIONS=0` configuration. The installed target remains
  policy-neutral.
- Changed: exception-mode compiler flags are centralized and independent from
  warning options, preventing `/EHsc` from silently overriding a disabled
  target.
- Verified: native MSVC builds standalone headers and macro cells, then runs
  representative library behavior plus installed and FetchContent consumers
  in a focused job parallel to the existing Windows CI job. Exception-free
  developer targets are excluded from ordinary build-all invocations to
  contain build cost without extending the Windows critical path.
- Compatibility: MSVC support is exception-free by construction. Unexpected
  exceptions, allocation failure, and thread-creation failure remain outside
  the recovery contract, and mixed modes remain unsupported.
- Affected docs: integration policy, exception-free architecture note, and
  the historical exception-free TDD implementation status.

## 2026-08-04 - Exception-free consumer mode

- Added: Clang-family and GCC consumers can compile all public headers,
  aggregates, and complete examples with `-fno-exceptions`; installed and
  FetchContent fixtures verify that the exported target remains policy-neutral.
- Added: checked block, portal-cache, and planned-dirty capacity results plus
  one internal abort path for legacy operations that cannot throw.
- Changed: phase executors use policy-specialized representations. The
  exception-free pool omits exception and cancellation state, while explicit
  no-throw callbacks retain that property through queued, result-channel,
  schedule, and auto-exec adapters in enabled builds.
- Compatibility: exception-enabled source behavior remains the default.
  General allocation failure, thread creation failure, and throwing
  application operations are outside the exception-free recovery contract.
  Native MSVC remains unsupported beyond a detection/STL/thread spike.
- Affected docs: integration policy, exception-free architecture note, block,
  queued operations, path, simulation, public-surface manifest, and the
  historical exception-free TDD.

## 2026-08-01 - Long-retention benchmark history (phase 6, slice a)

- Added: `tools/publish_benchmark_data.py` lays out per-main benchmark
  baselines for an orphan `benchmark-data` branch, sharded by month and
  keyed by commit so a re-run corrects its row rather than duplicating
  it. An empty publish is an error, not a quiet success.
- Added: a main-only `Publish Benchmark History` job that writes each
  run's baselines to that branch, retrying on the race between
  concurrent merges. Never gating, but in `report-failure`'s needs so a
  publish failure is visible.
- Changed: recorded the resolution of two section 12 open questions —
  the artifact store is a data branch, and the comparative repository is
  a separate repo named for the measurement domain rather than for this
  project (kept generic in tracked content until it is public).

## 2026-07-31 - Adaptive sparse chunk directory

- Changed: `SparseResidentWorld` uses direct key-to-slot indexing when its
  residency capacity covers the bounded world's complete chunk key space.
  Worlds whose key space exceeds the budget retain the fixed-capacity hash
  directory and its backward-shift deletion behavior.
- Rationale: sampled profiles of fully resident sparse path planning placed
  repeated hash probes in the dominant neighbor loop. The direct form removes
  those probes and reduces directory storage without changing slot, eviction,
  generation, or allocation-after-construction contracts.

## 2026-07-31 - Per-tick timing and allocation attribution

- Changed: diagnostics duration records carry inclusive allocation and
  deallocation byte deltas when an allocation sink is active. Allocation
  counters additionally report best-effort live and peak-live bytes without
  changing the exact count/total contract; unsized frees remain explicitly
  inexact.
- Added: diagnostics snapshots retain the newest 64 trace records and expose
  omitted records through their dropped count. The Dear ImGui diagnostics
  panel renders those duration spans in milliseconds alongside allocation
  traffic and renders live/peak allocation bytes.
- Changed: diagnostics-enabled schedules automatically time the complete tick
  and each executed task under its static task label. Diagnostics-off builds
  retain no timer or allocation-attribution code.

## 2026-07-31 - Parser fuzzing (phase 7, slice c)

- Added: `tests/test_parser_fuzz.py` drives seeded malformed and valid
  payloads through the benchmark and sentinel parsers, asserting each
  fails diagnosably rather than raising an exception that names no
  file.
- Fixed: `paired_bench.parse_results` did not catch `AttributeError`,
  so benchmark output whose top level was not an object — what a
  truncated write leaves behind — escaped as a raw traceback instead of
  a tool error. Found by the new fuzzing.
- Fixed: `paired_bench.load_config` indexed the sentinel and parameter
  objects without a shape check, so a hand-edited file failed with
  `AttributeError` or `TypeError` instead of naming the bad field.
  Each statistical parameter is now fetched and converted individually,
  so a missing or mistyped one names itself.

## 2026-07-31 - Weekly long-seed property tier (phase 7, slice c)

- Changed: property sweeps take their seed and step counts from
  `TESS_PROPERTY_SEEDS` and `TESS_PROPERTY_STEPS`, defaulting to the
  pull-request tier. A malformed or zero value fails the test rather
  than falling back, so a weekly run cannot report a long-seed pass it
  never performed.
- Added: a weekly `Long-Seed Property Sweeps` job running all four
  property suites at 400 seeds x 192 steps, which first asserts the
  override is honoured by checking that a zero budget is rejected.

## 2026-07-31 - Deferred dirty properties (phase 7, slice b-iii)

- Changed: `tess_dirty_property_test` drives seeded record, merge,
  partition-record and collect sequences over the deferred dirty
  accumulator, which planning alone cannot reach because planning
  creates no dirty records. Asserts the ignore-empty-mask rule, the
  out-of-range rejection, that a merge reports distinct chunks rather
  than record count, that a successful merge clears the accumulator,
  and that collection conserves every record.
- Changed: gates require a merge that actually coalesced, a zero mask,
  an out-of-range chunk, and a collection with records present.

## 2026-07-31 - Portal segment cache properties (phase 7, slice b-ii)

- Changed: `tess_cache_property_test` gains a
  `WeightedPortalSegmentCache` model driving seeded store, lookup,
  world-edit and sweep sequences. Asserts the entry budget, that a miss
  or stale entry leaves the caller's output untouched, and that
  re-storing a still-live request adds no second entry — scoped to live
  entries, since a stale match is skipped without being erased and a
  re-store below budget legitimately duplicates.
- Changed: gates require the sweep to serve a segment, compact, evict,
  miss with entries present, and re-store a live request.

## 2026-07-31 - Route cache properties (phase 7, slice b-ii)

- Changed: `tess_cache_property_test` gains a `RouteCacheScratch` model
  driving seeded query, oversized-query, class-rebind and invalidate
  sequences. Asserts both caps as hard bounds (the cache has no
  eviction: a breach invalidates wholesale), that every query resolves
  as exactly one of exact hit, suffix hit or miss, that a served route
  reports zero expanded and reached nodes, and that an oversized route
  is skipped without disturbing residents.
- Changed: coverage gates require the sweep to serve from cache, skip
  an oversized route with residents present, breach a cap, and rebind
  the movement class.

## 2026-07-31 - Field-product cache properties (phase 7, slice b-ii)

- Changed: `tess_cache_property_test` drives seeded store, lookup, world
  edit, clear and over-budget-store sequences against `FieldProductCache`,
  which had no seeded coverage at all. Checks the byte budget, entry and
  byte accounting against a from-scratch model, that a rejected store
  mutates nothing, and that a stale match counts as a rejection rather
  than a miss.
- Changed: the over-budget candidate is oversized by its goal count
  rather than by lowering the budget, which would evict every resident
  before the store and check the preserve-residents rule against an
  empty cache; a gate requires refusals with entries still present.
- Changed: pins least-recently-used eviction with a constructed case —
  fill to budget, refresh the oldest entry, force one eviction — rather
  than relying on the random sweep, which mutation testing showed cannot
  distinguish LRU from FIFO at the hit rates it reaches. The sweep-based
  residency test is named for what it does verify.

## 2026-07-31 - Queued-operation planning properties (phase 7, slice b-i)

- Changed: `tess_queued_property_test` drives seeded planning sequences
  over all nine `OperationKind` values, every write policy, four
  field-access patterns, and overlapping or disjoint chunk domains,
  replanning the whole batch after each enqueue. Asserts report
  cardinality and identity, plan accounting, ascending deduplicated
  chunks, the hazard rule, and that only a hazard rejection carries
  conflict diagnostics.
- Changed: pins the property that planning is independent of the
  operation kind — the planner never reads `op.kind` and the report
  does not carry it — by rewriting only that field and comparing.
- Changed: phase partitioning is asserted only for a successful phase
  plan; an unsupported write policy yields a prefix plus diagnostics,
  which is now asserted as such rather than treated as a full
  partition.
- Changed: the sweep must reach a hazard conflict, a multi-phase plan,
  an unsupported-policy prefix, all nine kinds, and the
  invalid-policy, invalid-domain and invalid-field-access rejections,
  so none of the above can hold vacuously. Non-dense identity is
  unreachable from `FrameOps` by construction and is covered by a
  focused test of the span overload instead.
- Fixed: a property's replay command named a hand-written string rather
  than a registered test, so the anchored `ctest -R` regex selected
  nothing and ctest exited zero — a reproduction command that silently
  ran nothing. The name now comes from the running test, and a test
  asserts it resolves to a registered one.

## 2026-07-31 - Property/state-machine harness (phase 7, slice a)

- Changed: `tests/property_harness.h` runs seeded operation sequences
  against a model, checks every invariant after every step, shrinks a
  failing sequence by delta debugging, and prints a replay command;
  `TESS_PROPERTY_REPLAY` replays an explicit sequence.
  `tess_property_test` applies it to residency
  (ensure/touch/evict/mark-dirty) and schedule ticks.
- Reason: redesign section 3.4. Those two areas had no seeded coverage
  at all — their tests drive fixed hand-written sequences, so an
  invariant that only breaks on an unusual interleaving had nothing
  looking for it. The invariants asserted are ones the library already
  computes: resident count and byte budget against their ceilings, the
  resident-implies-nonzero-generation pairing that lets a handle
  detect its own staleness, and
  `tasks_run + tasks_skipped == tasks_due` with a monotone tick.
- A deliberately broken model is part of the suite. It fails only
  after a specific operation appears twice, so the harness must
  detect it, shrink 64 steps to exactly those two operations, and
  produce a sequence that reproduces — a property harness that has
  never failed is indistinguishable from one that cannot fail.
- Remaining in this phase: queued-edit and cache sequences (the
  existing 24-seed queued test shuffles a fixed operation multiset
  rather than varying kind, and the caches have the richest stated
  invariants with no seeded coverage), the weekly long-seed tier, and
  parser fuzzing.
- Affected docs: testing and benchmarking redesign (item 7 status),
  tests/AGENTS.md.
- Affected code: new `tests/property_harness.h`,
  `tests/tess_property_test.cc`; `tests/CMakeLists.txt`.

## 2026-07-31 - Conan recipe and vcpkg overlay (phase 5, slice b)

- Changed: `conanfile.py` declares tess a Conan 2 `header-library`
  whose package id clears settings, and `ports/tess/` is a vcpkg
  overlay port. Both configure the same option set as the `consumer`
  preset, so what they install is the surface `find_package(tess)`
  installs. `tests/test_packaging_recipes.py` pins that
  correspondence, including the version, which lives only in
  `cmake/tess-version.cmake` and is read rather than restated.
- Reason: redesign section 10 item 5. `docs/packaging.md` previously
  said no recipe existed and that an in-repository one could prove
  packaging before a central submission; these are that.
- Limitations recorded rather than hidden: the port's `SHA512` is a
  placeholder filled at release tagging, because vcpkg hashes a
  published archive rather than a working tree; and neither tool is
  installed in CI, so the tests check recipe content and
  correspondence, not a completed package build.
- Affected docs: docs/packaging.md, tests/AGENTS.md.
- Affected code: new `conanfile.py`, `ports/tess/`,
  `tests/test_packaging_recipes.py`; `.github/workflows/ci.yml`.

## 2026-07-31 - Consumer-contract tests (phase 5, slice a)

- Changed: public and implementation headers now occupy separate
  CMake file sets, and `VERIFY_INTERFACE_HEADER_SETS` verifies
  `HEADERS;generated_headers` explicitly — compiling all 65 public
  headers standalone while leaving the path-detail fragments alone,
  which `#error` outside their owning header by design. Three probe
  translation units include the public set in declaration order,
  reverse order, and leaf-first with no umbrella, and are linked
  together; `tess_consumer_contract_test` then compares tess's own
  identity facilities (`tag_identity`, `planned_world_stamp`) across
  units. A macro-configuration matrix builds seven cells: bare,
  NDEBUG, diagnostics, EnTT-only, Flecs-only, ImGui, and WebGPU.
- Reason: redesign section 10 item 5. The install rule names the new
  file set explicitly, because every interface file set of an exported
  target must appear there or the template definitions the public
  headers need would be dropped; installed paths and files are
  unchanged, though the exported CMake metadata now shows the split.
  The mixed-adapter cells are genuinely new coverage — every dev
  preset pins EnTT and Flecs both on and every consumer preset pins
  both off, so no build anywhere had one without the other. The
  identity comparison was verified to have teeth by mutation: making
  `tag_identity` per-translation-unit fails the test.
- Deliberately not done: a configure-time compiler-version rejection.
  "Minimum tested" and "reject everything below" are different
  contracts, and a fatal check would refuse untested-but-working
  compilers, backports, and consumers who add tess as a subproject
  without using it. `target_compile_features(cxx_std_20)` already
  states the language requirement; a published matrix of continuously
  tested versions with pinned floor jobs is the honest form and is
  recorded as remaining.
- Affected docs: testing and benchmarking redesign (item 5 status),
  tests/AGENTS.md.
- Affected code: `CMakeLists.txt`, `tests/CMakeLists.txt`,
  `.github/workflows/ci.yml`, new `tests/consumer_contract/`,
  `tests/tess_consumer_contract_test.cc`.

## 2026-07-30 - Paired confirmation fetches unreachable commits

- Changed: the paired sentinel confirmation workflow fetches a
  requested commit explicitly when the checkout cannot already see it,
  and fails with a clear message when the object is genuinely
  unfetchable.
- Reason: a squash-merged pull request's head commit is not reachable
  from main, so the workflow's clone could not resolve it and
  `git worktree add` failed with `invalid reference`. Those are
  precisely the commits a confirmation replays — the 2026-07-23 catch
  that section 4.3's criterion 2 replays lives on a squashed pull
  request head — so the workflow could not perform its central job for
  the case that motivated it.
- Affected docs: threshold-retirement assessment.
- Affected code: `.github/workflows/paired-bench.yml`.

## 2026-07-30 - S3 sparse streaming under a residency budget (phase 3, slice 3)

- Changed: `tests/sparse_stream_harness.h` searches the S1 terrain in a
  `SparseResidentWorld` under a budget expressed as a fraction of the
  world's chunks, streaming chunks in and retrying on
  `PathStatus::Indeterminate` until the answer converges, with a dense
  reference world answering the identical requests.
  `tess_sparse_stream_test` asserts fully-resident equivalence,
  streaming soundness at 25% and 5% budgets, the budget ceiling, the
  measured cost of a tight budget, section 3.3's flow identities over
  residency admission and eviction, and determinism.
- Reason: redesign section 3.1's S3 bullet. The finding that shaped
  it: **a definitive answer is not a converged one.** The search
  returns `Found` on reaching the goal even when it skipped
  non-resident chunks, so a loop that stops at the first definitive
  answer reports an upper bound, not the optimum — witnessed at a
  quarter budget. Convergence therefore has to be certified: the loop
  keeps streaming past the first answer until nothing further could
  change it, and only then does the result equal the dense optimum,
  which the tests assert exactly. Where the budget cannot hold what
  the search needs, certification is impossible and the tests assert
  the bound and the soundness directions instead. The harness exposes
  both strategies, because the difference is the finding: stopping at
  the first answer costs 8 streaming rounds and leaves two of twelve
  routes longer than optimal, while streaming to certification costs
  28 rounds and matches the dense optimum everywhere. Recorded in the
  optimization log. Two further library facts shaped the harness: every search
  entry point defaults to `MissingChunkPolicy::TreatAsBlocked`, which
  would answer `NoPath` instead of asking for more chunks, and
  `ensure_resident` hands back a zeroed page, so a streamed chunk —
  including one evicted and streamed again — must be refilled.
  Residency has no flow-accounting hooks of its own, so the harness
  attributes admissions, coalesced hits, and LRU displacements around
  its own calls; library-side hooks would be a separate change.
- Affected docs: testing and benchmarking redesign (item 3 status),
  tests/AGENTS.md.
- Affected code: new `tests/sparse_stream_harness.h`,
  `tests/tess_sparse_stream_test.cc`; `tests/CMakeLists.txt`.

## 2026-07-30 - S2 colony macro-harness (phase 3, slice 2)

- Changed: `tests/colony_harness.h` drives N agents with goals through
  the production stack — `Schedule`, an `AutoExecTask` over queued ops
  with a result channel, weighted path agents with movement,
  incremental region-graph topology, and `DeltaCollector` publishes —
  parameterized by agent count, churn, executor and worker count,
  world size, chunk size, and field payload width. Terrain is the S1
  logical room map raster-scaled into the world, so the same topology
  carries across world sizes. `tess_colony_harness_test` runs the
  section 5 PR-tier matrix (N=100, serial plus pool at two worker
  counts) and asserts serial == pool, worker-count invariance,
  chunk-size invariance, payload-width invariance, incremental
  topology == a fresh rebuild both per churn event and at end of run,
  section 3.3's flow identities, and a serial-only outcome golden.
- Reason: redesign section 3.1's S2 bullet. Three wiring details are
  load-bearing and were each caught in review: `SimPhase::Movement`
  precedes `Topology`, so the rebuild registers in `Pathing` and only
  it marks pathing dirty — otherwise agents replan against a stale
  graph; the auto-exec task selects its executor by phase operation
  count, so churn enqueues one operation per distinct chunk and the
  tests assert `pool_phases > 0` rather than comparing two idle runs;
  and a cost of zero reads as blocked, so every passable tile carries
  a positive weight or the world is immobile. The churn script is
  chosen by coordinate rather than by chunk key, so two chunk shapes
  block identical tiles, and it skips tiles an agent currently
  occupies.
- Remaining for later slices: agent counts of 1k and 10k, worlds of
  1024 and 2048, worker counts of 1 and 8, multiple seeds and churn
  rates, wall removal, a repeated-goal workload that actually
  populates the caches (the current cold-cache test is a smoke check,
  not section 3.2's cache differential), deterministic work-counter
  goldens beyond the outcome golden, and the weekly soak.
- Affected docs: testing and benchmarking redesign (item 3 status),
  tests/AGENTS.md.
- Affected code: new `tests/colony_harness.h`,
  `tests/tess_colony_harness_test.cc`; `tests/CMakeLists.txt`.

## 2026-07-30 - S1 procedural generators and oracle leg (phase 3, slice 1)

- Changed: the scenario layer's in-repo S1 leg exists —
  tests/grid_map_generators.h provides seeded deterministic
  recursive-division-maze and room-and-corridor generators emitting
  strict Moving AI map text for the existing grid-benchmark harness
  parser, plus a single-BFS connectivity check and a deterministic
  endpoint sampler that always includes the flood fill's farthest
  pair. tess_grid_map_generators_test verifies determinism (pinned
  SplitMix64 stream and byte-identical regeneration), parser
  round-trips, full connectivity by construction, and the oracle leg:
  tess A* agrees exactly with the independent Dijkstra reference in
  both movement modes and both directions across the fixed generated
  matrix, on every PR.
- Reason: redesign section 3.1 S1 in-repo layer (sequencing item 3);
  the external-data legs stay rights-gated and untouched.
- Affected docs: testing and benchmarking redesign (item 3 status),
  tests/AGENTS.md.
- Affected code: new tests/grid_map_generators.h,
  tests/tess_grid_map_generators_test.cc; tests/CMakeLists.txt.

## 2026-07-30 - Pre-push slimming with tested test-impact labels (phase 2, slice 9)

- Changed: the pre-push hook runs configure + build + the
  affected-test subset instead of the full cycle. Every discovered
  test declares a forward impact label set in tests/CMakeLists.txt
  (`subsystem:<dir>` = a change there must run this test, curated
  from tests/AGENTS.md guarantees; `target:<name>` automatic;
  `prepush:always` on the installed-headers check, the
  counter-golden pair, and the link-free allocation-counter test).
  The hook classifies the pushed range tri-state — full, selected
  labels composed into one anchored ORed `-L` regex (repeated -L
  flags AND in ctest), or build-only for docs/examples/bench — and
  fails open: tools, CMake files, core/ and storage/ headers,
  umbrella headers, test helpers, unresolvable ranges, and new refs
  all run the full suite. `TESS_PREPUSH_FULL=1` overrides everything
  and adds the consumer smokes; the conditional benchmark build is
  dropped (the PR bench-smoke job owns compile rot). The mapping is
  tested: label declarations parse-checked against the target set
  and subsystem vocabulary, reverse coverage requires every
  subsystem to select at least one test (no acknowledged gaps — gpu
  and debug have direct tests), and both a local pytest and a CI
  dev-job step assert CMake actually propagated the labels.
- Reason: section 6 (minutes of friction on every push, multiplied
  for agent contributors) with section 3.7's label mapping promoted
  to a tested prerequisite; the hook-backstop CI job is unchanged
  and authoritative.
- Affected docs: docs/git-hooks.md, testing and benchmarking
  redesign (section 10 status), tests/AGENTS.md.
- Affected code: tests/CMakeLists.txt, CMakeLists.txt,
  tools/git_hooks.py, tests/test_git_hooks.py,
  .github/workflows/ci.yml.

## 2026-07-30 - Profiling protocol wired to its signals (phase 2, slice 8)

- Changed: the section 4.6 profiling protocol is now attached to the
  moments a contributor meets a performance signal. CONTRIBUTING.md
  documents the four-step escalation (counters first; reproduce
  paired locally with a concrete worktree + `paired_bench.py` recipe;
  profile and diff under `bench-profile`; record every outcome in the
  optimization log), and the three signal surfaces link it: the
  paired sentinel summary appends the pointer on every verdict plus a
  paste-ready `--suspects=` list when something flagged, the
  change-point suspect report appends the same after its
  confirm-first advice, and the counter-drift table names itself as
  step 1 (work changed means the diagnosis is algorithmic).
- Reason: the profiling tooling existed but nothing connected it to
  its triggers; contributors here are largely automated agents, so
  the protocol must live where the signal appears. Optional guidance,
  never a gate — hosted runners have no reliable PMU access.
- Affected docs: CONTRIBUTING.md, testing and benchmarking redesign
  (section 10 status).
- Affected code: `tools/paired_bench.py`,
  `tools/benchmark_changepoint.py`, `tools/check_counter_goldens.py`
  (report renderers only).

## 2026-07-30 - Benchmark workload-matrix catalog (phase 2, slice 7)

- Changed: benchmark workload coverage is now a declarative,
  drift-checked catalog (`bench/workload-matrix.json`). Operation-level
  family rules classify every registration through fail-closed
  grammars — anchored regexes whose capture groups map to dimensions —
  with curated family defaults and per-name overrides supplying what
  names cannot express (hidden chunk extents, storage semantics,
  fixture payloads). Measured cells are generated from classification,
  never hand-listed. Dimensions: world extent, chunk extent, layout
  (qualitative: open, room_portals, sparse_blockers, ...), storage
  (always_resident | sparse_resident | not_applicable), executor kind,
  worker count, payload. Known-unmeasured cells are structured
  selectors with reasons; a measured cell matching one fails the check
  so filled gaps force the entry to retire. `composite` registrations
  (one timing over conflated configurations) never count as measured
  cells. `tools/check_workload_matrix.py` verifies bidirectional
  coherence statically on the hooks tier (threshold manifests + lab
  literals) and against the compiled registration union of both bench
  binaries in the bench job — the runtime authority that survives the
  phase 4 threshold retirement.
- Reason: section 4.5's second bullet — the meaningful benchmark
  coverage axis is the workload space, recorded as reviewable
  statements with a drift check instead of audit-time discoveries.
  Curation from source reading, not names: the `sparse` token means
  three different things across families (sparse blockers in
  always-resident worlds, task-scheduling patterns, and — in exactly
  five registrations — true sparse-resident storage).
- Affected docs: testing and benchmarking redesign (section 10
  status), CONTRIBUTING.md, tests/AGENTS.md.
- Affected code: new `bench/workload-matrix.json`,
  `tools/check_workload_matrix.py`, `tests/test_workload_matrix.py`;
  `.github/workflows/ci.yml`.

## 2026-07-30 - Advisory coverage reporting (phase 2, slice 6)

- Changed: the weekly CI tier gains an advisory coverage job (never in
  ci-gate; a coverage-percentage gate stays an explicit non-goal). A
  new `TESS_ENABLE_COVERAGE` option routes Clang source-based coverage
  flags through the header-only interface target (instrumenting
  exactly the consuming translation units, not fetched dependencies,
  and kept out of the installed export set). The job publishes an
  llvm-cov summary of the full test suite — every instrumented test
  executable is passed via `-object`, because coverage mappings live
  in binaries rather than profiles — and a benchmark gap-finder:
  smoke-mode runs of every registration in both bench binaries joined
  by `tools/coverage_gaps.py` against the declared public header set
  (`TESS_PUBLIC_HEADERS` plus the generated `version.h`; the physical
  tree includes private implementation headers), reporting which
  public headers no benchmark executes. Headers absent from
  the export entirely (never included or never instantiated) are
  distinguished from mapped-but-unexecuted ones; acknowledged gaps
  live in an exact-header manifest (`tools/coverage_known_gaps.json`)
  with reasons and stale-entry detection.
- Reason: sections 3.6 and 4.5 — the 2026-07-11 audit found the
  sparse-storage benchmark gap by hand; this finds that class of gap
  mechanically. The first local run caught a wrong manifest
  assumption (maintenance benchmarks execute
  `experimental/maintenance.h`) and surfaced real gaps such as
  `core/lattice.h` and `storage/residency.h`.
- Affected docs: testing and benchmarking redesign (section 10
  status), CONTRIBUTING.md, tests/AGENTS.md.
- Affected code: `CMakeLists.txt`, `CMakePresets.json`,
  `.github/workflows/ci.yml`, new `tools/coverage_gaps.py`,
  `tools/coverage_known_gaps.json`, `tests/test_coverage_gaps.py`.

## 2026-07-30 - Suspect-scoped paired confirmation (phase 2, slice 5)

- Changed: the paired confirmation workflow accepts a predeclared
  suspect list (up to 64 benchmark names, typically pasted from the
  change-point issue) that replaces the sentinel set; each suspect is
  judged on its threshold-manifest metric (real time where the gate is
  real time — the parallel pool suite and the manually timed cache
  benchmarks — CPU time otherwise, including ungated lab
  registrations), the Bonferroni family sizes to the list, and
  benchmark output units are normalized. The sentinel default and the
  shadow job are unchanged.
- Reason: review of a literal full-suite mode showed five-round
  extreme-tail bootstrap intervals collapse to the sample minimum at
  193-way confidence (an illustrative 17% suite-level false-confirm
  rate), so section 4.2's confirmation leg is completed as targeted
  confirmation of predeclared suspects, with the broad sweep remaining
  the change-point detector's job. The plan records the resolution.
- Affected docs: testing and benchmarking redesign (section 10 status),
  tests/AGENTS.md.
- Affected code: `tools/paired_bench.py`,
  `.github/workflows/paired-bench.yml`, `tests/test_paired_bench.py`.

## 2026-07-29 - Runner fingerprinting and change-point alerting (phase 2, slice 4)

- Changed: benchmark baseline artifacts carry a runner fingerprint —
  CPU model, core count, runner image, compiler identity, and the
  normalized effective compile flags of the benchmark binary from
  `compile_commands.json` — with missing fields marking the artifact
  unusable rather than joining a shared null stratum.
  `tools/benchmark_changepoint.py` runs the section 12 resolution: a
  control-chart rule over per-artifact medians, stratified by
  fingerprint with stratum resumption, flagging only when the newest
  three same-stratum artifacts each clear a 10% relative and 2 µs
  absolute floor over the trailing-30 baseline median (minimum 8).
  A dedicated main-push-only `change-point` job — separate from the
  bench job, which executes pull-request code and therefore never
  holds write permissions — downloads the trailing artifacts, runs the
  detector, and files or extends one rolling `perf-change-point` issue
  with an idempotency marker; fingerprint series breaks point at the
  manual sentinel confirmation. Alerting never gates.
- Reason: section 4.2's hosted-runner alerting leg. The detector was
  backtested against the real artifact history before shipping and
  immediately surfaced a genuine finding: a sustained 3-4x fields
  family step at the v0.12 completion merge that the post-merge
  threshold recalibration had silently absorbed (optimization log,
  "Change-Point Backtest") — the section 2.3 calibration loophole this
  machinery exists to close.
- Affected docs: testing and benchmarking redesign (sections 10 and
  12), optimization log, tests/AGENTS.md.
- Affected code: `tools/benchmark_artifact_metadata.py`,
  `tools/benchmark_changepoint.py`,
  `tests/test_benchmark_changepoint.py`, `.github/workflows/ci.yml`,
  `.github/workflows/paired-bench.yml`.

## 2026-07-29 - Queue-flow accounting (phase 2, slice 3)

- Changed: `tess::diagnostics` gains ungated flow accounting —
  `FlowCounters` (the section 3.3 admission/terminal taxonomy plus a
  `failed` bucket), the caller-owned `FlowAccounting` accountant with
  delta-weighted tick observation, and the UI-agnostic
  `FlowHealthSnapshot`. Four flows account every transition at the
  point where the fact is known: the resumable work queue (exhaustive
  lifecycle mapping; completed-to-stale reclassification is the one
  documented non-monotonic bucket; immediate submission commits last,
  fixing an Unbound-slot leak on a throwing move; moves transfer the
  attachment, copies start unattached), event streams (retained
  inventory with `consume_all`/`discard_all`; legacy `clear` counts
  conservatively as discarded), the experimental maintenance schedulers
  (lock-scoped updates, in-flight work stays outstanding, throwing
  tasks terminalize failed, budget-delta consumed units, unbounded
  flush budgets are never offered work, immediate-backend
  self-schedules are coalesced offers), and the path-agent goal
  lifecycle (admission at tick-state goal arming with per-agent
  `armed_tick` stamps, supersede on live replacement, cancel on clear,
  complete at arrival, failed at `Unreachable`; bare state helpers stay
  unaccounted and say so). The counter-golden probe gains four flow
  workloads whose conservation identities are hard checks — invariants
  a golden `--update` must never launder — while their values are
  golden-gated in shadow like every other counter.
- Reason: section 3.3's queue-flow accounting discipline, brought to
  the transition points after review showed state reconstruction cannot
  recover admission or terminal history. The maintained plan is amended
  in the same slice: the terminal taxonomy carries `failed`, and
  admission is defined as the clean-to-pending transition.
- Affected docs: testing and benchmarking redesign (section 3.3),
  architecture diagnostics and simulation pages, surface manifest,
  tests/AGENTS.md.
- Affected code: `include/tess/diagnostics/diagnostics.h`,
  `include/tess/ops/async_work.h`, `include/tess/sim/event_stream.h`,
  `include/tess/experimental/maintenance.h`,
  `include/tess/sim/path_agent.h`, `include/tess/sim/path_agent_tick.h`,
  `tests/tess_flow_accounting_test.cc`,
  `tests/tess_counter_golden_probe.cc`, `tests/goldens/counters.json`.

## 2026-07-29 - Counter goldens in shadow mode (phase 2, slice 2)

- Changed: a gtest-free probe (`tests/tess_counter_golden_probe.cc`,
  diagnostics enabled target-locally) runs five fixed serial workloads —
  unit and weighted A* over the canonical serpentine maze, a unit
  distance-field product replay, a weighted product with
  nearest-target replay (the entry-cost read path), and a queued
  serial field update with dirty merge — and emits the observed
  `PathCounters`/`QueuedPhaseCounters` values as JSON. A ctest fixture
  pair compares them against the committed
  `tests/goldens/counters.json` through
  `tools/check_counter_goldens.py`: shadow mode reports drift in the dev
  job's step summary without gating, `TESS_COUNTER_GOLDENS_STRICT=1`
  fails on drift (the phase 4 promotion switch), and `--update` is the
  intentional-change workflow, committed in the same pull request.
  Serial-only per section 3.3 (pool workers do not aggregate into the
  caller's thread-local sink). Allocation counts are deliberately not
  golden-gated: container growth differs across standard libraries, and
  the zero-allocation guarantees already have dedicated tests.
- Reason: the redesign's section 4.1 counter-golden leg — exact,
  noise-free work accounting on pull requests that distinguishes "doing
  more work" from "running slower", entering service in shadow mode
  beside the ceilings per section 4.3 so cross-platform expansion-order
  drift is observed before anything gates.
- Affected docs: testing and benchmarking redesign (phase 2 status),
  CONTRIBUTING quality-gates section, tests/AGENTS.md.
- Affected code: `tests/tess_counter_golden_probe.cc`,
  `tests/goldens/counters.json`, `tests/CMakeLists.txt`,
  `tools/check_counter_goldens.py`,
  `tests/test_check_counter_goldens.py`, `.github/workflows/ci.yml`.

## 2026-07-29 - Paired sentinel benchmarks in shadow mode (phase 2, slice 1)

- Changed: pull requests touching perf-sensitive paths now run a paired
  base-vs-head sentinel benchmark job in shadow mode. Both sides build
  the new `bench-only` preset's `tess_bench` (the base with explicit
  flags, since its commit may predate the preset), interleave twelve
  composite sentinels in alternating rounds, and judge each through a
  paired per-round-ratio bootstrap: a sentinel flags only when the 95%
  CI lower bound clears an 8% effect floor and a 2 µs materiality
  floor, and a flag must survive one fresh re-run to be a regression.
  The shadow job never gates (it is not in `CI Gate`'s needs); a
  `workflow_dispatch` sentinel-confirmation workflow runs the same
  comparison Bonferroni-adjusted and fails on confirmed regressions.
  `bench/sentinels.json` also carries the section 4.5 source map with a
  test-enforced coherence rule: directories mapped to sentinels are
  exactly the directories the classifier calls perf-sensitive, and
  areas no sentinel can observe (ops/, diagnostics/, debug/, gpu/,
  experimental/) are declared unrepresented and skip the paired job.
- Reason: the redesign's section 4.1 — measure time through paired
  statistics where a ceiling cannot distinguish noise from work — with
  the section 4.3 shadow discipline: the calibrated threshold gates
  remain authoritative until the replacement reproduces the 2026-07-23
  catch and clears the exit criteria. Sentinel selection was measured,
  not assumed: families offering only nanosecond micro-benches (queued,
  scheduler) are recorded as gaps for the counter-golden slice instead
  of being mapped to sentinels that could never clear the materiality
  floor.
- Affected docs: testing and benchmarking redesign (phase 2 status),
  CONTRIBUTING quality-gates section, tests/AGENTS.md.
- Affected code: `.github/workflows/ci.yml`,
  `.github/workflows/paired-bench.yml`, `CMakePresets.json`,
  `bench/sentinels.json`, `tools/paired_bench.py`,
  `tools/ci_changes.py`, `tests/test_paired_bench.py`,
  `tests/test_ci_changes.py`, `tests/test_cmake_compatibility.py`.

## 2026-07-28 - Tiered CI topology (redesign phase 1)

- Changed: the CI workflow is tiered per the testing and benchmarking
  redesign's section 5. Pull requests block on dev, GCC, hook backstop,
  ASan, cppcheck, Windows, benchmark compile+smoke, a new diff-scoped
  clang-tidy job (`tools/clang_tidy_changed.py` — changed sources via the
  compilation database, changed headers via synthesized translation
  units), and TSan only when `tools/ci_changes.py` classifies a changed
  path as concurrency-sensitive (fail-closed; a test-enforced drift scan
  keeps the curated path set aligned with headers owning threading
  primitives). Pushes to main, a new weekly scheduled run, and manual
  dispatches additionally run werror, release, TSan, macOS, the
  full-tree clang-tidy sweep, the benchmark threshold gates, and
  baseline artifact collection; failed non-PR runs file or extend a
  rolling `ci-failure` issue. The `Protect main` ruleset's required
  contexts — `CI Gate` from this workflow and `Build documentation` from
  the Documentation workflow — keep their exact names, so no ruleset
  edit accompanies the re-tier; `CI Gate` gains per-event expectations.
- Reason: the 2026-07-28 failure classification
  ([planning record](../planning/ci-failure-classification-2026-07-28.md))
  re-verified that dev-werror, release, and both macOS jobs have never
  failed in isolation, that clang-tidy's catches do not require the
  full-tree run's latency on the PR critical path, and that cppcheck's
  classified record (one isolated real catch, ~3-minute post-#59 cost)
  keeps its blocking seat. Threshold gating moves to the main tier and
  stays authoritative until the redesign's phase 4 shadow-mode exit
  criteria are met.
- Affected docs: testing and benchmarking redesign (phase 1 status),
  CONTRIBUTING quality-gates section, tests/AGENTS.md catalog entries
  for the classifier and the diff-scoped runner.
- Affected code: `.github/workflows/ci.yml`, `tools/ci_changes.py`,
  `tools/clang_tidy_changed.py`, `tests/test_ci_changes.py`,
  `tests/test_clang_tidy_changed.py`, and the workflow-structure pins
  in `tests/test_git_hooks.py` and `tests/test_benchmark_tools.py`.

## Template

```md
## YYYY-MM-DD - Title

- Changed:
- Reason:
- Affected docs:
- Affected code:
```
