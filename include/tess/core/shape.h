#pragma once

#include <cstdint>

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

struct Box3 {
  Coord3 origin;
  Extent3 extent;

  friend constexpr bool operator==(Box3 lhs, Box3 rhs) = default;
};

constexpr Coord3 to_coord3(Coord2 coord) { return Coord3{coord.x, coord.y, 0}; }

namespace detail {

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
  static constexpr std::uint64_t chunk_count =
      chunk_count_x * chunk_count_y * chunk_count_z;

  static constexpr std::uint64_t local_tile_count = chunk.x * chunk.y * chunk.z;

  static constexpr bool single_chunk = chunk_count == 1;
  static constexpr bool degenerate_x = size.x == 1;
  static constexpr bool degenerate_y = size.y == 1;
  static constexpr bool degenerate_z = size.z == 1;
};

template <typename Shape>
constexpr bool contains(Coord3 coord) {
  return contains(Box3{Coord3{0, 0, 0}, ShapeTraits<Shape>::size}, coord);
}

}  // namespace tess
