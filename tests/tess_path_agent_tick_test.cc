#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <thread>
#include <vector>

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

TEST(TessPathAgentTick, PlainMovementProgressResetsBlockedRetryStreak) {
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
  ASSERT_EQ(stats.movement.advanced, 1u);

  // A successful replan after a transient movement block deliberately
  // preserves the streak until actual movement proves forward progress.
  agents[0].blocked_retries = 3;
  stats = tess::tick_unit_path_agents<World, PassableTag>(tick_state, world,
                                                          agents, runtime);

  ASSERT_EQ(stats.movement.advanced, 1u);
  EXPECT_EQ(agents[0].blocked_retries, 0u);
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
  EXPECT_FALSE(second.processed_paths);
  EXPECT_EQ(second.repaths_requested, 0u);
  EXPECT_EQ(agents[0].position, (tess::Coord3{1, 0, 0}));

  for (int tick = 0; tick < 8 && agents[0].has_goal; ++tick) {
    (void)tick_movement(tick_state, world, agents, runtime);
  }

  EXPECT_FALSE(agents[0].has_goal);
  EXPECT_EQ(agents[0].position, (tess::Coord3{3, 0, 0}));
  EXPECT_FALSE(agents[1].has_goal);
  EXPECT_EQ(agents[1].position, (tess::Coord3{1, 2, 0}));
}

TEST(TessPathAgentTick, PermanentOccupancyWaitIsBoundedWithoutReplanning) {
  MovementWorld world;
  fill_movement_world(world);

  // A permanently parked blocker occupies the mover's next tile.
  // Occupancy is not planning passability, so planning again cannot improve
  // this route. The retained step should be retried without another search.
  std::array<tess::PathAgentState, 1> agents{{
      {.position = tess::Coord3{0, 0, 0}},
  }};
  world.template field<OccupancyTag>(agents[0].position) = true;
  world.template field<OccupancyTag>(tess::Coord3{1, 0, 0}) = true;
  tess::set_path_agent_goal(agents[0], tess::Coord3{2, 0, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::PathAgentTickState tick_state;
  const auto options = tess::PathAgentTickOptions{
      .max_blocked_retries = 1,
      .blocked_exhaustion_policy =
          tess::BlockedAgentExhaustionPolicy::MarkUnreachable,
  };

  auto stats = tick_movement(tick_state, world, agents, runtime, options);
  EXPECT_EQ(stats.movement.blocked_waits, 1u);
  EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::Blocked);
  // The initial blocked movement records the condition; retry accounting
  // starts on the following tick.
  EXPECT_EQ(agents[0].blocked_retries, 0u);

  // The one budgeted wait retries the retained step without path processing.
  stats = tick_movement(tick_state, world, agents, runtime, options);
  EXPECT_FALSE(stats.processed_paths);
  EXPECT_EQ(stats.repaths_requested, 0u);
  EXPECT_EQ(stats.repath_exhausted, 0u);
  EXPECT_NE(agents[0].phase, tess::PathAgentPhase::Unreachable);

  // The next tick exhausts the consecutive-block budget and becomes an
  // explicit terminal outcome instead of an infinite plan/block cycle.
  stats = tick_movement(tick_state, world, agents, runtime, options);
  EXPECT_FALSE(stats.processed_paths);
  EXPECT_EQ(stats.repath_exhausted, 1u);
  EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::Unreachable);
  EXPECT_EQ(agents[0].position, (tess::Coord3{0, 0, 0}));
}

