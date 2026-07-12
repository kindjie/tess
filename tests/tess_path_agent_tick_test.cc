#include <gtest/gtest.h>
#include <tess/tess.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "allocation_counter.h"

namespace {

struct PassableTag {};
struct CostTag {};
struct OccupancyTag {};
struct ReservationTag {};

using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                 tess::Field<CostTag, std::uint32_t>>;
using Runtime2D = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{8, 8, 1}>;
using World = tess::AlwaysResidentWorld<Runtime2D, Schema>;
using MovementSchema = tess::FieldSchema<
    tess::Field<PassableTag, bool>, tess::Field<CostTag, std::uint32_t>,
    tess::Field<OccupancyTag, bool>, tess::Field<ReservationTag, bool>>;
using MovementWorld = tess::AlwaysResidentWorld<Runtime2D, MovementSchema>;
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

void fill_movement_world(MovementWorld& world) {
  for (auto& page : world.chunks()) {
    auto passable = page.template field_span<PassableTag>();
    auto cost = page.template field_span<CostTag>();
    auto occupancy = page.template field_span<OccupancyTag>();
    auto reservations = page.template field_span<ReservationTag>();
    for (std::size_t i = 0; i < passable.size(); ++i) {
      passable[i] = true;
      cost[i] = 1u;
      occupancy[i] = false;
      reservations[i] = false;
    }
  }
}

void mark_movement_passable(MovementWorld& world, tess::Coord3 coord,
                            bool passable) {
  world.template field<PassableTag>(coord) = passable;
  world.mark_dirty(tess::chunk_key<Runtime2D>(tess::tile_key<Runtime2D>(coord)),
                   1u, tess::Box3{coord, tess::Extent3{1, 1, 1}});
}

auto tick_movement(tess::PathAgentTickState& tick_state, MovementWorld& world,
                   std::span<tess::PathAgentState> agents,
                   tess::PathRequestRuntime& runtime,
                   tess::PathAgentTickOptions options = {})
    -> tess::PathAgentTickStats {
  return tess::tick_unit_path_agents_with_movement<
      MovementWorld, PassableTag, OccupancyTag, ReservationTag>(
      tick_state, world, agents, runtime, options);
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

TEST(TessPathAgentTick, WorldEditRequiresExplicitDirtyMark) {
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
  stats = tess::tick_unit_path_agents<World, PassableTag>(tick_state, world,
                                                          agents, runtime);
  EXPECT_FALSE(stats.processed_paths);
  EXPECT_EQ(agents[0].position, (tess::Coord3{2, 0, 0}));
}

TEST(TessPathAgentTick, TickGoalAssignmentSchedulesProcessing) {
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

// One agent re-arming its goal must replan ONLY itself: the other agent's
// retained route keeps advancing untouched through the selective
// (NeedsOnly) pass (per-agent pathing dirt; the S11.4 soak observation was
// one goal re-arm replanning the whole batch every tick).
TEST(TessPathAgentTick, GoalRearmReplansOnlyThatAgent) {
  World world;
  fill_world(world);

  std::array<tess::PathAgentState, 2> agents{{
      {.position = tess::Coord3{0, 0, 0}},
      {.position = tess::Coord3{0, 10, 0}},
  }};
  tess::set_path_agent_goal(agents[0], tess::Coord3{5, 0, 0});
  tess::set_path_agent_goal(agents[1], tess::Coord3{5, 10, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::PathAgentTickState tick_state;

  auto stats = tess::tick_unit_path_agents<World, PassableTag>(
      tick_state, world, agents, runtime);
  ASSERT_EQ(stats.pathing.submitted, 2u);
  ASSERT_EQ(agents[0].position, (tess::Coord3{1, 0, 0}));

  // Re-arm agent 1 every remaining tick; agent 0 must still walk its
  // retained route one tile per tick and arrive on schedule.
  for (std::int64_t tick = 2; tick <= 5; ++tick) {
    tess::set_path_agent_goal(tick_state, agents[1],
                              tess::Coord3{5, 10 + tick, 0});
    stats = tess::tick_unit_path_agents<World, PassableTag>(tick_state, world,
                                                            agents, runtime);
    EXPECT_TRUE(stats.processed_paths);
    EXPECT_EQ(stats.pathing.submitted, 1u);  // only the re-armed agent
    EXPECT_EQ(agents[0].position, (tess::Coord3{tick, 0, 0}));
  }
  EXPECT_FALSE(agents[0].has_goal);  // arrived at x=5 on tick 5
  EXPECT_EQ(stats.movement.arrived, 1u);
}

// mark_pathing_dirty stays world-scoped: after it, EVERY agent replans.
TEST(TessPathAgentTick, WorldDirtyMarkReplansEveryAgent) {
  World world;
  fill_world(world);

  std::array<tess::PathAgentState, 2> agents{{
      {.position = tess::Coord3{0, 0, 0}},
      {.position = tess::Coord3{0, 10, 0}},
  }};
  tess::set_path_agent_goal(agents[0], tess::Coord3{7, 0, 0});
  tess::set_path_agent_goal(agents[1], tess::Coord3{7, 10, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::PathAgentTickState tick_state;

  auto stats = tess::tick_unit_path_agents<World, PassableTag>(
      tick_state, world, agents, runtime);
  ASSERT_EQ(stats.pathing.submitted, 2u);

  tess::mark_pathing_dirty(tick_state);
  stats = tess::tick_unit_path_agents<World, PassableTag>(tick_state, world,
                                                          agents, runtime);
  EXPECT_TRUE(stats.processed_paths);
  EXPECT_EQ(stats.pathing.submitted, 2u);
}

// Steady goal-churn ticks must stay allocation-free once the runtime and
// the per-agent route vectors are warm (the retained-route pool reuses
// capacity across replans).
TEST(TessPathAgentTick, WarmGoalChurnTicksAreAllocationFree) {
  World world;
  fill_world(world);

  std::array<tess::PathAgentState, 2> agents{{
      {.position = tess::Coord3{0, 0, 0}},
      {.position = tess::Coord3{0, 10, 0}},
  }};
  tess::set_path_agent_goal(agents[0], tess::Coord3{20, 0, 0});
  tess::set_path_agent_goal(agents[1], tess::Coord3{8, 10, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::PathAgentTickState tick_state;

  // Warm: full pass + one re-arm cycle of each churn goal.
  (void)tess::tick_unit_path_agents<World, PassableTag>(tick_state, world,
                                                        agents, runtime);
  for (int warm = 0; warm < 2; ++warm) {
    tess::set_path_agent_goal(tick_state, agents[1],
                              tess::Coord3{8, warm % 2 == 0 ? 12 : 10, 0});
    (void)tess::tick_unit_path_agents<World, PassableTag>(tick_state, world,
                                                          agents, runtime);
  }

  tess_test::ScopedAllocationCounter counter;
  for (int tick = 0; tick < 4; ++tick) {
    tess::set_path_agent_goal(tick_state, agents[1],
                              tess::Coord3{8, tick % 2 == 0 ? 12 : 10, 0});
    (void)tess::tick_unit_path_agents<World, PassableTag>(tick_state, world,
                                                          agents, runtime);
  }
  EXPECT_EQ(counter.count(), 0u);
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

TEST(TessPathAgentTick, PlainGoalAssignmentIsProcessedWithoutDirtyMark) {
  World world;
  fill_world(world);

  std::array<tess::PathAgentState, 1> agents{{
      {.position = tess::Coord3{0, 0, 0}},
  }};
  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::PathAgentTickState tick_state;

  // Consume the initial dirty flag with no goals assigned.
  auto stats = tess::tick_unit_path_agents<World, PassableTag>(
      tick_state, world, agents, runtime);
  ASSERT_EQ(stats.pathing.submitted, 0u);

  // The two-argument goal overload never touches tick state; the next tick
  // must still pick the agent up without a manual dirty mark.
  tess::set_path_agent_goal(agents[0], tess::Coord3{3, 0, 0});
  stats = tess::tick_unit_path_agents<World, PassableTag>(tick_state, world,
                                                          agents, runtime);
  EXPECT_TRUE(stats.processed_paths);
  EXPECT_EQ(stats.pathing.found, 1u);
  EXPECT_EQ(stats.movement.advanced, 1u);
  EXPECT_EQ(agents[0].position, (tess::Coord3{1, 0, 0}));
}

TEST(TessPathAgentTick, TransientlyBlockedAgentResumesAndArrives) {
  MovementWorld world;
  fill_movement_world(world);

  // The mover's route crosses the blocker's start tile; the blocker walks
  // off that tile on the same ticks, so the block is transient.
  std::array<tess::PathAgentState, 2> agents{{
      {.position = tess::Coord3{0, 0, 0}},
      {.position = tess::Coord3{1, 0, 0}},
  }};
  world.template field<OccupancyTag>(agents[0].position) = true;
  world.template field<OccupancyTag>(agents[1].position) = true;
  tess::set_path_agent_goal(agents[0], tess::Coord3{3, 0, 0});
  tess::set_path_agent_goal(agents[1], tess::Coord3{1, 2, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::PathAgentTickState tick_state;

  const auto first = tick_movement(tick_state, world, agents, runtime);
  EXPECT_EQ(first.movement.movement_failures.occupied, 1u);
  EXPECT_EQ(first.movement.blocked_waits, 1u);
  EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::Blocked);
  EXPECT_EQ(agents[0].status, tess::PathStatus::Found);
  EXPECT_EQ(agents[0].position, (tess::Coord3{0, 0, 0}));
  EXPECT_EQ(agents[1].position, (tess::Coord3{1, 1, 0}));

  const auto second = tick_movement(tick_state, world, agents, runtime);
  EXPECT_TRUE(second.processed_paths);
  EXPECT_EQ(second.repaths_requested, 1u);
  EXPECT_EQ(agents[0].position, (tess::Coord3{1, 0, 0}));

  for (int tick = 0; tick < 8 && agents[0].has_goal; ++tick) {
    (void)tick_movement(tick_state, world, agents, runtime);
  }

  EXPECT_FALSE(agents[0].has_goal);
  EXPECT_EQ(agents[0].position, (tess::Coord3{3, 0, 0}));
  EXPECT_FALSE(agents[1].has_goal);
  EXPECT_EQ(agents[1].position, (tess::Coord3{1, 2, 0}));
}

TEST(TessPathAgentTick, MovementBlockGetsFullRepathBudget) {
  MovementWorld world;
  fill_movement_world(world);

  // A permanently parked blocker occupies the mover's next tile.
  // Occupancy is not planning passability, so every re-path keeps
  // planning Found through the occupied tile.
  std::array<tess::PathAgentState, 1> agents{{
      {.position = tess::Coord3{0, 0, 0}},
  }};
  world.template field<OccupancyTag>(agents[0].position) = true;
  world.template field<OccupancyTag>(tess::Coord3{1, 0, 0}) = true;
  tess::set_path_agent_goal(agents[0], tess::Coord3{2, 0, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::PathAgentTickState tick_state;
  const auto options = tess::PathAgentTickOptions{.max_blocked_retries = 1};

  auto stats = tick_movement(tick_state, world, agents, runtime, options);
  EXPECT_EQ(stats.movement.blocked_waits, 1u);
  EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::Blocked);
  // A blocked movement tick must not consume re-path budget itself...
  EXPECT_EQ(agents[0].blocked_retries, 0u);

  // ...so the next processed tick still gets the single budgeted
  // re-path attempt instead of going terminally Unreachable unheard.
  stats = tick_movement(tick_state, world, agents, runtime, options);
  EXPECT_TRUE(stats.processed_paths);
  EXPECT_EQ(stats.repaths_requested, 1u);
  EXPECT_EQ(stats.repath_exhausted, 0u);
  EXPECT_NE(agents[0].phase, tess::PathAgentPhase::Unreachable);

  // Every Found re-path resets the budget, so a permanent blocker keeps
  // the agent re-pathing indefinitely by design; it never exhausts.
  for (int tick = 0; tick < 8; ++tick) {
    stats = tick_movement(tick_state, world, agents, runtime, options);
    EXPECT_EQ(stats.repaths_requested, 1u);
    EXPECT_EQ(stats.repath_exhausted, 0u);
  }
  EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::Blocked);
  EXPECT_EQ(agents[0].position, (tess::Coord3{0, 0, 0}));
}

TEST(TessPathAgentTick, WallInsertedMidRouteRepathsAroundAndArrives) {
  MovementWorld world;
  fill_movement_world(world);

  std::array<tess::PathAgentState, 1> agents{{
      {.position = tess::Coord3{0, 0, 0}},
  }};
  world.template field<OccupancyTag>(agents[0].position) = true;
  tess::set_path_agent_goal(agents[0], tess::Coord3{4, 0, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::PathAgentTickState tick_state;

  auto stats = tick_movement(tick_state, world, agents, runtime);
  ASSERT_EQ(agents[0].position, (tess::Coord3{1, 0, 0}));

  // The wall lands on the cached route. Even without a manual dirty mark
  // the blocked step must trigger a re-path on the next tick.
  mark_movement_passable(world, tess::Coord3{2, 0, 0}, false);
  stats = tick_movement(tick_state, world, agents, runtime);
  EXPECT_EQ(stats.movement.movement_failures.blocked, 1u);
  EXPECT_EQ(stats.movement.blocked_waits, 1u);
  EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::Blocked);
  EXPECT_EQ(agents[0].position, (tess::Coord3{1, 0, 0}));

  stats = tick_movement(tick_state, world, agents, runtime);
  EXPECT_TRUE(stats.processed_paths);
  EXPECT_EQ(stats.repaths_requested, 1u);
  EXPECT_NE(agents[0].position, (tess::Coord3{1, 0, 0}));

  for (int tick = 0; tick < 12 && agents[0].has_goal; ++tick) {
    (void)tick_movement(tick_state, world, agents, runtime);
  }
  EXPECT_FALSE(agents[0].has_goal);
  EXPECT_EQ(agents[0].position, (tess::Coord3{4, 0, 0}));
  EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::Idle);
}

TEST(TessPathAgentTick, BoxedInGoalExhaustsRepathsAndStopsProcessing) {
  MovementWorld world;
  fill_movement_world(world);

  std::array<tess::PathAgentState, 1> agents{{
      {.position = tess::Coord3{0, 0, 0}},
  }};
  world.template field<OccupancyTag>(agents[0].position) = true;
  tess::set_path_agent_goal(agents[0], tess::Coord3{5, 0, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::PathAgentTickState tick_state;
  const auto options = tess::PathAgentTickOptions{.max_blocked_retries = 3};

  auto stats = tick_movement(tick_state, world, agents, runtime, options);
  ASSERT_EQ(agents[0].position, (tess::Coord3{1, 0, 0}));

  // Box the goal in completely and mark pathing dirty like ops would.
  mark_movement_passable(world, tess::Coord3{4, 0, 0}, false);
  mark_movement_passable(world, tess::Coord3{6, 0, 0}, false);
  mark_movement_passable(world, tess::Coord3{5, 1, 0}, false);
  tess::mark_pathing_dirty(tick_state);

  stats = tick_movement(tick_state, world, agents, runtime, options);
  ASSERT_TRUE(stats.processed_paths);
  ASSERT_EQ(stats.pathing.no_path, 1u);
  EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::Blocked);

  std::size_t processed_ticks = 0;
  std::size_t repaths_total = 0;
  std::size_t exhausted_total = 0;
  for (int tick = 0; tick < 10; ++tick) {
    stats = tick_movement(tick_state, world, agents, runtime, options);
    processed_ticks += stats.processed_paths ? 1u : 0u;
    repaths_total += stats.repaths_requested;
    exhausted_total += stats.repath_exhausted;
  }

  // Retries are bounded: three re-path attempts, then the agent becomes
  // terminally unreachable and stops consuming processing entirely.
  EXPECT_EQ(processed_ticks, 3u);
  EXPECT_EQ(repaths_total, 3u);
  EXPECT_EQ(exhausted_total, 1u);
  EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::Unreachable);
  EXPECT_EQ(agents[0].status, tess::PathStatus::NoPath);
  EXPECT_TRUE(agents[0].has_goal);
  EXPECT_FALSE(stats.processed_paths);
  EXPECT_EQ(stats.repaths_requested, 0u);
  EXPECT_EQ(agents[0].position, (tess::Coord3{1, 0, 0}));

  // A fresh goal re-arms processing without any manual dirty mark.
  tess::set_path_agent_goal(agents[0], tess::Coord3{1, 2, 0});
  stats = tick_movement(tick_state, world, agents, runtime, options);
  EXPECT_TRUE(stats.processed_paths);
  EXPECT_EQ(agents[0].position, (tess::Coord3{1, 1, 0}));
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

  tess_test::ScopedAllocationCounter counter;
  const auto stats = tess::tick_unit_path_agents<World, PassableTag>(
      tick_state, world, agents, runtime);

  // The warm clean tick must skip path processing yet still advance every
  // agent along its cached route (an early-return no-op would also count
  // zero allocations), and do so allocation-free.
  EXPECT_FALSE(stats.processed_paths);
  EXPECT_EQ(stats.movement.advanced, agents.size());
  EXPECT_EQ(counter.count(), 0u);
}

}  // namespace
