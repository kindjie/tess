#pragma once

#include <tess/core/assert.h>
#include <tess/core/uint128.h>

#include <cstdint>
#include <limits>
#include <type_traits>

namespace tess {

/** Unsigned three-axis size used for worlds, chunks, and boxes. */
struct Extent3 {
  std::uint64_t x = 0;
  std::uint64_t y = 0;
  std::uint64_t z = 1;

  friend constexpr bool operator==(Extent3 lhs, Extent3 rhs) noexcept = default;
};

/** Signed coordinate in a two-dimensional consumer view. */
struct Coord2 {
  std::int64_t x = 0;
  std::int64_t y = 0;

  friend constexpr bool operator==(Coord2 lhs, Coord2 rhs) noexcept = default;
};

/** Canonical signed coordinate used by all world-space APIs. */
struct Coord3 {
  std::int64_t x = 0;
  std::int64_t y = 0;
  std::int64_t z = 0;

  friend constexpr bool operator==(Coord3 lhs, Coord3 rhs) noexcept = default;
};

/** Unsigned coordinate of a chunk within a finite shape. */
struct ChunkCoord3 {
  std::uint64_t x = 0;
  std::uint64_t y = 0;
  std::uint64_t z = 0;

  friend constexpr bool operator==(ChunkCoord3 lhs,
                                   ChunkCoord3 rhs) noexcept = default;
};

/** Unsigned coordinate of a tile relative to its owning chunk. */
struct LocalCoord3 {
  std::uint64_t x = 0;
  std::uint64_t y = 0;
  std::uint64_t z = 0;

  friend constexpr bool operator==(LocalCoord3 lhs,
                                   LocalCoord3 rhs) noexcept = default;
};

/** Linear row-major tile identifier within one chunk. */
struct LocalTileId {
  std::uint64_t value = 0;

  friend constexpr bool operator==(LocalTileId lhs,
                                   LocalTileId rhs) noexcept = default;
};

/** Linear row-major identifier of a chunk within a shape. */
struct ChunkKey {
  std::uint64_t value = 0;

  friend constexpr bool operator==(ChunkKey lhs,
                                   ChunkKey rhs) noexcept = default;
};

/** Half-open axis-aligned world-space box. */
struct Box3 {
  Coord3 origin{};
  Extent3 extent{};

  friend constexpr bool operator==(Box3 lhs, Box3 rhs) noexcept = default;
};

template <typename Shape>
/** Shape-specific packed key for one world tile. */
struct TileKey;

template <typename Shape>
/** Chunk and local identifiers obtained by resolving one tile. */
struct ResolvedTile {
  ChunkKey chunk_key{};
  LocalTileId local_tile_id{};