TEST(TessPathAgentTick, RemainBlockedExhaustionDoesNotInventNoPath) {
  MovementWorld world;
  fill_movement_world(world);

  std::array<tess::PathAgentState, 1> agents{{
      {.position = tess::Coord3{0, 0, 0}},
  }};
  world.template field<OccupancyTag>(agents[0].position) = true;
  const auto occupied = tess::Coord3{1, 0, 0};
  world.template field<OccupancyTag>(occupied) = true;
  tess::set_path_agent_goal(agents[0], tess::Coord3{2, 0, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::PathAgentTickState tick_state;
  // RemainBlocked is the default: exhausting a time budget is not evidence
  // that an otherwise valid goal is unreachable.
  const auto options = tess::PathAgentTickOptions{.max_blocked_retries = 1};

  for (int tick = 0; tick < 8; ++tick) {
    const auto stats =
        tick_movement(tick_state, world, agents, runtime, options);
    EXPECT_EQ(stats.repath_exhausted, 0u);
  }

  EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::Blocked);
  EXPECT_EQ(agents[0].status, tess::PathStatus::Found);
  EXPECT_EQ(agents[0].blocked_retries, 1u);

  world.template field<OccupancyTag>(occupied) = false;
  auto stats = tick_movement(tick_state, world, agents, runtime, options);
  EXPECT_EQ(stats.movement.advanced, 1u);
  EXPECT_EQ(agents[0].position, occupied);
  EXPECT_EQ(agents[0].blocked_retries, 0u);

  stats = tick_movement(tick_state, world, agents, runtime, options);
  EXPECT_EQ(stats.movement.arrived, 1u);
  EXPECT_FALSE(agents[0].has_goal);
}

TEST(TessPathAgentTick, RemainBlockedNoPathSleepsUntilWorldChanges) {
  MovementWorld world;
  fill_movement_world(world);

  std::array<tess::PathAgentState, 1> agents{{
      {.position = tess::Coord3{0, 0, 0}},
  }};
  world.template field<OccupancyTag>(agents[0].position) = true;
  const auto goal = tess::Coord3{2, 0, 0};
  mark_movement_passable(world, tess::Coord3{1, 0, 0}, false);
  mark_movement_passable(world, tess::Coord3{3, 0, 0}, false);
  mark_movement_passable(world, tess::Coord3{2, 1, 0}, false);
  tess::set_path_agent_goal(agents[0], goal);

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::PathAgentTickState tick_state;
  const auto options = tess::PathAgentTickOptions{
      .max_blocked_retries = 1,
      .blocked_exhaustion_policy =
          tess::BlockedAgentExhaustionPolicy::RemainBlocked,
  };

  auto stats = tick_movement(tick_state, world, agents, runtime, options);
  ASSERT_TRUE(stats.processed_paths);
  ASSERT_EQ(stats.pathing.no_path, 1u);

  stats = tick_movement(tick_state, world, agents, runtime, options);
  ASSERT_TRUE(stats.processed_paths);
  stats = tick_movement(tick_state, world, agents, runtime, options);
  EXPECT_FALSE(stats.processed_paths);
  EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::Blocked);
  EXPECT_EQ(agents[0].status, tess::PathStatus::NoPath);

  for (int tick = 0; tick < 4; ++tick) {
    stats = tick_movement(tick_state, world, agents, runtime, options);
    EXPECT_FALSE(stats.processed_paths);
    EXPECT_EQ(stats.repath_exhausted, 0u);
  }

  mark_movement_passable(world, tess::Coord3{1, 0, 0}, true);
  tess::mark_pathing_dirty(tick_state);
  stats = tick_movement(tick_state, world, agents, runtime, options);
  EXPECT_TRUE(stats.processed_paths);
  EXPECT_EQ(stats.pathing.found, 1u);
  EXPECT_EQ(stats.movement.advanced, 1u);
  EXPECT_EQ(agents[0].position, (tess::Coord3{1, 0, 0}));
}

