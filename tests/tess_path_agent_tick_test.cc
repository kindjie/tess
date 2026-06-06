#include <gtest/gtest.h>
#include <tess/tess.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

namespace {

std::atomic<bool> count_allocations{false};
std::atomic<int> allocation_count{0};

}  // namespace

void* operator new(std::size_t size) {
  if (count_allocations.load(std::memory_order_relaxed)) {
    allocation_count.fetch_add(1, std::memory_order_relaxed);
  }
  if (void* ptr = std::malloc(size)) {
    return ptr;
  }
  throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
  if (count_allocations.load(std::memory_order_relaxed)) {
    allocation_count.fetch_add(1, std::memory_order_relaxed);
  }
  if (void* ptr = std::malloc(size)) {
    return ptr;
  }
  throw std::bad_alloc();
}

void operator delete(void* ptr) noexcept { std::free(ptr); }

void operator delete[](void* ptr) noexcept { std::free(ptr); }

void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }

void operator delete[](void* ptr, std::size_t) noexcept { std::free(ptr); }

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

void reserve_runtime(tess::PathRequestRuntime& runtime,
                     std::size_t request_count) {
  runtime.reserve_requests(request_count);
  runtime.reserve_search_nodes(RuntimeTileCount);
  runtime.reserve_path_nodes(8192);
  runtime.reserve_unit_routes(request_count);
}

void mark_passable(World& world, tess::Coord3 coord, bool passable) {
  world.template field<PassableTag>(coord) = passable;
  world.mark_dirty(tess::chunk_key<Runtime2D>(tess::tile_key<Runtime2D>(coord)),
                   1u, tess::Box3{coord, tess::Extent3{1, 1, 1}});
}

