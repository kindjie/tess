#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>

namespace tess {

struct Extent3 {
  std::uint64_t x;
  std::uint64_t y;
  std::uint64_t z = 1;

  friend constexpr bool operator==(Extent3 lhs, Extent3 rhs) = default;
};

struct Coord2 {
  std::int64_t x;
  std::int64_t y;

  friend constexpr bool operator==(Coord2 lhs, Coord2 rhs) = default;
};

struct Coord3 {
  std::int64_t x;
  std::int64_t y;
  std::int64_t z = 0;

  friend constexpr bool operator==(Coord3 lhs, Coord3 rhs) = default;
};

struct ChunkCoord3 {
  std::uint64_t x;
  std::uint64_t y;
  std::uint64_t z = 0;

  friend constexpr bool operator==(ChunkCoord3 lhs, ChunkCoord3 rhs) = default;
};

struct LocalCoord3 {
  std::uint64_t x;
  std::uint64_t y;
  std::uint64_t z = 0;

  friend constexpr bool operator==(LocalCoord3 lhs, LocalCoord3 rhs) = default;
};

struct LocalTileId {
  std::uint64_t value;

  friend constexpr bool operator==(LocalTileId lhs, LocalTileId rhs) = default;
};

struct ChunkKey {
  std::uint64_t value;

  friend constexpr bool operator==(ChunkKey lhs, ChunkKey rhs) = default;
};

struct Box3 {
  Coord3 origin;
  Extent3 extent;

  friend constexpr bool operator==(Box3 lhs, Box3 rhs) = default;
};

template <typename Shape>
struct TileKey;

template <typename Shape>
struct ResolvedTile {
  ChunkKey chunk_key;
  LocalTileId local_tile_id;

  friend constexpr bool operator==(ResolvedTile lhs,
                                   ResolvedTile rhs) = default;
};

constexpr Coord3 to_coord3(Coord2 coord) { return Coord3{coord.x, coord.y, 0}; }

namespace detail {

using UInt128 = unsigned __int128;

constexpr bool is_power_of_two(std::uint64_t value) {
  return value != 0 && (value & (value - 1)) == 0;
}

constexpr bool is_valid_extent(Extent3 extent) {
  return extent.x > 0 && extent.y > 0 && extent.z > 0;
}

constexpr bool is_divisible_by(Extent3 size, Extent3 chunk) {
  return size.x % chunk.x == 0 && size.y % chunk.y == 0 &&
         size.z % chunk.z == 0;
}

constexpr UInt128 product(Extent3 extent) {
  return static_cast<UInt128>(extent.x) * static_cast<UInt128>(extent.y) *
         static_cast<UInt128>(extent.z);
}

constexpr UInt128 chunk_count(Extent3 size, Extent3 chunk) {
  return static_cast<UInt128>(size.x / chunk.x) *
         static_cast<UInt128>(size.y / chunk.y) *
         static_cast<UInt128>(size.z / chunk.z);
}

constexpr std::uint32_t bit_width(UInt128 value) {
  std::uint32_t bits = 0;
  while (value != 0) {
    ++bits;
    value >>= 1;
  }
  return bits;
}

constexpr std::uint32_t bits_for_count(UInt128 count) {
  return count <= 1 ? 0 : bit_width(count - 1);
}

template <std::uint32_t Bits>
using KeyStorage = std::conditional_t<Bits <= 64, std::uint64_t, UInt128>;

constexpr std::uint64_t magnitude(std::int64_t value) {
  return static_cast<std::uint64_t>(-(value + 1)) + 1;
}

constexpr bool axis_contains(std::int64_t origin, std::uint64_t extent,
                             std::int64_t coord) {
  if (coord < origin) {
    return false;
  }

  if (origin >= 0) {
    return static_cast<std::uint64_t>(coord - origin) < extent;
  }

  const auto origin_magnitude = magnitude(origin);
  if (coord < 0) {
    return origin_magnitude - magnitude(coord) < extent;
  }

  return origin_magnitude + static_cast<std::uint64_t>(coord) < extent;
}

}  // namespace detail

constexpr bool contains(Box3 box, Coord3 coord) {
  return detail::axis_contains(box.origin.x, box.extent.x, coord.x) &&
         detail::axis_contains(box.origin.y, box.extent.y, coord.y) &&
         detail::axis_contains(box.origin.z, box.extent.z, coord.z);
}

template <Extent3 Size, Extent3 Chunk>
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
struct ShapeTraits {
  static constexpr Extent3 size = Shape::size;
  static constexpr Extent3 chunk = Shape::chunk;

