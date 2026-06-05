#pragma once

#include <tess/core/shape.h>
#include <tess/diagnostics/diagnostics.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace tess {

enum class PathStatus : std::uint8_t {
  Found,
  InvalidStart,
  InvalidGoal,
  NoPath,
};
static_assert(sizeof(PathStatus) == sizeof(std::uint8_t));

struct PathRequest {
  Coord3 start;
  Coord3 goal;
};

struct PathResult {
  PathStatus status = PathStatus::NoPath;
  std::uint32_t cost = 0;
  std::size_t expanded_nodes = 0;
  std::size_t reached_nodes = 0;
  std::span<const Coord3> path;
};

class PathScratch {
 public:
  struct OpenNode {
    std::uint64_t index = 0;
    std::uint32_t g = 0;
    std::uint32_t f = 0;
  };

  void reserve_nodes(std::size_t node_count) {
    open_.reserve(node_count);
    generation_.reserve(node_count);
    state_.reserve(node_count);
    g_.reserve(node_count);
    parent_.reserve(node_count);
    touched_.reserve(node_count);
    path_.reserve(node_count);
  }

  void clear() noexcept {
    advance_epoch();
    open_.clear();
    touched_.clear();
    path_.clear();
  }

  [[nodiscard]] auto capacity_nodes() const noexcept -> std::size_t {
    return state_.capacity();
  }

 private:
  template <typename World, typename Tag>
  friend auto astar_path(const World& world, PathRequest request,
                         PathScratch& scratch) -> PathResult;

  void advance_epoch() noexcept {
    ++epoch_;
    if (epoch_ == 0) {
      std::fill(generation_.begin(), generation_.end(), 0);
      epoch_ = 1;
    }
  }

  [[nodiscard]] auto is_current(std::size_t offset) const noexcept -> bool {
    return generation_[offset] == epoch_;
  }

  [[nodiscard]] auto state_at(std::size_t offset,
                              std::uint8_t unseen) const noexcept
      -> std::uint8_t {
    return is_current(offset) ? state_[offset] : unseen;
  }

  [[nodiscard]] auto g_at(std::size_t offset,
                          std::uint32_t infinite_cost) const noexcept
      -> std::uint32_t {
    return is_current(offset) ? g_[offset] : infinite_cost;
  }

  void touch_node(std::uint64_t index) {
    const auto offset = static_cast<std::size_t>(index);
    generation_[offset] = epoch_;
    touched_.push_back(index);
  }

  std::vector<OpenNode> open_;
  std::vector<std::uint32_t> generation_;
  std::uint32_t epoch_ = 1;
  std::vector<std::uint8_t> state_;
  std::vector<std::uint32_t> g_;
  std::vector<std::uint64_t> parent_;
  std::vector<std::uint64_t> touched_;
  std::vector<Coord3> path_;
};

