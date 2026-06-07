#include <gtest/gtest.h>
#include <tess/tess.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "allocation_counter.h"

namespace {

struct PassableTag {};
struct CostTag {};

using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                 tess::Field<CostTag, std::uint32_t>>;
using Runtime2D = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{8, 8, 1}>;
using World = tess::AlwaysResidentWorld<Runtime2D, Schema>;
constexpr auto RuntimeTileCount =
    Runtime2D::size.x * Runtime2D::size.y * Runtime2D::size.z;

template <typename FieldTag, typename Value>
void fill_field(World& world, Value value) {
  for (auto& page : world.chunks()) {
    auto field = page.template field_span<FieldTag>();
    for (auto& tile : field) {
      tile = value;
    }
  }
}

void fill_world(World& world) {
  fill_field<PassableTag>(world, true);
  fill_field<CostTag>(world, 1u);
}

void mark_passable(World& world, tess::Coord3 coord, bool passable) {
  world.template field<PassableTag>(coord) = passable;
  world.mark_dirty(tess::chunk_key<Runtime2D>(tess::tile_key<Runtime2D>(coord)),
                   1u, tess::Box3{coord, tess::Extent3{1, 1, 1}});
}

void mark_cost(World& world, tess::Coord3 coord, std::uint32_t cost) {
  world.template field<CostTag>(coord) = cost;
  world.mark_dirty(tess::chunk_key<Runtime2D>(tess::tile_key<Runtime2D>(coord)),
                   2u, tess::Box3{coord, tess::Extent3{1, 1, 1}});
}

void reserve_runtime(tess::PathRequestRuntime& runtime,
                     std::size_t request_count) {
  runtime.reserve_requests(request_count);
  runtime.reserve_search_nodes(RuntimeTileCount);
  runtime.reserve_path_nodes(8192);
  runtime.reserve_unit_routes(request_count);
  runtime.reserve_portal_segments(request_count);
  runtime.portal_segment_cache().reserve_path_nodes(1024);
}

TEST(TessPathAgent, UnitAgentsProcessAdvanceAndArrive) {
  World world;
  fill_world(world);

  std::array<tess::PathAgentState, 3> agents{{
      {.position = tess::Coord3{0, 0, 0}},
      {.position = tess::Coord3{0, 1, 0}},
      {.position = tess::Coord3{0, 2, 0}},
  }};
  for (auto& agent : agents) {
    tess::set_path_agent_goal(agent, tess::Coord3{3, agent.position.y, 0});
  }

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());

  auto stats = tess::process_unit_path_agents<World, PassableTag>(world, agents,
                                                                  runtime);
  EXPECT_EQ(stats.submitted, 3u);
  EXPECT_EQ(stats.completed, 3u);
  EXPECT_EQ(stats.found, 3u);

  auto advance = tess::advance_path_agents(agents, runtime);
  EXPECT_EQ(advance.advanced, 3u);
  EXPECT_EQ(agents[0].position, (tess::Coord3{1, 0, 0}));
  EXPECT_TRUE(agents[0].has_goal);

  advance = tess::advance_path_agents(agents, runtime, 8);
  EXPECT_EQ(advance.arrived, 3u);
  for (const auto& agent : agents) {
    EXPECT_FALSE(agent.has_goal);
    EXPECT_EQ(agent.status, tess::PathStatus::Found);
  }
}

TEST(TessPathAgent, WeightedAgentsShareGoalAndExposeRuntimeBatchStats) {
  World world;
  fill_world(world);
  mark_cost(world, tess::Coord3{1, 0, 0}, 5);

  std::array<tess::PathAgentState, 8> agents{};
  for (std::size_t i = 0; i < agents.size(); ++i) {
    agents[i].position = tess::Coord3{0, static_cast<std::int64_t>(i), 0};
    tess::set_path_agent_goal(agents[i], tess::Coord3{31, 31, 0});
  }

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());

  const auto stats =
      tess::process_weighted_path_agents<World, PassableTag, CostTag, 8>(
          world, agents, runtime);
  EXPECT_EQ(stats.submitted, agents.size());
  EXPECT_EQ(stats.found, agents.size());

  const auto runtime_stats = runtime.stats();
  EXPECT_EQ(runtime_stats.weighted_batch.requests, agents.size());
  EXPECT_EQ(runtime_stats.weighted_batch.unique_goals, 1u);
  EXPECT_EQ(runtime_stats.weighted_batch.field_builds, 1u);
}

