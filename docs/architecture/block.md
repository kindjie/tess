# Block Foundation

The current block layer is a minimal serial domain executor over always-resident
world storage. It lives in `include/tess/block/block.h` and is exported by
`tess/tess.h`.

## Public Surface

- `WritePolicy` records intended write discipline: `ReadOnly`,
  `UniquePerTile`, `UniquePerChunk`, and `Unsafe`.
- `ChunkDomain` is a non-owning `std::span<const ChunkKey>` wrapper.
- `OwnedChunkDomain` owns sorted chunk keys returned by allocating domain
  builders and can be adapted to `ChunkDomain` while the owner lives.
- `chunk_domain(span)` adapts a prebuilt key span without allocation.
- `explicit_chunk_domain(span)` copies and sorts explicit keys in ascending
  `ChunkKey` order.
- `dirty_chunk_domain(world, flags)` and `active_chunk_domain(world, flags)`
  return owning domains using the current always-resident metadata queries.
- `BlockScratch` owns caller-reusable temporary storage backed by a heap
  `std::byte[]` buffer aligned for `std::max_align_t`. `reserve_bytes(bytes)`
  grows the backing store when needed by allocating a fresh buffer: growth
  invalidates previously returned spans and does not preserve contents,
  while `used_bytes()` accounting carries over. `reset()` rewinds the bump
  offset, and `capacity_bytes()`, `used_bytes()`, and `remaining_bytes()`
  expose byte accounting. The class is move-only.
- `BlockScratch::allocate<T>(count)` returns an aligned `std::span<T>` from
  the current bump offset. It does not allocate when existing capacity is
  sufficient. Zero-count requests, byte-count overflow, and capacity
  exhaustion all return an empty span and leave `used_bytes()` unchanged.
  `T` must be trivially default-constructible and trivially destructible
  (implicit-lifetime), so the spans over the implicitly created objects in
  the `std::byte` array storage are well-defined.
- `BlockDiagnostics` owns caller-reusable counters for serial block execution.
  It currently records `scratch_allocation_failures`, with explicit
  `record_scratch_allocation_failure()` and `reset()` calls.
- `BlockCtx<World, Policy>` is a non-owning serial execution context over a
  world, `ChunkDomain`, compile-time `WritePolicy`, and optional
  `BlockScratch` and `BlockDiagnostics`. Callers must keep the world, domain
  key storage, scratch storage, and diagnostics storage alive for the context
  lifetime.
- `block_ctx<Policy>(world, domain)` constructs a policy-typed `BlockCtx`
  without allocation.
- `block_ctx<Policy>(world, domain, scratch)` constructs a policy-typed
  `BlockCtx` with a non-owning scratch pointer.
- `block_ctx<Policy>(world, domain, diagnostics)` constructs a policy-typed
  `BlockCtx` with a non-owning diagnostics pointer.
- `block_ctx<Policy>(world, domain, scratch, diagnostics)` constructs a
  policy-typed `BlockCtx` with both optional caller-owned facilities.
- `BlockCtx::world()`, `domain()`, `policy()`, `size()`, and `empty()` expose
  the context inputs and domain state.
- `BlockCtx::scratch()` returns the optional scratch pointer, and
  `BlockCtx::reset_scratch()` rewinds it when present. Context iteration does
  not reset scratch automatically; callers choose whether scratch lifetime is
  per domain, per chunk, or per algorithm.
- `BlockCtx::diagnostics()` returns the optional diagnostics pointer, and
  `BlockCtx::reset_diagnostics()` clears it when present. Scratch exhaustion is
  still reported explicitly by caller code after `allocate<T>` returns an empty
  span.
- `BlockCtx::chunk_view(key)` returns an explicit chunk view for a chunk key.
  `ReadOnly` contexts expose `ChunkView<const World>` even when the stored
  world object is mutable. Other current policies expose `ChunkView<World>`.
- `BlockCtx::for_each_chunk(fn)` walks the domain serially and invokes
  `fn(view)` with the policy-selected view type.
- `for_each_chunk<Policy>(world, domain, fn)` constructs a policy-typed
  context and walks the domain serially without allocation.
