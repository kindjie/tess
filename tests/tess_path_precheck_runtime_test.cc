#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstddef>
#include <cstdint>
#include <span>

#include "allocation_counter.h"

namespace {

struct PassableTag {};
struct CostTag {};

using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                 tess::Field<CostTag, std::uint32_t>>;
// 32x32 in 8x8 chunks (chunk columns at x in [0,8), [8,16), [16,24), [24,32)).
using Grid = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{8, 8, 1}>;
using World = tess::AlwaysResidentWorld<Grid, Schema>;
constexpr auto TileCount = Grid::size.x * Grid::size.y * Grid::size.z;

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

constexpr auto kStart = tess::Coord3{0, 0, 0};
constexpr auto kReachableGoal = tess::Coord3{15, 15, 0};
constexpr auto kUnreachableGoal = tess::Coord3{20, 20, 0};

// Walls the four orthogonal neighbours of `center`, enclosing that single
// passable tile as its own disconnected region. Unlike a full-column wall this
// is NOT a full-axis barrier, so A*'s cheap dense fast-path cannot rule it out
// -- it must flood the reachable component before reporting NoPath -- which is
// exactly the case the topology precheck exists to short-circuit.
void enclose(World& world, tess::Coord3 center) {
  const tess::Coord3 neighbours[] = {
      {center.x - 1, center.y, center.z},
      {center.x + 1, center.y, center.z},
      {center.x, center.y - 1, center.z},
      {center.x, center.y + 1, center.z},
  };
  for (const auto n : neighbours) {
    world.field<PassableTag>(n) = false;
    world.mark_dirty(tess::chunk_key<Grid>(tess::tile_key<Grid>(n)), 1u,
                     tess::Box3{n, tess::Extent3{1, 1, 1}});
  }
}

// Builds a world with kUnreachableGoal sealed off, plus a region graph over
// PassableTag that reports it as a disconnected singleton region.
void build_split(World& world, tess::RegionGraph& graph) {
  fill_world(world);
  enclose(world, kUnreachableGoal);
  tess::LocalTopologyScratch scratch;
  const auto built =
      tess::build_region_graph<World, PassableTag>(world, scratch, graph);
  ASSERT_EQ(built.status, tess::TopologyStatus::Built);
}

void reserve_runtime(tess::PathRequestRuntime& runtime, std::size_t requests) {
  runtime.reserve_requests(requests);
  runtime.reserve_search_nodes(TileCount);
  runtime.reserve_path_nodes(4096);
  runtime.reserve_unit_routes(requests);
}

}  // namespace

TEST(TessPrecheckRuntime, UnitRulesOutUnreachableGoalWithoutExpanding) {
  World world;
  tess::RegionGraph graph;
  build_split(world, graph);

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, 1);
  (void)runtime.submit(tess::PathRequest{kStart, kUnreachableGoal});

  const auto results =
      runtime.process_unit_cached<World, PassableTag>(world, {}, &graph);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].status, tess::PathStatus::NoPath);
  EXPECT_EQ(results[0].expanded_nodes, 0u);  // A* never ran

  const auto stats = runtime.stats();
  EXPECT_EQ(stats.precheck_ruled_out, 1u);
  EXPECT_EQ(stats.no_path, 1u);  // ruled-out counts as a proven no-route
  EXPECT_LE(stats.precheck_ruled_out, stats.no_path);
}

TEST(TessPrecheckRuntime, UnitReachableGoalStillRunsAStar) {
  World world;
  tess::RegionGraph graph;
  build_split(world, graph);

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, 1);
  (void)runtime.submit(tess::PathRequest{kStart, kReachableGoal});

  const auto results =
      runtime.process_unit_cached<World, PassableTag>(world, {}, &graph);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].status, tess::PathStatus::Found);
  EXPECT_GT(results[0].expanded_nodes, 0u);
  EXPECT_EQ(runtime.stats().precheck_ruled_out, 0u);
}

TEST(TessPrecheckRuntime, UnitNoGraphRunsAStarUnchanged) {
  World world;
  tess::RegionGraph graph;
  build_split(world, graph);

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, 1);
  (void)runtime.submit(tess::PathRequest{kStart, kUnreachableGoal});

  // No graph supplied: legacy behavior, A* explores the reachable component.
  const auto results =
      runtime.process_unit_cached<World, PassableTag>(world, {});
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].status, tess::PathStatus::NoPath);
  EXPECT_GT(results[0].expanded_nodes, 0u);  // A* did run
  EXPECT_EQ(runtime.stats().precheck_ruled_out, 0u);
}