TEST(TessPathAgentTick, RecoveryScheduleIsDeterministicAndBoundedPerTick) {
  constexpr std::size_t AgentCount = 64;
  constexpr std::size_t PerTickBudget = 5;
  std::array<tess::PathAgentState, AgentCount> agents{};
  for (auto& agent : agents) {
    agent.has_goal = true;
    agent.phase = tess::PathAgentPhase::Blocked;
    agent.status = tess::PathStatus::Found;
  }

  const auto options = tess::BlockedAgentRecoveryOptions{
      .initial_delay_ticks = 4,
      .max_delay_ticks = 32,
      .max_probes_per_tick = PerTickBudget,
      .jitter_seed = 0x12345678u,
  };
  tess::BlockedAgentRecoverySchedule first;
  tess::BlockedAgentRecoverySchedule second;
  first.reserve(AgentCount);
  second.reserve(AgentCount);
  std::array<std::size_t, AgentCount> attempts{};

  for (std::uint64_t tick = 1; tick <= 20; ++tick) {
    const auto first_stats = first.collect_due(agents, tick, options);
    const auto second_stats = second.collect_due(agents, tick, options);
    EXPECT_EQ(first_stats.selected, first.due_agent_indices().size());
    EXPECT_EQ(first_stats.selected, second_stats.selected);
    const auto first_due = first.due_agent_indices();
    const auto second_due = second.due_agent_indices();
    EXPECT_TRUE(std::equal(first_due.begin(), first_due.end(),
                           second_due.begin(), second_due.end()));
    EXPECT_LE(first_stats.selected, PerTickBudget);
    EXPECT_EQ(first_stats.due, first_stats.selected + first_stats.deferred);

    for (const auto index : first.due_agent_indices()) {
      ++attempts[index];
      first.record_attempt(index, tick, options);
    }
    for (const auto index : second.due_agent_indices()) {
      second.record_attempt(index, tick, options);
    }
  }

  for (const auto count : attempts) {
    EXPECT_GE(count, 1u);
  }
}

TEST(TessPathAgentTick, RecoveryScheduleResetsAfterProgress) {
  std::array<tess::PathAgentState, 1> agents{{
      {
          .status = tess::PathStatus::Found,
          .phase = tess::PathAgentPhase::Blocked,
          .has_goal = true,
      },
  }};
  const auto options = tess::BlockedAgentRecoveryOptions{
      .initial_delay_ticks = 2,
      .max_delay_ticks = 8,
      .max_probes_per_tick = 1,
      .jitter_seed = 7,
  };
  tess::BlockedAgentRecoverySchedule schedule;

  auto selected_tick = std::uint64_t{0};
  for (std::uint64_t tick = 1; tick <= 4; ++tick) {
    (void)schedule.collect_due(agents, tick, options);
    if (!schedule.due_agent_indices().empty()) {
      selected_tick = tick;
      schedule.record_attempt(0, tick, options);
      break;
    }
  }
  ASSERT_NE(selected_tick, 0u);

  agents[0].phase = tess::PathAgentPhase::Following;
  (void)schedule.collect_due(agents, selected_tick + 1, options);
  EXPECT_TRUE(schedule.due_agent_indices().empty());

  agents[0].phase = tess::PathAgentPhase::Blocked;
  (void)schedule.collect_due(agents, selected_tick + 2, options);
  EXPECT_TRUE(schedule.due_agent_indices().empty());
}

TEST(TessPathAgentTick, RecoveryScheduleRestartsAfterBlockedPositionChange) {
  std::array<tess::PathAgentState, 1> agents{{
      {
          .position = {0, 0, 0},
          .status = tess::PathStatus::Found,
          .phase = tess::PathAgentPhase::Blocked,
          .has_goal = true,
      },
  }};
  const auto options = tess::BlockedAgentRecoveryOptions{
      .initial_delay_ticks = 2,
      .max_delay_ticks = 32,
      .max_probes_per_tick = 1,
      .jitter_seed = 7,
  };
  tess::BlockedAgentRecoverySchedule schedule;

  auto tick = std::uint64_t{1};
  for (int attempt = 0; attempt < 6; ++attempt) {
    for (;;) {
      (void)schedule.collect_due(agents, tick, options);
      if (!schedule.due_agent_indices().empty()) {
        schedule.record_attempt(0, tick, options);
        break;
      }
      ++tick;
    }
    ++tick;
  }

  agents[0].position = {1, 0, 0};
  auto selected_after_progress = false;
  for (std::uint32_t elapsed = 0; elapsed <= options.initial_delay_ticks;
       ++elapsed) {
    (void)schedule.collect_due(agents, tick + elapsed, options);
    selected_after_progress |= !schedule.due_agent_indices().empty();
  }
  EXPECT_TRUE(selected_after_progress);
}