  friend constexpr bool operator==(ResolvedTile lhs,
                                   ResolvedTile rhs) noexcept = default;
};

/** Lifts a two-dimensional coordinate into the canonical z-zero plane. */
[[nodiscard]] constexpr Coord3 to_coord3(Coord2 coord) noexcept {
  return Coord3{coord.x, coord.y, 0};
}

namespace detail {

[[nodiscard]] constexpr bool is_power_of_two(std::uint64_t value) noexcept {
  return value != 0 && (value & (value - 1)) == 0;
}

[[nodiscard]] constexpr bool is_valid_extent(Extent3 extent) noexcept {
  return extent.x > 0 && extent.y > 0 && extent.z > 0;
}

[[nodiscard]] constexpr bool is_divisible_by(Extent3 size,
                                             Extent3 chunk) noexcept {
  return size.x % chunk.x == 0 && size.y % chunk.y == 0 &&
         size.z % chunk.z == 0;
}

// Wrap-proof "x * y * z fits uint64" for nonzero axes: UInt128 arithmetic
// wraps mod 2^128, so a triple product of uint64 axes can wrap and satisfy a
// fits-uint64 comparison it should fail. Division sidesteps that; purely
// compile-time (used only in ShapeTraits static_asserts).
[[nodiscard]] constexpr bool product_fits_uint64(std::uint64_t x,
                                                 std::uint64_t y,
                                                 std::uint64_t z) noexcept {
  constexpr auto max = std::numeric_limits<std::uint64_t>::max();
  if (x > max / y) {
    return false;
  }
  return x * y <= max / z;
}

[[nodiscard]] constexpr UInt128 product(Extent3 extent) noexcept {
  return static_cast<UInt128>(extent.x) * static_cast<UInt128>(extent.y) *
         static_cast<UInt128>(extent.z);
}

[[nodiscard]] constexpr UInt128 chunk_count(Extent3 size,
                                            Extent3 chunk) noexcept {
  return static_cast<UInt128>(size.x / chunk.x) *
         static_cast<UInt128>(size.y / chunk.y) *
         static_cast<UInt128>(size.z / chunk.z);
}

[[nodiscard]] constexpr std::uint32_t bit_width(UInt128 value) noexcept {
  std::uint32_t bits = 0;
  while (value != 0) {
    ++bits;
    value >>= 1;
  }
  return bits;
}

[[nodiscard]] constexpr std::uint32_t bits_for_count(UInt128 count) noexcept {
  return count <= 1 ? 0 : bit_width(count - 1);
}

template <std::uint32_t Bits>
using KeyStorage = std::conditional_t<Bits <= 64, std::uint64_t, UInt128>;

[[nodiscard]] constexpr std::uint64_t magnitude(std::int64_t value) noexcept {
  return static_cast<std::uint64_t>(-(value + 1)) + 1;
}

[[nodiscard]] constexpr std::uint64_t axis_delta(std::int64_t origin,
                                                 std::int64_t coord) noexcept {
  if (origin >= 0) {
    return static_cast<std::uint64_t>(coord - origin);
  }

  const auto origin_magnitude = magnitude(origin);
  if (coord < 0) {
    return origin_magnitude - magnitude(coord);
  }

  return origin_magnitude + static_cast<std::uint64_t>(coord);
}

[[nodiscard]] constexpr bool axis_contains(std::int64_t origin,
                                           std::uint64_t extent,
                                           std::int64_t coord) noexcept {
  if (coord < origin) {
    return false;
  }

  return axis_delta(origin, coord) < extent;
}

// Magnitude of the difference between two signed coordinates without the
// signed-overflow UB of `lhs - rhs` at the int64 extremes.
[[nodiscard]] constexpr std::uint64_t abs_delta(std::int64_t lhs,
                                                std::int64_t rhs) noexcept {
  return lhs < rhs
             ? static_cast<std::uint64_t>(rhs) - static_cast<std::uint64_t>(lhs)
             : static_cast<std::uint64_t>(lhs) -
                   static_cast<std::uint64_t>(rhs);
}

[[nodiscard]] constexpr std::uint64_t saturating_add(
    std::uint64_t lhs, std::uint64_t rhs) noexcept {
  constexpr auto max = std::numeric_limits<std::uint64_t>::max();
  return lhs > max - rhs ? max : lhs + rhs;
}

}  // namespace detail

/** Returns whether a coordinate lies in a half-open box without overflow. */
[[nodiscard]] constexpr bool contains(Box3 box, Coord3 coord) noexcept {
  return detail::axis_contains(box.origin.x, box.extent.x, coord.x) &&
         detail::axis_contains(box.origin.y, box.extent.y, coord.y) &&
         detail::axis_contains(box.origin.z, box.extent.z, coord.z);
}

/**
 * Returns overflow-safe Manhattan distance, saturated at `uint64_t` maximum.
 */
[[nodiscard]] constexpr auto manhattan_distance(Coord3 lhs, Coord3 rhs) noexcept
    -> std::uint64_t {
  return detail::saturating_add(
      detail::abs_delta(lhs.x, rhs.x),
      detail::saturating_add(detail::abs_delta(lhs.y, rhs.y),
                             detail::abs_delta(lhs.z, rhs.z)));
}

template <Extent3 Size, Extent3 Chunk>
/** Compile-time finite world shape and power-of-two chunk geometry. */
struct Shape {
  static constexpr Extent3 size = Size;
  static constexpr Extent3 chunk = Chunk;

  static_assert(detail::is_valid_extent(Size),
                "Shape size dimensions must be greater than zero.");
  static_assert(detail::is_valid_extent(Chunk),
                "Shape chunk dimensions must be greater than zero.");
  static_assert(detail::is_power_of_two(Chunk.x) &&
                    detail::is_power_of_two(Chunk.y) &&
                    detail::is_power_of_two(Chunk.z),
                "Shape chunk dimensions must be powers of two.");
  static_assert(detail::is_divisible_by(Size, Chunk),
                "Shape size dimensions must be multiples of chunk "
                "dimensions.");
};

template <typename Shape>
/** Derived counts, bit widths, and degeneracy flags for a `Shape`. */
struct ShapeTraits {
  static constexpr Extent3 size = Shape::size;
  static constexpr Extent3 chunk = Shape::chunk;

  static constexpr std::uint64_t chunk_count_x = size.x / chunk.x;
  static constexpr std::uint64_t chunk_count_y = size.y / chunk.y;
  static constexpr std::uint64_t chunk_count_z = size.z / chunk.z;

