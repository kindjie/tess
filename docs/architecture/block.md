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
- `for_each_chunk(world, domain, policy, fn)` walks the domain serially and
  invokes `fn(ChunkView<World>)`.
- `ChunkView<World>` exposes the resolved page, metadata, key, chunk
  coordinate, chunk bounds, typed field spans through `ChunkPage`, and
  chunk-local tile helpers.
- `ChunkView<World>::local_coord(LocalTileId)` and
  `ChunkView<World>::local_tile_id(LocalCoord3)` convert local tile positions
  using row-major chunk-local order.
- `ChunkView<World>::world_coord(LocalCoord3)` and
  `ChunkView<World>::world_coord(LocalTileId)` convert local positions to
  world coordinates for the current chunk.
- `ChunkView<World>::for_each_tile(fn)` invokes
  `fn(LocalTileId, LocalCoord3)` for every local tile in ascending
  `LocalTileId` order.

Iteration is deterministic when domains are produced by the provided builders.
The hot executor path does not allocate when passed a prebuilt `ChunkDomain`,
and chunk-local tile iteration does not materialize ranges or decode global
`TileKey` values.

## TDD Divergences

The historical block-kernel pipeline TDD describes a richer staged executor.
This first M3 slice intentionally diverges:

- No planner, phase graph, barrier model, diagnostics, scratch storage, or
  external scheduler backend is implemented yet.
- `WritePolicy` is validated as a known enum value, but it is not enforced.
- Execution remains serial only for both chunk and tile iteration; parallel
  chunk scheduling is deferred.
- Domains are chunk-key spans over always-resident storage only; sparse
  residency, tile subranges, and dynamic residency transitions are not covered.
- Field access stays on `ChunkPage` spans instead of introducing kernel
  parameter binding or generated accessors.

These divergences keep the implemented API small while preserving a route to
the larger TDD pipeline once domain semantics and benchmark baselines settle.