TEST(TessPathAgentTick, RecoveryScheduleHandlesMaximumConfiguredDelay) {
  std::array<tess::PathAgentState, 1> agents{{
      {
          .status = tess::PathStatus::Found,
          .phase = tess::PathAgentPhase::Blocked,
          .has_goal = true,
      },
  }};
  constexpr auto MaxDelay = std::numeric_limits<std::uint32_t>::max();
  const auto options = tess::BlockedAgentRecoveryOptions{
      .initial_delay_ticks = MaxDelay,
      .max_delay_ticks = MaxDelay,
      .max_probes_per_tick = 1,
  };
  tess::BlockedAgentRecoverySchedule schedule;

  auto stats = schedule.collect_due(agents, 1, options);
  EXPECT_EQ(stats.blocked, 1u);
  EXPECT_EQ(stats.selected, 0u);

  constexpr auto EarliestDueTick =
      std::uint64_t{1} + (std::uint64_t{MaxDelay} + 1U) / 2U;
  stats = schedule.collect_due(agents, EarliestDueTick - 1U, options);
  EXPECT_EQ(stats.selected, 0u);
  stats = schedule.collect_due(agents, std::uint64_t{1} + MaxDelay, options);
  EXPECT_EQ(stats.selected, 1u);
}

TEST(TessPathAgentTick, RecoveryScheduleSupportsImmediateRetryPolicy) {
  std::array<tess::PathAgentState, 1> agents{{
      {
          .status = tess::PathStatus::Found,
          .phase = tess::PathAgentPhase::Blocked,
          .has_goal = true,
      },
  }};
  const auto options = tess::BlockedAgentRecoveryOptions{
      .initial_delay_ticks = 0,
      .max_delay_ticks = 32,
      .max_probes_per_tick = 1,
  };
  tess::BlockedAgentRecoverySchedule schedule;

  for (std::uint64_t tick = 1; tick <= 100; ++tick) {
    const auto stats = schedule.collect_due(agents, tick, options);
    ASSERT_EQ(stats.selected, 1u);
    schedule.record_attempt(0, tick, options);
  }
}

TEST(TessPathAgentTick, ReplanQueueDeduplicatesAndPreservesOrder) {
  std::array<tess::PathAgentState, 4> agents{};
  for (std::size_t i = 0; i < 3; ++i) {
    agents[i].has_goal = true;
    agents[i].phase = tess::PathAgentPhase::Following;
  }
  tess::PathAgentReplanQueue queue;
  queue.reserve(agents.size());

  queue.request_all(agents);
  queue.request_all(agents);
  EXPECT_EQ(queue.pending(), 3u);
  ASSERT_EQ(queue.front(), 0u);
  queue.pop_front();
  EXPECT_TRUE(queue.request(0, agents[0]));
  ASSERT_EQ(queue.front(), 1u);
  queue.pop_front();
  ASSERT_EQ(queue.front(), 2u);
  queue.pop_front();
  ASSERT_EQ(queue.front(), 0u);
  queue.pop_front();
  EXPECT_TRUE(queue.empty());
}