namespace detail {

enum class Axis : std::uint8_t {
  X,
  Y,
  Z,
};

[[nodiscard]] constexpr auto abs_delta(std::int64_t lhs,
                                       std::int64_t rhs) noexcept
    -> std::uint64_t {
  return lhs < rhs ? static_cast<std::uint64_t>(rhs - lhs)
                   : static_cast<std::uint64_t>(lhs - rhs);
}

[[nodiscard]] constexpr auto manhattan(Coord3 lhs, Coord3 rhs) noexcept
    -> std::uint32_t {
  const auto distance = abs_delta(lhs.x, rhs.x) + abs_delta(lhs.y, rhs.y) +
                        abs_delta(lhs.z, rhs.z);
  if (distance > std::numeric_limits<std::uint32_t>::max()) {
    return std::numeric_limits<std::uint32_t>::max();
  }
  return static_cast<std::uint32_t>(distance);
}

template <typename World>
[[nodiscard]] constexpr auto tile_count() noexcept -> std::size_t {
  static_assert(World::chunk_count <= std::numeric_limits<std::size_t>::max() /
                                          World::local_tile_count);
  return static_cast<std::size_t>(World::chunk_count * World::local_tile_count);
}

template <typename Shape>
[[nodiscard]] constexpr auto tile_index(Coord3 coord) noexcept
    -> std::uint64_t {
  static_assert(ShapeTraits<Shape>::tile_key_bits <= 64,
                "MVP pathfinding supports shapes with u64 tile keys.");
  return static_cast<std::uint64_t>(tile_key<Shape>(coord).value);
}

template <typename Shape>
[[nodiscard]] constexpr auto tile_coord(std::uint64_t index) noexcept
    -> Coord3 {
  using Storage = typename ShapeTraits<Shape>::TileKeyStorage;
  return coord<Shape>(TileKey<Shape>{static_cast<Storage>(index)});
}

template <typename World, typename Tag>
[[nodiscard]] auto is_passable(const World& world, Coord3 coord) noexcept
    -> bool {
  const auto* value = world.template try_field<Tag>(coord);
  return value != nullptr && static_cast<bool>(*value);
}

template <typename World, typename Tag>
[[nodiscard]] auto is_passable_index(const World& world,
                                     std::uint64_t index) noexcept -> bool {
  using Shape = typename World::shape_type;
  using Storage = typename ShapeTraits<Shape>::TileKeyStorage;
  const auto key = TileKey<Shape>{static_cast<Storage>(index)};
  const auto& value = world.chunk(chunk_key<Shape>(key))
                          .template field<Tag>(local_tile_id<Shape>(key));
  return static_cast<bool>(value);
}

template <typename World, typename Tag>
[[nodiscard]] auto is_full_axis_barrier(const World& world, Coord3 blocked,
                                        Axis axis) noexcept -> bool {
  using Shape = typename World::shape_type;
  constexpr auto size = ShapeTraits<Shape>::size;

  if (axis == Axis::X) {
    for (std::int64_t z = 0; z < static_cast<std::int64_t>(size.z); ++z) {
      for (std::int64_t y = 0; y < static_cast<std::int64_t>(size.y); ++y) {
        const auto coord = Coord3{blocked.x, y, z};
        TESS_DIAG_EVENT(path_passability_check);
        if (is_passable_index<World, Tag>(world, tile_index<Shape>(coord))) {
          return false;
        }
      }
    }
    return true;
  }

  if (axis == Axis::Y) {
    for (std::int64_t z = 0; z < static_cast<std::int64_t>(size.z); ++z) {
      for (std::int64_t x = 0; x < static_cast<std::int64_t>(size.x); ++x) {
        const auto coord = Coord3{x, blocked.y, z};
        TESS_DIAG_EVENT(path_passability_check);
        if (is_passable_index<World, Tag>(world, tile_index<Shape>(coord))) {
          return false;
        }
      }
    }
    return true;
  }

  for (std::int64_t y = 0; y < static_cast<std::int64_t>(size.y); ++y) {
    for (std::int64_t x = 0; x < static_cast<std::int64_t>(size.x); ++x) {
      const auto coord = Coord3{x, y, blocked.z};
      TESS_DIAG_EVENT(path_passability_check);
      if (is_passable_index<World, Tag>(world, tile_index<Shape>(coord))) {
        return false;
      }
    }
  }
  return true;
}

template <typename Fn>
void for_each_axis_neighbor(Coord3 coord, Fn&& fn) {
  fn(Coord3{coord.x + 1, coord.y, coord.z});
  fn(Coord3{coord.x - 1, coord.y, coord.z});
  fn(Coord3{coord.x, coord.y + 1, coord.z});
  fn(Coord3{coord.x, coord.y - 1, coord.z});
  fn(Coord3{coord.x, coord.y, coord.z + 1});
  fn(Coord3{coord.x, coord.y, coord.z - 1});
}

template <typename Shape, typename Fn>
void for_each_indexed_axis_neighbor(Coord3 coord, std::uint64_t index,
                                    Fn&& fn) {
  using Traits = ShapeTraits<Shape>;
  constexpr auto size = Traits::size;
  constexpr auto chunk = Traits::chunk;
  constexpr auto local_bits = Traits::local_bits;
  constexpr auto chunk_index_stride =
      local_bits >= 64 ? std::uint64_t{0} : (std::uint64_t{1} << local_bits);
  constexpr auto chunk_y_stride = Traits::chunk_count_x * chunk_index_stride;

  const auto local_x = static_cast<std::uint64_t>(coord.x) & (chunk.x - 1);
  const auto local_y = static_cast<std::uint64_t>(coord.y) & (chunk.y - 1);

  if constexpr (!Traits::degenerate_x) {
    if (static_cast<std::uint64_t>(coord.x) + 1 < size.x) {
      const auto next_index = local_x + 1 < chunk.x
                                  ? index + 1
                                  : index + chunk_index_stride - local_x;
      fn(Coord3{coord.x + 1, coord.y, coord.z}, next_index);
    }
    if (coord.x > 0) {
      const auto next_index =
          local_x > 0 ? index - 1 : index - chunk_index_stride + (chunk.x - 1);
      fn(Coord3{coord.x - 1, coord.y, coord.z}, next_index);
    }
  }

  if constexpr (!Traits::degenerate_y) {
    if (static_cast<std::uint64_t>(coord.y) + 1 < size.y) {
      const auto next_index = local_y + 1 < chunk.y
                                  ? index + chunk.x
                                  : index + chunk_y_stride - local_y * chunk.x;
      fn(Coord3{coord.x, coord.y + 1, coord.z}, next_index);
    }
    if (coord.y > 0) {
      const auto next_index =
          local_y > 0 ? index - chunk.x
                      : index - chunk_y_stride + (chunk.y - 1) * chunk.x;
      fn(Coord3{coord.x, coord.y - 1, coord.z}, next_index);
    }
  }

  if constexpr (!Traits::degenerate_z) {
    constexpr auto chunk_z_stride =
        Traits::chunk_count_x * Traits::chunk_count_y * chunk_index_stride;
    const auto local_xy = chunk.x * chunk.y;
    const auto local_z = static_cast<std::uint64_t>(coord.z) & (chunk.z - 1);
    if (static_cast<std::uint64_t>(coord.z) + 1 < size.z) {
      const auto next_index = local_z + 1 < chunk.z
                                  ? index + local_xy
                                  : index + chunk_z_stride - local_z * local_xy;
      fn(Coord3{coord.x, coord.y, coord.z + 1}, next_index);
    }
    if (coord.z > 0) {
      const auto next_index =
          local_z > 0 ? index - local_xy
                      : index - chunk_z_stride + (chunk.z - 1) * local_xy;
      fn(Coord3{coord.x, coord.y, coord.z - 1}, next_index);
    }
  }
}

[[nodiscard]] constexpr bool open_node_less(
    PathScratch::OpenNode lhs, PathScratch::OpenNode rhs) noexcept {
  if (lhs.f != rhs.f) {
    return lhs.f > rhs.f;
  }
  if (lhs.g != rhs.g) {
    return lhs.g < rhs.g;
  }
  return lhs.index > rhs.index;
}

}  // namespace detail

