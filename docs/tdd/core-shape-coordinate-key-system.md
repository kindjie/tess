# TDD: Core Shape / Coordinate / Key System

## 1. Summary

This document defines the compile-time world shape system, coordinate model, canonical tile identity, and derived capabilities for the tile/path simulation substrate.

The design is 3D-only internally. Degenerate dimensions are valid, including traditional top-down 2D (`z == 1`) and vertical 2D side-view worlds (`x == 1` or `y == 1` with `z > 1`).

World dimensions and chunk dimensions are compile-time constants. Runtime world dimensions are out of scope for production code.

## 2. Goals

- Provide one coordinate/key model for 2D, vertical 2D, 3D, dense, sparse, and huge bounded worlds.
- Let game developers specify only essential shape data: world size and chunk size.
- Derive internal traits automatically.
- Provide fast coordinate -> key -> chunk/local math.
- Support enormous bounded worlds without implying full allocation.
- Fail at compile time for invalid or unsupported shapes.
- Avoid special-case runtime branches in hot paths.

## 3. Non-goals

- No runtime-configurable world dimensions.
- No unbounded/infinite coordinate space in v1.
- No arbitrary non-power-of-two chunk sizes.
- No partial edge chunks.
- No separate 2D implementation.
- No per-tile hash lookup in hot loops.
- No assumption that adjacent TileKeys are grid neighbors.

## 4. User-facing shape

Conceptual preferred syntax:

```cpp
using World = tiles::World<
  tiles::Shape{
    .size = {1024, 1024, 1},
    .chunk = {64, 64, 1}
  },
  Fields
>;
```

Equivalent named type syntax is acceptable if compiler ergonomics require it.

The game developer describes the world shape. They do not choose storage policies directly.

## 5. Shape constraints

Required compile-time constraints:

- all size dimensions > 0
- all chunk dimensions > 0
- chunk dimensions are powers of two
- size dimensions are multiples of chunk dimensions
- local tile count fits LocalTileId
- chunk count fits derived ChunkKey layout
- TileKey fits u64 or u128
- dense/all-resident memory estimate fits budget when AlwaysResident is required

Invalid shapes fail at compile time with clear diagnostics.

## 6. Core types

- Extent3
- Coord3
- Coord2, convenience only
- ChunkCoord3
- LocalTileId
- ChunkKey
- TileKey
- ResolvedTile = ChunkHandle + LocalTileId
- Box3

Coord2 lowers to Coord3 `{x, y, 0}` only as a convenience for top-down 2D APIs. Vertical 2D worlds should expose appropriate helpers but still use Coord3 internally.

## 7. Derived traits

For each Shape, derive:

- effective_dimensions
- degenerate_axes
- chunk_count_x/y/z
- chunk_count
- local_tile_count
- local_bits
- chunk_bits
- tile_key_bits
- tile_key_width: u64 if possible, else u128
- single_chunk
- all_chunks_resident
- missing_chunks_possible
- topology capabilities
- dense_products_allowed
- sparse_products_preferred

## 8. TileKey layout

Preferred layout for fixed power-of-two chunk sizes:

```text
TileKey = (ChunkKey << local_bits) | LocalTileId
```

LocalTileId is row-major inside the chunk:

```text
local_id = local_x + local_y * chunk.x + local_z * chunk.x * chunk.y
```

For degenerate dimensions, the degenerate local coordinate is always 0.

TileKey ordering is useful for sorting/batching, but adjacent keys are not guaranteed to be adjacent tiles. Neighbor lookup must use coordinate/transition providers.

## 9. ChunkKey ordering

Initial ordering:

- single chunk: zero key
- 2D chunked: Morton2D by default
- vertical 2D: Morton over effective two axes
- 3D: Morton3D by default
- Hilbert: benchmark candidate
- Sierpinski: experimental/lower priority for cubic chunks

Chunk order is used for scheduling, streaming, serialization, batching, eviction heuristics, and debug output. It is not the hot per-tile memory layout.

## 10. Coordinate conversion

Required operations:

- Coord3 -> ChunkCoord3
- Coord3 -> LocalCoord3
- Coord3 -> TileKey
- TileKey -> ChunkKey
- TileKey -> LocalTileId
- TileKey -> Coord3
- ChunkCoord3 -> ChunkKey
- ChunkKey -> ChunkCoord3
- ChunkHandle + LocalTileId -> Coord3