TEST(TessPathAgentTick, ReplanQueueBoundsExactPlanningAcrossTicks) {
  World world;
  fill_world(world);
  std::array<tess::PathAgentState, 3> agents{{
      {.position = {0, 0, 0}},
      {.position = {0, 1, 0}},
      {.position = {0, 2, 0}},
  }};
  for (std::size_t i = 0; i < agents.size(); ++i) {
    tess::set_path_agent_goal(agents[i], {7, static_cast<std::int64_t>(i), 0});
  }
  tess::PathAgentRoutes routes;
  routes.ensure_size(agents.size());
  tess::PathAgentReplanQueue queue;
  queue.reserve(agents.size());
  queue.request_all(agents);
  tess::PathScratch scratch;
  scratch.reserve_nodes(RuntimeTileCount);
  const auto options = tess::PathAgentReplanOptions{.max_requests = 2};

  auto stats = tess::process_unit_path_agent_replans<World, PassableTag>(
      world, agents, routes, queue, scratch, options);
  EXPECT_EQ(stats.submitted, 2u);
  EXPECT_EQ(stats.found, 2u);
  EXPECT_EQ(queue.pending(), 1u);
  EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::Following);
  EXPECT_EQ(agents[1].phase, tess::PathAgentPhase::Following);
  EXPECT_EQ(agents[2].phase, tess::PathAgentPhase::NeedsPath);
  EXPECT_FALSE(routes.routes[0].empty());
  EXPECT_FALSE(routes.routes[1].empty());
  EXPECT_TRUE(routes.routes[2].empty());

  stats = tess::process_unit_path_agent_replans<World, PassableTag>(
      world, agents, routes, queue, scratch, options);
  EXPECT_EQ(stats.submitted, 1u);
  EXPECT_EQ(stats.found, 1u);
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(agents[2].phase, tess::PathAgentPhase::Following);
  EXPECT_FALSE(routes.routes[2].empty());
}

TEST(TessPathAgentTick, QueuedReplanPreservesBlockedRetryStreak) {
  World world;
  fill_world(world);
  std::array<tess::PathAgentState, 1> agents{{
      {.position = {0, 0, 0}},
  }};
  tess::set_path_agent_goal(agents[0], {7, 0, 0});
  agents[0].status = tess::PathStatus::Found;
  agents[0].phase = tess::PathAgentPhase::Blocked;
  agents[0].blocked_retries = 3;

  tess::PathAgentRoutes routes;
  routes.ensure_size(agents.size());
  tess::PathAgentReplanQueue queue;
  queue.reserve(agents.size());
  ASSERT_TRUE(queue.request(0, agents[0]));
  tess::PathScratch scratch;
  scratch.reserve_nodes(RuntimeTileCount);

  const auto stats = tess::process_unit_path_agent_replans<World, PassableTag>(
      world, agents, routes, queue, scratch);

  ASSERT_EQ(stats.found, 1u);
  EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::Following);
  EXPECT_EQ(agents[0].blocked_retries, 3u);
}

TEST(TessPathAgentTick, WarmReplanQueueDrainDoesNotAllocate) {
  World world;
  fill_world(world);
  std::array<tess::PathAgentState, 8> agents{};
  tess::PathAgentRoutes routes;
  routes.ensure_size(agents.size());
  for (std::size_t i = 0; i < agents.size(); ++i) {
    agents[i].position = {0, static_cast<std::int64_t>(i), 0};
    tess::set_path_agent_goal(agents[i], {15, static_cast<std::int64_t>(i), 0});
    routes.routes[i].reserve(16);
  }
  tess::PathAgentReplanQueue queue;
  queue.reserve(agents.size());
  tess::PathScratch scratch;
  scratch.reserve_nodes(RuntimeTileCount);
  const auto options = tess::PathAgentReplanOptions{
      .max_requests = agents.size(),
  };
  queue.request_all(agents);
  (void)tess::process_unit_path_agent_replans<World, PassableTag>(
      world, agents, routes, queue, scratch, options);

  tess_test::ScopedAllocationCounter counter;
  queue.request_all(agents);
  const auto stats = tess::process_unit_path_agent_replans<World, PassableTag>(
      world, agents, routes, queue, scratch, options);

  EXPECT_EQ(stats.found, agents.size());
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(counter.count(), 0u);
}

