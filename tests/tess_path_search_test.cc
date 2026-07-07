#include <gtest/gtest.h>
#include <tess/tess.h>

#include <array>
#include <cstdint>
#include <vector>

#include "path_test_util.h"

// Coverage for the real A* heap search loop. Every fixture here defeats
// all pre-A* fast paths (see path_test_util.h), so Found results can only
// come from the banded two-list unit search or the weighted binary-heap
// search. Fast paths structurally return
// `expanded_nodes == path.size()`; the heap loop counts closed nodes, so
// `expanded_nodes > path.size()` discriminates the two on these mazes.
// tess_diagnostics_enabled_test.cc additionally pins heap_pushes > 0 on
// the same fixtures as a permanent mutation guard.

namespace {

using tess_test::SerpChunked3D;
using tess_test::SerpCostTag;
using tess_test::SerpPassableTag;
using tess_test::SerpSchema;
using tess_test::SerpTopDown2D;
using tess_test::SerpVertical2D;

constexpr std::uint32_t kBatchMaxCost = 64;

template <typename Shape>
using SerpWorld = tess::AlwaysResidentWorld<Shape, SerpSchema>;

TEST(TessPathSearch, UnitSerpentineTopDown2DMatchesBfsOracle) {
  SerpWorld<SerpTopDown2D> world;
  const auto endpoints = tess_test::build_serpentine_topdown(world);
  const auto optimal =
      tess_test::bfs_unit_cost(world, endpoints.start, endpoints.goal);
  ASSERT_NE(optimal, tess_test::kUnreachable);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  const auto result = tess::astar_path<decltype(world), SerpPassableTag>(
      world, tess::PathRequest{endpoints.start, endpoints.goal}, scratch);

  ASSERT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(result.cost, optimal);
  EXPECT_EQ(result.path.size(), static_cast<std::size_t>(optimal) + 1u);
  EXPECT_TRUE(tess_test::valid_path_walk(world, result.path, endpoints.start,
                                         endpoints.goal));
  // Fast paths return expanded_nodes == path.size(); only the heap loop
  // expands off-path nodes.
  EXPECT_GT(result.expanded_nodes, result.path.size());
  EXPECT_GE(result.reached_nodes, result.expanded_nodes);
}

TEST(TessPathSearch, UnitSerpentineVertical2DMatchesBfsOracle) {
  SerpWorld<SerpVertical2D> world;
  const auto endpoints = tess_test::build_serpentine_vertical(world);
  const auto optimal =
      tess_test::bfs_unit_cost(world, endpoints.start, endpoints.goal);
  ASSERT_NE(optimal, tess_test::kUnreachable);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  const auto result = tess::astar_path<decltype(world), SerpPassableTag>(
      world, tess::PathRequest{endpoints.start, endpoints.goal}, scratch);

  ASSERT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(result.cost, optimal);
  EXPECT_EQ(result.path.size(), static_cast<std::size_t>(optimal) + 1u);
  EXPECT_TRUE(tess_test::valid_path_walk(world, result.path, endpoints.start,
                                         endpoints.goal));
  EXPECT_GT(result.expanded_nodes, result.path.size());
  EXPECT_GE(result.reached_nodes, result.expanded_nodes);
}

TEST(TessPathSearch, UnitSerpentineChunked3DMatchesBfsOracle) {
  SerpWorld<SerpChunked3D> world;
  const auto endpoints = tess_test::build_serpentine_chunked3d(world);
  const auto optimal =
      tess_test::bfs_unit_cost(world, endpoints.start, endpoints.goal);
  ASSERT_NE(optimal, tess_test::kUnreachable);

  tess::PathScratch scratch;
  scratch.reserve_nodes(256);
  const auto result = tess::astar_path<decltype(world), SerpPassableTag>(
      world, tess::PathRequest{endpoints.start, endpoints.goal}, scratch);

  ASSERT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(result.cost, optimal);
  EXPECT_EQ(result.path.size(), static_cast<std::size_t>(optimal) + 1u);
  EXPECT_TRUE(tess_test::valid_path_walk(world, result.path, endpoints.start,
                                         endpoints.goal));
  EXPECT_GT(result.expanded_nodes, result.path.size());
  EXPECT_GE(result.reached_nodes, result.expanded_nodes);
}

TEST(TessPathSearch, WeightedSerpentineMatchesDijkstraOracle) {
  SerpWorld<SerpTopDown2D> world;
  const auto endpoints = tess_test::build_serpentine_topdown(world);
  tess_test::fill_cost(world, 1);
  // Make the x = 4 corridor column expensive so the cheap descent runs
  // through x = 3; the weighted search must diverge from the unit-optimal
  // tile count to stay cost-optimal.
  for (std::int64_t y = 0; y < 8; ++y) {
    world.template field<SerpCostTag>(tess::Coord3{4, y, 0}) = 5;
  }
  const auto optimal =
      tess_test::dijkstra_weighted_cost(world, endpoints.start, endpoints.goal);
  ASSERT_NE(optimal, tess_test::kUnreachable);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  const auto result =
      tess::weighted_astar_path<decltype(world), SerpPassableTag, SerpCostTag>(
          world, tess::PathRequest{endpoints.start, endpoints.goal}, scratch);

  ASSERT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(result.cost, optimal);
  EXPECT_TRUE(tess_test::valid_path_walk(world, result.path, endpoints.start,
                                         endpoints.goal));
  EXPECT_GT(result.expanded_nodes, result.path.size());
  EXPECT_GE(result.reached_nodes, result.expanded_nodes);
}

// --- start == goal semantics (audit H4) -----------------------------------
// Every public entry point must report Found with the single-node path
// {start} and cost 0 when the start already satisfies the goal (and the
// usual Invalid* statuses when the shared tile is invalid).

TEST(TessPathSearch, StartEqualsGoalUnitAndWeightedAStar) {
  SerpWorld<SerpTopDown2D> world;
  tess_test::fill_passable(world, true);
  tess_test::fill_cost(world, 1);
  const auto tile = tess::Coord3{3, 4, 0};
  // A non-unit start-tile entry cost must not affect a zero-length route.
  world.template field<SerpCostTag>(tile) = 7;

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  const auto unit = tess::astar_path<decltype(world), SerpPassableTag>(
      world, tess::PathRequest{tile, tile}, scratch);
  ASSERT_EQ(unit.status, tess::PathStatus::Found);
  EXPECT_EQ(unit.cost, 0u);
  ASSERT_EQ(unit.path.size(), 1u);
  EXPECT_EQ(unit.path.front(), tile);

  const auto weighted =
      tess::weighted_astar_path<decltype(world), SerpPassableTag, SerpCostTag>(
          world, tess::PathRequest{tile, tile}, scratch);
  ASSERT_EQ(weighted.status, tess::PathStatus::Found);
  EXPECT_EQ(weighted.cost, 0u);
  ASSERT_EQ(weighted.path.size(), 1u);
  EXPECT_EQ(weighted.path.front(), tile);

  world.template field<SerpPassableTag>(tile) = false;
  const auto blocked = tess::astar_path<decltype(world), SerpPassableTag>(
      world, tess::PathRequest{tile, tile}, scratch);
  EXPECT_EQ(blocked.status, tess::PathStatus::InvalidStart);
}

TEST(TessPathSearch, StartEqualsGoalCachedAStarMissAndHit) {
  SerpWorld<SerpTopDown2D> world;
  tess_test::fill_passable(world, true);
  const auto tile = tess::Coord3{2, 6, 0};

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::RouteCacheScratch cache;
  const auto miss = tess::cached_astar_path<decltype(world), SerpPassableTag>(
      world, tess::PathRequest{tile, tile}, scratch, cache);
  const auto hit = tess::cached_astar_path<decltype(world), SerpPassableTag>(
      world, tess::PathRequest{tile, tile}, scratch, cache);

  for (const auto& result : {miss, hit}) {
    ASSERT_EQ(result.status, tess::PathStatus::Found);
    EXPECT_EQ(result.cost, 0u);
    ASSERT_EQ(result.path.size(), 1u);
    EXPECT_EQ(result.path.front(), tile);
  }
  EXPECT_EQ(cache.stats().hits, 1u);
}

TEST(TessPathSearch, StartEqualsGoalDistanceFieldPaths) {
  SerpWorld<SerpTopDown2D> world;
  tess_test::fill_passable(world, true);
  tess_test::fill_cost(world, 1);
  const auto tile = tess::Coord3{5, 5, 0};

  tess::DistanceFieldScratch unit_scratch;
  unit_scratch.reserve_nodes(64);
  const auto unit_field =
      tess::build_distance_field<decltype(world), SerpPassableTag>(
          world, tile, unit_scratch);
  ASSERT_EQ(unit_field.status, tess::PathStatus::Found);
  const auto unit_path =
      tess::distance_field_path<decltype(world), SerpPassableTag>(
          world, tile, tile, unit_scratch);
  ASSERT_EQ(unit_path.status, tess::PathStatus::Found);
  EXPECT_EQ(unit_path.cost, 0u);
  ASSERT_EQ(unit_path.path.size(), 1u);
  EXPECT_EQ(unit_path.path.front(), tile);

  tess::DistanceFieldScratch weighted_scratch;
  weighted_scratch.reserve_nodes(64);
  const auto weighted_field =
      tess::build_weighted_distance_field<decltype(world), SerpPassableTag,
                                          SerpCostTag>(world, tile,
                                                       weighted_scratch);
  ASSERT_EQ(weighted_field.status, tess::PathStatus::Found);
  const auto weighted_path =
      tess::weighted_distance_field_path<decltype(world), SerpPassableTag,
                                         SerpCostTag>(world, tile, tile,
                                                      weighted_scratch);
  ASSERT_EQ(weighted_path.status, tess::PathStatus::Found);
  EXPECT_EQ(weighted_path.cost, 0u);
  ASSERT_EQ(weighted_path.path.size(), 1u);
  EXPECT_EQ(weighted_path.path.front(), tile);

  tess::DistanceFieldScratch bounded_scratch;
  bounded_scratch.reserve_nodes(64);
  const auto bounded_field = tess::build_bounded_weighted_distance_field<
      decltype(world), SerpPassableTag, SerpCostTag, kBatchMaxCost>(
      world, tile, bounded_scratch);
  ASSERT_EQ(bounded_field.status, tess::PathStatus::Found);
  const auto bounded_path =
      tess::weighted_distance_field_path<decltype(world), SerpPassableTag,
                                         SerpCostTag>(world, tile, tile,
                                                      bounded_scratch);
  ASSERT_EQ(bounded_path.status, tess::PathStatus::Found);
  EXPECT_EQ(bounded_path.cost, 0u);
  ASSERT_EQ(bounded_path.path.size(), 1u);

  tess::DistanceFieldScratch boxed_scratch;
  boxed_scratch.reserve_nodes(64);
  const auto domain = tess::Box3{tess::Coord3{4, 4, 0}, tess::Extent3{4, 4, 1}};
  const auto boxed_field =
      tess::build_weighted_distance_field_in_box<decltype(world),
                                                 SerpPassableTag, SerpCostTag>(
          world, tile, domain, boxed_scratch);
  ASSERT_EQ(boxed_field.status, tess::PathStatus::Found);
  const auto boxed_path =
      tess::weighted_distance_field_path<decltype(world), SerpPassableTag,
                                         SerpCostTag>(world, tile, tile,
                                                      boxed_scratch);
  ASSERT_EQ(boxed_path.status, tess::PathStatus::Found);
  EXPECT_EQ(boxed_path.cost, 0u);
  ASSERT_EQ(boxed_path.path.size(), 1u);
}

TEST(TessPathSearch, StartEqualsGoalDistanceFieldProductAndNearestTarget) {
  SerpWorld<SerpTopDown2D> world;
  tess_test::fill_passable(world, true);
  const auto tile = tess::Coord3{1, 6, 0};

  tess::GoalSet goals;
  goals.add(tile);
  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(64);
  tess::DistanceFieldProduct product;
  const auto field =
      tess::build_distance_field_product<decltype(world), SerpPassableTag>(
          world, goals, scratch, product);
  ASSERT_EQ(field.status, tess::PathStatus::Found);

  const auto path =
      tess::distance_field_product_path<decltype(world), SerpPassableTag>(
          world, tile, product, scratch);
  ASSERT_EQ(path.status, tess::PathStatus::Found);
  EXPECT_EQ(path.cost, 0u);
  ASSERT_EQ(path.path.size(), 1u);
  EXPECT_EQ(path.path.front(), tile);

  const auto nearest = tess::nearest_target<decltype(world), SerpPassableTag>(
      world, tile, product, scratch);
  ASSERT_EQ(nearest.status, tess::PathStatus::Found);
  EXPECT_EQ(nearest.cost, 0u);
  EXPECT_EQ(nearest.target, tile);
}

TEST(TessPathSearch, StartEqualsGoalRouteAndPortalProducts) {
  SerpWorld<SerpTopDown2D> world;
  tess_test::fill_passable(world, true);
  tess_test::fill_cost(world, 1);
  const auto tile = tess::Coord3{6, 2, 0};
  const auto request = tess::PathRequest{tile, tile};

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);