TEST(TessPathAgent, WorldEditReprocessesActiveAgentsConservatively) {
  World world;
  fill_world(world);

  std::array<tess::PathAgentState, 1> agents{{
      {.position = tess::Coord3{0, 0, 0}},
  }};
  tess::set_path_agent_goal(agents[0], tess::Coord3{7, 0, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());

  auto stats = tess::process_unit_path_agents<World, PassableTag>(world, agents,
                                                                  runtime);
  ASSERT_EQ(stats.found, 1u);
  ASSERT_EQ(runtime.result(agents[0].ticket).path.size(), 8u);

  auto advance = tess::advance_path_agents(agents, runtime);
  ASSERT_EQ(advance.advanced, 1u);
  ASSERT_EQ(agents[0].position, (tess::Coord3{1, 0, 0}));

  mark_passable(world, tess::Coord3{2, 0, 0}, false);
  stats = tess::process_unit_path_agents<World, PassableTag>(world, agents,
                                                             runtime);
  EXPECT_EQ(stats.found, 1u);
  EXPECT_EQ(runtime.stats().world_cache_invalidations, 1u);
  EXPECT_GT(runtime.result(agents[0].ticket).path.size(), 8u);

  advance = tess::advance_path_agents(agents, runtime);
  EXPECT_EQ(advance.advanced, 1u);
  EXPECT_NE(agents[0].position, (tess::Coord3{2, 0, 0}));
}

TEST(TessPathAgent, InvalidAndUnreachableGoalsDoNotMoveAgents) {
  World world;
  fill_world(world);
  for (std::int64_t y = 0; y < 32; ++y) {
    mark_passable(world, tess::Coord3{1, y, 0}, false);
  }

  std::array<tess::PathAgentState, 2> agents{{
      {.position = tess::Coord3{0, 0, 0}},
      {.position = tess::Coord3{0, 1, 0}},
  }};
  tess::set_path_agent_goal(agents[0], tess::Coord3{64, 0, 0});
  tess::set_path_agent_goal(agents[1], tess::Coord3{2, 1, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());

  const auto stats = tess::process_unit_path_agents<World, PassableTag>(
      world, agents, runtime);
  EXPECT_EQ(stats.invalid_goal, 1u);
  EXPECT_EQ(stats.no_path, 1u);
  EXPECT_EQ(agents[0].status, tess::PathStatus::InvalidGoal);
  EXPECT_EQ(agents[1].status, tess::PathStatus::NoPath);

  const auto advance = tess::advance_path_agents(agents, runtime);
  EXPECT_EQ(advance.advanced, 0u);
  EXPECT_EQ(agents[0].position, (tess::Coord3{0, 0, 0}));
  EXPECT_EQ(agents[1].position, (tess::Coord3{0, 1, 0}));
  EXPECT_TRUE(agents[0].has_goal);
  EXPECT_TRUE(agents[1].has_goal);
}

TEST(TessPathAgent, WarmUnitAgentProcessingDoesNotAllocate) {
  World world;
  fill_world(world);

  std::array<tess::PathAgentState, 8> agents{};
  for (std::size_t i = 0; i < agents.size(); ++i) {
    agents[i].position = tess::Coord3{static_cast<std::int64_t>(i), 0, 0};
    tess::set_path_agent_goal(agents[i], tess::Coord3{15, 0, 0});
  }

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  (void)tess::process_unit_path_agents<World, PassableTag>(world, agents,
                                                           runtime);

  tess_test::reset_allocation_count();
  tess_test::set_allocation_counting(true);
  (void)tess::process_unit_path_agents<World, PassableTag>(world, agents,
                                                           runtime);
  tess_test::set_allocation_counting(false);

  EXPECT_EQ(tess_test::allocation_count(), 0);
}

TEST(TessPathAgent, WarmWeightedAgentProcessingDoesNotAllocate) {
  World world;
  fill_world(world);

  std::array<tess::PathAgentState, 8> agents{};
  for (std::size_t i = 0; i < agents.size(); ++i) {
    agents[i].position = tess::Coord3{0, static_cast<std::int64_t>(i), 0};
    tess::set_path_agent_goal(agents[i], tess::Coord3{31, 31, 0});
  }

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  (void)tess::process_weighted_path_agents<World, PassableTag, CostTag, 8>(
      world, agents, runtime);

  tess_test::reset_allocation_count();
  tess_test::set_allocation_counting(true);
  (void)tess::process_weighted_path_agents<World, PassableTag, CostTag, 8>(
      world, agents, runtime);
  tess_test::set_allocation_counting(false);

  EXPECT_EQ(tess_test::allocation_count(), 0);
}

}  // namespace