For power-of-two chunks, use shifts and masks.

Hot loops should work on resolved chunks and local ids rather than repeatedly converting global coordinates.

## 11. Bounds behavior

Finite constexpr bounds are authoritative.

`contains(Coord3)` checks all axes.

Out-of-bounds behavior:

- checked APIs return expected/optional/false
- debug builds assert for unchecked APIs
- unsafe APIs assume valid coordinates

## 12. Degenerate dimensions

Any dimension with extent 1 should simplify cleanly.

Cases:

- top-down 2D: `size.z == 1`
- vertical 2D x/z plane: `size.y == 1 && size.z > 1`
- vertical 2D y/z plane: `size.x == 1 && size.z > 1`
- column: `size.x == 1 && size.y == 1`

Rules:

- disabled axes produce no normal neighbor transitions
- Box3 remains the universal bounds type
- chunk ordering uses effective dimensions
- path topology must not assume x/y is always the main plane
- render deltas preserve actual coordinates

## 13. Residency inference dependency

Residency is derived after field schema is known.

Inputs:

- Shape
- field schema
- chunk metadata estimate
- topology metadata estimate
- spatial index estimate
- configured resident budget

Default:

- Auto

Advanced:

- RequireAlwaysResident
- RequireSparseResident

## 14. Large bounded worlds

Huge dimensions are allowed if key layout and estimates fit.

Rules:

- logical extents are compile-time bounds
- resident/generated chunks consume memory
- full-world iteration/products require explicit opt-in
- planner warns/rejects dangerous operations
- key packing is validated at compile time

Large dimensions are fine; large implicit work is not.

## 15. Save compatibility

Save metadata records:

- shape ID or shape metadata
- chunk dimensions
- key layout version
- field schema version
- chunk/order version
- library version

Changing any of these may invalidate TileKeys and require migration or save invalidation.

Optional robustness: store Coord3 alongside long-lived TileKeys for debug/migration.

## 16. API sketch

```cpp
struct Extent3 { uint64_t x, y, z = 1; };
struct Coord3 { int64_t x, y, z = 0; };
struct Coord2 { int64_t x, y; };

template<class Shape>
struct ShapeTraits {
  static constexpr bool single_chunk;
  static constexpr uint32_t local_bits;
  static constexpr uint32_t chunk_bits;
  using TileKeyStorage = uint64_t_or_uint128_t;
};

template<class Shape>
struct TileKey {
  typename ShapeTraits<Shape>::TileKeyStorage value;
};
```

World methods:

- contains
- key
- coord
- try_resolve
- resolve_unchecked
- chunk_coord
- chunk_key

## 17. Compile-time diagnostics

Diagnostics should mention offending value, required constraint, and suggested fix.

Examples:

- chunk dimensions must be powers of two
- world dimensions must be multiples of chunk dimensions
- TileKey does not fit u64 or u128
- shape is incompatible with selected ordering
- field schema causes residency inference to exceed budget when RequireAlwaysResident is set

## 18. Performance concerns

- Avoid key decoding in hot loops.
- Use Morton first; benchmark Hilbert.
- u128 keys may cost more in maps/sorts/frontiers.
- Operation planning must prevent accidental huge work.

## 19. Tests

Compile-time:

- valid single-chunk top-down 2D
- valid vertical 2D
- valid chunked 3D
- valid huge bounded sparse world
- invalid non-power-of-two chunk
- invalid non-divisible size
- invalid key overflow

Runtime:

- coord/key round trip
- coord/chunk/local conversion
- degenerate-axis behavior
- chunk ordering stability
- save metadata compatibility

## 20. Benchmarks

- coord -> key
- key -> coord
- key -> chunk/local
- Morton2D vs Morton3D with degenerate axis
- u64 vs u128 key operations
- single-chunk overhead vs flat array baseline

## 21. Acceptance criteria

- Shape validates at compile time.
- Top-down 2D, vertical 2D, and 3D use the same model.
- TileKey width is inferred as u64 or u128.
- Invalid shapes fail with understandable errors.
- Hot chunk-local iteration does not require global key decode per tile.
- Huge bounded worlds do not imply full allocation.
- Save metadata can detect incompatible key/shape changes.
