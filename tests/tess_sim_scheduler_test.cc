#include <gtest/gtest.h>
#include <tess/tess.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace {

struct PassableTag {};
struct CostTag {};
struct OccupancyTag {};
struct ReservationTag {};
struct TerrainTag {};

constexpr std::uint32_t DirtyTerrain = 1u << 0u;
constexpr std::uint32_t DirtyOccupancy = 1u << 1u;
constexpr std::uint32_t DirtyRender = DirtyTerrain | DirtyOccupancy;

using Shape = tess::Shape<tess::Extent3{16, 16, 1}, tess::Extent3{8, 8, 1}>;
using Schema = tess::FieldSchema<
    tess::Field<PassableTag, bool>, tess::Field<CostTag, std::uint32_t>,
    tess::Field<OccupancyTag, bool>, tess::Field<ReservationTag, bool>,
    tess::Field<TerrainTag, std::uint16_t>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

void fill_world(World& world) {
  for (auto& page : world.chunks()) {
    auto passable = page.template field_span<PassableTag>();
    auto cost = page.template field_span<CostTag>();
    auto occupancy = page.template field_span<OccupancyTag>();
    auto reservations = page.template field_span<ReservationTag>();
    auto terrain = page.template field_span<TerrainTag>();
    for (std::size_t i = 0; i < passable.size(); ++i) {
      passable[i] = true;
      cost[i] = 1u;
      occupancy[i] = false;
      reservations[i] = false;
      terrain[i] = 0u;
    }
  }
}

void reserve_runtime(tess::PathRequestRuntime& runtime,
                     std::size_t request_count) {
  runtime.reserve_requests(request_count);
  runtime.reserve_search_nodes(Shape::size.x * Shape::size.y * Shape::size.z);
  runtime.reserve_path_nodes(1024);
  runtime.reserve_unit_routes(request_count);
}

TEST(TessMovement, CommitValidatesPassabilityOccupancyReservationAndVersions) {
  World world;
  fill_world(world);
  world.template field<OccupancyTag>(tess::Coord3{0, 0, 0}) = true;

  const auto from_chunk = world.resolve(tess::Coord3{0, 0, 0}).chunk_key;
  const auto to_chunk = world.resolve(tess::Coord3{1, 0, 0}).chunk_key;
  const auto versions = tess::MovementVersionCheck{
      world.meta(from_chunk).version,
      world.meta(to_chunk).version,
      world.meta(from_chunk).topology_version,
      world.meta(to_chunk).topology_version,
  };

  auto result = tess::commit_movement_intent<World, PassableTag, OccupancyTag,
                                             ReservationTag>(
      world,
      tess::MovementIntent{tess::Coord3{0, 0, 0}, tess::Coord3{1, 0, 0},
                           versions},
      DirtyOccupancy);
  EXPECT_EQ(result.status, tess::MovementStatus::Moved);
  EXPECT_FALSE(world.template field<OccupancyTag>(tess::Coord3{0, 0, 0}));
  EXPECT_TRUE(world.template field<OccupancyTag>(tess::Coord3{1, 0, 0}));
  EXPECT_EQ(world.meta(from_chunk).field_dirty_flags, DirtyOccupancy);
  EXPECT_EQ(world.meta(to_chunk).field_dirty_flags, DirtyOccupancy);

  world.template field<ReservationTag>(tess::Coord3{2, 0, 0}) = true;
  result = tess::commit_movement_intent<World, PassableTag, OccupancyTag,
                                        ReservationTag>(
      world,
      tess::MovementIntent{tess::Coord3{1, 0, 0}, tess::Coord3{2, 0, 0}, {}});
  EXPECT_EQ(result.status, tess::MovementStatus::Reserved);
  EXPECT_TRUE(world.template field<OccupancyTag>(tess::Coord3{1, 0, 0}));

  world.template field<ReservationTag>(tess::Coord3{2, 0, 0}) = false;
  world.template field<PassableTag>(tess::Coord3{2, 0, 0}) = false;
  result = tess::commit_movement_intent<World, PassableTag, OccupancyTag,
                                        ReservationTag>(
      world,
      tess::MovementIntent{tess::Coord3{1, 0, 0}, tess::Coord3{2, 0, 0}, {}});
  EXPECT_EQ(result.status, tess::MovementStatus::BlockedTo);

  result = tess::commit_movement_intent<World, PassableTag, OccupancyTag,
                                        ReservationTag>(
      world, tess::MovementIntent{
                 tess::Coord3{1, 0, 0},
                 tess::Coord3{1, 1, 0},
                 tess::MovementVersionCheck{0u, 0u, std::nullopt, std::nullopt},
             });
  EXPECT_EQ(result.status, tess::MovementStatus::StaleVersion);
}

