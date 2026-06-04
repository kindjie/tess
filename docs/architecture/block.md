# Block Foundation

The current block layer is a minimal serial domain executor over always-resident
world storage. It lives in `include/tess/block/block.h` and is exported by
`tess/tess.h`.

## Public Surface

- `WritePolicy` records intended write discipline: `ReadOnly`,
  `UniquePerTile`, `UniquePerChunk`, and `Unsafe`.
- `ChunkDomain` is a non-owning `std::span<const ChunkKey>` wrapper.
- `chunk_domain(span)` adapts a prebuilt key span without allocation.
- `explicit_chunk_domain(span)` copies and sorts explicit keys in ascending
  `ChunkKey` order.
- `dirty_chunk_domain(world, flags)` and `active_chunk_domain(world, flags)`
  return allocating vectors using the current always-resident metadata queries.
- `BlockCtx<World, Policy>` is a non-owning serial execution context over a
  world, `ChunkDomain`, and compile-time `WritePolicy`. Callers must keep the
  world and domain key storage alive for the context lifetime.
- `block_ctx<Policy>(world, domain)` constructs a policy-typed `BlockCtx`
  without allocation.
- `BlockCtx::world()`, `domain()`, `policy()`, `size()`, and `empty()` expose
  the context inputs and domain state.
- `BlockCtx::chunk_view(key)` returns an explicit chunk view for a chunk key.
  `ReadOnly` contexts expose `ChunkView<const World>` even when the stored
  world object is mutable. Other current policies expose `ChunkView<World>`.
- `BlockCtx::for_each_chunk(fn)` walks the domain serially and invokes
  `fn(view)` with the policy-selected view type.
- `for_each_chunk<Policy>(world, domain, fn)` constructs a policy-typed
  context and walks the domain serially without allocation.
- `for_each_chunk(world, domain, policy, fn)` remains a runtime-policy
  compatibility overload. It validates the policy enum value but keeps the view
  type derived from the world argument, because a runtime enum cannot change a
  C++ callback parameter type.
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
access at compile time. Prebuilt `BlockCtx` iteration is also allocation-free.
Chunk-local tile iteration does not materialize ranges or decode global
`TileKey` values.

Boundary and local-candidate helpers only describe the current chunk. They do
not define movement legality, neighbor ordering, direction enums, halo loading,
transition providers, or cross-chunk field access. Future topology and path
systems can use signed local candidates plus `contains_local` to decide whether
a candidate remains inside the chunk or needs an explicit transition.

## TDD Divergences

The historical block-kernel pipeline TDD describes a richer staged executor.
This first M3 slice intentionally diverges:

- No planner, phase graph, barrier model, diagnostics, scratch storage, or
  external scheduler backend is implemented yet.
- `BlockCtx` is only the current serial context. It does not yet provide
  scratch arenas, diagnostics, scheduling, phase graphs, or planner state.
- Only `ReadOnly` is enforced today, and only through policy-typed block
  contexts and `for_each_chunk<Policy>`. `UniquePerTile`, `UniquePerChunk`,
  and `Unsafe` still record intended write discipline without ownership checks.
- Execution remains serial only for both chunk and tile iteration; parallel
  chunk scheduling is deferred.
- Domains are chunk-key spans over always-resident storage only; sparse
  residency, tile subranges, and dynamic residency transitions are not covered.
- Field access stays on `ChunkPage` spans instead of introducing kernel
  parameter binding or generated accessors.

These divergences keep the implemented API small while preserving a route to
the larger TDD pipeline once domain semantics and benchmark baselines settle.
