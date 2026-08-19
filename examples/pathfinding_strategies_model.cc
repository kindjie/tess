#include "pathfinding_strategies_model.h"

#include <tess/pathfinding.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace tess::examples::pathfinding_strategies {
namespace {

// [strategy-world]
struct PassableTag {};
struct CostTag {};

using Shape = tess::Shape<tess::Extent3{16, 16, 1}, tess::Extent3{8, 8, 1}>;
using Schema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>,
                                 tess::Field<CostTag, std::uint32_t>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;
// [strategy-world]

// [strategy-requests]
constexpr auto kGoal = tess::Coord3{15, 15, 0};

constexpr auto kRequests = std::array{
    tess::PathRequest{tess::Coord3{0, 0, 0}, kGoal},
    tess::PathRequest{tess::Coord3{0, 1, 0}, kGoal},
    tess::PathRequest{tess::Coord3{0, 2, 0}, kGoal},
};
// [strategy-requests]

[[nodiscard]] auto bounded_u32(std::size_t value) -> std::uint32_t {
  return static_cast<std::uint32_t>(std::min(
      value,
      static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())));
}

[[nodiscard]] auto browser_status(tess::PathStatus status)
    -> BrowserPathStatus {
  switch (status) {
    case tess::PathStatus::Found:
      return BrowserPathStatus::Found;
    case tess::PathStatus::InvalidStart:
      return BrowserPathStatus::InvalidStart;
    case tess::PathStatus::InvalidGoal:
      return BrowserPathStatus::InvalidGoal;
    case tess::PathStatus::NoPath:
      return BrowserPathStatus::NoPath;
    case tess::PathStatus::CostOverflow:
      return BrowserPathStatus::CostOverflow;
    case tess::PathStatus::Indeterminate:
      return BrowserPathStatus::Indeterminate;
  }
  return BrowserPathStatus::NoPath;
}

[[nodiscard]] auto copy_result(const tess::PathResult& result)
    -> RequestSnapshot {
  auto snapshot = RequestSnapshot{
      .status = browser_status(result.status),
      .cost = result.cost,
      .expanded_nodes = bounded_u32(result.expanded_nodes),
      .path_size = bounded_u32(result.path.size()),
  };
  if (result.path.size() > snapshot.path.size()) {
    snapshot.status = BrowserPathStatus::NoPath;
    snapshot.path_size = 0;
    return snapshot;
  }
  for (std::size_t index = 0; index < result.path.size(); ++index) {
    snapshot.path[index] = {
        static_cast<std::int32_t>(result.path[index].x),
        static_cast<std::int32_t>(result.path[index].y),
    };
  }
  return snapshot;
}

// [strategy-obstacles]
[[nodiscard]] constexpr auto demo_cell_passable(std::int64_t x, std::int64_t y)
    -> bool {
  if (x == 4) {
    return y == 4;
  }
  if (x == 8) {
    return y == 11;
  }
  if (x == 12) {
    return y == 6;
  }
  return true;
}
// [strategy-obstacles]

void configure_world(World& world,
                     std::array<std::uint8_t, path_capacity>& passable) {
  for (std::int64_t y = 0; y < static_cast<std::int64_t>(Shape::size.y); ++y) {
    for (std::int64_t x = 0; x < static_cast<std::int64_t>(Shape::size.x);
         ++x) {
      const auto is_passable = demo_cell_passable(x, y);
      world.field<PassableTag>(tess::Coord3{x, y, 0}) = is_passable ? 1U : 0U;
      world.field<CostTag>(tess::Coord3{x, y, 0}) = 1;
      const auto index = static_cast<std::size_t>(y * width + x);
      passable[index] = is_passable ? 1U : 0U;
    }
  }
}

[[nodiscard]] auto run_independent_astar(const World& world)
    -> StrategySnapshot {
  auto snapshot = StrategySnapshot{
      .kind = StrategyKind::IndependentAstar,
      .request_count = static_cast<std::uint32_t>(kRequests.size()),
  };
  // [strategy-astar]
  tess::PathScratch scratch;
  for (std::size_t index = 0; index < kRequests.size(); ++index) {
    const auto result =
        tess::astar_path<World, PassableTag>(world, kRequests[index], scratch);
    snapshot.requests[index] = copy_result(result);
  }
  // [strategy-astar]
  return snapshot;
}