- `for_each_chunk(world, domain, policy, fn)` validates and dispatches the
  runtime policy. `ReadOnly` invokes `fn(view)` with `ChunkView<const World>`
  for mutable worlds; other current policies invoke `ChunkView<World>`. Because
  the policy is runtime but the callback type is compile-time, callbacks passed
  to this overload must accept the selected policy view type; selecting an
  invalid runtime policy value or incompatible callback/policy pair is a
  programmer error and fails fast. Prefer
  `for_each_chunk<Policy>(world, domain, fn)` or `BlockCtx<World, Policy>`
  when the policy is already known.
- Parallel-ready ownership validation currently lives above the raw block API
  in queued-operation phase planning. `plan_parallel_execution_phases(plan)`
  accepts only `ReadOnly` and `UniquePerChunk` planned operations, keeps
  same-chunk mutable work in separate phases, and rejects `UniquePerTile` until
  tile subdomains exist.
- `ChunkView<World>` exposes the resolved page, metadata, key, chunk
  coordinate, chunk bounds, typed field spans through `ChunkPage`, and
  chunk-local tile helpers.
- `ChunkView<World>::local_coord(LocalTileId)` and
  `ChunkView<World>::local_tile_id(LocalCoord3)` convert local tile positions
  using row-major chunk-local order.
- `ChunkView<World>::local_bounds()` returns the signed local candidate box
  `{Coord3{0, 0, 0}, ShapeTraits<Shape>::chunk}`.
- `ChunkView<World>::contains_local(Coord3)` and
  `ChunkView<World>::try_local_coord(Coord3)` validate signed local candidate
  coordinates before converting them to unsigned `LocalCoord3`.
- `ChunkView<World>::is_boundary(LocalCoord3)` reports whether a valid local
  tile touches any non-degenerate chunk face, and
  `ChunkView<World>::is_interior(LocalCoord3)` is its inverse for valid local
  coordinates. Axes with chunk extent `1` do not make every tile a boundary.
- `ChunkView<World>::world_coord(LocalCoord3)` and
  `ChunkView<World>::world_coord(LocalTileId)` convert local positions to
  world coordinates for the current chunk.
- `ChunkView<World>::world_coord(Coord3)` converts signed local candidates,
  including one-step-out candidates, to world coordinates for the current
  chunk.
- `ChunkView<World>::for_each_tile(fn)` invokes
  `fn(LocalTileId, LocalCoord3)` for every local tile in ascending
  `LocalTileId` order.

Iteration is deterministic when domains are produced by the provided builders.
The hot executor path does not allocate when passed a prebuilt `ChunkDomain`.
Policy-typed `ReadOnly` contexts enforce const page, metadata, and field span
access at compile time. Prebuilt `BlockCtx` iteration is also allocation-free,
including use of pre-reserved `BlockScratch` during chunk and tile iteration.
Scratch allocation can occur during `reserve_bytes`, but not during
`allocate<T>` when capacity is sufficient. Chunk-local tile iteration does not
materialize ranges or decode global `TileKey` values.

Boundary and local-candidate helpers only describe the current chunk. They do
not define movement legality, neighbor ordering, direction enums, halo loading,
transition providers, or cross-chunk field access. Future topology and path
systems can use signed local candidates plus `contains_local` to decide whether
a candidate remains inside the chunk or needs an explicit transition.

## TDD Divergences

The historical block-kernel pipeline TDD describes a richer staged executor.
This first M3 slice intentionally diverges:

- No planner, phase graph, barrier model, rich diagnostics reporting,
  planner-owned scratch arenas, worker pools, or external scheduler backend is
  implemented yet.
- `BlockCtx` is only the current serial context. It does not yet provide
  scheduling, phase graphs, or planner state. Its scratch and diagnostics
  pointers are caller-owned and optional.
- Only `ReadOnly` is enforced today, and only through policy-typed block
  contexts and `for_each_chunk<Policy>`. `UniquePerTile`, `UniquePerChunk`,
  and `Unsafe` still record intended write discipline without ownership checks
  in raw block iteration. Queued-operation phase planning adds the first
  conservative parallel ownership check for planned `UniquePerChunk` work.
- Execution remains serial only for both chunk and tile iteration; parallel
  chunk scheduling is deferred.
- Domains are chunk-key spans over always-resident storage only; sparse
  residency, tile subranges, and dynamic residency transitions are not covered.
- Field access stays on `ChunkPage` spans instead of introducing kernel
  parameter binding or generated accessors.

These divergences keep the implemented API small while preserving a route to
the larger TDD pipeline once domain semantics and benchmark baselines settle.