  tess::WeightedRouteProduct route;
  const auto built =
      tess::build_weighted_route_product<decltype(world), SerpPassableTag,
                                         SerpCostTag>(world, request, scratch,
                                                      route);
  ASSERT_EQ(built.status, tess::PathStatus::Found);
  EXPECT_EQ(built.cost, 0u);
  ASSERT_EQ(built.path.size(), 1u);
  EXPECT_EQ(built.path.front(), tile);
  const auto replay = tess::weighted_route_product_path(world, route);
  ASSERT_EQ(replay.status, tess::PathStatus::Found);
  EXPECT_EQ(replay.cost, 0u);
  ASSERT_EQ(replay.path.size(), 1u);

  tess::WeightedPortalRouteProduct portal;
  const auto portal_built =
      tess::build_weighted_portal_route_product<decltype(world),
                                                SerpPassableTag, SerpCostTag>(
          world, request, {}, scratch, portal);
  ASSERT_EQ(portal_built.status, tess::PathStatus::Found);
  EXPECT_EQ(portal_built.cost, 0u);
  ASSERT_EQ(portal_built.path.size(), 1u);
  EXPECT_EQ(portal_built.path.front(), tile);
  const auto portal_replay =
      tess::weighted_portal_route_product_path(world, portal);
  ASSERT_EQ(portal_replay.status, tess::PathStatus::Found);
  EXPECT_EQ(portal_replay.cost, 0u);

