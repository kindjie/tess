#include <gtest/gtest.h>
#include <tess/tess.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

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
  runtime.reserve_unit_field_products(request_count);
  runtime.reserve_unit_field_product_dependencies(World::chunk_count);
  runtime.reserve_portal_segments(request_count);
  runtime.portal_segment_cache().reserve_path_nodes(1024);
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

auto validate_move(const MovementWorld& world, tess::Coord3 from,
                   tess::Coord3 to, tess::MovementVersionCheck versions = {})
    -> tess::MovementResult {
  return tess::validate_movement_intent<MovementWorld, PassableTag,
                                        OccupancyTag, ReservationTag>(
      world, tess::MovementIntent{from, to, versions});
}

TEST(TessMovement, ValidateRejectsInvalidEndpointsAndNonAdjacentMoves) {
  MovementWorld world;
  fill_movement_world(world);

  auto result =
      validate_move(world, tess::Coord3{-1, 0, 0}, tess::Coord3{0, 0, 0});
  EXPECT_EQ(result.status, tess::MovementStatus::InvalidFrom);

  result = validate_move(world, tess::Coord3{31, 0, 0}, tess::Coord3{32, 0, 0});
  EXPECT_EQ(result.status, tess::MovementStatus::InvalidTo);

  result = validate_move(world, tess::Coord3{0, 0, 0}, tess::Coord3{2, 0, 0});
  EXPECT_EQ(result.status, tess::MovementStatus::NotAdjacent);

  result = validate_move(world, tess::Coord3{0, 0, 0}, tess::Coord3{1, 1, 0});
  EXPECT_EQ(result.status, tess::MovementStatus::NotAdjacent);

  result = validate_move(world, tess::Coord3{0, 0, 0}, tess::Coord3{0, 0, 0});
  EXPECT_EQ(result.status, tess::MovementStatus::NotAdjacent);
}

TEST(TessMovement, ValidateRejectsBlockedFromAndCommitLeavesWorldUntouched) {
  MovementWorld world;
  fill_movement_world(world);
  world.template field<PassableTag>(tess::Coord3{0, 0, 0}) = false;
  world.template field<OccupancyTag>(tess::Coord3{0, 0, 0}) = true;

  const auto result =
      validate_move(world, tess::Coord3{0, 0, 0}, tess::Coord3{1, 0, 0});
  EXPECT_EQ(result.status, tess::MovementStatus::BlockedFrom);

  const auto commit =
      tess::commit_movement_intent<MovementWorld, PassableTag, OccupancyTag,
                                   ReservationTag>(
          world, tess::MovementIntent{
                     tess::Coord3{0, 0, 0}, tess::Coord3{1, 0, 0}, {}});
  EXPECT_EQ(commit.status, tess::MovementStatus::BlockedFrom);
  EXPECT_TRUE(world.template field<OccupancyTag>(tess::Coord3{0, 0, 0}));
  EXPECT_FALSE(world.template field<OccupancyTag>(tess::Coord3{1, 0, 0}));
}

TEST(TessMovement, ValidateRejectsStaleTopologyAndToVersionBranches) {
  MovementWorld world;
  fill_movement_world(world);

  const auto from = tess::Coord3{0, 0, 0};
  const auto to = tess::Coord3{1, 0, 0};
  const auto from_meta = world.meta(world.resolve(from).chunk_key);
  const auto to_meta = world.meta(world.resolve(to).chunk_key);

  auto result = validate_move(
      world, from, to,
      tess::MovementVersionCheck{std::nullopt, std::nullopt,
                                 from_meta.topology_version + 1, std::nullopt});
  EXPECT_EQ(result.status, tess::MovementStatus::StaleTopology);

  result = validate_move(
      world, from, to,
      tess::MovementVersionCheck{std::nullopt, std::nullopt, std::nullopt,
                                 to_meta.topology_version + 1});
  EXPECT_EQ(result.status, tess::MovementStatus::StaleTopology);

  result = validate_move(
      world, from, to,
      tess::MovementVersionCheck{std::nullopt, to_meta.version + 1,
                                 std::nullopt, std::nullopt});
  EXPECT_EQ(result.status, tess::MovementStatus::StaleVersion);

  result =
      validate_move(world, from, to,
                    tess::MovementVersionCheck{
                        from_meta.version, to_meta.version,
                        from_meta.topology_version, to_meta.topology_version});
  EXPECT_EQ(result.status, tess::MovementStatus::Moved);
}