TEST(TessPathAgentTick, WarmContinuouslyPendingReplanQueueDoesNotGrow) {
  std::array<tess::PathAgentState, 8> agents{};
  for (auto& agent : agents) {
    agent.has_goal = true;
    agent.phase = tess::PathAgentPhase::Following;
  }
  tess::PathAgentReplanQueue queue;
  queue.reserve(agents.size());
  queue.request_all(agents);

  tess_test::ScopedAllocationCounter counter;
  for (std::size_t turn = 0; turn < 100; ++turn) {
    const auto pending_index = queue.front();
    if (!pending_index.has_value()) {
      FAIL() << "continuously pending queue became empty";
      return;
    }
    const auto index = pending_index.value();
    queue.pop_front();
    EXPECT_TRUE(queue.request(index, agents[index]));
  }

  EXPECT_EQ(queue.pending(), agents.size());
  EXPECT_EQ(counter.count(), 0u);
}

TEST(TessPathAgentTick, WarmRecoverySchedulePassDoesNotAllocate) {
  constexpr std::size_t AgentCount = 1024;
  std::array<tess::PathAgentState, AgentCount> agents{};
  for (auto& agent : agents) {
    agent.status = tess::PathStatus::Found;
    agent.phase = tess::PathAgentPhase::Blocked;
    agent.has_goal = true;
  }
  const auto options = tess::BlockedAgentRecoveryOptions{
      .initial_delay_ticks = 1,
      .max_delay_ticks = 32,
      .max_probes_per_tick = 8,
  };
  tess::BlockedAgentRecoverySchedule schedule;
  schedule.reserve(AgentCount);
  (void)schedule.collect_due(agents, 1, options);

  tess_test::ScopedAllocationCounter counter;
  const auto stats = schedule.collect_due(agents, 2, options);

  EXPECT_EQ(stats.blocked, AgentCount);
  EXPECT_EQ(stats.selected, options.max_probes_per_tick);
  EXPECT_EQ(counter.count(), 0u);
}

TEST(TessPathAgentTick, IndependentRecoverySchedulesCanRunConcurrently) {
  constexpr std::size_t AgentCount = 128;
  std::array<std::size_t, 2> checksums{};
  const auto run = [&](std::size_t worker) {
    std::array<tess::PathAgentState, AgentCount> agents{};
    for (auto& agent : agents) {
      agent.status = tess::PathStatus::Found;
      agent.phase = tess::PathAgentPhase::Blocked;
      agent.has_goal = true;
    }
    const auto options = tess::BlockedAgentRecoveryOptions{
        .initial_delay_ticks = 1,
        .max_delay_ticks = 16,
        .max_probes_per_tick = 7,
        .jitter_seed = 99,
    };
    tess::BlockedAgentRecoverySchedule schedule;
    schedule.reserve(AgentCount);
    for (std::uint64_t tick = 1; tick <= 64; ++tick) {
      (void)schedule.collect_due(agents, tick, options);
      for (const auto index : schedule.due_agent_indices()) {
        checksums[worker] += index + 1;
        schedule.record_attempt(index, tick, options);
      }
    }
  };

  std::thread first(run, 0);
  std::thread second(run, 1);
  first.join();
  second.join();
  EXPECT_EQ(checksums[0], checksums[1]);
  EXPECT_GT(checksums[0], 0u);
}