  tess::WeightedPortalRouteProduct chunk_portal;
  const auto chunk_built = tess::build_weighted_chunk_portal_route_product<
      decltype(world), SerpPassableTag, SerpCostTag>(world, request, scratch,
                                                     chunk_portal);
  ASSERT_EQ(chunk_built.status, tess::PathStatus::Found);
  EXPECT_EQ(chunk_built.cost, 0u);
  ASSERT_EQ(chunk_built.path.size(), 1u);
  EXPECT_EQ(chunk_built.path.front(), tile);
}

TEST(TessPathSearch, StartEqualsGoalWeightedBatchSingleAndSharedGoal) {
  SerpWorld<SerpTopDown2D> world;
  tess_test::fill_passable(world, true);
  tess_test::fill_cost(world, 1);
  const auto tile = tess::Coord3{4, 1, 0};

  tess::WeightedPathBatchScratch scratch;
  const auto single_requests = std::array{tess::PathRequest{tile, tile}};
  const auto single =
      tess::weighted_path_batch<decltype(world), SerpPassableTag, SerpCostTag,
                                kBatchMaxCost>(world, single_requests, scratch);
  ASSERT_EQ(single.size(), 1u);
  ASSERT_EQ(single[0].status, tess::PathStatus::Found);
  EXPECT_EQ(single[0].cost, 0u);
  ASSERT_EQ(single[0].path.size(), 1u);
  EXPECT_EQ(single[0].path.front(), tile);

  // Two requests sharing the goal route through the shared-field branch.
  const auto shared_requests = std::array{
      tess::PathRequest{tess::Coord3{0, 1, 0}, tile},
      tess::PathRequest{tile, tile},
  };
  const auto shared =
      tess::weighted_path_batch<decltype(world), SerpPassableTag, SerpCostTag,
                                kBatchMaxCost>(world, shared_requests, scratch);
  ASSERT_EQ(shared.size(), 2u);
  ASSERT_EQ(shared[1].status, tess::PathStatus::Found);
  EXPECT_EQ(shared[1].cost, 0u);
  ASSERT_EQ(shared[1].path.size(), 1u);
  EXPECT_EQ(shared[1].path.front(), tile);
}