  static constexpr std::uint64_t chunk_count_x = size.x / chunk.x;
  static constexpr std::uint64_t chunk_count_y = size.y / chunk.y;
  static constexpr std::uint64_t chunk_count_z = size.z / chunk.z;

  static constexpr auto precise_chunk_count = detail::chunk_count(size, chunk);
  static constexpr auto precise_local_tile_count = detail::product(chunk);

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
struct TileKey {
  typename ShapeTraits<Shape>::TileKeyStorage value;

  friend constexpr bool operator==(TileKey lhs, TileKey rhs) = default;
};

template <typename Shape>
constexpr bool contains(Coord3 coord) {
  return contains(Box3{Coord3{0, 0, 0}, ShapeTraits<Shape>::size}, coord);
}

template <typename Shape>
constexpr ChunkCoord3 chunk_coord(Coord3 coord) {
  const auto chunk = ShapeTraits<Shape>::chunk;
  return ChunkCoord3{
      static_cast<std::uint64_t>(coord.x) / chunk.x,
      static_cast<std::uint64_t>(coord.y) / chunk.y,
      static_cast<std::uint64_t>(coord.z) / chunk.z,
  };
}

template <typename Shape>
constexpr LocalCoord3 local_coord(Coord3 coord) {
  const auto chunk = ShapeTraits<Shape>::chunk;
  return LocalCoord3{
      static_cast<std::uint64_t>(coord.x) % chunk.x,
      static_cast<std::uint64_t>(coord.y) % chunk.y,
      static_cast<std::uint64_t>(coord.z) % chunk.z,
  };
}

template <typename Shape>
constexpr LocalTileId local_tile_id(LocalCoord3 coord) {
  const auto chunk = ShapeTraits<Shape>::chunk;
  return LocalTileId{coord.x + coord.y * chunk.x + coord.z * chunk.x * chunk.y};
}

template <typename Shape>
constexpr Coord3 coord(ChunkCoord3 chunk_coord, LocalTileId local_tile_id) {
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
constexpr ChunkKey chunk_key(ChunkCoord3 coord) {
  using Traits = ShapeTraits<Shape>;
  return ChunkKey{coord.x + coord.y * Traits::chunk_count_x +
                  coord.z * Traits::chunk_count_x * Traits::chunk_count_y};
}

template <typename Shape>
constexpr ChunkCoord3 chunk_coord(ChunkKey key) {
  using Traits = ShapeTraits<Shape>;
  const auto chunk_xy = Traits::chunk_count_x * Traits::chunk_count_y;
  const auto z = key.value / chunk_xy;
  const auto remainder = key.value % chunk_xy;
  const auto y = remainder / Traits::chunk_count_x;
  const auto x = remainder % Traits::chunk_count_x;

  return ChunkCoord3{x, y, z};
}

template <typename Shape>
constexpr TileKey<Shape> tile_key(Coord3 coord) {
  using Traits = ShapeTraits<Shape>;
  const auto chunk = chunk_key<Shape>(chunk_coord<Shape>(coord));
  const auto local = local_tile_id<Shape>(local_coord<Shape>(coord));
  using Storage = typename Traits::TileKeyStorage;

  return TileKey<Shape>{
      (static_cast<Storage>(chunk.value) << Traits::local_bits) |
          static_cast<Storage>(local.value),
  };
}

template <typename Shape>
constexpr ChunkKey chunk_key(TileKey<Shape> key) {
  using Traits = ShapeTraits<Shape>;
  return ChunkKey{
      static_cast<std::uint64_t>(key.value >> Traits::local_bits),
  };
}

template <typename Shape>
constexpr LocalTileId local_tile_id(TileKey<Shape> key) {
  using Traits = ShapeTraits<Shape>;
  using Storage = typename Traits::TileKeyStorage;
  Storage mask = 0;
  if constexpr (Traits::local_bits == 64) {
    mask = static_cast<Storage>(std::numeric_limits<std::uint64_t>::max());
  } else {
    mask = (static_cast<Storage>(1) << Traits::local_bits) - 1;
  }

  return LocalTileId{static_cast<std::uint64_t>(key.value & mask)};
}

template <typename Shape>
constexpr Coord3 coord(TileKey<Shape> key) {
  return coord<Shape>(chunk_coord<Shape>(chunk_key<Shape>(key)),
                      local_tile_id<Shape>(key));
}

}  // namespace tess