TEST(TessMovement, RecordsEveryFailureStatusInItsOwnCounter) {
  tess::MovementFailureCounts counts;

  tess::record_movement_failure(counts, tess::MovementStatus::Moved);
  tess::record_movement_failure(counts, tess::MovementStatus::InvalidFrom);
  tess::record_movement_failure(counts, tess::MovementStatus::InvalidTo);
  tess::record_movement_failure(counts, tess::MovementStatus::NotAdjacent);
  tess::record_movement_failure(counts, tess::MovementStatus::BlockedFrom);
  tess::record_movement_failure(counts, tess::MovementStatus::BlockedTo);
  tess::record_movement_failure(counts, tess::MovementStatus::Occupied);
  tess::record_movement_failure(counts, tess::MovementStatus::Reserved);
  tess::record_movement_failure(counts, tess::MovementStatus::StaleVersion);
  tess::record_movement_failure(counts, tess::MovementStatus::StaleTopology);

  // Moved must not count anywhere; the three invalid statuses share one
  // bucket; every remaining status has its own dedicated counter.
  EXPECT_EQ(counts.invalid, 3u);
  EXPECT_EQ(counts.blocked, 2u);
  EXPECT_EQ(counts.occupied, 1u);
  EXPECT_EQ(counts.reserved, 1u);
  EXPECT_EQ(counts.stale_version, 1u);
  EXPECT_EQ(counts.stale_topology, 1u);
}

TEST(TessMovement, ClassifiesTransientVersusTerminalFailures) {
  using tess::is_transient_movement_failure;
  using tess::MovementStatus;

  // Transient: the world state can legitimately change; re-path and retry.
  EXPECT_TRUE(is_transient_movement_failure(MovementStatus::BlockedFrom));
  EXPECT_TRUE(is_transient_movement_failure(MovementStatus::BlockedTo));
  EXPECT_TRUE(is_transient_movement_failure(MovementStatus::Occupied));
  EXPECT_TRUE(is_transient_movement_failure(MovementStatus::Reserved));
  EXPECT_TRUE(is_transient_movement_failure(MovementStatus::StaleVersion));
  EXPECT_TRUE(is_transient_movement_failure(MovementStatus::StaleTopology));

  // Terminal (the false branch): success and caller bugs must never route an
  // agent into the blocked-retry lifecycle.
  EXPECT_FALSE(is_transient_movement_failure(MovementStatus::Moved));
  EXPECT_FALSE(is_transient_movement_failure(MovementStatus::InvalidFrom));
  EXPECT_FALSE(is_transient_movement_failure(MovementStatus::InvalidTo));
  EXPECT_FALSE(is_transient_movement_failure(MovementStatus::NotAdjacent));
}

TEST(TessMovement, ManhattanDistanceIsOverflowSafeAtInt64Extremes) {
  constexpr auto min = std::numeric_limits<std::int64_t>::min();
  constexpr auto max = std::numeric_limits<std::int64_t>::max();
  constexpr auto saturated = std::numeric_limits<std::uint64_t>::max();

  // Runtime (non-constexpr) values so sanitizers observe the arithmetic.
  const auto lo = tess::Coord3{min, 0, 0};
  const auto hi = tess::Coord3{max, 0, 0};
  EXPECT_EQ(tess::manhattan_distance(lo, hi), saturated);
  EXPECT_EQ(tess::manhattan_distance(hi, lo), saturated);

  // Multi-axis extremes must saturate instead of wrapping.
  EXPECT_EQ(tess::manhattan_distance(tess::Coord3{min, min, min},
                                     tess::Coord3{max, max, max}),
            saturated);
  EXPECT_EQ(
      tess::manhattan_distance(tess::Coord3{-2, 5, 7}, tess::Coord3{3, -5, 7}),
      15u);
}

TEST(TessPathAgent, GoalAssignmentAndClearDrivePhaseLifecycle) {
  tess::PathAgentState agent{.position = tess::Coord3{0, 0, 0}};
  EXPECT_EQ(agent.phase, tess::PathAgentPhase::Idle);
  EXPECT_EQ(agent.blocked_retries, 0u);

  agent.blocked_retries = 5;
  tess::set_path_agent_goal(agent, tess::Coord3{3, 0, 0});
  EXPECT_EQ(agent.phase, tess::PathAgentPhase::NeedsPath);
  EXPECT_EQ(agent.blocked_retries, 0u);
  EXPECT_TRUE(agent.has_goal);

  agent.phase = tess::PathAgentPhase::Blocked;
  agent.blocked_retries = 3;
  tess::clear_path_agent_goal(agent);
  EXPECT_EQ(agent.phase, tess::PathAgentPhase::Idle);
  EXPECT_EQ(agent.blocked_retries, 0u);
  EXPECT_FALSE(agent.has_goal);
}