TEST(TessMovement, PathAgentMovementCommitsOccupancyAndRejectsConflicts) {
  World world;
  fill_world(world);
  world.template field<OccupancyTag>(tess::Coord3{0, 0, 0}) = true;
  world.template field<OccupancyTag>(tess::Coord3{2, 0, 0}) = true;

  std::array<tess::PathAgentState, 2> agents{{
      {.position = tess::Coord3{0, 0, 0}},
      {.position = tess::Coord3{2, 0, 0}},
  }};
  tess::set_path_agent_goal(agents[0], tess::Coord3{1, 0, 0});
  tess::set_path_agent_goal(agents[1], tess::Coord3{1, 0, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::PathAgentTickState state;

  const auto stats =
      tess::tick_unit_path_agents_with_movement<World, PassableTag,
                                                OccupancyTag, ReservationTag>(
          state, world, agents, runtime, tess::PathAgentTickOptions{1, {}},
          DirtyOccupancy);

  EXPECT_EQ(stats.movement.advanced, 1u);
  EXPECT_EQ(stats.movement.movement_failures.occupied, 1u);
  EXPECT_TRUE(world.template field<OccupancyTag>(tess::Coord3{1, 0, 0}));
  EXPECT_FALSE(world.template field<OccupancyTag>(tess::Coord3{0, 0, 0}));
  EXPECT_TRUE(world.template field<OccupancyTag>(tess::Coord3{2, 0, 0}));
}

TEST(TessTime, FixedAccumulatorHonorsPauseSpeedAndClamp) {
  tess::FixedStepAccumulator accumulator(20, 8);

  auto frame = accumulator.consume(1.0, {tess::SimSpeed::Paused});
  EXPECT_EQ(frame.ticks, 0u);
  EXPECT_DOUBLE_EQ(accumulator.accumulated_seconds(), 0.0);

  frame = accumulator.consume(0.05, {tess::SimSpeed::Speed1x});
  EXPECT_EQ(frame.ticks, 1u);

  frame = accumulator.consume(0.05, {tess::SimSpeed::Speed2x});
  EXPECT_EQ(frame.ticks, 2u);

  frame = accumulator.consume(0.05, {tess::SimSpeed::Speed4x});
  EXPECT_EQ(frame.ticks, 4u);

  frame = accumulator.consume(1.0, {tess::SimSpeed::Speed4x});
  EXPECT_EQ(frame.ticks, 8u);
  EXPECT_GT(accumulator.accumulated_seconds(), 0.0);
  EXPECT_LE(frame.alpha, 1.0);
}

TEST(TessTime, FixedAccumulatorDropsBacklogBeyondOneStepWhenCapped) {
  tess::FixedStepAccumulator accumulator(20, 8);

  // A 10 s stall at 4x banks 40 s of sim time; only the capped frame's
  // ticks plus at most one step of carry may survive it.
  auto frame = accumulator.consume(10.0, {tess::SimSpeed::Speed4x});
  EXPECT_EQ(frame.ticks, 8u);
  EXPECT_LE(accumulator.accumulated_seconds(), 0.05 + 1e-9);

  std::size_t drained = 0;
  for (int i = 0; i < 20; ++i) {
    drained += accumulator.consume(0.0, {tess::SimSpeed::Speed4x}).ticks;
  }
  EXPECT_LE(drained, 1u);
}

TEST(TessTime, FixedAccumulatorReportsDroppedSecondsOnCapOverflow) {
  tess::FixedStepAccumulator accumulator(20, 8);

  auto frame = accumulator.consume(10.0, {tess::SimSpeed::Speed4x});
  EXPECT_EQ(frame.ticks, 8u);
  // 40 s banked, 8 ticks (0.4 s) executed, one step (0.05 s) of carry kept.
  EXPECT_NEAR(frame.dropped_seconds, 40.0 - 0.4 - 0.05, 1e-9);

  frame = accumulator.consume(0.05, {tess::SimSpeed::Speed1x});
  EXPECT_DOUBLE_EQ(frame.dropped_seconds, 0.0);
}

TEST(TessTime, FixedAccumulatorGuardsZeroRatesAndReset) {
  tess::FixedStepAccumulator zero_tps(0, 8);
  auto frame = zero_tps.consume(1.0, {tess::SimSpeed::Speed1x});
  EXPECT_EQ(frame.ticks, 0u);
  EXPECT_DOUBLE_EQ(frame.alpha, 0.0);

  tess::FixedStepAccumulator zero_ticks(20, 0);
  frame = zero_ticks.consume(1.0, {tess::SimSpeed::Speed1x});
  EXPECT_EQ(frame.ticks, 0u);

  tess::FixedStepAccumulator accumulator(20, 8);
  static_cast<void>(accumulator.consume(0.07, {tess::SimSpeed::Speed1x}));
  EXPECT_GT(accumulator.accumulated_seconds(), 0.0);
  accumulator.reset();
  EXPECT_DOUBLE_EQ(accumulator.accumulated_seconds(), 0.0);
  EXPECT_EQ(accumulator.base_tps(), 20u);
  EXPECT_EQ(accumulator.max_ticks_per_frame(), 8u);
}

TEST(TessTime, SpeedMultiplierAndEffectiveTpsCoverAllSpeeds) {
  EXPECT_EQ(tess::sim_speed_multiplier(tess::SimSpeed::Paused), 0u);
  EXPECT_EQ(tess::sim_speed_multiplier(tess::SimSpeed::Speed1x), 1u);
  EXPECT_EQ(tess::sim_speed_multiplier(tess::SimSpeed::Speed2x), 2u);
  EXPECT_EQ(tess::sim_speed_multiplier(tess::SimSpeed::Speed4x), 4u);

  EXPECT_EQ(tess::effective_tps(20, tess::SimSpeed::Speed4x), 80u);
  EXPECT_EQ(tess::effective_tps(20, tess::SimSpeed::Paused), 0u);

  // Saturates instead of wrapping when base_tps * multiplier overflows.
  constexpr auto max_tps = std::numeric_limits<std::uint32_t>::max();
  EXPECT_EQ(tess::effective_tps(max_tps, tess::SimSpeed::Speed4x), max_tps);
}

TEST(TessTime, FixedAccumulatorSurvivesHugeAndInvalidDeltas) {
  tess::FixedStepAccumulator accumulator(20, 8);

  auto frame = accumulator.consume(1e300, {tess::SimSpeed::Speed1x});
  EXPECT_EQ(frame.ticks, 8u);
  EXPECT_LE(accumulator.accumulated_seconds(), 0.05 + 1e-9);
  EXPECT_GE(frame.alpha, 0.0);
  EXPECT_LE(frame.alpha, 1.0);

  frame = accumulator.consume(-5.0, {tess::SimSpeed::Speed1x});
  EXPECT_GE(accumulator.accumulated_seconds(), 0.0);

  frame = accumulator.consume(std::numeric_limits<double>::quiet_NaN(),
                              {tess::SimSpeed::Speed1x});
  EXPECT_EQ(frame.ticks, 0u);
  EXPECT_GE(accumulator.accumulated_seconds(), 0.0);
}

TEST(TessRenderDelta, CollectsDirtyBoundsAndClearsRenderMask) {
  World world;
  fill_world(world);

  world.mark_dirty(world.resolve(tess::Coord3{1, 1, 0}).chunk_key, DirtyTerrain,
                   tess::Box3{tess::Coord3{1, 1, 0}, tess::Extent3{2, 1, 1}});

  auto deltas = tess::render_tile_deltas(world, DirtyRender);
  ASSERT_EQ(deltas.size(), 2u);
  EXPECT_EQ(deltas[0].coord, (tess::Coord3{1, 1, 0}));
  EXPECT_EQ(deltas[1].coord, (tess::Coord3{2, 1, 0}));
  EXPECT_EQ(deltas[0].dirty_flags, DirtyTerrain);
  EXPECT_EQ(deltas[0].chunk_version, 1u);

  tess::clear_render_delta_dirty(world, DirtyRender);
  EXPECT_TRUE(tess::render_tile_deltas(world, DirtyRender).empty());
}

TEST(TessScheduler, QueuedEditDirtiesPathingAndEmitsRenderDeltas) {
  World world;
  fill_world(world);

  std::array<tess::PathAgentState, 1> agents{{
      {.position = tess::Coord3{0, 0, 0}},
  }};
  tess::set_path_agent_goal(agents[0], tess::Coord3{4, 0, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::SimSchedulerState scheduler;
  std::vector<tess::RenderTileDelta> render_deltas;

  auto stats = tess::tick_unit_scheduler<World, PassableTag,
                                         tess::WritePolicy::UniquePerChunk>(
      scheduler, world, tess::FrameOps{}, agents, runtime, render_deltas,
      [](auto) {},
      tess::SimSchedulerOptions{
          DirtyTerrain,
          DirtyRender,
          tess::PathAgentTickOptions{1, {}},
          false,
      });
  ASSERT_TRUE(stats.path_agents.processed_paths);
  ASSERT_EQ(stats.path_agents.pathing.found, 1u);
  ASSERT_EQ(agents[0].position, (tess::Coord3{1, 0, 0}));

  const std::vector<tess::ChunkKey> chunks{
      world.resolve(tess::Coord3{2, 0, 0}).chunk_key,
  };
  tess::FrameOps ops;
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(chunks),
                         tess::FieldAccessDesc{0, DirtyTerrain, DirtyTerrain},
                         tess::WritePolicy::UniquePerChunk);

  stats = tess::tick_unit_scheduler<World, PassableTag,
                                    tess::WritePolicy::UniquePerChunk>(
      scheduler, world, ops, agents, runtime, render_deltas,
      [](auto view) {
        const auto local = tess::local_tile_id<Shape>(
            tess::local_coord<Shape>(tess::Coord3{2, 0, 0}));
        view.template field_span<PassableTag>()[local.value] = false;
        view.template field_span<TerrainTag>()[local.value] = 7u;
      },
      tess::SimSchedulerOptions{
          DirtyTerrain,
          DirtyRender,
          tess::PathAgentTickOptions{1, {}},
          false,
      });

  EXPECT_TRUE(stats.executed_ops);
  EXPECT_TRUE(stats.path_agents.processed_paths);
  EXPECT_EQ(runtime.stats().world_cache_invalidations, 1u);
  EXPECT_NE(agents[0].position, (tess::Coord3{2, 0, 0}));
  EXPECT_GE(stats.render_delta_count, 1u);
  EXPECT_FALSE(render_deltas.empty());
}

TEST(TessScheduler, MovementSchedulerCommitsOccupancyAndEmitsDeltas) {
  World world;
  fill_world(world);
  world.template field<OccupancyTag>(tess::Coord3{0, 0, 0}) = true;

  std::array<tess::PathAgentState, 1> agents{{
      {.position = tess::Coord3{0, 0, 0}},
  }};
  tess::set_path_agent_goal(agents[0], tess::Coord3{2, 0, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::SimSchedulerState scheduler;
  std::vector<tess::RenderTileDelta> render_deltas;

  const auto stats =
      tess::tick_unit_movement_scheduler<World, PassableTag, OccupancyTag,
                                         ReservationTag,
                                         tess::WritePolicy::ReadOnly>(
          scheduler, world, tess::FrameOps{}, agents, runtime, render_deltas,
          [](auto) {},
          tess::SimSchedulerOptions{
              DirtyTerrain,
              DirtyRender,
              tess::PathAgentTickOptions{1, {}},
              false,
              DirtyOccupancy,
          });

  EXPECT_EQ(stats.path_agents.movement.advanced, 1u);
  EXPECT_FALSE(world.template field<OccupancyTag>(tess::Coord3{0, 0, 0}));
  EXPECT_TRUE(world.template field<OccupancyTag>(tess::Coord3{1, 0, 0}));
  EXPECT_GE(stats.render_delta_count, 1u);
  EXPECT_FALSE(render_deltas.empty());
}

TEST(TessScheduler, WeightedTickUsesBatchPathing) {
  World world;
  fill_world(world);

  std::array<tess::PathAgentState, 3> agents{};
  for (std::size_t i = 0; i < agents.size(); ++i) {
    agents[i].position = tess::Coord3{0, static_cast<std::int64_t>(i), 0};
    tess::set_path_agent_goal(agents[i], tess::Coord3{8, 8, 0});
  }

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::SimSchedulerState scheduler;
  std::vector<tess::RenderTileDelta> render_deltas;

  const auto stats =
      tess::tick_weighted_scheduler<World, PassableTag, CostTag, 8,
                                    tess::WritePolicy::ReadOnly>(
          scheduler, world, tess::FrameOps{}, agents, runtime, render_deltas,
          [](auto) {});

  EXPECT_TRUE(stats.path_agents.processed_paths);
  EXPECT_EQ(stats.path_agents.pathing.found, agents.size());
  EXPECT_EQ(runtime.stats().weighted_batch.unique_goals, 1u);
}

}  // namespace
