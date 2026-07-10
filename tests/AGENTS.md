# Tests

- Tests use GoogleTest.
- `allocation_counter.{h,cc}` is shared only by allocation-sensitive test
  binaries that need global `new`/`delete` counters. Do not link it into more
  than one translation unit inside the same executable.
- Allocation counting is enabled exclusively through the RAII
  `tess_test::ScopedAllocationCounter` (construction resets and enables,
  destruction disables). There is intentionally no free-function
  enable/disable API: failed `ASSERT_*`s return early, and a raw flag would
  leak enabled counting into later tests. Read results via `counter.count()`
  / `counter.bytes()` inside the scope, or the read-only
  `tess_test::allocation_count()` / `allocation_bytes()` snapshots. The
  counter state is relaxed-atomic: it can under-count cross-thread
  allocations racing a scope boundary (never over-counts, so `== 0`
  assertions never false-fail).
- Under AddressSanitizer or ThreadSanitizer, `allocation_counter.cc` and
  `bench/tess_diagnostics_alloc_hooks.cc` must use sanitizer allocation
  hooks instead of replacing global `new`, so the sanitizer's alloc/dealloc
  mismatch and alloc-dealloc-size checks remain meaningful. Without a
  sanitizer they replace the complete standard `operator new`/`delete`
  overload set (plain, array, aligned, nothrow) so every allocation form is
  counted portably (no dlsym chaining to Itanium-mangled symbols).
- `tess_allocation_counter_test`: verifies the scoped allocation counter
  itself — plain/array, aligned, nothrow, and aligned-nothrow `operator new`
  forms are all counted with byte totals, construction resets counters, and
  a fatal gtest failure inside a counting scope disables counting during
  unwind instead of poisoning later assertions.
- `tess_smoke`: verifies that the public `tess::tess` target is consumable,
  that the root public header compiles, and that the version macros and
  `library_version` match a hand-maintained release-version literal in the
  test (a mirror of the top-level CMake `project(... VERSION ...)`), so a
  `tess.h` bump that forgets the test fails; a CMake-only bump is not
  detectable there.
- `tess_shape_test`: verifies public shape primitives, constexpr shape traits,
  degenerate-axis handling, containment helpers, key width inference,
  coordinate/chunk/local/tile key conversion helpers, the portable
  `tess::detail::UInt128` operations (carrying multiply, borrow subtract,
  boundary shifts including counts of 64/127/>=128, comparisons, narrowing,
  non-negative int construction, a death test for the negative-int
  constructor precondition when asserts are enabled,
  and `bit_width`/`bits_for_count` at the 64-bit boundary), >64-bit
  tile-key round-trips on the huge bounded shape, and the largest legal
  64-bit boundary shape (single chunk, 2^63 local tiles, `chunk_bits == 0`)
  round-tripping without wide shifts.
- `tess_storage_test`: verifies typed field schemas, resident chunk pages, and
  always-resident dense worlds, including SoA field independence, contiguous
  typed spans, metadata, const access, key/coord lookup, coordinate resolution,
  checked invalid-coordinate behavior, per-chunk dirty/active/topology-version
  metadata, dirty-bounds union across all relative box orientations including
  z cases, zero-initialized fresh-world field values without prior writes,
  appending out-parameter dirty/active chunk queries that match the by-value
  queries and do not allocate into reserved vectors, generation-stamped
  `observe_dirty`/`clear_dirty_observed` maintenance clears that preserve
  marks landing after observation, noexcept hot accessors, and
  allocation-free local field/span/world access after construction.
- `tess_residency_test`: verifies the byte-budgeted `SparseResidentWorld`:
  an enormous bounded shape (~3e10 chunks) constructs and stays empty until
  `ensure_resident`, which materializes a zeroed page and reports residency;
  out-of-bounds keys are never resident and are distinct from `Missing`
  in-bounds keys; `ensure_resident` is idempotent and preserves data; the
  byte budget caps resident bytes under least-recently-used eviction; an
  evicted chunk reloads with a strictly greater generation (invalidating any
  prior `ResidencyHandle`) and fresh zeroed data; explicit `evict` releases
  residency and bytes; `resident_chunk_keys` enumerates exactly the resident
  set; dirty/active queries visit only resident chunks (no full-world scan);
  residency survives directory backward-shift deletion under churn; and both
  the warm resident-set access path and evict/reload slot reuse allocate
  nothing after warmup.