TEST(TessPrecheckRuntime, UnitStaleGraphFallsBackToAStar) {
  World world;
  tess::RegionGraph graph;
  build_split(world, graph);
  // A topology edit after the build makes the graph stale; the precheck must
  // degrade to A* rather than trust the snapshot.
  world.mark_topology_rebuilt(tess::ChunkKey{0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, 1);
  (void)runtime.submit(tess::PathRequest{kStart, kUnreachableGoal});

  const auto results =
      runtime.process_unit_cached<World, PassableTag>(world, {}, &graph);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].status, tess::PathStatus::NoPath);
  EXPECT_GT(results[0].expanded_nodes, 0u);  // A* ran (graph was stale)
  EXPECT_EQ(runtime.stats().precheck_ruled_out, 0u);
}

TEST(TessPrecheckRuntime, WeightedRulesOutUnreachablePreservingSurvivorSlots) {
  World world;
  tess::RegionGraph graph;
  build_split(world, graph);

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, 3);
  // Slot 0 reachable, slot 1 unreachable, slot 2 reachable: proves the
  // survivor partition scatters batch results back to their original slots.
  (void)runtime.submit(tess::PathRequest{kStart, kReachableGoal});
  (void)runtime.submit(tess::PathRequest{kStart, kUnreachableGoal});
  (void)runtime.submit(
      tess::PathRequest{tess::Coord3{0, 1, 0}, tess::Coord3{14, 14, 0}});

  const auto results =
      runtime.process_weighted_batch<World, PassableTag, CostTag, 64>(world, {},
                                                                      &graph);
  ASSERT_EQ(results.size(), 3u);

  EXPECT_EQ(results[0].status, tess::PathStatus::Found);
  ASSERT_FALSE(results[0].path.empty());
  EXPECT_EQ(results[0].path.back(), kReachableGoal);

  EXPECT_EQ(results[1].status, tess::PathStatus::NoPath);
  EXPECT_EQ(results[1].expanded_nodes, 0u);

  EXPECT_EQ(results[2].status, tess::PathStatus::Found);
  ASSERT_FALSE(results[2].path.empty());
  EXPECT_EQ(results[2].path.back(), (tess::Coord3{14, 14, 0}));

  const auto stats = runtime.stats();
  EXPECT_EQ(stats.precheck_ruled_out, 1u);
  EXPECT_EQ(stats.found, 2u);
  EXPECT_EQ(stats.no_path, 1u);
  EXPECT_LE(stats.precheck_ruled_out, stats.no_path);
}

TEST(TessPrecheckRuntime, WeightedNoGraphRunsBatchUnchanged) {
  World world;
  tess::RegionGraph graph;
  build_split(world, graph);

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, 3);
  (void)runtime.submit(tess::PathRequest{kStart, kReachableGoal});
  (void)runtime.submit(tess::PathRequest{kStart, kUnreachableGoal});
  (void)runtime.submit(
      tess::PathRequest{tess::Coord3{0, 1, 0}, tess::Coord3{14, 14, 0}});

  const auto results =
      runtime.process_weighted_batch<World, PassableTag, CostTag, 64>(world,
                                                                      {});
  ASSERT_EQ(results.size(), 3u);
  EXPECT_EQ(results[0].status, tess::PathStatus::Found);
  EXPECT_EQ(results[1].status, tess::PathStatus::NoPath);
  EXPECT_EQ(results[2].status, tess::PathStatus::Found);
  EXPECT_EQ(runtime.stats().precheck_ruled_out, 0u);
}

TEST(TessPrecheckRuntime, WarmUnitPrecheckRuleOutIsAllocationFree) {
  World world;
  tess::RegionGraph graph;
  build_split(world, graph);

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, 2);
  (void)runtime.submit(tess::PathRequest{kStart, kUnreachableGoal});
  (void)runtime.submit(
      tess::PathRequest{tess::Coord3{0, 1, 0}, kUnreachableGoal});

  // Warm the runtime scratch (precheck epoch vector, result vectors) once.
  (void)runtime.process_unit_cached<World, PassableTag>(world, {}, &graph);
  {
    tess_test::ScopedAllocationCounter counter;
    const auto results =
        runtime.process_unit_cached<World, PassableTag>(world, {}, &graph);
    EXPECT_EQ(results[0].status, tess::PathStatus::NoPath);
    EXPECT_EQ(runtime.stats().precheck_ruled_out, 2u);
    EXPECT_EQ(counter.count(), 0u);
    EXPECT_EQ(counter.bytes(), 0u);
  }
}

TEST(TessPrecheckRuntime, TickWeightedAgentPrecheckRulesOutGoal) {
  World world;
  tess::RegionGraph graph;
  build_split(world, graph);

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, 1);
  tess::PathAgentTickState state;
  std::array<tess::PathAgentState, 1> agents{};
  agents[0].position = kStart;
  tess::set_path_agent_goal(state, agents[0], kUnreachableGoal);

  const auto stats =
      tess::tick_weighted_path_agents<World, PassableTag, CostTag, 64>(
          state, world, agents, runtime, {}, &graph);

  EXPECT_TRUE(stats.processed_paths);
  EXPECT_EQ(stats.pathing.precheck_ruled_out, 1u);
  EXPECT_EQ(stats.pathing.no_path, 1u);
  EXPECT_EQ(stats.movement.advanced, 0u);  // never moved toward a walled goal
  EXPECT_EQ(agents[0].position, kStart);
}