[[nodiscard]] auto run_cached_repeats(const World& world) -> StrategySnapshot {
  auto snapshot = StrategySnapshot{
      .kind = StrategyKind::ExactRouteCache,
      .request_count = 2,
  };
  // [strategy-cache]
  tess::PathScratch scratch;
  tess::RouteCacheScratch cache;
  const auto first = tess::cached_astar_path<World, PassableTag>(
      world, kRequests.front(), scratch, cache);
  snapshot.requests[0] = copy_result(first);
  const auto repeated = tess::cached_astar_path<World, PassableTag>(
      world, kRequests.front(), scratch, cache);
  snapshot.requests[1] = copy_result(repeated);
  // [strategy-cache]
  const auto stats = cache.stats();
  snapshot.cache_hits = bounded_u32(stats.hits);
  snapshot.cache_misses = bounded_u32(stats.misses);
  return snapshot;
}

[[nodiscard]] auto run_weighted_batch(const World& world) -> StrategySnapshot {
  auto snapshot = StrategySnapshot{
      .kind = StrategyKind::WeightedBatch,
      .request_count = static_cast<std::uint32_t>(kRequests.size()),
  };
  // [strategy-batch]
  tess::WeightedPathBatchScratch scratch;
  const auto results =
      tess::weighted_path_batch<World, PassableTag, CostTag, /*MaxCost=*/32>(
          world, kRequests, scratch);
  for (std::size_t index = 0; index < results.size(); ++index) {
    snapshot.requests[index] = copy_result(results[index]);
  }
  // [strategy-batch]
  const auto stats = scratch.stats();
  snapshot.unique_goals = bounded_u32(stats.unique_goals);
  snapshot.field_builds = bounded_u32(stats.field_builds);
  snapshot.astar_fallbacks = bounded_u32(stats.astar_fallbacks);
  return snapshot;
}

[[nodiscard]] auto run_shared_goal_field(const World& world)
    -> StrategySnapshot {
  auto snapshot = StrategySnapshot{
      .kind = StrategyKind::DistanceField,
      .request_count = static_cast<std::uint32_t>(kRequests.size()),
  };
  // [strategy-distance-field]
  tess::DistanceFieldScratch scratch;
  const auto field =
      tess::build_distance_field<World, PassableTag>(world, kGoal, scratch);
  for (std::size_t index = 0; index < kRequests.size(); ++index) {
    const auto result = tess::distance_field_path<World, PassableTag>(
        world, kRequests[index], scratch);
    snapshot.requests[index] = copy_result(result);
  }
  // [strategy-distance-field]
  snapshot.field_builds = field.status == tess::PathStatus::Found ? 1U : 0U;
  snapshot.field_build_expansions = bounded_u32(field.expanded_nodes);
  snapshot.field_reached_nodes = bounded_u32(field.reached_nodes);
  return snapshot;
}

[[nodiscard]] auto request_valid(const RequestSnapshot& request,
                                 PathPoint start) -> bool {
  if (request.status != BrowserPathStatus::Found || request.path_size == 0 ||
      request.path_size > request.path.size() || request.path[0] != start ||
      request.path[request.path_size - 1U] != PathPoint{15, 15} ||
      request.cost + 1U != request.path_size) {
    return false;
  }
  for (std::size_t index = 1; index < request.path_size; ++index) {
    const auto previous = request.path[index - 1U];
    const auto current = request.path[index];
    const auto distance =
        std::abs(current.x - previous.x) + std::abs(current.y - previous.y);
    if (distance != 1) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] auto same_copied_path(const RequestSnapshot& lhs,
                                    const RequestSnapshot& rhs) -> bool {
  return lhs.status == rhs.status && lhs.cost == rhs.cost &&
         lhs.path_size == rhs.path_size &&
         std::equal(lhs.path.begin(), lhs.path.begin() + lhs.path_size,
                    rhs.path.begin());
}

}  // namespace

StrategyModel::StrategyModel() {
  auto world = World{};
  configure_world(world, passable_);
  strategies_[0] = run_independent_astar(world);
  strategies_[1] = run_cached_repeats(world);
  strategies_[2] = run_weighted_batch(world);
  strategies_[3] = run_shared_goal_field(world);
  valid_ = copied_paths_valid();
}