- `tess_block_test`: verifies chunk-domain builders, policy-typed `BlockCtx`
  construction and iteration, serial block iteration, owned domain lifetimes,
  const-correct chunk views and world access including compile-time and runtime
  `ReadOnly` policy enforcement, runtime dispatch across every `WritePolicy`
  value (including mutable views for `UniquePerTile`, `UniquePerChunk`, and
  `Unsafe`), runtime invalid write-policy fail-fast
  behavior, chunk bounds for 2D vertical and 3D worlds, chunk-local tile
  iteration and coordinate helpers,
  boundary/local-candidate helpers across 2D/3D and degenerate axes, and
  allocation-free iteration for prebuilt domains and contexts, including
  pre-reserved caller-owned block scratch and explicit scratch-exhaustion
  diagnostics. `BlockScratch` coverage pins zero-count allocations,
  byte-count overflow rejection (no wrapped tiny allocations), mixed
  char/`std::uint64_t`/max-aligned allocations staying aligned and
  disjoint, and growth keeping `used_bytes()` while serving new
  allocations from fresh storage.
- `tess_queued_test`: verifies the M4 queued-operations scaffold, including
  empty-frame planning, stable handles and enqueue-order ids, explicit/dirty/
  active/resident chunk-domain expansion, enqueue-order plan preservation,
  diagnostic access metadata, untyped field access mask propagation,
  structured invalid write-policy, invalid field-access, and explicit-domain
  rejection, report lookup/count helpers, mixed valid/invalid report ordering,
  deterministic field-mask hazard validation, deterministic parallel phase
  grouping/rejection, plan-to-block adapters, policy mismatch rejection,
  allocation-free prebuilt planned block iteration, explicit planned execution
  with direct and deferred dirty propagation, dirty-record coalescing,
  phase-range deferred execution and rejection, backend-executor range dispatch,
  partitioned dirty phase execution and merge including test-only threaded
  mutable and read-only dispatch, scoped threaded executor equivalence and
  replay stress over shuffled legal phase plans, failure ordering, read-only
  const-view enforcement, partitioned threaded failure semantics,
  policy-mismatch execution rejection, allocation-free prebuilt planned
  execution, source-location capture, and allocation-free inspection of
  already-built queue/report/plan spans. It also verifies the structural
  serial-executor guard (compile-time `static_assert`s that
  `execute_phase_deferred_dirty_with` accepts `tess::SerialExecutor`-tagged
  executors and rejects `ScopedThreadPhaseExecutor`, while the partitioned
  variant accepts both), a tagged custom serial executor end to end,
  `FrameOps::clear()` id restart and allocation-free warm re-enqueue,
  zero-value default-constructed `OpId`/`OpHandle`, write-then-read hazard
  rejection and same-chunk write-then-read phase splitting, legal empty and
  end-anchored phase ranges plus empty-plan phase planning, explicit-domain
  duplicate-key deduplication and empty explicit domains, and
  `ScopedThreadPhaseExecutor` worker-count clamping and zero-count early
  return. Threaded replay stress compares every tile of every chunk between
  serial and threaded worlds via field spans, and the same replay harness
  drives one reused `WorkerPoolPhaseExecutor` across all stress seeds to pin
  persistent-pool equivalence with serial execution. Multi-worker rendezvous
  points use a bounded 30s spin (`await_rendezvous`) that fails with a clear
  message instead of hanging until the ctest timeout.
- `tess_phase_executor_test`: verifies the public `tess/ops/phase_executor.h`
  contract in isolation: compile-time `PhaseExecutor` concept conformance
  (satisfied by `SerialPhaseExecutor`, `ScopedThreadPhaseExecutor`,
  `WorkerPoolPhaseExecutor`, and a minimal custom executor; rejected for
  wrong return types, non-const `for_each_operation`, and non-executors),
  `SerialExecutor` tag relationships, exact-range single-visit dispatch for
  serial, threaded, pool, and custom executors, serial first-failure
  short-circuit ordering, threaded failure reporting after join,
  concept-constrained `execute_operation_index_range` dispatch, and
  allocation-free warm serial dispatch. Worker-pool coverage adds
  worker-count clamping, empty-range early return, worker reuse across many
  phases, first-failure reporting in operation order, allocation-free
  steady-state warm dispatch after `reserve_operations` (several warm
  dispatches run and only the last must not allocate, since the counter is
  process-global while pool workers are live), repeated create/run/stop
  lifecycle cycles, and destruction without ever running a phase.