TEST(TessPathAgent, TransientMovementFailureKeepsFoundStatusAndBlocks) {
  MovementWorld world;
  fill_movement_world(world);
  world.template field<OccupancyTag>(tess::Coord3{0, 0, 0}) = true;
  world.template field<OccupancyTag>(tess::Coord3{1, 0, 0}) = true;

  std::array<tess::PathAgentState, 1> agents{{
      {.position = tess::Coord3{0, 0, 0}},
  }};
  tess::set_path_agent_goal(agents[0], tess::Coord3{3, 0, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  (void)tess::process_unit_path_agents<MovementWorld, PassableTag>(
      world, agents, runtime);
  ASSERT_EQ(agents[0].status, tess::PathStatus::Found);

  const auto stats =
      tess::advance_path_agents_with_movement<MovementWorld, PassableTag,
                                              OccupancyTag, ReservationTag>(
          world, agents, runtime);
  EXPECT_EQ(stats.blocked_waits, 1u);
  EXPECT_EQ(stats.movement_failures.occupied, 1u);
  EXPECT_EQ(agents[0].status, tess::PathStatus::Found);
  EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::Blocked);
  EXPECT_EQ(agents[0].blocked_retries, 1u);
  EXPECT_EQ(agents[0].position, (tess::Coord3{0, 0, 0}));

  // Once the destination frees up, a successful step resets the budget.
  world.template field<OccupancyTag>(tess::Coord3{1, 0, 0}) = false;
  (void)tess::process_unit_path_agents<MovementWorld, PassableTag>(
      world, agents, runtime);
  const auto resumed =
      tess::advance_path_agents_with_movement<MovementWorld, PassableTag,
                                              OccupancyTag, ReservationTag>(
          world, agents, runtime);
  EXPECT_EQ(resumed.advanced, 1u);
  EXPECT_EQ(agents[0].position, (tess::Coord3{1, 0, 0}));
  EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::Following);
  EXPECT_EQ(agents[0].blocked_retries, 0u);
}

TEST(TessPathAgent, StructuralMovementFailureIsTerminalUntilNewGoal) {
  MovementWorld world;
  fill_movement_world(world);
  world.template field<OccupancyTag>(tess::Coord3{0, 0, 0}) = true;

  std::array<tess::PathAgentState, 1> agents{{
      {.position = tess::Coord3{0, 0, 0}},
  }};
  tess::set_path_agent_goal(agents[0], tess::Coord3{3, 0, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  (void)tess::process_unit_path_agents<MovementWorld, PassableTag>(
      world, agents, runtime);
  ASSERT_EQ(agents[0].status, tess::PathStatus::Found);

  // An external system teleported the agent off its route; stepping to the
  // next path node is no longer an adjacent move, which indicates a caller
  // bug rather than a world change.
  agents[0].position = tess::Coord3{5, 5, 0};
  const auto stats =
      tess::advance_path_agents_with_movement<MovementWorld, PassableTag,
                                              OccupancyTag, ReservationTag>(
          world, agents, runtime);
  EXPECT_EQ(stats.movement_failures.invalid, 1u);
  EXPECT_EQ(stats.blocked_waits, 0u);
  EXPECT_EQ(agents[0].status, tess::PathStatus::NoPath);
  EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::Unreachable);

  // A new goal re-arms the lifecycle.
  tess::set_path_agent_goal(agents[0], tess::Coord3{6, 5, 0});
  EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::NeedsPath);
  EXPECT_EQ(agents[0].blocked_retries, 0u);
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

  tess_test::ScopedAllocationCounter counter;
  const auto stats = tess::process_unit_path_agents<World, PassableTag>(
      world, agents, runtime);

  // The warm frame must do the real work (an early-return no-op would also
  // count zero allocations), and do it allocation-free.
  EXPECT_EQ(stats.submitted, agents.size());
  EXPECT_EQ(stats.found, agents.size());
  EXPECT_EQ(counter.count(), 0u);
}

TEST(TessPathAgent, WarmUnitFieldProductProcessingDoesNotAllocate) {
  World world;
  fill_world(world);

  std::array<tess::PathAgentState, 8> agents{};
  for (std::size_t i = 0; i < agents.size(); ++i) {
    agents[i].position = tess::Coord3{0, static_cast<std::int64_t>(i), 0};
    tess::set_path_agent_goal(agents[i], tess::Coord3{31, 31, 0});
  }

  const auto policy = tess::PathRuntimeCachePolicy{
      .use_unit_field_product_cache = true,
      .unit_field_product_min_start_chunks = 1,
  };
  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  (void)tess::process_unit_path_agents<World, PassableTag>(world, agents,
                                                           runtime, policy);

  tess_test::ScopedAllocationCounter counter;
  (void)tess::process_unit_path_agents<World, PassableTag>(world, agents,
                                                           runtime, policy);

  EXPECT_EQ(runtime.stats().field_product_cache.entries, 1u);
  EXPECT_EQ(counter.count(), 0u);
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

  tess_test::ScopedAllocationCounter counter;
  const auto stats =
      tess::process_weighted_path_agents<World, PassableTag, CostTag, 8>(
          world, agents, runtime);

  // As above: pin the observable result so a skipped warm frame cannot pass.
  EXPECT_EQ(stats.submitted, agents.size());
  EXPECT_EQ(stats.found, agents.size());
  EXPECT_EQ(counter.count(), 0u);
}

}  // namespace