TEST(TessPrecheckRuntime, TickUnitAgentPrecheckRulesOutGoal) {
  World world;
  tess::RegionGraph graph;
  build_split(world, graph);

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, 1);
  tess::PathAgentTickState state;
  std::array<tess::PathAgentState, 1> agents{};
  agents[0].position = kStart;
  tess::set_path_agent_goal(state, agents[0], kUnreachableGoal);

  const auto stats = tess::tick_unit_path_agents<World, PassableTag>(
      state, world, agents, runtime, {}, &graph);

  EXPECT_TRUE(stats.processed_paths);
  EXPECT_EQ(stats.pathing.precheck_ruled_out, 1u);
  EXPECT_EQ(stats.movement.advanced, 0u);
  EXPECT_EQ(agents[0].position, kStart);
}

namespace {

struct RuntimeConstructionTag {};
using ClassSchema =
    tess::FieldSchema<tess::Field<PassableTag, bool>,
                      tess::Field<RuntimeConstructionTag, std::uint8_t>,
                      tess::Field<CostTag, std::uint32_t>>;
using ClassWorld = tess::AlwaysResidentWorld<Grid, ClassSchema>;
using RuntimeBuilder = tess::movement::MovementClass<
    tess::movement::AnyOf<tess::movement::Field<PassableTag>,
                          tess::movement::Field<RuntimeConstructionTag>>,
    tess::movement::UnitCost>;

}  // namespace

// A graph stamped for the raw walker tag must never prune a Builder request:
// the runtime's precheck reads it as GraphStale (nothing ruled out) and the
// Builder's own A* walks through the construction gap.
TEST(TessPrecheckRuntime, UnitWrongClassGraphFallsBackToAStar) {
  ClassWorld world;
  for (auto& page : world.chunks()) {
    auto passable = page.template field_span<PassableTag>();
    for (auto& tile : passable) {
      tile = true;
    }
    auto cost = page.template field_span<CostTag>();
    for (auto& tile : cost) {
      tile = 1u;
    }
  }
  // Seal the goal for the walker, then turn one wall tile into a site the
  // Builder may enter.
  const tess::Coord3 gap{kUnreachableGoal.x - 1, kUnreachableGoal.y, 0};
  const tess::Coord3 walls[] = {
      gap,
      {kUnreachableGoal.x + 1, kUnreachableGoal.y, 0},
      {kUnreachableGoal.x, kUnreachableGoal.y - 1, 0},
      {kUnreachableGoal.x, kUnreachableGoal.y + 1, 0},
  };
  for (const auto n : walls) {
    world.field<PassableTag>(n) = false;
  }
  world.field<RuntimeConstructionTag>(gap) = 1;

  tess::LocalTopologyScratch local_scratch;
  tess::RegionGraph graph;
  const auto built = tess::build_region_graph<ClassWorld, PassableTag>(
      world, local_scratch, graph);
  ASSERT_EQ(built.status, tess::TopologyStatus::Built);

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, 1);
  const auto ticket = runtime.submit({kStart, kUnreachableGoal});
  (void)runtime.process_unit_cached<ClassWorld, RuntimeBuilder>(world, {},
                                                                &graph);
  // Nothing ruled out (wrong-class graph reads GraphStale), and the Builder
  // route through the site is found by the search itself.
  EXPECT_EQ(runtime.stats().precheck_ruled_out, 0u);
  EXPECT_EQ(runtime.result(ticket).status, tess::PathStatus::Found);

  // A graph stamped FOR the Builder does rule out a genuinely sealed goal.
  world.field<RuntimeConstructionTag>(gap) = 0;
  world.mark_topology_rebuilt(tess::chunk_key<Grid>(tess::tile_key<Grid>(gap)));
  const auto rebuilt = tess::build_region_graph<ClassWorld, RuntimeBuilder>(
      world, local_scratch, graph);
  ASSERT_EQ(rebuilt.status, tess::TopologyStatus::Built);
  const auto sealed_ticket = runtime.submit({kStart, kUnreachableGoal});
  (void)runtime.process_unit_cached<ClassWorld, RuntimeBuilder>(world, {},
                                                                &graph);
  // The queue still holds both requests; the matched-class graph rules out
  // each of them without a search.
  EXPECT_EQ(runtime.stats().precheck_ruled_out, 2u);
  EXPECT_EQ(runtime.result(sealed_ticket).status, tess::PathStatus::NoPath);
  EXPECT_EQ(runtime.result(ticket).status, tess::PathStatus::NoPath);
}
