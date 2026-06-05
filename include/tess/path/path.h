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
  constexpr auto chunk_z_stride =
      Traits::chunk_count_x * Traits::chunk_count_y * chunk_index_stride;

  const auto local_x = static_cast<std::uint64_t>(coord.x) & (chunk.x - 1);
  const auto local_y = static_cast<std::uint64_t>(coord.y) & (chunk.y - 1);
  const auto local_z = static_cast<std::uint64_t>(coord.z) & (chunk.z - 1);

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
    const auto local_xy = chunk.x * chunk.y;
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
  const auto step_direct_axis = [&](auto Coord3::* member, detail::Axis axis) {
    while (direct_current.*member != request.goal.*member) {
      direct_current.*member +=
          direct_current.*member < request.goal.*member ? 1 : -1;
      TESS_DIAG_EVENT(path_passability_check);
      if (!detail::is_passable<World, Tag>(world, direct_current)) {
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
