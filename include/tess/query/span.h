#pragma once

#include <tess/core/assert.h>
#include <tess/core/shape.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>

namespace tess {

/// One contiguous canonical x-axis run at a fixed world y and z coordinate.
struct TileSpan {
  Coord3 origin{};
  std::uint32_t x_count = 0;

  friend constexpr bool operator==(TileSpan lhs,
                                   TileSpan rhs) noexcept = default;
};

/// Counts exact canonical tiles and emitted x-axis runs for one query.
struct SpanQueryStats {
  std::uint64_t spans = 0;
  std::uint64_t tiles = 0;
};

template <typename Shape>
/** Returns the canonical non-negative coordinate bounds of a shape. */
[[nodiscard]] constexpr auto shape_bounds() noexcept -> Box3 {
  return Box3{{0, 0, 0}, ShapeTraits<Shape>::size};
}

namespace detail {

struct ClippedAxis {
  std::int64_t origin = 0;
  std::uint64_t count = 0;
};

[[nodiscard]] constexpr auto clip_axis(std::int64_t origin,
                                       std::uint64_t extent,
                                       std::uint64_t limit) noexcept
    -> ClippedAxis {
  if (extent == 0 || limit == 0) {
    return {};
  }
  const auto first = std::max<std::int64_t>(origin, 0);
  const auto skipped = tess::detail::axis_delta(origin, first);
  const auto first_unsigned = static_cast<std::uint64_t>(first);
  if (skipped >= extent || first_unsigned >= limit) {
    return {};
  }
  return ClippedAxis{
      first,
      std::min(extent - skipped, limit - first_unsigned),
  };
}

[[nodiscard]] constexpr auto lower_radius_bound(std::int64_t center,
                                                std::uint32_t radius) noexcept
    -> std::int64_t {
  constexpr auto min = std::numeric_limits<std::int64_t>::min();
  const auto distance = static_cast<std::uint64_t>(radius);
  if (center < 0 && tess::detail::magnitude(center) >
                        tess::detail::magnitude(min) - distance) {
    return min;
  }
  return center - static_cast<std::int64_t>(radius);
}

[[nodiscard]] inline auto integer_sqrt(std::uint64_t value) noexcept
    -> std::uint32_t {
  auto root =
      static_cast<std::uint64_t>(std::sqrt(static_cast<long double>(value)));
  while (root != 0 && root > value / root) {
    --root;
  }
  while (root < std::numeric_limits<std::uint32_t>::max() &&
         root + 1 <= value / (root + 1)) {
    ++root;
  }
  return static_cast<std::uint32_t>(root);
}

constexpr void add_tiles(SpanQueryStats& stats, std::uint64_t count) noexcept {
  stats.tiles = tess::detail::saturating_add(stats.tiles, count);
}

template <typename Fn>
void emit_x_runs(std::int64_t x, std::uint64_t count, std::int64_t y,
                 std::int64_t z, SpanQueryStats& stats, Fn& fn) {
  constexpr auto max = std::numeric_limits<std::uint32_t>::max();
  auto emitted = std::uint64_t{0};
  while (emitted < count) {
    const auto run = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(count - emitted, max));
    std::invoke(fn,
                TileSpan{{x + static_cast<std::int64_t>(emitted), y, z}, run});
    stats.spans = tess::detail::saturating_add(stats.spans, 1);
    add_tiles(stats, run);
    emitted += run;
  }
}

}  // namespace detail

template <typename Shape, typename Fn>
/** Emits exact clipped x-axis runs for a half-open world-space box. */
auto for_each_box_span(Box3 box, Fn&& fn) -> SpanQueryStats {
  const auto size = ShapeTraits<Shape>::size;
  const auto x = detail::clip_axis(box.origin.x, box.extent.x, size.x);
  const auto y = detail::clip_axis(box.origin.y, box.extent.y, size.y);
  const auto z = detail::clip_axis(box.origin.z, box.extent.z, size.z);
  auto stats = SpanQueryStats{};
  auto&& callback = fn;
  for (std::uint64_t z_offset = 0; z_offset < z.count; ++z_offset) {
    for (std::uint64_t y_offset = 0; y_offset < y.count; ++y_offset) {
      detail::emit_x_runs(
          x.origin, x.count, y.origin + static_cast<std::int64_t>(y_offset),
          z.origin + static_cast<std::int64_t>(z_offset), stats, callback);
    }
  }
  return stats;
}