template <typename World, typename Tag>
auto astar_path(const World& world, PathRequest request, PathScratch& scratch)
    -> PathResult {
  using Shape = typename World::shape_type;
  constexpr auto unseen = std::uint8_t{0};
  constexpr auto open = std::uint8_t{1};
  constexpr auto closed = std::uint8_t{2};
  constexpr auto no_parent = std::numeric_limits<std::uint64_t>::max();
  constexpr auto infinite_cost = std::numeric_limits<std::uint32_t>::max();

  TESS_DIAG_EVENT_VALUE(path_clear, scratch.touched_.size());
  scratch.clear();
  if (!contains<Shape>(request.start)) {
    return PathResult{PathStatus::InvalidStart, 0, 0, 0, scratch.path_};
  }
  TESS_DIAG_EVENT(path_start_passability_check);
  if (!detail::is_passable<World, Tag>(world, request.start)) {
    return PathResult{PathStatus::InvalidStart, 0, 0, 0, scratch.path_};
  }
  if (!contains<Shape>(request.goal)) {
    return PathResult{PathStatus::InvalidGoal, 0, 0, 0, scratch.path_};
  }
  TESS_DIAG_EVENT(path_goal_passability_check);
  if (!detail::is_passable<World, Tag>(world, request.goal)) {
    return PathResult{PathStatus::InvalidGoal, 0, 0, 0, scratch.path_};
  }

  auto direct_current = request.start;
  auto direct_blocked_by_barrier = false;
  auto direct_blocked_coord = request.start;
  auto direct_blocked_axis = detail::Axis::X;
  const auto step_direct_axis = [&](auto Coord3::* member, detail::Axis axis) {
    while (direct_current.*member != request.goal.*member) {
      direct_current.*member +=
          direct_current.*member < request.goal.*member ? 1 : -1;
      TESS_DIAG_EVENT(path_passability_check);
      if (!detail::is_passable<World, Tag>(world, direct_current)) {
        direct_blocked_coord = direct_current;
        direct_blocked_axis = axis;
        direct_blocked_by_barrier = detail::is_full_axis_barrier<World, Tag>(
            world, direct_current, axis);
        scratch.path_.clear();
        return false;
      }
      scratch.path_.push_back(direct_current);
      TESS_DIAG_EVENT(path_reconstruct_node);
    }
    return true;
  };
  const auto try_direct_order = [&](auto first_member, detail::Axis first_axis,
                                    auto second_member,
                                    detail::Axis second_axis, auto third_member,
                                    detail::Axis third_axis) {
    direct_current = request.start;
    scratch.path_.clear();
    scratch.path_.push_back(direct_current);
    TESS_DIAG_EVENT(path_reconstruct_node);
    return step_direct_axis(first_member, first_axis) &&
           step_direct_axis(second_member, second_axis) &&
           step_direct_axis(third_member, third_axis);
  };
  auto direct_path_found = false;
  if constexpr (ShapeTraits<Shape>::degenerate_z) {
    direct_path_found =
        try_direct_order(&Coord3::x, detail::Axis::X, &Coord3::y,
                         detail::Axis::Y, &Coord3::z, detail::Axis::Z) ||
        (!direct_blocked_by_barrier &&
         try_direct_order(&Coord3::y, detail::Axis::Y, &Coord3::x,
                          detail::Axis::X, &Coord3::z, detail::Axis::Z));
  } else if constexpr (ShapeTraits<Shape>::degenerate_y) {
    direct_path_found =
        try_direct_order(&Coord3::x, detail::Axis::X, &Coord3::z,
                         detail::Axis::Z, &Coord3::y, detail::Axis::Y) ||
        (!direct_blocked_by_barrier &&
         try_direct_order(&Coord3::z, detail::Axis::Z, &Coord3::x,
                          detail::Axis::X, &Coord3::y, detail::Axis::Y));
  } else if constexpr (ShapeTraits<Shape>::degenerate_x) {
    direct_path_found =
        try_direct_order(&Coord3::y, detail::Axis::Y, &Coord3::z,
                         detail::Axis::Z, &Coord3::x, detail::Axis::X) ||
        (!direct_blocked_by_barrier &&
         try_direct_order(&Coord3::z, detail::Axis::Z, &Coord3::y,
                          detail::Axis::Y, &Coord3::x, detail::Axis::X));
  } else {
    direct_path_found =
        try_direct_order(&Coord3::x, detail::Axis::X, &Coord3::y,
                         detail::Axis::Y, &Coord3::z, detail::Axis::Z) ||
        (!direct_blocked_by_barrier &&
         try_direct_order(&Coord3::y, detail::Axis::Y, &Coord3::x,
                          detail::Axis::X, &Coord3::z, detail::Axis::Z)) ||
        (!direct_blocked_by_barrier &&
         try_direct_order(&Coord3::x, detail::Axis::X, &Coord3::z,
                          detail::Axis::Z, &Coord3::y, detail::Axis::Y)) ||
        (!direct_blocked_by_barrier &&
         try_direct_order(&Coord3::z, detail::Axis::Z, &Coord3::x,
                          detail::Axis::X, &Coord3::y, detail::Axis::Y)) ||
        (!direct_blocked_by_barrier &&
         try_direct_order(&Coord3::y, detail::Axis::Y, &Coord3::z,
                          detail::Axis::Z, &Coord3::x, detail::Axis::X)) ||
        (!direct_blocked_by_barrier &&
         try_direct_order(&Coord3::z, detail::Axis::Z, &Coord3::y,
                          detail::Axis::Y, &Coord3::x, detail::Axis::X));
  }
  if (direct_path_found) {
    const auto cost = detail::manhattan(request.start, request.goal);
    return PathResult{PathStatus::Found, cost, scratch.path_.size(),
                      scratch.path_.size(), scratch.path_};
  }
  if (direct_blocked_by_barrier) {
    return PathResult{PathStatus::NoPath, 0, 0, 0, scratch.path_};
  }

  const auto coord_member = [](detail::Axis axis) -> std::int64_t Coord3::* {
    if (axis == detail::Axis::X) {
      return &Coord3::x;
    }
    if (axis == detail::Axis::Y) {
      return &Coord3::y;
    }
    return &Coord3::z;
  };
  const auto axis_extent = [](detail::Axis axis) -> std::uint64_t {
    if (axis == detail::Axis::X) {
      return ShapeTraits<Shape>::size.x;
    }
    if (axis == detail::Axis::Y) {
      return ShapeTraits<Shape>::size.y;
    }
    return ShapeTraits<Shape>::size.z;
  };
  const auto is_degenerate_axis = [](detail::Axis axis) {
    if (axis == detail::Axis::X) {
      return ShapeTraits<Shape>::degenerate_x;
    }
    if (axis == detail::Axis::Y) {
      return ShapeTraits<Shape>::degenerate_y;
    }
    return ShapeTraits<Shape>::degenerate_z;
  };
  const auto other_2d_axis = [](detail::Axis axis) {
    if constexpr (ShapeTraits<Shape>::degenerate_z) {
      return axis == detail::Axis::X ? detail::Axis::Y : detail::Axis::X;
    } else if constexpr (ShapeTraits<Shape>::degenerate_y) {
      return axis == detail::Axis::X ? detail::Axis::Z : detail::Axis::X;
    } else {
      return axis == detail::Axis::Y ? detail::Axis::Z : detail::Axis::Y;
    }
  };

  const auto append_segment_2d = [&](Coord3 from, Coord3 to, auto first_member,
                                     auto second_member) {
    const auto restore_size = scratch.path_.size();
    auto current = from;
    const auto append_axis = [&](auto Coord3::* member) {
      while (current.*member != to.*member) {
        current.*member += current.*member < to.*member ? 1 : -1;
        TESS_DIAG_EVENT(path_passability_check);
        if (!detail::is_passable<World, Tag>(world, current)) {
          scratch.path_.resize(restore_size);
          return false;
        }
        scratch.path_.push_back(current);
        TESS_DIAG_EVENT(path_reconstruct_node);
      }
      return true;
    };

    return append_axis(first_member) && append_axis(second_member);
  };
  auto plane_gap_cost = infinite_cost;
  const auto try_plane_gap_route_2d = [&]() {
    if constexpr (!(ShapeTraits<Shape>::degenerate_x ||
                    ShapeTraits<Shape>::degenerate_y ||
                    ShapeTraits<Shape>::degenerate_z)) {
      return false;
    }
    if (is_degenerate_axis(direct_blocked_axis)) {
      return false;
    }

    const auto scan_axis = other_2d_axis(direct_blocked_axis);
    const auto blocked_member = coord_member(direct_blocked_axis);
    const auto scan_member = coord_member(scan_axis);

    auto best_gap = Coord3{0, 0, 0};
    auto best_cost = infinite_cost;
    for (std::int64_t value = 0;
         value < static_cast<std::int64_t>(axis_extent(scan_axis)); ++value) {
      auto gap = direct_blocked_coord;
      gap.*scan_member = value;
      TESS_DIAG_EVENT(path_passability_check);
      if (!detail::is_passable_index<World, Tag>(
              world, detail::tile_index<Shape>(gap))) {
        continue;
      }
      const auto cost = detail::manhattan(request.start, gap) +
                        detail::manhattan(gap, request.goal);
      if (cost < best_cost) {
        best_cost = cost;
        best_gap = gap;
      }
    }

    if (best_cost == infinite_cost) {
      return false;
    }

    scratch.path_.clear();
    scratch.path_.push_back(request.start);
    TESS_DIAG_EVENT(path_reconstruct_node);
    const auto first_leg =
        append_segment_2d(request.start, best_gap, blocked_member,
                          scan_member) ||
        append_segment_2d(request.start, best_gap, scan_member, blocked_member);
    if (!first_leg) {
      scratch.path_.clear();
      return false;
    }
    const auto second_leg =
        append_segment_2d(best_gap, request.goal, blocked_member,
                          scan_member) ||
        append_segment_2d(best_gap, request.goal, scan_member, blocked_member);
    if (!second_leg) {
      scratch.path_.clear();
      return false;
    }
    plane_gap_cost = best_cost;
    return true;
  };
  if (try_plane_gap_route_2d()) {
    return PathResult{PathStatus::Found, plane_gap_cost, scratch.path_.size(),
                      scratch.path_.size(), scratch.path_};
  }

  const auto append_segment_3d = [&](Coord3 from, Coord3 to, auto first_member,
                                     auto second_member, auto third_member) {
    const auto restore_size = scratch.path_.size();
    auto current = from;
    const auto append_axis = [&](auto Coord3::* member) {
      while (current.*member != to.*member) {
        current.*member += current.*member < to.*member ? 1 : -1;
        TESS_DIAG_EVENT(path_passability_check);
        if (!detail::is_passable<World, Tag>(world, current)) {
          scratch.path_.resize(restore_size);
          return false;
        }
        scratch.path_.push_back(current);
        TESS_DIAG_EVENT(path_reconstruct_node);
      }
      return true;
    };

    return append_axis(first_member) && append_axis(second_member) &&
           append_axis(third_member);
  };
  const auto append_any_segment_3d = [&](Coord3 from, Coord3 to) {
    return append_segment_3d(from, to, &Coord3::x, &Coord3::y, &Coord3::z) ||
           append_segment_3d(from, to, &Coord3::x, &Coord3::z, &Coord3::y) ||
           append_segment_3d(from, to, &Coord3::y, &Coord3::x, &Coord3::z) ||
           append_segment_3d(from, to, &Coord3::y, &Coord3::z, &Coord3::x) ||
           append_segment_3d(from, to, &Coord3::z, &Coord3::x, &Coord3::y) ||
           append_segment_3d(from, to, &Coord3::z, &Coord3::y, &Coord3::x);
  };
  auto plane_gap_cost_3d = infinite_cost;
  const auto try_plane_gap_route_3d = [&]() {
    if constexpr (ShapeTraits<Shape>::degenerate_x ||
                  ShapeTraits<Shape>::degenerate_y ||
                  ShapeTraits<Shape>::degenerate_z) {
      return false;
    }

    auto first_scan_axis = detail::Axis::Y;
    auto second_scan_axis = detail::Axis::Z;
    if (direct_blocked_axis == detail::Axis::Y) {
      first_scan_axis = detail::Axis::X;
      second_scan_axis = detail::Axis::Z;
    } else if (direct_blocked_axis == detail::Axis::Z) {
      first_scan_axis = detail::Axis::X;
      second_scan_axis = detail::Axis::Y;
    }
    const auto first_scan_member = coord_member(first_scan_axis);
    const auto second_scan_member = coord_member(second_scan_axis);

    auto best_gap = Coord3{0, 0, 0};
    auto best_cost = infinite_cost;
    for (std::int64_t first_value = 0;
         first_value < static_cast<std::int64_t>(axis_extent(first_scan_axis));
         ++first_value) {
      for (std::int64_t second_value = 0;
           second_value <
           static_cast<std::int64_t>(axis_extent(second_scan_axis));
           ++second_value) {
        auto gap = direct_blocked_coord;
        gap.*first_scan_member = first_value;
        gap.*second_scan_member = second_value;
        TESS_DIAG_EVENT(path_passability_check);
        if (!detail::is_passable_index<World, Tag>(
                world, detail::tile_index<Shape>(gap))) {
          continue;
        }
        const auto cost = detail::manhattan(request.start, gap) +
                          detail::manhattan(gap, request.goal);
        if (cost < best_cost) {
          best_cost = cost;
          best_gap = gap;
        }
      }
    }

    if (best_cost == infinite_cost) {
      return false;
    }

    scratch.path_.clear();
    scratch.path_.push_back(request.start);
    TESS_DIAG_EVENT(path_reconstruct_node);
    if (!append_any_segment_3d(request.start, best_gap)) {
      scratch.path_.clear();
      return false;
    }
    if (!append_any_segment_3d(best_gap, request.goal)) {
      scratch.path_.clear();
      return false;
    }
    plane_gap_cost_3d = best_cost;
    return true;
  };
  if (try_plane_gap_route_3d()) {
    return PathResult{PathStatus::Found, plane_gap_cost_3d,
                      scratch.path_.size(), scratch.path_.size(),
                      scratch.path_};
  }

  auto forced_plane_gap_cost = infinite_cost;
  auto forced_plane_gap_no_path = false;
  const auto try_forced_plane_gaps_2d = [&](detail::Axis progress_axis) {
    if constexpr (!(ShapeTraits<Shape>::degenerate_x ||
                    ShapeTraits<Shape>::degenerate_y ||
                    ShapeTraits<Shape>::degenerate_z)) {
      return false;
    }
    if (is_degenerate_axis(progress_axis)) {
      return false;
    }

    const auto progress_member = coord_member(progress_axis);
    const auto gap_axis = other_2d_axis(progress_axis);
    const auto gap_member = coord_member(gap_axis);
    if (request.start.*progress_member == request.goal.*progress_member) {
      return false;
    }

    const auto progress_step =
        request.start.*progress_member < request.goal.*progress_member ? 1 : -1;
    auto current = request.start;
    auto found_forced_gap = false;

    scratch.path_.clear();
    scratch.path_.push_back(current);
    TESS_DIAG_EVENT(path_reconstruct_node);

    const auto append_checked = [&](Coord3 next) {
      TESS_DIAG_EVENT(path_passability_check);
      if (!detail::is_passable<World, Tag>(world, next)) {
        scratch.path_.clear();
        return false;
      }
      scratch.path_.push_back(next);
      TESS_DIAG_EVENT(path_reconstruct_node);
      current = next;
      return true;
    };
    const auto append_progress = [&](std::int64_t target) {
      while (current.*progress_member != target) {
        auto next = current;
        next.*progress_member += current.*progress_member < target ? 1 : -1;
        if (!append_checked(next)) {
          return false;
        }
      }
      return true;
    };
    const auto append_gap = [&](std::int64_t target) {
      while (current.*gap_member != target) {
        auto next = current;
        next.*gap_member += current.*gap_member < target ? 1 : -1;
        if (!append_checked(next)) {
          return false;
        }
      }
      return true;
    };

    while (current.*progress_member != request.goal.*progress_member) {
      auto next = current;
      next.*progress_member += progress_step;
      TESS_DIAG_EVENT(path_passability_check);
      if (detail::is_passable<World, Tag>(world, next)) {
        scratch.path_.push_back(next);
        TESS_DIAG_EVENT(path_reconstruct_node);
        current = next;
        continue;
      }

      auto passable_count = std::uint64_t{0};
      auto gap_value = std::int64_t{0};
      auto blocked_count = std::uint64_t{0};
      for (std::int64_t value = 0;
           value < static_cast<std::int64_t>(axis_extent(gap_axis)); ++value) {
        auto coord = next;
        coord.*gap_member = value;
        TESS_DIAG_EVENT(path_passability_check);
        if (detail::is_passable_index<World, Tag>(
                world, detail::tile_index<Shape>(coord))) {
          ++passable_count;
          gap_value = value;
        } else {
          ++blocked_count;
        }
      }

      if (passable_count == 0) {
        forced_plane_gap_no_path = true;
        scratch.path_.clear();
        return false;
      }
      if (blocked_count == 0) {
        continue;
      }
      if (passable_count > 1) {
        scratch.path_.clear();
        return false;
      }

      found_forced_gap = true;
      auto gap = next;
      gap.*gap_member = gap_value;
      if (!append_gap(gap.*gap_member) ||
          !append_progress(gap.*progress_member)) {
        return false;
      }
    }

    if (!found_forced_gap) {
      scratch.path_.clear();
      return false;
    }
    if (!append_progress(request.goal.*progress_member) ||
        !append_gap(request.goal.*gap_member)) {
      return false;
    }

    forced_plane_gap_cost =
        static_cast<std::uint32_t>(scratch.path_.size() - 1);
    return true;
  };
  if (try_forced_plane_gaps_2d(detail::Axis::X) ||
      try_forced_plane_gaps_2d(detail::Axis::Y) ||
      try_forced_plane_gaps_2d(detail::Axis::Z)) {
    return PathResult{PathStatus::Found, forced_plane_gap_cost,
                      scratch.path_.size(), scratch.path_.size(),
                      scratch.path_};
  }
  if (forced_plane_gap_no_path) {
    return PathResult{PathStatus::NoPath, 0, 0, 0, scratch.path_};
  }

  const auto try_axis_aligned_detour = [&](auto Coord3::* primary,
                                           auto Coord3::* detour,
                                           std::int64_t detour_step) {
    direct_current = request.start;
    direct_current.*detour += detour_step;
    if (!contains<Shape>(direct_current)) {
      return false;
    }
    TESS_DIAG_EVENT(path_passability_check);
    if (!detail::is_passable<World, Tag>(world, direct_current)) {
      return false;
    }

    scratch.path_.clear();
    scratch.path_.push_back(request.start);
    TESS_DIAG_EVENT(path_reconstruct_node);
    scratch.path_.push_back(direct_current);
    TESS_DIAG_EVENT(path_reconstruct_node);

    while (direct_current.*primary != request.goal.*primary) {
      direct_current.*primary +=
          direct_current.*primary < request.goal.*primary ? 1 : -1;
      TESS_DIAG_EVENT(path_passability_check);
      if (!detail::is_passable<World, Tag>(world, direct_current)) {
        scratch.path_.clear();
        return false;
      }
      scratch.path_.push_back(direct_current);
      TESS_DIAG_EVENT(path_reconstruct_node);
    }

    direct_current.*detour -= detour_step;
    TESS_DIAG_EVENT(path_passability_check);
    if (!detail::is_passable<World, Tag>(world, direct_current)) {
      scratch.path_.clear();
      return false;
    }
    scratch.path_.push_back(direct_current);
    TESS_DIAG_EVENT(path_reconstruct_node);
    return true;
  };
  const auto detour_cost = detail::manhattan(request.start, request.goal) + 2;
  if (request.start.y == request.goal.y && request.start.z == request.goal.z &&
      (try_axis_aligned_detour(&Coord3::x, &Coord3::y, 1) ||
       try_axis_aligned_detour(&Coord3::x, &Coord3::y, -1) ||
       try_axis_aligned_detour(&Coord3::x, &Coord3::z, 1) ||
       try_axis_aligned_detour(&Coord3::x, &Coord3::z, -1))) {
    return PathResult{PathStatus::Found, detour_cost, scratch.path_.size(),
                      scratch.path_.size(), scratch.path_};
  }
  if (request.start.x == request.goal.x && request.start.z == request.goal.z &&
      (try_axis_aligned_detour(&Coord3::y, &Coord3::x, 1) ||
       try_axis_aligned_detour(&Coord3::y, &Coord3::x, -1) ||
       try_axis_aligned_detour(&Coord3::y, &Coord3::z, 1) ||
       try_axis_aligned_detour(&Coord3::y, &Coord3::z, -1))) {
    return PathResult{PathStatus::Found, detour_cost, scratch.path_.size(),
                      scratch.path_.size(), scratch.path_};
  }
  if (request.start.x == request.goal.x && request.start.y == request.goal.y &&
      (try_axis_aligned_detour(&Coord3::z, &Coord3::x, 1) ||
       try_axis_aligned_detour(&Coord3::z, &Coord3::x, -1) ||
       try_axis_aligned_detour(&Coord3::z, &Coord3::y, 1) ||
       try_axis_aligned_detour(&Coord3::z, &Coord3::y, -1))) {
    return PathResult{PathStatus::Found, detour_cost, scratch.path_.size(),
                      scratch.path_.size(), scratch.path_};
  }

  const auto node_count = detail::tile_count<World>();
  if (scratch.state_.size() != node_count) {
    TESS_DIAG_EVENT(path_initialize);
    scratch.generation_.assign(node_count, 0);
    scratch.state_.assign(node_count, unseen);
    scratch.g_.assign(node_count, infinite_cost);
    scratch.parent_.assign(node_count, no_parent);
  }

  const auto start = detail::tile_index<Shape>(request.start);
  const auto goal = detail::tile_index<Shape>(request.goal);
  scratch.g_[static_cast<std::size_t>(start)] = 0;
  scratch.state_[static_cast<std::size_t>(start)] = open;
  scratch.touch_node(start);
  TESS_DIAG_EVENT(path_touch_node);
  TESS_DIAG_EVENT(path_heuristic);
  scratch.open_.push_back(PathScratch::OpenNode{
      start,
      0,
      detail::manhattan(request.start, request.goal),
  });
  TESS_DIAG_EVENT(path_heap_push);
  std::push_heap(scratch.open_.begin(), scratch.open_.end(),
                 detail::open_node_less);

  std::size_t expanded_nodes = 0;
  while (!scratch.open_.empty()) {
    TESS_DIAG_EVENT(path_heap_pop);
    std::pop_heap(scratch.open_.begin(), scratch.open_.end(),
                  detail::open_node_less);
    const auto current = scratch.open_.back();
    scratch.open_.pop_back();

    const auto current_offset = static_cast<std::size_t>(current.index);
    const auto current_state = scratch.state_at(current_offset, unseen);
    if (current_state == closed ||
        current.g != scratch.g_at(current_offset, infinite_cost)) {
      TESS_DIAG_EVENT_VALUE(path_skip_pop, current_state == closed);
      continue;
    }
    scratch.state_[current_offset] = closed;
    ++expanded_nodes;

    if (current.index == goal) {
      auto step = current.index;
      while (true) {
        scratch.path_.push_back(detail::tile_coord<Shape>(step));
        TESS_DIAG_EVENT(path_reconstruct_node);
        if (step == start) {
          break;
        }
        step = scratch.parent_[static_cast<std::size_t>(step)];
      }
      std::reverse(scratch.path_.begin(), scratch.path_.end());
      return PathResult{PathStatus::Found, current.g, expanded_nodes,
                        scratch.touched_.size(), scratch.path_};
    }

    const auto current_coord = detail::tile_coord<Shape>(current.index);
    detail::for_each_indexed_axis_neighbor<Shape>(
        current_coord, current.index,
        [&](Coord3 neighbor, std::uint64_t neighbor_index) {
          TESS_DIAG_EVENT(path_neighbor_candidate);
          const auto neighbor_offset = static_cast<std::size_t>(neighbor_index);
          const auto neighbor_state = scratch.state_at(neighbor_offset, unseen);
          if (neighbor_state == closed) {
            TESS_DIAG_EVENT(path_neighbor_closed);
            return;
          }
          const auto tentative_g = current.g + 1;
          TESS_DIAG_EVENT(path_relax_attempt);
          if (neighbor_state == unseen) {
            TESS_DIAG_EVENT(path_passability_check);
            if (!detail::is_passable_index<World, Tag>(world, neighbor_index)) {
              TESS_DIAG_EVENT(path_neighbor_blocked);
              return;
            }
          }
          if (tentative_g < scratch.g_at(neighbor_offset, infinite_cost)) {
            TESS_DIAG_EVENT(path_relax_success);
            if (neighbor_state == unseen) {
              scratch.touch_node(neighbor_index);
              TESS_DIAG_EVENT(path_touch_node);
            }
            scratch.g_[neighbor_offset] = tentative_g;
            scratch.parent_[neighbor_offset] = current.index;
            scratch.state_[neighbor_offset] = open;
            TESS_DIAG_EVENT(path_heuristic);
            const auto updated_node = PathScratch::OpenNode{
                neighbor_index,
                tentative_g,
                tentative_g + detail::manhattan(neighbor, request.goal),
            };
            scratch.open_.push_back(updated_node);
            TESS_DIAG_EVENT(path_heap_push);
            std::push_heap(scratch.open_.begin(), scratch.open_.end(),
                           detail::open_node_less);
          }
        });
  }

  return PathResult{PathStatus::NoPath, 0, expanded_nodes,
                    scratch.touched_.size(), scratch.path_};
}

}  // namespace tess
