#pragma once

// Shared pathfinding test fixtures and reference oracles.
//
// The serpentine mazes built here are the canonical fixtures that defeat
// every pre-A* fast path in tess::astar_path (direct axis orders, the
// full-axis-barrier rejection, 2D/3D plane-gap stitching, forced plane
// gaps, and axis-aligned one-tile detours), so a Found result can only be
// produced by the real heap search loop. The construction recipe: two
// parallel walls, each with TWO adjacent gaps (so no full-axis barrier and
// forced-plane-gaps bails on passable_count > 1), gaps at OPPOSITE ends
// (so every L-shaped stitch through one wall's gap is blocked by the other
// wall), and start/goal displaced on both non-degenerate axes (so the
// axis-aligned detour fast paths are skipped).
//
// The BFS/Dijkstra oracles compute exact optimal costs with their own
// indexing and containers, independent of the library's search internals.

#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstdint>
#include <limits>
#include <queue>
#include <span>
#include <utility>
#include <vector>

namespace tess_test {

struct SerpPassableTag {};
struct SerpCostTag {};

using SerpSchema = tess::FieldSchema<tess::Field<SerpPassableTag, bool>,
                                     tess::Field<SerpCostTag, std::uint32_t>>;
using SerpTopDown2D =
    tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{4, 4, 1}>;
using SerpVertical2D =
    tess::Shape<tess::Extent3{1, 8, 8}, tess::Extent3{1, 4, 4}>;
// Two separated two-gap walls need wall/corridor/wall/corridor room along
// one axis, which a 4-tile extent cannot host; this is the smallest
// multi-chunk 3D shape that can.
using SerpChunked3D =
    tess::Shape<tess::Extent3{8, 8, 4}, tess::Extent3{4, 4, 2}>;

inline constexpr auto kUnreachable = std::numeric_limits<std::uint32_t>::max();

struct SerpentineEndpoints {
  tess::Coord3 start{};
  tess::Coord3 goal{};
};

// Wall positions and gap coordinates along the wall for the two parallel
// walls. `gaps_a` sit at the high end and `gaps_b` at the low end of the
// scan axis by default, which is what defeats the plane-gap stitchers.
struct SerpentineSpec {
  std::int64_t wall_a = 2;
  std::int64_t wall_b = 5;
  std::int64_t gap_a_lo = 6;
  std::int64_t gap_a_hi = 7;
  std::int64_t gap_b_lo = 0;
  std::int64_t gap_b_hi = 1;
};

template <typename World>
void fill_passable(World& world, bool value) {
  for (auto& page : world.chunks()) {
    auto passable = page.template field_span<SerpPassableTag>();
    for (auto& tile : passable) {
      tile = value;
    }
  }
}

template <typename World>
void fill_cost(World& world, std::uint32_t value) {
  for (auto& page : world.chunks()) {
    auto costs = page.template field_span<SerpCostTag>();
    for (auto& tile : costs) {
      tile = value;
    }
  }
}

// 8x8 top-down maze: vertical walls at x = wall_a (gaps at high y) and
// x = wall_b (gaps at low y).
template <typename World>
auto build_serpentine_topdown(World& world, SerpentineSpec spec = {})
    -> SerpentineEndpoints {
  fill_passable(world, true);
  for (std::int64_t y = 0; y < 8; ++y) {
    if (y != spec.gap_a_lo && y != spec.gap_a_hi) {
      world.template field<SerpPassableTag>(tess::Coord3{spec.wall_a, y, 0}) =
          false;
    }
    if (y != spec.gap_b_lo && y != spec.gap_b_hi) {
      world.template field<SerpPassableTag>(tess::Coord3{spec.wall_b, y, 0}) =
          false;
    }
  }
  return SerpentineEndpoints{tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}};
}

