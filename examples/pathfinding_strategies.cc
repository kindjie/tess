#include <tess/pathfinding.h>

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>

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

void open_world(World& world) {
  for (std::int64_t y = 0; y < static_cast<std::int64_t>(Shape::size.y); ++y) {
    for (std::int64_t x = 0; x < static_cast<std::int64_t>(Shape::size.x);
         ++x) {
      world.field<PassableTag>(tess::Coord3{x, y, 0}) = 1;
      world.field<CostTag>(tess::Coord3{x, y, 0}) = 1;
    }
  }
}

auto run_one_off_astar(const World& world) -> bool {
  // [strategy-astar]
  tess::PathScratch scratch;
  const auto result =
      tess::astar_path<World, PassableTag>(world, kRequests.front(), scratch);
  // [strategy-astar]

  return result.status == tess::PathStatus::Found && result.cost == 30u;
}

auto run_cached_repeats(const World& world) -> bool {
  // [strategy-cache]
  tess::PathScratch scratch;
  tess::RouteCacheScratch cache;
  const auto first = tess::cached_astar_path<World, PassableTag>(
      world, kRequests.front(), scratch, cache);
  const auto repeated = tess::cached_astar_path<World, PassableTag>(
      world, kRequests.front(), scratch, cache);
  // [strategy-cache]

  const auto stats = cache.stats();
  return first.status == tess::PathStatus::Found &&
         repeated.status == tess::PathStatus::Found &&
         first.expanded_nodes > 0u && repeated.expanded_nodes == 0u &&
         stats.misses == 1u && stats.hits == 1u;
}

auto run_weighted_batch(const World& world) -> bool {
  // [strategy-batch]
  tess::WeightedPathBatchScratch scratch;
  const auto results =
      tess::weighted_path_batch<World, PassableTag, CostTag, /*MaxCost=*/32>(
          world, kRequests, scratch);
  // [strategy-batch]

  if (results.size() != kRequests.size()) {
    return false;
  }
  for (std::size_t index = 0; index < results.size(); ++index) {
    if (results[index].status != tess::PathStatus::Found ||
        results[index].cost != 30u - index) {
      return false;
    }
  }
  const auto stats = scratch.stats();
  return stats.requests == kRequests.size() && stats.unique_goals == 1u &&
         stats.field_builds == 1u;
}

auto run_shared_goal_field(const World& world) -> bool {
  // [strategy-distance-field]
  tess::DistanceFieldScratch scratch;
  const auto field =
      tess::build_distance_field<World, PassableTag>(world, kGoal, scratch);
  std::array<tess::PathResult, kRequests.size()> results{};
  for (std::size_t index = 0; index < kRequests.size(); ++index) {
    results[index] = tess::distance_field_path<World, PassableTag>(
        world, kRequests[index], scratch);
  }
  // [strategy-distance-field]

  if (field.status != tess::PathStatus::Found) {
    return false;
  }
  for (std::size_t index = 0; index < results.size(); ++index) {
    if (results[index].status != tess::PathStatus::Found ||
        results[index].cost != 30u - index) {
      return false;
    }
  }
  return true;
}

}  // namespace

int main() {
#if TESS_HAS_EXCEPTIONS
  try {
#endif
    World world;
    open_world(world);
    if (!run_one_off_astar(world) || !run_cached_repeats(world) ||
        !run_weighted_batch(world) || !run_shared_goal_field(world)) {
      std::cerr << "pathfinding strategy comparison failed\n";
      return 1;
    }
    std::cout << "pathfinding strategies: ok\n";
#if TESS_HAS_EXCEPTIONS
  } catch (const std::exception& error) {
    std::cerr << "pathfinding strategy comparison failed: " << error.what()
              << '\n';
    return 1;
  }
#endif
  return 0;
}