TEST(TessPathSearch, StartEqualsGoalPathRequestRuntime) {
  SerpWorld<SerpTopDown2D> world;
  tess_test::fill_passable(world, true);
  tess_test::fill_cost(world, 1);
  const auto tile = tess::Coord3{7, 0, 0};

  tess::PathRequestRuntime runtime;
  const auto unit_ticket = runtime.submit(tess::PathRequest{tile, tile});
  const auto unit_results =
      runtime.process_unit_cached<decltype(world), SerpPassableTag>(world);
  ASSERT_EQ(unit_results.size(), 1u);
  const auto unit_result = runtime.result(unit_ticket);
  ASSERT_EQ(unit_result.status, tess::PathStatus::Found);
  EXPECT_EQ(unit_result.cost, 0u);
  ASSERT_EQ(unit_result.path.size(), 1u);
  EXPECT_EQ(unit_result.path.front(), tile);

  runtime.clear_requests();
  const auto weighted_ticket = runtime.submit(tess::PathRequest{tile, tile});
  const auto weighted_results =
      runtime.process_weighted_batch<decltype(world), SerpPassableTag,
                                     SerpCostTag, kBatchMaxCost>(world);
  ASSERT_EQ(weighted_results.size(), 1u);
  const auto weighted_result = runtime.result(weighted_ticket);
  ASSERT_EQ(weighted_result.status, tess::PathStatus::Found);
  EXPECT_EQ(weighted_result.cost, 0u);
  ASSERT_EQ(weighted_result.path.size(), 1u);
  EXPECT_EQ(weighted_result.path.front(), tile);
}