- `tess_topology_test`: verifies local chunk-region labeling, blocked-tile
  region rejection, boundary exits, invalid chunks, inter-chunk portal pairing,
  reachability, and top-down 2D, vertical 2D, and 3D degenerate-axis behavior.
  Reachability coverage includes same-region, multi-hop, disconnected, enclosed,
  blocked-seam, invalid endpoint, and vertical 2D cases. It also verifies
  region bounds for known 2D and 3D layouts, Z-face portal pairing across
  stacked chunks, multi-region-per-chunk seam pairing, the checked 1-based
  `region(id)` accessor, the dense region index (`region_count`,
  `region_index`, sentinel rejection), CSR reachability parity against a
  portal-scan reference BFS on a seeded multi-chunk maze including visited
  counts, and `update_region_graph` equivalence with full rebuilds: empty
  dirty-set no-op, invalid dirty-chunk rejection without mutation,
  single-chunk and two-chunk seam edits, all-chunks-dirty rebuilds, and 40
  seeded single-tile edits compared graph-for-graph (regions, portals,
  region contents, and reachability probes) after every edit.
- `tess_topology_sparse_test`: verifies `RegionGraphT<SparseResident>`
  (`SparseRegionGraph`): building over only the resident chunk set (sized by
  resident count, not chunk count), reachability across resident chunks,
  `Indeterminate` across a non-resident boundary and for a non-resident
  endpoint, `Unreachable` for a fully-resident enclosed component (single-chunk
  world), sparse `update_region_graph` equivalence with a fresh build after a
  seam edit (graph-for-graph plus a reachability probe), and the
  residency-generation staleness guard forcing a full rebuild after a chunk
  loads post-build.
- `tess_path_precheck_test`: verifies the pre-A* topology gate `precheck_path`
  and `precheck_rules_out_path`: a reachable goal within a connected region, an
  `Unreachable` verdict across a walled chunk boundary (the only status that
  licenses skipping A*), out-of-bounds and walled starts reported as
  `InvalidStart` (never ruling out), an out-of-bounds goal reported as
  `InvalidGoal`, an
  unbuilt graph as `NoGraph`, a post-build topology edit degrading to
  `GraphStale` rather than a wrong `Unreachable`, a sparse corridor exiting into
  a non-resident chunk reported as `MissingChunk`, and an allocation-free warm
  precheck query.
- `tess_path_precheck_runtime_test`: verifies the optional precheck gate wired
  into `PathRequestRuntime` and the agent tick. A goal sealed off by an
  enclosing wall (not a full-axis barrier, so A*'s dense fast-path cannot rule
  it out) is resolved to `NoPath` with zero expanded nodes and counted in
  `precheck_ruled_out` when a graph is supplied, while the same request without
  a graph floods A* (expanded nodes > 0) and a reachable goal still runs A*; a
  post-build topology edit degrades to A* (`GraphStale`) instead of a wrong
  verdict; a mixed weighted batch proves the survivor partition scatters results
  back to their original slots; the warm unit rule-out path is allocation-free;
  and the weighted and unit ticks surface `precheck_ruled_out` through
  `PathAgentFrameStats` while the ruled-out agent never advances.
- `tess_path_test`: verifies the MVP A* path foundation, including top-down 2D
  paths around blocked tiles, invalid start and goal reporting, no-path
  reporting, direct-path and uniform-cost fast paths across top-down 2D,
  vertical 2D, and 3D layouts, coordinate support, exact route-cache and
  same-goal suffix reuse, explicit cache clearing, invalidation, and
  world-version invalidation, route-cache hit/suffix-hit span validity across
  later misses that grow cache storage (hits copy into the caller scratch),
  explicit chunk-version dependency tracking, exact weighted route-product
  replay and dependency invalidation, shared-goal, supplied-waypoint, and
  chunk-boundary portal route-product replay and dependency invalidation,
  chunk-boundary portal candidate counters, warmed portal segment-cache
  reuse, segment-cache `lookup_append` hit/miss/stale semantics including
  junction-node stitching and untouched caller storage on miss, stale
  segment rejection and caller-managed clear, failed-segment cache bypass,
  shared-goal distance-field builds and reconstruction, unit-cost multi-goal
  distance-field products, nearest-target reconstruction, product
  stale-version rejection, byte-budgeted field-product cache
  hit/miss/eviction/stale stats, move-only field-product store without
  world-sized copies, oversized-store whole-cache clearing, zero byte
  budget, same-key replacement byte accounting, least-recently-used (not
  insertion-order) eviction, distance-field error-status families
  (InvalidGoal/InvalidStart/empty `GoalSet` across plain, weighted, and
  product builds and reconstruction, with no garbage field or path left
  behind), local-domain weighted field bounds, mismatched-field rejection,
  weighted entry-cost routing, weighted direct and detour fast paths,
  weighted shared-goal fields, bounded weighted field builds and fallback,
  weighted batch grouping, endpoint validation, and allocation-free repeated
  queries with pre-reserved path scratch.