  static constexpr auto precise_chunk_count = detail::chunk_count(size, chunk);
  static constexpr auto precise_local_tile_count = detail::product(chunk);

  // The precise_* UInt128 products wrap mod 2^128 for extreme axes, which
  // would defeat the fits-uint64 asserts below; establish wrap-proof
  // (division-based) bounds first. Also require each size axis to fit int64
  // so coord() cannot wrap a Coord3 component. Compile-time only.
  static_assert(detail::product_fits_uint64(chunk_count_x, chunk_count_y,
                                            chunk_count_z),
                "Shape chunk count must fit std::uint64_t without wrapping.");
  static_assert(detail::product_fits_uint64(chunk.x, chunk.y, chunk.z),
                "Shape local tile count must fit std::uint64_t without "
                "wrapping.");
  static_assert(
      size.x <= static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max()) &&
          size.y <= static_cast<std::uint64_t>(
                        std::numeric_limits<std::int64_t>::max()) &&
          size.z <= static_cast<std::uint64_t>(
                        std::numeric_limits<std::int64_t>::max()),
      "Shape size axes must fit std::int64_t so coordinates cannot wrap.");

  static_assert(precise_chunk_count <=
                    static_cast<detail::UInt128>(
                        std::numeric_limits<std::uint64_t>::max()),
                "Shape chunk count must fit std::uint64_t.");
  static_assert(precise_local_tile_count <=
                    static_cast<detail::UInt128>(
                        std::numeric_limits<std::uint64_t>::max()),
                "Shape local tile count must fit LocalTileId.");

  static constexpr std::uint64_t chunk_count =
      static_cast<std::uint64_t>(precise_chunk_count);
  static constexpr std::uint64_t local_tile_count =
      static_cast<std::uint64_t>(precise_local_tile_count);

  static constexpr std::uint32_t local_bits =
      detail::bits_for_count(precise_local_tile_count);
  static constexpr std::uint32_t chunk_bits =
      detail::bits_for_count(precise_chunk_count);
  static constexpr std::uint32_t tile_key_bits = local_bits + chunk_bits;

  static_assert(chunk_bits <= 64, "ChunkKey must fit std::uint64_t.");
  static_assert(tile_key_bits <= 128, "TileKey must fit u64 or u128.");

  using TileKeyStorage = detail::KeyStorage<tile_key_bits>;

  static constexpr bool single_chunk = chunk_count == 1;
  static constexpr bool degenerate_x = size.x == 1;
  static constexpr bool degenerate_y = size.y == 1;
  static constexpr bool degenerate_z = size.z == 1;
};

template <typename Shape>
/** Packed shape-specific tile key using 64 or 128 bits as required. */
struct TileKey {
  ShapeTraits<Shape>::TileKeyStorage value{};

  friend constexpr bool operator==(TileKey lhs, TileKey rhs) noexcept = default;
};

template <typename Shape>
/** Returns whether a coordinate lies within a compile-time shape. */
[[nodiscard]] constexpr bool contains(Coord3 coord) noexcept {
  return contains(Box3{Coord3{0, 0, 0}, ShapeTraits<Shape>::size}, coord);
}

template <typename Shape>
/** Maps a contained world coordinate to its owning chunk coordinate. */
[[nodiscard]] constexpr ChunkCoord3 chunk_coord(Coord3 coord) noexcept {
  const auto chunk = ShapeTraits<Shape>::chunk;
  return ChunkCoord3{
      static_cast<std::uint64_t>(coord.x) / chunk.x,
      static_cast<std::uint64_t>(coord.y) / chunk.y,
      static_cast<std::uint64_t>(coord.z) / chunk.z,
  };
}

template <typename Shape>
/** Maps a contained world coordinate to its coordinate within a chunk. */
[[nodiscard]] constexpr LocalCoord3 local_coord(Coord3 coord) noexcept {
  const auto chunk = ShapeTraits<Shape>::chunk;
  return LocalCoord3{
      static_cast<std::uint64_t>(coord.x) % chunk.x,
      static_cast<std::uint64_t>(coord.y) % chunk.y,
      static_cast<std::uint64_t>(coord.z) % chunk.z,
  };
}

template <typename Shape>
/** Linearizes a local coordinate in the shape's row-major chunk layout. */
[[nodiscard]] constexpr LocalTileId local_tile_id(LocalCoord3 coord) noexcept {
  const auto chunk = ShapeTraits<Shape>::chunk;
  return LocalTileId{coord.x + coord.y * chunk.x + coord.z * chunk.x * chunk.y};
}

