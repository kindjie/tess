#pragma once

#include <tess/core/shape.h>

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
    state_.reserve(node_count);
    g_.reserve(node_count);
    parent_.reserve(node_count);
    touched_.reserve(node_count);
    path_.reserve(node_count);
  }

  void clear() noexcept {
    for (const auto index : touched_) {
      const auto offset = static_cast<std::size_t>(index);
      state_[offset] = 0;
      g_[offset] = std::numeric_limits<std::uint32_t>::max();
      parent_[offset] = std::numeric_limits<std::uint64_t>::max();
    }
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

  std::vector<OpenNode> open_;
  std::vector<std::uint8_t> state_;
  std::vector<std::uint32_t> g_;
  std::vector<std::uint64_t> parent_;
  std::vector<std::uint64_t> touched_;
  std::vector<Coord3> path_;
};

namespace detail {

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

template <typename Fn>
void for_each_axis_neighbor(Coord3 coord, Fn&& fn) {
  fn(Coord3{coord.x + 1, coord.y, coord.z});
  fn(Coord3{coord.x - 1, coord.y, coord.z});
  fn(Coord3{coord.x, coord.y + 1, coord.z});
  fn(Coord3{coord.x, coord.y - 1, coord.z});
  fn(Coord3{coord.x, coord.y, coord.z + 1});
  fn(Coord3{coord.x, coord.y, coord.z - 1});
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

  scratch.clear();
  if (!contains<Shape>(request.start) ||
      !detail::is_passable<World, Tag>(world, request.start)) {
    return PathResult{PathStatus::InvalidStart, 0, 0, 0, scratch.path_};
  }
  if (!contains<Shape>(request.goal) ||
      !detail::is_passable<World, Tag>(world, request.goal)) {
    return PathResult{PathStatus::InvalidGoal, 0, 0, 0, scratch.path_};
  }

  const auto node_count = detail::tile_count<World>();
  if (scratch.state_.size() != node_count) {
    scratch.state_.assign(node_count, unseen);
    scratch.g_.assign(node_count, infinite_cost);
    scratch.parent_.assign(node_count, no_parent);
  }

  const auto start = detail::tile_index<Shape>(request.start);
  const auto goal = detail::tile_index<Shape>(request.goal);
  scratch.g_[static_cast<std::size_t>(start)] = 0;
  scratch.state_[static_cast<std::size_t>(start)] = open;
  scratch.touched_.push_back(start);
  scratch.open_.push_back(PathScratch::OpenNode{
      start,
      0,
      detail::manhattan(request.start, request.goal),
  });
  std::push_heap(scratch.open_.begin(), scratch.open_.end(),
                 detail::open_node_less);

  std::size_t expanded_nodes = 0;
  while (!scratch.open_.empty()) {
    std::pop_heap(scratch.open_.begin(), scratch.open_.end(),
                  detail::open_node_less);
    const auto current = scratch.open_.back();
    scratch.open_.pop_back();

    const auto current_offset = static_cast<std::size_t>(current.index);
    if (scratch.state_[current_offset] == closed ||
        current.g != scratch.g_[current_offset]) {
      continue;
    }
    scratch.state_[current_offset] = closed;
    ++expanded_nodes;

    if (current.index == goal) {
      auto step = current.index;
      while (true) {
        scratch.path_.push_back(detail::tile_coord<Shape>(step));
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
    detail::for_each_axis_neighbor(current_coord, [&](Coord3 neighbor) {
      if (!contains<Shape>(neighbor) ||
          !detail::is_passable<World, Tag>(world, neighbor)) {
        return;
      }

      const auto neighbor_index = detail::tile_index<Shape>(neighbor);
      const auto neighbor_offset = static_cast<std::size_t>(neighbor_index);
      if (scratch.state_[neighbor_offset] == closed) {
        return;
      }
      const auto tentative_g = current.g + 1;
      if (tentative_g < scratch.g_[neighbor_offset]) {
        if (scratch.state_[neighbor_offset] == unseen) {
          scratch.touched_.push_back(neighbor_index);
        }
        scratch.g_[neighbor_offset] = tentative_g;
        scratch.parent_[neighbor_offset] = current.index;
        scratch.state_[neighbor_offset] = open;
        scratch.open_.push_back(PathScratch::OpenNode{
            neighbor_index,
            tentative_g,
            tentative_g + detail::manhattan(neighbor, request.goal),
        });
        std::push_heap(scratch.open_.begin(), scratch.open_.end(),
                       detail::open_node_less);
      }
    });
  }

  return PathResult{PathStatus::NoPath, 0, expanded_nodes,
                    scratch.touched_.size(), scratch.path_};
}

}  // namespace tess