template <typename Shape, typename Fn>
/** Emits exact clipped x-axis runs for an inclusive Euclidean tile radius. */
auto for_each_radius_span(Coord3 center, std::uint32_t radius, Fn&& fn)
    -> SpanQueryStats {
  const auto diameter = static_cast<std::uint64_t>(radius) * 2 + 1;
  const auto broad = Box3{
      {
          detail::lower_radius_bound(center.x, radius),
          detail::lower_radius_bound(center.y, radius),
          detail::lower_radius_bound(center.z, radius),
      },
      {diameter, diameter, diameter},
  };
  const auto size = ShapeTraits<Shape>::size;
  const auto y = detail::clip_axis(broad.origin.y, broad.extent.y, size.y);
  const auto z = detail::clip_axis(broad.origin.z, broad.extent.z, size.z);
  const auto radius_squared = static_cast<std::uint64_t>(radius) * radius;
  auto stats = SpanQueryStats{};
  auto&& callback = fn;

  for (std::uint64_t z_offset = 0; z_offset < z.count; ++z_offset) {
    const auto world_z = z.origin + static_cast<std::int64_t>(z_offset);
    const auto dz = tess::detail::abs_delta(center.z, world_z);
    if (dz > radius) {
      continue;
    }
    const auto dz_squared = dz * dz;
    if (dz_squared > radius_squared) {
      continue;
    }
    for (std::uint64_t y_offset = 0; y_offset < y.count; ++y_offset) {
      const auto world_y = y.origin + static_cast<std::int64_t>(y_offset);
      const auto dy = tess::detail::abs_delta(center.y, world_y);
      if (dy > radius) {
        continue;
      }
      const auto dy_squared = dy * dy;
      if (dy_squared > radius_squared - dz_squared) {
        continue;
      }
      const auto half_width =
          detail::integer_sqrt(radius_squared - dz_squared - dy_squared);
      const auto x = detail::clip_axis(
          detail::lower_radius_bound(center.x, half_width),
          static_cast<std::uint64_t>(half_width) * 2 + 1, size.x);
      detail::emit_x_runs(x.origin, x.count, world_y, world_z, stats, callback);
    }
  }
  return stats;
}

template <typename Shape, typename Fn>
/** Emits world-space runs for a clipped chunk-local half-open box. */
auto for_each_chunk_span(ChunkKey key, Box3 local_box, Fn&& fn)
    -> SpanQueryStats {
  using Traits = ShapeTraits<Shape>;
  TESS_ASSERT(key.value < Traits::chunk_count);
  if (key.value >= Traits::chunk_count) {
    return {};
  }
  const auto chunk = chunk_coord<Shape>(key);
  const auto base = Coord3{
      static_cast<std::int64_t>(chunk.x * Traits::chunk.x),
      static_cast<std::int64_t>(chunk.y * Traits::chunk.y),
      static_cast<std::int64_t>(chunk.z * Traits::chunk.z),
  };
  const auto x = detail::clip_axis(local_box.origin.x, local_box.extent.x,
                                   Traits::chunk.x);
  const auto y = detail::clip_axis(local_box.origin.y, local_box.extent.y,
                                   Traits::chunk.y);
  const auto z = detail::clip_axis(local_box.origin.z, local_box.extent.z,
                                   Traits::chunk.z);
  auto stats = SpanQueryStats{};
  auto&& callback = fn;
  for (std::uint64_t z_offset = 0; z_offset < z.count; ++z_offset) {
    for (std::uint64_t y_offset = 0; y_offset < y.count; ++y_offset) {
      detail::emit_x_runs(
          base.x + x.origin, x.count,
          base.y + y.origin + static_cast<std::int64_t>(y_offset),
          base.z + z.origin + static_cast<std::int64_t>(z_offset), stats,
          callback);
    }
  }
  return stats;
}

}  // namespace tess