auto StrategyModel::copied_paths_valid() const noexcept -> bool {
  constexpr auto starts = std::array{
      PathPoint{0, 0},
      PathPoint{0, 1},
      PathPoint{0, 2},
  };
  const auto& astar = strategies_[0];
  const auto& cache = strategies_[1];
  const auto& batch = strategies_[2];
  const auto& field = strategies_[3];
  const auto passable_cells = static_cast<std::uint32_t>(
      std::count(passable_.begin(), passable_.end(), std::uint8_t{1}));
  if (astar.request_count != 3U || cache.request_count != 2U ||
      batch.request_count != 3U || field.request_count != 3U) {
    return false;
  }
  for (std::size_t index = 0; index < starts.size(); ++index) {
    if (!request_valid(astar.requests[index], starts[index]) ||
        !request_valid(batch.requests[index], starts[index]) ||
        !request_valid(field.requests[index], starts[index]) ||
        !same_copied_path(astar.requests[index], batch.requests[index]) ||
        !same_copied_path(astar.requests[index], field.requests[index])) {
      return false;
    }
  }
  const auto all_paths_avoid_obstacles = [this]() {
    for (const auto& strategy : strategies_) {
      for (std::size_t request_index = 0;
           request_index < strategy.request_count; ++request_index) {
        const auto& request = strategy.requests[request_index];
        for (std::size_t point_index = 0; point_index < request.path_size;
             ++point_index) {
          const auto point = request.path[point_index];
          if (!cell_passable(point.x, point.y).value_or(false)) {
            return false;
          }
        }
      }
    }
    return true;
  };
  return all_paths_avoid_obstacles() &&
         same_copied_path(astar.requests[0], cache.requests[0]) &&
         same_copied_path(cache.requests[0], cache.requests[1]) &&
         request_valid(cache.requests[0], starts[0]) &&
         request_valid(cache.requests[1], starts[0]) &&
         cache.requests[0].expanded_nodes > 0U &&
         cache.requests[1].expanded_nodes == 0U && cache.cache_hits == 1U &&
         cache.cache_misses == 1U && batch.unique_goals == 1U &&
         batch.field_builds == 1U && batch.astar_fallbacks == 0U &&
         field.field_builds == 1U && field.field_build_expansions > 0U &&
         field.field_reached_nodes == passable_cells;
}

auto StrategyModel::strategy(std::int32_t strategy_index) const noexcept
    -> std::optional<std::reference_wrapper<const StrategySnapshot>> {
  if (strategy_index < 0 ||
      strategy_index >= static_cast<std::int32_t>(strategies_.size())) {
    return std::nullopt;
  }
  return std::cref(strategies_[static_cast<std::size_t>(strategy_index)]);
}

auto StrategyModel::request(std::int32_t strategy_index,
                            std::int32_t request_index) const noexcept
    -> std::optional<std::reference_wrapper<const RequestSnapshot>> {
  const auto selected = strategy(strategy_index);
  if (!selected.has_value() || request_index < 0 ||
      request_index >=
          static_cast<std::int32_t>(selected->get().request_count)) {
    return std::nullopt;
  }
  return std::cref(
      selected->get().requests[static_cast<std::size_t>(request_index)]);
}

auto StrategyModel::path_point(std::int32_t strategy_index,
                               std::int32_t request_index,
                               std::int32_t point_index) const noexcept
    -> std::optional<PathPoint> {
  const auto selected = request(strategy_index, request_index);
  if (!selected.has_value() || point_index < 0 ||
      point_index >= static_cast<std::int32_t>(selected->get().path_size)) {
    return std::nullopt;
  }
  return selected->get().path[static_cast<std::size_t>(point_index)];
}

auto StrategyModel::cell_passable(std::int32_t x, std::int32_t y) const noexcept
    -> std::optional<bool> {
  if (x < 0 || x >= width || y < 0 || y >= height) {
    return std::nullopt;
  }
  const auto index = static_cast<std::size_t>(y * width + x);
  return passable_[index] != 0U;
}

}  // namespace tess::examples::pathfinding_strategies