- `tess_path_view_test`: verifies the non-owning `PathView` handed out by
  `PathResult`: a default view is empty, a view mirrors its underlying nodes
  (size, front/back, indexing, iteration, `data()` identity) without copying,
  `span()` recovers the raw span, `suffix(offset)` returns the remaining path
  sharing the same storage (composing across suffixes) and bounds-clamps at or
  past the end, view and suffix operations allocate nothing, and a real
  `astar_path` result exposes a walkable suffix into its scratch storage.
- `tess_path_search_test`: verifies the real A* heap search loops against
  reference oracles. `path_test_util.h` provides shared serpentine maze
  builders (top-down 2D, vertical 2D, and multi-chunk 3D shapes) that
  defeat every pre-A* fast path — two parallel walls with two adjacent
  gaps each, at opposite ends, with start/goal displaced on both
  non-degenerate axes — plus an independent unit-cost BFS oracle and a
  weighted Dijkstra oracle. Serpentine tests pin exact optimal costs
  against the oracles, walk path validity, and assert
  `expanded_nodes > path.size()` (fast paths structurally return
  `expanded_nodes == path.size()`). It also pins start == goal semantics
  (Found, single-node path, cost 0) across every public path entry point:
  unit/weighted/cached A*, plain/weighted/bounded/boxed distance fields,
  distance-field products and `nearest_target`, weighted route and portal
  route products, the weighted batch (astar-fallback and shared-field
  branches), `PathRequestRuntime` unit and weighted processing, and agent
  ticks arriving immediately when the goal equals the position.
- `tess_path_cache_test`: verifies path-cache eviction and indexing, including
  the portal segment-cache budget (stale-entry sweeps that compact both the
  entry list and the path-node arena across repeated world edits, sweep
  removal of stale same-request duplicates, insertion-order eviction of live
  entries at budget, and sweep/eviction/stale-rejection stats), and the route
  cache's hash-indexed lookups (first-stored-entry-wins suffix determinism
  pinned against the pre-index linear scan, exact hits under aliased
  low-bit coordinate patterns, and entry/path-node cap breaches invalidating
  the whole cache with a `cap_invalidations` stat).
