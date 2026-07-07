# Tests

- Tests use GoogleTest.
- `allocation_counter.{h,cc}` is shared only by allocation-sensitive test
  binaries that need global `new`/`delete` counters. Do not link it into more
  than one translation unit inside the same executable.
- Under AddressSanitizer, `allocation_counter.cc` must use sanitizer allocation
  hooks instead of replacing global `new`, so ASan's alloc/dealloc mismatch
  check remains meaningful.
- `tess_smoke`: verifies that the public `tess::tess` target is consumable,
  that the root public header compiles, and that public version constants match.
- `tess_shape_test`: verifies public shape primitives, constexpr shape traits,
  degenerate-axis handling, containment helpers, key width inference,
  coordinate/chunk/local/tile key conversion helpers, the portable
  `tess::detail::UInt128` operations (carrying multiply, borrow subtract,
  boundary shifts including counts of 64/127/>=128, comparisons, narrowing,
  and `bit_width`/`bits_for_count` at the 64-bit boundary), >64-bit
  tile-key round-trips on the huge bounded shape, and the largest legal
  64-bit boundary shape (single chunk, 2^63 local tiles, `chunk_bits == 0`)
  round-tripping without wide shifts.
- `tess_storage_test`: verifies typed field schemas, resident chunk pages, and
  always-resident dense worlds, including SoA field independence, contiguous
  typed spans, metadata, const access, key/coord lookup, coordinate resolution,
  checked invalid-coordinate behavior, per-chunk dirty/active/topology-version
  metadata, noexcept hot accessors, and allocation-free local field/span/world
  access after construction.
- `tess_block_test`: verifies chunk-domain builders, policy-typed `BlockCtx`
  construction and iteration, serial block iteration, owned domain lifetimes,
  const-correct chunk views and world access including compile-time and runtime
  `ReadOnly` policy enforcement, runtime invalid write-policy fail-fast
  behavior, chunk bounds for 2D vertical and 3D worlds, chunk-local tile
  iteration and coordinate helpers,
  boundary/local-candidate helpers across 2D/3D and degenerate axes, and
  allocation-free iteration for prebuilt domains and contexts, including
  pre-reserved caller-owned block scratch and explicit scratch-exhaustion
  diagnostics.
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
  already-built queue/report/plan spans.
- `tess_topology_test`: verifies local chunk-region labeling, blocked-tile
  region rejection, boundary exits, invalid chunks, inter-chunk portal pairing,
  reachability, and top-down 2D, vertical 2D, and 3D degenerate-axis behavior.
  Reachability coverage includes same-region, multi-hop, disconnected, enclosed,
  blocked-seam, invalid endpoint, and vertical 2D cases.
- `tess_path_test`: verifies the MVP A* path foundation, including top-down 2D
  paths around blocked tiles, invalid start and goal reporting, no-path
  reporting, direct-path and uniform-cost fast paths across top-down 2D,
  vertical 2D, and 3D layouts, coordinate support, exact route-cache and
  same-goal suffix reuse, explicit cache clearing, invalidation, and
  world-version invalidation, explicit chunk-version dependency tracking,
  exact weighted route-product replay and dependency invalidation, shared-goal,
  supplied-waypoint, and chunk-boundary portal route-product replay and
  dependency invalidation, chunk-boundary portal candidate counters, warmed
  portal segment-cache reuse, stale segment rejection and caller-managed clear,
  failed-segment cache bypass, shared-goal distance-field builds and
  reconstruction, unit-cost multi-goal distance-field products,
  nearest-target reconstruction, product stale-version rejection,
  byte-budgeted field-product cache hit/miss/eviction/stale stats,
  local-domain weighted field bounds, mismatched-field rejection, weighted
  entry-cost routing, weighted direct and detour fast paths, weighted
  shared-goal fields, bounded weighted field builds and fallback, weighted
  batch grouping, endpoint validation, and allocation-free repeated queries
  with pre-reserved path scratch.
- `tess_path_runtime_test`: verifies the path request runtime MVP, including
  ticketed request/result lookup, stable copied result spans, unit route-cache
  reuse and invalidation across world edits, opt-in unit field-product cache
  reuse for repeated goals, start-chunk policy skip/use counters, stale product
  rejection, runtime cache clearing cadence, many-agent weighted batch
  processing through shared-goal fields, and caller-configured cache clearing
  after repeated world edits.
- `tess_path_agent_test`: verifies the public path-agent wrapper, including
  goal assignment, runtime-backed request/result processing, tile-by-tile
  advancement and arrival, conservative reprocessing after world edits,
  invalid/unreachable goal handling, weighted shared-goal processing, and
  allocation-free warm unit, unit field-product, and weighted agent batches.
- `tess_path_agent_tick_test`: verifies the minimal path-agent tick wrapper,
  including tick advancement, dirty-gated path processing, movement ordering,
  explicit dirty-mark requirements after world edits, dirty reprocessing after
  world edits and goal changes, unreachable goals, weighted shared-goal ticks,
  and allocation-free warm clean ticks.
- `tess_assert_test`: verifies the `TESS_ASSERT` debug precondition policy —
  death tests for out-of-shape coordinates and out-of-range keys/tickets on
  unchecked accessors (`World::resolve`, `World::chunk`, `World::meta`,
  `tile_key`, `PathRequestRuntime::result`), that the disabled form does not
  evaluate its condition, and that guarded accessors stay `noexcept`.
- `tess_diagnostics_default_test`: verifies public diagnostic macros are
  disabled by default and do not evaluate arguments, including generic events.
- `tess_diagnostics_enabled_test`: verifies public diagnostic macros evaluate
  exactly once when `TESS_ENABLE_DIAGNOSTICS` is defined, and that scoped path
  and queued phase counters record generic diagnostic events including weighted
  cost reads and queued partitioned execution. It also links diagnostic
  allocation hooks and verifies scoped allocation counters observe global
  `new`/`delete`.