TEST(TessPathAgentTick, UnitTicksProcessOnceThenAdvanceUntilArrival) {
  World world;
  fill_world(world);

  std::array<tess::PathAgentState, 1> agents{{
      {.position = tess::Coord3{0, 0, 0}},
  }};
  tess::set_path_agent_goal(agents[0], tess::Coord3{3, 0, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::PathAgentTickState tick_state;

  auto stats = tess::tick_unit_path_agents<World, PassableTag>(
      tick_state, world, agents, runtime);
  EXPECT_EQ(stats.tick, 1u);
  EXPECT_TRUE(stats.processed_paths);
  EXPECT_EQ(stats.pathing.found, 1u);
  EXPECT_EQ(stats.movement.advanced, 1u);
  EXPECT_EQ(agents[0].position, (tess::Coord3{1, 0, 0}));

  stats = tess::tick_unit_path_agents<World, PassableTag>(tick_state, world,
                                                          agents, runtime);
  EXPECT_EQ(stats.tick, 2u);
  EXPECT_FALSE(stats.processed_paths);
  EXPECT_EQ(stats.movement.advanced, 1u);

  stats = tess::tick_unit_path_agents<World, PassableTag>(tick_state, world,
                                                          agents, runtime);
  EXPECT_EQ(stats.tick, 3u);
  EXPECT_EQ(stats.movement.arrived, 1u);
  EXPECT_FALSE(agents[0].has_goal);
}

TEST(TessPathAgentTick, DirtyPathingReprocessesBeforeMovement) {
  World world;
  fill_world(world);

  std::array<tess::PathAgentState, 1> agents{{
      {.position = tess::Coord3{0, 0, 0}},
  }};
  tess::set_path_agent_goal(agents[0], tess::Coord3{7, 0, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::PathAgentTickState tick_state;

  auto stats = tess::tick_unit_path_agents<World, PassableTag>(
      tick_state, world, agents, runtime);
  ASSERT_TRUE(stats.processed_paths);
  ASSERT_EQ(agents[0].position, (tess::Coord3{1, 0, 0}));

  mark_passable(world, tess::Coord3{2, 0, 0}, false);
  tess::mark_pathing_dirty(tick_state);
  stats = tess::tick_unit_path_agents<World, PassableTag>(tick_state, world,
                                                          agents, runtime);
  EXPECT_TRUE(stats.processed_paths);
  EXPECT_EQ(runtime.stats().world_cache_invalidations, 1u);
  EXPECT_NE(agents[0].position, (tess::Coord3{2, 0, 0}));
}

TEST(TessPathAgentTick, TickGoalAssignmentMarksPathingDirty) {
  World world;
  fill_world(world);

  std::array<tess::PathAgentState, 1> agents{{
      {.position = tess::Coord3{0, 0, 0}},
  }};
  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::PathAgentTickState tick_state;

  tess::set_path_agent_goal(tick_state, agents[0], tess::Coord3{2, 0, 0});
  auto stats = tess::tick_unit_path_agents<World, PassableTag>(
      tick_state, world, agents, runtime);
  ASSERT_TRUE(stats.processed_paths);
  ASSERT_EQ(agents[0].position, (tess::Coord3{1, 0, 0}));

  tess::set_path_agent_goal(tick_state, agents[0], tess::Coord3{0, 0, 0});
  stats = tess::tick_unit_path_agents<World, PassableTag>(tick_state, world,
                                                          agents, runtime);
  EXPECT_TRUE(stats.processed_paths);
  EXPECT_EQ(agents[0].position, (tess::Coord3{0, 0, 0}));
}

TEST(TessPathAgentTick, FailedPathsDoNotMove) {
  World world;
  fill_world(world);
  for (std::int64_t y = 0; y < 32; ++y) {
    mark_passable(world, tess::Coord3{1, y, 0}, false);
  }

  std::array<tess::PathAgentState, 1> agents{{
      {.position = tess::Coord3{0, 0, 0}},
  }};
  tess::set_path_agent_goal(agents[0], tess::Coord3{2, 0, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::PathAgentTickState tick_state;

  const auto stats = tess::tick_unit_path_agents<World, PassableTag>(
      tick_state, world, agents, runtime);
  EXPECT_TRUE(stats.processed_paths);
  EXPECT_EQ(stats.pathing.no_path, 1u);
  EXPECT_EQ(stats.movement.advanced, 0u);
  EXPECT_EQ(agents[0].position, (tess::Coord3{0, 0, 0}));
}

TEST(TessPathAgentTick, WeightedTickProcessesSharedGoalAndAdvances) {
  World world;
  fill_world(world);

  std::array<tess::PathAgentState, 4> agents{};
  for (std::size_t i = 0; i < agents.size(); ++i) {
    agents[i].position = tess::Coord3{0, static_cast<std::int64_t>(i), 0};
    tess::set_path_agent_goal(agents[i], tess::Coord3{31, 31, 0});
  }

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::PathAgentTickState tick_state;

  const auto stats =
      tess::tick_weighted_path_agents<World, PassableTag, CostTag, 8>(
          tick_state, world, agents, runtime);
  EXPECT_TRUE(stats.processed_paths);
  EXPECT_EQ(stats.pathing.found, agents.size());
  EXPECT_EQ(stats.movement.advanced, agents.size());
  EXPECT_EQ(runtime.stats().weighted_batch.unique_goals, 1u);
}

TEST(TessPathAgentTick, WarmUnitTickWithoutDirtyPathingDoesNotAllocate) {
  World world;
  fill_world(world);

  std::array<tess::PathAgentState, 8> agents{};
  for (std::size_t i = 0; i < agents.size(); ++i) {
    agents[i].position = tess::Coord3{static_cast<std::int64_t>(i), 0, 0};
    tess::set_path_agent_goal(agents[i], tess::Coord3{15, 0, 0});
  }

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::PathAgentTickState tick_state;
  (void)tess::tick_unit_path_agents<World, PassableTag>(tick_state, world,
                                                        agents, runtime);

  allocation_count.store(0, std::memory_order_relaxed);
  count_allocations.store(true, std::memory_order_relaxed);
  (void)tess::tick_unit_path_agents<World, PassableTag>(tick_state, world,
                                                        agents, runtime);
  count_allocations.store(false, std::memory_order_relaxed);

  EXPECT_EQ(allocation_count.load(std::memory_order_relaxed), 0);
}

}  // namespace