template <typename Shape>
/** Reconstructs a world coordinate from chunk and local identifiers. */
[[nodiscard]] constexpr Coord3 coord(ChunkCoord3 chunk_coord,
                                     LocalTileId local_tile_id) noexcept {
  const auto chunk = ShapeTraits<Shape>::chunk;
  const auto local_xy = chunk.x * chunk.y;
  const auto local_z = local_tile_id.value / local_xy;
  const auto remainder = local_tile_id.value % local_xy;
  const auto local_y = remainder / chunk.x;
  const auto local_x = remainder % chunk.x;

  return Coord3{
      static_cast<std::int64_t>(chunk_coord.x * chunk.x + local_x),
      static_cast<std::int64_t>(chunk_coord.y * chunk.y + local_y),
      static_cast<std::int64_t>(chunk_coord.z * chunk.z + local_z),
  };
}

template <typename Shape>
/** Linearizes an in-shape chunk coordinate into a `ChunkKey`. */
[[nodiscard]] constexpr ChunkKey chunk_key(ChunkCoord3 coord) noexcept {
  using Traits = ShapeTraits<Shape>;
  return ChunkKey{coord.x + coord.y * Traits::chunk_count_x +
                  coord.z * Traits::chunk_count_x * Traits::chunk_count_y};
}

template <typename Shape>
/** Decodes a `ChunkKey` into the shape's chunk-grid coordinate. */
[[nodiscard]] constexpr ChunkCoord3 chunk_coord(ChunkKey key) noexcept {
  using Traits = ShapeTraits<Shape>;
  const auto chunk_xy = Traits::chunk_count_x * Traits::chunk_count_y;
  const auto z = key.value / chunk_xy;
  const auto remainder = key.value % chunk_xy;
  const auto y = remainder / Traits::chunk_count_x;
  const auto x = remainder % Traits::chunk_count_x;

  return ChunkCoord3{x, y, z};
}

template <typename Shape>
/** Encodes a contained world coordinate as a packed tile key. */
[[nodiscard]] constexpr TileKey<Shape> tile_key(Coord3 coord) noexcept {
  using Traits = ShapeTraits<Shape>;
  TESS_ASSERT(contains<Shape>(coord));
  const auto local = local_tile_id<Shape>(local_coord<Shape>(coord));
  using Storage = Traits::TileKeyStorage;

  if constexpr (Traits::chunk_bits == 0) {
    // Single-chunk shapes: the key is the local id. Shifting the chunk key
    // into place would shift by local_bits, which may equal the storage
    // width and would be undefined behavior.
    return TileKey<Shape>{static_cast<Storage>(local.value)};
  } else {
    const auto chunk = chunk_key<Shape>(chunk_coord<Shape>(coord));
    return TileKey<Shape>{
        (static_cast<Storage>(chunk.value) << Traits::local_bits) |
            static_cast<Storage>(local.value),
    };
  }
}

template <typename Shape>
/** Extracts the owning chunk from a packed tile key. */
[[nodiscard]] constexpr ChunkKey chunk_key(TileKey<Shape> key) noexcept {
  using Traits = ShapeTraits<Shape>;
  if constexpr (Traits::chunk_bits == 0) {
    // Single-chunk shapes store only the local id; shifting right by
    // local_bits may equal the storage width and would be undefined
    // behavior.
    static_cast<void>(key);
    return ChunkKey{0};
  } else {
    return ChunkKey{
        static_cast<std::uint64_t>(key.value >> Traits::local_bits),
    };
  }
}

template <typename Shape>
/** Extracts the chunk-local tile identifier from a packed tile key. */
[[nodiscard]] constexpr LocalTileId local_tile_id(TileKey<Shape> key) noexcept {
  using Traits = ShapeTraits<Shape>;
  using Storage = Traits::TileKeyStorage;
  Storage mask = 0;
  if constexpr (Traits::local_bits == 64) {
    mask = static_cast<Storage>(std::numeric_limits<std::uint64_t>::max());
  } else {
    mask = (static_cast<Storage>(1) << Traits::local_bits) - 1;
  }

  return LocalTileId{static_cast<std::uint64_t>(key.value & mask)};
}

template <typename Shape>
/** Decodes a packed tile key into its world coordinate. */
[[nodiscard]] constexpr Coord3 coord(TileKey<Shape> key) noexcept {
  return coord<Shape>(chunk_coord<Shape>(chunk_key<Shape>(key)),
                      local_tile_id<Shape>(key));
}

}  // namespace tess