TEST(TessPathSearch, StartEqualsGoalAgentTickArrivesImmediately) {
  SerpWorld<SerpTopDown2D> world;
  tess_test::fill_passable(world, true);
  tess_test::fill_cost(world, 1);
  const auto tile = tess::Coord3{2, 2, 0};

  tess::PathRequestRuntime runtime;
  tess::PathAgentTickState state;
  std::array<tess::PathAgentState, 1> agents{};
  agents[0].position = tile;
  tess::set_path_agent_goal(state, agents[0], tile);

  const auto unit_stats =
      tess::tick_unit_path_agents<decltype(world), SerpPassableTag>(
          state, world, agents, runtime);
  EXPECT_TRUE(unit_stats.processed_paths);
  EXPECT_EQ(unit_stats.pathing.arrived, 1u);
  EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::Idle);
  EXPECT_FALSE(agents[0].has_goal);
  EXPECT_EQ(agents[0].position, tile);

  tess::set_path_agent_goal(state, agents[0], tile);
  const auto weighted_stats =
      tess::tick_weighted_path_agents<decltype(world), SerpPassableTag,
                                      SerpCostTag, kBatchMaxCost>(
          state, world, agents, runtime);
  EXPECT_EQ(weighted_stats.pathing.arrived, 1u);
  EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::Idle);
  EXPECT_FALSE(agents[0].has_goal);
  EXPECT_EQ(agents[0].position, tile);
}

}  // namespace