TEST(TessPathAgentTick, IndependentReplanQueuesCanRunConcurrently) {
  World world;
  fill_world(world);
  std::array<std::size_t, 2> checksums{};
  const auto run = [&](std::size_t worker) {
    std::array<tess::PathAgentState, 4> agents{};
    for (std::size_t i = 0; i < agents.size(); ++i) {
      agents[i].position = {0, static_cast<std::int64_t>(i), 0};
      tess::set_path_agent_goal(agents[i],
                                {15, static_cast<std::int64_t>(i), 0});
    }
    tess::PathAgentRoutes routes;
    tess::PathAgentReplanQueue queue;
    queue.reserve(agents.size());
    queue.request_all(agents);
    tess::PathScratch scratch;
    scratch.reserve_nodes(RuntimeTileCount);
    const auto stats =
        tess::process_unit_path_agent_replans<World, PassableTag>(
            world, agents, routes, queue, scratch,
            tess::PathAgentReplanOptions{
                .max_requests = agents.size(),
            });
    checksums[worker] = stats.found;
    for (const auto& route : routes.routes) {
      checksums[worker] += route.size();
    }
  };

  std::thread first(run, 0);
  std::thread second(run, 1);
  first.join();
  second.join();
  EXPECT_EQ(checksums[0], checksums[1]);
  EXPECT_GT(checksums[0], 0u);
}

TEST(TessPathAgentTick, PermanentReservationWaitIsBoundedWithoutReplanning) {
  MovementWorld world;
  fill_movement_world(world);

  std::array<tess::PathAgentState, 1> agents{{
      {.position = tess::Coord3{0, 0, 0}},
  }};
  world.template field<OccupancyTag>(agents[0].position) = true;
  world.template field<ReservationTag>(tess::Coord3{1, 0, 0}) = true;
  tess::set_path_agent_goal(agents[0], tess::Coord3{2, 0, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::PathAgentTickState tick_state;
  const auto options = tess::PathAgentTickOptions{
      .max_blocked_retries = 1,
      .blocked_exhaustion_policy =
          tess::BlockedAgentExhaustionPolicy::MarkUnreachable,
  };

  auto stats = tick_movement(tick_state, world, agents, runtime, options);
  EXPECT_EQ(stats.movement.movement_failures.reserved, 1u);
  EXPECT_EQ(stats.movement.blocked_waits, 1u);
  EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::Blocked);
  EXPECT_EQ(agents[0].status, tess::PathStatus::Found);

  stats = tick_movement(tick_state, world, agents, runtime, options);
  EXPECT_FALSE(stats.processed_paths);
  EXPECT_EQ(stats.repaths_requested, 0u);
  EXPECT_EQ(stats.repath_exhausted, 0u);
  EXPECT_EQ(stats.movement.movement_failures.reserved, 1u);

  stats = tick_movement(tick_state, world, agents, runtime, options);
  EXPECT_FALSE(stats.processed_paths);
  EXPECT_EQ(stats.repath_exhausted, 1u);
  EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::Unreachable);
  EXPECT_EQ(agents[0].position, (tess::Coord3{0, 0, 0}));
}

