# Shape, Coordinate, and Key Foundation

The shape layer defines the compile-time world geometry plus the coordinate
and key vocabulary every other layer builds on. It lives in
`include/tess/core/shape.h` (with `include/tess/core/assert.h`,
`include/tess/core/lattice.h`, and `include/tess/core/uint128.h` as support
headers) and is exported by
`tess/tess.h`. It implements the early slices of the historical
[shape/coordinate/key TDD][shape-tdd].

[shape-tdd]: https://github.com/kindjie/tess/blob/main/docs/tdd/core-shape-coordinate-key-system.md

## Public Surface

- `Extent3` records unsigned per-axis sizes. `z` defaults to `1` so a
  brace-initialized 2D extent (`Extent3{64, 64}`) is already well-formed.
- `Coord2` and `Coord3` are signed world coordinates. `to_coord3(coord)`
  lifts a `Coord2` to `Coord3` with `z = 0`.
- `HexCoord` is a signed axial `(q, r)` coordinate. `to_coord3` maps it to
  `(q, r, 0)`, `to_hex_coord` performs the inverse for the z-zero plane, and
  `hex_distance` returns saturated, overflow-safe axial distance without
  signed intermediate arithmetic.
- `ChunkCoord3` and `LocalCoord3` are unsigned chunk-grid and chunk-local
  coordinates.
- `LocalTileId` is the row-major tile index inside one chunk; `ChunkKey` is
  the row-major chunk index inside the shape.
- `Box3` is a signed origin plus unsigned `Extent3`. `contains(box, coord)`
  tests membership without signed-overflow UB, including boxes whose origin
  is negative.
- `ResolvedTile<Shape>` pairs a `ChunkKey` with a `LocalTileId`; storage
  lookups such as `World::resolve` return it.
- `manhattan_distance(lhs, rhs)` is the overflow-safe Manhattan distance:
  per-axis magnitudes are computed in unsigned arithmetic (the
  `detail::abs_delta` helper avoids `lhs - rhs` signed overflow at the
  int64 extremes) and the sum saturates at the `std::uint64_t` maximum
  instead of wrapping.
- `Shape<Size, Chunk, Lattice>` is the compile-time world description;
  `Lattice` defaults to `lattice::Orthogonal`, preserving existing
  two-argument declarations. `lattice::HexAxial` selects an axial hex shape
  and requires both world and chunk z extents to equal one. Lattice types
  carry explicit stable `Identity` and version constants for persistent
  metadata and derived-product stamps. The `LatticeType` concept checks that
  public lattice contract; built-ins are `lattice::Orthogonal` and
  `lattice::HexAxial`.
- A shape rejects invalid geometry with `static_assert`: all extents must be
  nonzero, chunk
  dimensions must be powers of two, and world size must be a multiple of
  the chunk size on every axis.
- `ShapeTraits<Shape>` exposes `lattice_type`, `lattice_identity`, and
  `lattice_version`, then derives per-axis chunk counts, total `chunk_count`
  and `local_tile_count`, key bit widths (`local_bits`, `chunk_bits`,
  `tile_key_bits`), the `TileKeyStorage` type, and the `single_chunk` /
  `degenerate_x` / `degenerate_y` / `degenerate_z` flags.
- `TileKey<Shape>` is the packed global tile key. Its storage type is
  chosen per shape: `std::uint64_t` when `tile_key_bits <= 64`, otherwise
  the portable 128-bit `detail::UInt128`.
- Conversion helpers are all `constexpr` and shape-templated:
  `chunk_coord(Coord3)`, `local_coord(Coord3)`,
  `local_tile_id(LocalCoord3)`, `coord(ChunkCoord3, LocalTileId)`,
  `chunk_key(ChunkCoord3)`, `chunk_coord(ChunkKey)`, `tile_key(Coord3)`,
  `chunk_key(TileKey)`, `local_tile_id(TileKey)`, and `coord(TileKey)`.
- All of the core value types above default-construct to a defined state
  through default member initializers (zero, except `Extent3::z = 1`), so
  a brace-initialized `Coord3{}` or `TileKey<Shape>{}` never carries
  indeterminate field values.
- `TESS_ENABLE_ASSERTS` reports whether contract assertions are active;
  `TESS_ASSERT(expr)` and `TESS_ASSERT_MSG(expr, message)` terminate on a
  failed contract in those builds and compile out under `NDEBUG`.

## Key Packing

Local tile ids and chunk keys are row-major:

```text
local_tile_id = x + y * chunk.x + z * chunk.x * chunk.y
chunk_key     = cx + cy * chunk_count_x + cz * chunk_count_x * chunk_count_y
tile_key      = (chunk_key << local_bits) | local_tile_id
```

`ShapeTraits` computes tile and chunk counts in `detail::UInt128`
(`precise_chunk_count`, `precise_local_tile_count`) so the intermediate
products cannot overflow before validation, then enforces the 64-bit
boundaries with `static_assert`: the chunk count and local tile count must
each fit `std::uint64_t`, `chunk_bits` must be at most 64 (`ChunkKey` is a
`std::uint64_t`), and `tile_key_bits` must be at most 128.

`detail::UInt128` is a portable 128-bit unsigned integer used
unconditionally on every compiler, so Clang and GCC CI exercise exactly the
code MSVC compiles (MSVC has no unsigned `__int128`). It provides only the
operations shape.h needs; arithmetic wraps modulo 2^128 like the builtin,
shifts define counts >= 128 as zero, and no operation shifts a
`std::uint64_t` by 64, so none of it has undefined behavior.

Two boundary cases are handled explicitly instead of shifting by a full
storage width (which would be UB):

- Single-chunk shapes have `chunk_bits == 0`; the tile key is the local id
  alone, and `chunk_key(TileKey)` returns `ChunkKey{0}` without shifting.
- Shapes with `local_bits == 64` select a full-width mask directly in
  `local_tile_id(TileKey)` instead of computing `(1 << 64) - 1`.

## Behavior

`contains(Box3, Coord3)` and the shape-level `contains<Shape>(coord)`
overload delegate to unsigned per-axis delta math (`detail::axis_delta`)
that remains correct when the box origin is negative or when the extent
does not fit in a signed range.

The coordinate-to-chunk conversions (`chunk_coord`, `local_coord`) divide
and mask after casting to unsigned, so they require coordinates inside the
shape (nonnegative on every axis). `tile_key(Coord3)` documents and
enforces that precondition with `TESS_ASSERT(contains<Shape>(coord))`.

`TESS_ASSERT` (in `tess/core/assert.h`) is the project-wide precondition
policy for unchecked fast-path APIs:

- Checked entry points (`try_resolve`, `try_field`, plan validation) stay
  the runtime-validated API and never assert on bad input.
- Unchecked hot accessors keep `noexcept` and assert their preconditions.
- Asserts are enabled when `TESS_ENABLE_ASSERTS` is defined nonzero, and
  default to on exactly when `NDEBUG` is absent, so release and bench
  builds pay zero cost.
- A failed assert aborts; it never throws, so `noexcept` functions stay
  `noexcept`.

## Deliberate Limits

This layer is compile-time geometry only. It does not implement sparse or
unbounded worlds, runtime-sized shapes, non-power-of-two chunks, region or
hierarchy keys, or serialization of keys across differently shaped worlds.
Key values are only meaningful relative to their `Shape`.