// 8x8 vertical maze in the (y, z) plane: walls at y = wall_a (gaps at high
// z) and y = wall_b (gaps at low z).
template <typename World>
auto build_serpentine_vertical(World& world, SerpentineSpec spec = {})
    -> SerpentineEndpoints {
  fill_passable(world, true);
  for (std::int64_t z = 0; z < 8; ++z) {
    if (z != spec.gap_a_lo && z != spec.gap_a_hi) {
      world.template field<SerpPassableTag>(tess::Coord3{0, spec.wall_a, z}) =
          false;
    }
    if (z != spec.gap_b_lo && z != spec.gap_b_hi) {
      world.template field<SerpPassableTag>(tess::Coord3{0, spec.wall_b, z}) =
          false;
    }
  }
  return SerpentineEndpoints{tess::Coord3{0, 0, 0}, tess::Coord3{0, 7, 7}};
}

// 8x8x4 maze: full planes at x = wall_a (two adjacent gaps at high (y, z))
// and x = wall_b (two adjacent gaps at low (y, z)).
template <typename World>
auto build_serpentine_chunked3d(World& world, SerpentineSpec spec = {})
    -> SerpentineEndpoints {
  fill_passable(world, true);
  for (std::int64_t z = 0; z < 4; ++z) {
    for (std::int64_t y = 0; y < 8; ++y) {
      const auto gap_a = z == 3 && (y == spec.gap_a_lo || y == spec.gap_a_hi);
      if (!gap_a) {
        world.template field<SerpPassableTag>(tess::Coord3{spec.wall_a, y, z}) =
            false;
      }
      const auto gap_b = z == 0 && (y == spec.gap_b_lo || y == spec.gap_b_hi);
      if (!gap_b) {
        world.template field<SerpPassableTag>(tess::Coord3{spec.wall_b, y, z}) =
            false;
      }
    }
  }
  return SerpentineEndpoints{tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 3}};
}

namespace detail {

template <typename Shape>
[[nodiscard]] auto oracle_index(tess::Coord3 coord) noexcept -> std::size_t {
  constexpr auto size = tess::ShapeTraits<Shape>::size;
  return static_cast<std::size_t>(
      (static_cast<std::uint64_t>(coord.z) * size.y +
       static_cast<std::uint64_t>(coord.y)) *
          size.x +
      static_cast<std::uint64_t>(coord.x));
}

template <typename Shape, typename Fn>
void oracle_for_each_neighbor(tess::Coord3 coord, Fn&& fn) {
  constexpr auto size = tess::ShapeTraits<Shape>::size;
  const auto consider = [&](tess::Coord3 next) {
    if (next.x < 0 || next.y < 0 || next.z < 0 ||
        next.x >= static_cast<std::int64_t>(size.x) ||
        next.y >= static_cast<std::int64_t>(size.y) ||
        next.z >= static_cast<std::int64_t>(size.z)) {
      return;
    }
    fn(next);
  };
  consider(tess::Coord3{coord.x + 1, coord.y, coord.z});
  consider(tess::Coord3{coord.x - 1, coord.y, coord.z});
  consider(tess::Coord3{coord.x, coord.y + 1, coord.z});
  consider(tess::Coord3{coord.x, coord.y - 1, coord.z});
  consider(tess::Coord3{coord.x, coord.y, coord.z + 1});
  consider(tess::Coord3{coord.x, coord.y, coord.z - 1});
}

template <typename World>
[[nodiscard]] auto oracle_passable(const World& world,
                                   tess::Coord3 coord) noexcept -> bool {
  const auto* value = world.template try_field<SerpPassableTag>(coord);
  return value != nullptr && *value;
}

}  // namespace detail

// Reference unit-cost BFS. Returns the exact optimal step count from
// `start` to `goal`, or kUnreachable.
template <typename World>
[[nodiscard]] auto bfs_unit_cost(const World& world, tess::Coord3 start,
                                 tess::Coord3 goal) -> std::uint32_t {
  using Shape = typename World::shape_type;
  constexpr auto size = tess::ShapeTraits<Shape>::size;
  if (!detail::oracle_passable(world, start) ||
      !detail::oracle_passable(world, goal)) {
    return kUnreachable;
  }

  std::vector<std::uint32_t> distance(
      static_cast<std::size_t>(size.x * size.y * size.z), kUnreachable);
  std::queue<tess::Coord3> frontier;
  distance[detail::oracle_index<Shape>(start)] = 0;
  frontier.push(start);
  while (!frontier.empty()) {
    const auto current = frontier.front();
    frontier.pop();
    const auto current_distance =
        distance[detail::oracle_index<Shape>(current)];
    if (current == goal) {
      return current_distance;
    }
    detail::oracle_for_each_neighbor<Shape>(current, [&](tess::Coord3 next) {
      auto& slot = distance[detail::oracle_index<Shape>(next)];
      if (slot != kUnreachable || !detail::oracle_passable(world, next)) {
        return;
      }
      slot = current_distance + 1;
      frontier.push(next);
    });
  }
  return kUnreachable;
}