TEST(TessPathAgentTick, ZeroStepTicksPreserveBlockedRetryBudget) {
  MovementWorld world;
  fill_movement_world(world);
  std::array<tess::PathAgentState, 1> agents{{
      {.position = tess::Coord3{0, 0, 0}},
  }};
  world.template field<OccupancyTag>(agents[0].position) = true;
  world.template field<OccupancyTag>(tess::Coord3{1, 0, 0}) = true;
  tess::set_path_agent_goal(agents[0], tess::Coord3{2, 0, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::PathAgentTickState tick_state;
  auto options = tess::PathAgentTickOptions{.max_blocked_retries = 1};
  auto stats = tick_movement(tick_state, world, agents, runtime, options);
  ASSERT_EQ(stats.movement.blocked_waits, 1u);

  options.max_steps = 0;
  for (int tick = 0; tick < 4; ++tick) {
    stats = tick_movement(tick_state, world, agents, runtime, options);
    EXPECT_EQ(stats.repath_exhausted, 0u);
    EXPECT_EQ(agents[0].blocked_retries, 0u);
    EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::Blocked);
  }

  world.template field<OccupancyTag>(tess::Coord3{1, 0, 0}) = false;
  options.max_steps = 1;
  stats = tick_movement(tick_state, world, agents, runtime, options);
  EXPECT_EQ(stats.movement.advanced, 1u);
  EXPECT_EQ(agents[0].position, (tess::Coord3{1, 0, 0}));
}

TEST(TessPathAgentTick, ScopedPassSkipsOccupancyWaitingAgents) {
  MovementWorld world;
  fill_movement_world(world);
  std::array<tess::PathAgentState, 2> agents{{
      {.position = tess::Coord3{0, 0, 0}},
      {.position = tess::Coord3{0, 4, 0}},
  }};
  world.template field<OccupancyTag>(agents[0].position) = true;
  world.template field<OccupancyTag>(agents[1].position) = true;
  world.template field<OccupancyTag>(tess::Coord3{1, 0, 0}) = true;
  tess::set_path_agent_goal(agents[0], tess::Coord3{2, 0, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::PathAgentTickState tick_state;
  auto stats = tick_movement(tick_state, world, agents, runtime);
  ASSERT_EQ(agents[0].phase, tess::PathAgentPhase::Blocked);

  tess::set_path_agent_goal(agents[1], tess::Coord3{2, 4, 0});
  stats = tick_movement(tick_state, world, agents, runtime);
  EXPECT_TRUE(stats.processed_paths);
  EXPECT_EQ(stats.pathing.submitted, 1u);
  EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::Blocked);
  EXPECT_EQ(agents[0].blocked_retries, 1u);
  EXPECT_EQ(agents[1].position, (tess::Coord3{1, 4, 0}));
}

TEST(TessPathAgentTick, BottleneckHasBoundedPlanningAndTerminalOutcomes) {
  MovementWorld world;
  fill_movement_world(world);
  for (std::uint64_t y = 0; y < Runtime2D::size.y; ++y) {
    if (y != 16) {
      mark_movement_passable(
          world, tess::Coord3{16, static_cast<std::int64_t>(y), 0}, false);
    }
  }

  constexpr std::size_t agent_count = 24;
  std::vector<tess::PathAgentState> agents(agent_count);
  for (std::size_t i = 0; i < agents.size(); ++i) {
    const auto lane = static_cast<std::int64_t>(i % 6);
    const auto rank = static_cast<std::int64_t>(i / 6);
    agents[i].position = tess::Coord3{1 + rank, 2 + lane * 4, 0};
    world.template field<OccupancyTag>(agents[i].position) = true;
    tess::set_path_agent_goal(agents[i],
                              tess::Coord3{30 - rank, 2 + lane * 4, 0});
  }

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::PathAgentTickState tick_state;
  const auto options = tess::PathAgentTickOptions{
      .max_blocked_retries = 64,
      .blocked_exhaustion_policy =
          tess::BlockedAgentExhaustionPolicy::MarkUnreachable,
  };

  std::size_t submitted = 0;
  std::size_t processed_ticks = 0;
  for (int tick = 0; tick < 512; ++tick) {
    const auto stats =
        tick_movement(tick_state, world, agents, runtime, options);
    submitted += stats.pathing.submitted;
    processed_ticks += stats.processed_paths ? 1u : 0u;
    auto active = false;
    for (const auto& agent : agents) {
      active = active || agent.has_goal;
    }
    if (!active) {
      break;
    }
  }

  EXPECT_EQ(submitted, agent_count);
  EXPECT_EQ(processed_ticks, 1u);
  std::size_t arrived = 0;
  std::size_t unreachable = 0;
  for (const auto& agent : agents) {
    arrived += agent.phase == tess::PathAgentPhase::Idle ? 1u : 0u;
    unreachable += agent.phase == tess::PathAgentPhase::Unreachable ? 1u : 0u;
    EXPECT_NE(agent.phase, tess::PathAgentPhase::Blocked);
  }
  EXPECT_GT(arrived, 0u);
  EXPECT_EQ(arrived + unreachable, agent_count);
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
  const auto options = tess::PathAgentTickOptions{
      .max_blocked_retries = 3,
      .blocked_exhaustion_policy =
          tess::BlockedAgentExhaustionPolicy::MarkUnreachable,
  };

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