- `tess_path_runtime_test`: verifies the path request runtime MVP, including
  ticketed request/result lookup, stable copied result spans, unit route-cache
  reuse and invalidation across world edits, opt-in unit field-product cache
  reuse for repeated goals, start-chunk policy skip/use counters, stale product
  rejection, runtime cache clearing cadence, many-agent weighted batch
  processing through shared-goal fields, caller-configured cache clearing
  after repeated world edits, field-product-cache lookup-pointer stability
  across stores of other keys, and portal segment-cache runtime stats and
  `clear_caches()` for entries stored through the runtime accessor (the
  runtime's own processing passes do not populate that cache). It also
  covers runtime lifecycle: `clear_requests()` starting a fresh frame with
  regenerated tickets, empty request lists processing to empty results,
  failure stat tallies (invalid start/goal, no path) over mixed unit and
  weighted batches, and the policy byte budget driving real field-product
  eviction through `process_unit_cached`. Seeded (`std::mt19937`, fixed
  seeds) randomized equivalence pins repeated-goal grouping against a
  per-request A* oracle — statuses, costs, and the candidate/used/skipped
  group counters versus a reference computation — across both start-chunk
  policies, and a warm identical frame is allocation-free.
- `tess_path_runtime_sparse_test`: verifies the path runtime over a
  `SparseResidentWorld` (Slice 5a): unit route-cache hits within the resident
  set with no spurious invalidation, an in-place edit invalidating via the
  version term, an evict+reload of the SAME key at unchanged resident count
  invalidating via `residency_generation` alone (the case a version-only
  fingerprint misses, since a reloaded chunk's version resets to 0), an
  eviction on a cached route re-planning to a non-Found result rather than
  serving stale, and `process_weighted_batch` routing across resident chunks
  (Found, never Indeterminate under the default policy) then reporting NoPath
  after a wall edit, and `validate_movement_intent` / `movement_versions_match`
  rejecting a move into or out of an evicted chunk (`InvalidFrom`/`InvalidTo`/
  `StaleVersion`) rather than reading a non-resident slot out of bounds, and
  `render_tile_deltas` / `clear_render_delta_dirty` scanning only the resident
  set (a dirty resident chunk contributes its deltas; a non-resident chunk is
  never scanned) — all run under ASan.
- `tess_path_weighted_batch_test`: verifies `weighted_path_batch` edges:
  empty batches, all-distinct goals (pure A* fallbacks, no field builds),
  duplicate identical requests sharing one field build, per-member statuses
  for failed shared-goal groups matching `weighted_astar_path`'s endpoint
  validation precedence (invalid starts are not mislabeled with the goal's
  failure status), >MaxCost corridor tiles engaging the unbounded fallback
  (exact costs plus bounded-vs-unbounded build equality), seeded random-cost
  bounded/unbounded field equivalence, seeded batch-vs-oracle equivalence
  including the grouping stats counters, and allocation-free warm repeat
  batches.
- `tess_path_portal_route_test`: verifies chunk-portal route product corner
  cases: invalid endpoints reported before any candidate search, sealed
  start chunks yielding NoPath after all seven candidates (six axis orders
  plus greedy) fail, segment failure clearing the partially assembled path,
  a blocked-seam layout where only the greedy interleaved candidate finds a
  route (axis-order candidates all fail), and a multi-seam L-shaped route
  crossing several chunk boundaries with a contiguous stitched path.
- `tess_path_agent_test`: verifies the public path-agent wrapper, including
  goal assignment, runtime-backed request/result processing, tile-by-tile
  advancement and arrival, conservative reprocessing after world edits,
  invalid/unreachable goal handling, weighted shared-goal processing,
  allocation-free warm unit, unit field-product, and weighted agent batches,
  the phase lifecycle (goal set/clear transitions, transient movement
  failures keeping Found status while entering Blocked, structural failures
  turning terminally Unreachable), movement validation rejection statuses
  including stale topology/version branches, `record_movement_failure`
  bucketing every `MovementStatus` into its dedicated counter, the
  transient-versus-terminal `is_transient_movement_failure` classification
  of every status, and overflow-safe `manhattan_distance` at `int64`
  extremes. The warm no-alloc batches also pin submitted/found stats so a
  skipped frame cannot pass as allocation-free.
- `tess_path_agent_tick_test`: verifies the minimal path-agent tick wrapper,
  including tick advancement, dirty-gated path processing, movement ordering,
  explicit dirty-mark requirements after world edits, dirty reprocessing after
  world edits and goal changes, unreachable goals, weighted shared-goal ticks,
  allocation-free warm clean ticks (pinning that path processing is skipped
  while every agent still advances), two-argument goal assignment processed
  without a manual dirty mark, transiently blocked agents resuming and
  arriving, mid-route wall insertion triggering bounded re-paths, and boxed-in
  goals exhausting the retry budget into terminal Unreachable that stops
  consuming processing.
- `tess_assert_test`: verifies the `TESS_ASSERT`/`TESS_ASSERT_MSG` debug
  precondition policy — death tests for out-of-shape coordinates and
  out-of-range keys/tickets on unchecked accessors (`World::resolve`,
  `World::chunk`, `World::meta`, `tile_key`, `PathRequestRuntime::result`),
  `TESS_ASSERT_MSG` aborting with the caller's custom message and passing
  silently when the condition holds, that the disabled forms do not evaluate
  their conditions, and that guarded accessors stay `noexcept`.
- `tess_sim_scheduler_test`: verifies the simulation integration slice,
  including movement intent validation and commit, fixed-step accumulator
  pause/speed/clamp behavior with exact interpolation alpha values at known
  accumulator states, render-delta collection from dirty bounds including
  chunk-border and out-of-shape clipping, scheduler-driven render-dirty
  clearing after collection, queued-edit pathing invalidation with reroute to
  arrival around the edited tile, rejected-plan early return that reports
  planned-but-not-executed operations while leaving the world untouched and
  still ticking agents, unit and weighted movement scheduler occupancy
  commits, weighted cost-band detour routing, and movement dirty-mask
  metadata interplay for nonzero and zero masks.
- `tess_diagnostics_default_test`: verifies public diagnostic macros are
  disabled by default and do not evaluate arguments, including generic events.
- `tess_diagnostics_enabled_test`: verifies public diagnostic macros evaluate
  exactly once when `TESS_ENABLE_DIAGNOSTICS` is defined, and that scoped path
  and queued phase counters record generic diagnostic events including weighted
  cost reads and queued partitioned execution. It also links diagnostic
  allocation hooks and verifies scoped allocation counters observe global
  `new`/`delete` (via sanitizer malloc/free hooks under ASan/TSan). It
  hosts the serpentine-maze mutation guards: unit and weighted searches on
  the `path_test_util.h` fixtures must record `heap_pushes > 0`, which
  permanently fails if a future fast path learns to answer the mazes
  without the heap loop; the unit maze additionally pins the
  initialization, start/goal endpoint-check, and closed-neighbor counters.
- `tess_diagnostics_trace_test` (diagnostics-enabled, links the allocation
  hooks): covers the S4 trace layer. Warning sink -- `WarningSink` concept
  satisfaction, oldest-first ring semantics with overflow `dropped()` counts,
  `source_location` defaulting to the call site, and allocation-free `warn()`.
  Trace buffer -- ring insert/overflow with monotonic sequence gaps, empty-span
  drop, per-category `record_timing` accumulation (first-sample min/max, Count
  sentinel guarded on both record and record_timing), `clear()`, `ScopedTrace`
  nesting, the `ScopedTimer` capture-at-construction binding (a second buffer
  installed before the timer ends must not receive the sample), and
  allocation-free recording. Planner trace -- a conflicting plan and a
  phase-assignment plan produce the expected `Planner` records. Snapshot export
  -- `capture_diagnostics`/`capture_timing` copy every category and counter.
- `tess_diagnostics_panels_test` (diagnostics-enabled, `TESS_ENABLE_IMGUI` on):
  compile-and-run check for the opt-in `debug/imgui/panels.h` against a minimal
  ImGui stub (`tests/imgui_stub/imgui.h`) so a panel bug surfaces here rather
  than only in a real-ImGui consumer. The stub's `Text` carries real ImGui's
  printf-format attribute (`IM_FMTARGS` mirror) so `-Wformat` checks panel
  format strings. Exercises every draw function over a
  populated snapshot and pins `category_name` for every trace category.
- `tests/test_benchmark_tools.py`: pytest coverage for the benchmark gating
  tools (run with `uv run --frozen --group dev pytest`, and in the CI
  hooks-backstop job alongside `tests/test_git_hooks.py`). Verifies
  `tools/benchmark_thresholds.py` rejects duplicate benchmark names, selects
  repetition aggregates (median default, `--aggregate` override), converts
  all four Google Benchmark time units, fails on missing benchmarks, skips
  null limits, and reports missing/malformed input files as clear errors;
  and that `tools/benchmark_baseline_summary.py` filters aggregates by
  `run_type` and quotes CSV fields.
- `tests/test_check_public_surface.py`: pytest coverage for the advisory
  public-surface manifest checker (`tools/check_public_surface.py`, run in
  the same CI hooks-backstop pytest invocation). Synthetic header fixtures
  verify type and free-function extraction at namespace scope, skipping of
  members, function-local declarations, comments, macro-body braces, and
  `detail`/`internal` namespaces, plus failure messages for undocumented
  symbols and missing headers. One test asserts the committed
  `docs/architecture/surface.json` stays complete against the real
  `TESS_PUBLIC_HEADERS` headers.
- The benchmark binaries (`tess_bench`, `tess_bench_diagnostics`) enforce
  correctness checks after their timed loops via an aborting `bench_check`
  helper (endpoints, legal unit steps onto passable tiles, expected costs,
  agent/frame stats, cache outcomes). `tess_bench_diagnostics` additionally
  asserts the warm `path/astar_open_2d` iteration performs zero allocations.