// Reference weighted Dijkstra. Edge cost is the entry cost of the
// destination tile (matching tess::weighted_astar_path semantics); tiles
// with entry cost 0 are treated as blocked. Returns the exact optimal cost
// or kUnreachable.
template <typename World>
[[nodiscard]] auto dijkstra_weighted_cost(const World& world,
                                          tess::Coord3 start, tess::Coord3 goal)
    -> std::uint32_t {
  using Shape = typename World::shape_type;
  constexpr auto size = tess::ShapeTraits<Shape>::size;
  const auto entry_cost = [&](tess::Coord3 coord) -> std::uint32_t {
    const auto* value = world.template try_field<SerpCostTag>(coord);
    return value == nullptr ? 0u : *value;
  };
  if (!detail::oracle_passable(world, start) ||
      !detail::oracle_passable(world, goal) || entry_cost(start) == 0 ||
      entry_cost(goal) == 0) {
    return kUnreachable;
  }

  using QueueEntry = std::pair<std::uint64_t, tess::Coord3>;
  const auto entry_greater = [](const QueueEntry& lhs, const QueueEntry& rhs) {
    return lhs.first > rhs.first;
  };
  std::vector<std::uint64_t> distance(
      static_cast<std::size_t>(size.x * size.y * size.z),
      std::numeric_limits<std::uint64_t>::max());
  std::priority_queue<QueueEntry, std::vector<QueueEntry>,
                      decltype(entry_greater)>
      frontier{entry_greater};
  distance[detail::oracle_index<Shape>(start)] = 0;
  frontier.push(QueueEntry{0, start});
  while (!frontier.empty()) {
    const auto [current_distance, current] = frontier.top();
    frontier.pop();
    if (current_distance > distance[detail::oracle_index<Shape>(current)]) {
      continue;
    }
    if (current == goal) {
      return static_cast<std::uint32_t>(current_distance);
    }
    detail::oracle_for_each_neighbor<Shape>(current, [&](tess::Coord3 next) {
      if (!detail::oracle_passable(world, next)) {
        return;
      }
      const auto cost = entry_cost(next);
      if (cost == 0) {
        return;
      }
      const auto next_distance = current_distance + cost;
      auto& slot = distance[detail::oracle_index<Shape>(next)];
      if (next_distance < slot) {
        slot = next_distance;
        frontier.push(QueueEntry{next_distance, next});
      }
    });
  }
  return kUnreachable;
}

// Walks a returned path: non-empty, correct endpoints, unit-adjacent
// consecutive steps, and every tile passable.
template <typename World>
[[nodiscard]] auto valid_path_walk(const World& world,
                                   std::span<const tess::Coord3> path,
                                   tess::Coord3 start, tess::Coord3 goal)
    -> ::testing::AssertionResult {
  if (path.empty()) {
    return ::testing::AssertionFailure() << "path is empty";
  }
  if (path.front() != start) {
    return ::testing::AssertionFailure() << "path does not begin at start";
  }
  if (path.back() != goal) {
    return ::testing::AssertionFailure() << "path does not end at goal";
  }
  for (std::size_t i = 0; i < path.size(); ++i) {
    if (!detail::oracle_passable(world, path[i])) {
      return ::testing::AssertionFailure()
             << "path visits impassable tile at index " << i;
    }
    if (i == 0) {
      continue;
    }
    const auto step = tess::manhattan_distance(path[i - 1], path[i]);
    if (step != 1) {
      return ::testing::AssertionFailure()
             << "path step " << i << " is not unit-adjacent (distance " << step
             << ")";
    }
  }
  return ::testing::AssertionSuccess();
}

}  // namespace tess_test
