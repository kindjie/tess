#include <gtest/gtest.h>
#include <tess/tess.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
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
  EXPECT_DOUBLE_EQ(frame.alpha, 0.0);

  frame = accumulator.consume(0.05, {tess::SimSpeed::Speed1x});
  EXPECT_EQ(frame.ticks, 1u);
  EXPECT_DOUBLE_EQ(frame.alpha, 0.0);

  frame = accumulator.consume(0.05, {tess::SimSpeed::Speed2x});
  EXPECT_EQ(frame.ticks, 2u);
  EXPECT_DOUBLE_EQ(frame.alpha, 0.0);

  frame = accumulator.consume(0.05, {tess::SimSpeed::Speed4x});
  EXPECT_EQ(frame.ticks, 4u);
  EXPECT_DOUBLE_EQ(frame.alpha, 0.0);

  frame = accumulator.consume(0.025, {tess::SimSpeed::Speed1x});
  EXPECT_EQ(frame.ticks, 0u);
  EXPECT_NEAR(frame.alpha, 0.5, 1e-12);
  EXPECT_NEAR(accumulator.accumulated_seconds(), 0.025, 1e-12);

  frame = accumulator.consume(1.0, {tess::SimSpeed::Speed4x});
  EXPECT_EQ(frame.ticks, 8u);
  EXPECT_GT(accumulator.accumulated_seconds(), 0.0);
  EXPECT_DOUBLE_EQ(frame.alpha, 1.0);
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

TEST(TessRenderDelta, ChunkBorderBoundsEmitOnlyOwningChunkTiles) {
  World world;
  fill_world(world);

  const auto chunk_a = world.resolve(tess::Coord3{7, 0, 0}).chunk_key;
  const auto chunk_b = world.resolve(tess::Coord3{8, 0, 0}).chunk_key;
  ASSERT_NE(chunk_a, chunk_b);

  const auto border_span =
      tess::Box3{tess::Coord3{6, 0, 0}, tess::Extent3{4, 1, 1}};
  world.mark_dirty(chunk_a, DirtyTerrain, border_span);
  world.mark_dirty(chunk_b, DirtyTerrain, border_span);

  const auto deltas = tess::render_tile_deltas(world, DirtyRender);
  ASSERT_EQ(deltas.size(), 4u);
  EXPECT_EQ(deltas[0].coord, (tess::Coord3{6, 0, 0}));
  EXPECT_EQ(deltas[1].coord, (tess::Coord3{7, 0, 0}));
  EXPECT_EQ(deltas[2].coord, (tess::Coord3{8, 0, 0}));
  EXPECT_EQ(deltas[3].coord, (tess::Coord3{9, 0, 0}));
  EXPECT_EQ(deltas[0].chunk_key, chunk_a);
  EXPECT_EQ(deltas[1].chunk_key, chunk_a);
  EXPECT_EQ(deltas[2].chunk_key, chunk_b);
  EXPECT_EQ(deltas[3].chunk_key, chunk_b);
  for (const auto& delta : deltas) {
    EXPECT_EQ(delta.chunk_key, world.resolve(delta.coord).chunk_key);
    EXPECT_EQ(delta.local_tile_id, world.resolve(delta.coord).local_tile_id);
  }
}

TEST(TessRenderDelta, ClampedBoundsSkipOutOfShapeTiles) {
  World world;
  fill_world(world);

  const auto corner_chunk = world.resolve(tess::Coord3{15, 15, 0}).chunk_key;
  world.mark_dirty(corner_chunk, DirtyTerrain,
                   tess::Box3{tess::Coord3{14, 14, 0}, tess::Extent3{4, 4, 1}});

  auto deltas = tess::render_tile_deltas(world, DirtyRender);
  ASSERT_EQ(deltas.size(), 4u);
  EXPECT_EQ(deltas[0].coord, (tess::Coord3{14, 14, 0}));
  EXPECT_EQ(deltas[1].coord, (tess::Coord3{15, 14, 0}));
  EXPECT_EQ(deltas[2].coord, (tess::Coord3{14, 15, 0}));
  EXPECT_EQ(deltas[3].coord, (tess::Coord3{15, 15, 0}));

  World negative;
  fill_world(negative);
  negative.mark_dirty(
      negative.resolve(tess::Coord3{0, 0, 0}).chunk_key, DirtyTerrain,
      tess::Box3{tess::Coord3{-1, -1, 0}, tess::Extent3{2, 2, 1}});

  deltas = tess::render_tile_deltas(negative, DirtyRender);
  ASSERT_EQ(deltas.size(), 1u);
  EXPECT_EQ(deltas[0].coord, (tess::Coord3{0, 0, 0}));
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

  std::vector<tess::Coord3> visited{agents[0].position};
  for (int tick = 0; tick < 32 && agents[0].has_goal; ++tick) {
    stats = tess::tick_unit_scheduler<World, PassableTag,
                                      tess::WritePolicy::UniquePerChunk>(
        scheduler, world, tess::FrameOps{}, agents, runtime, render_deltas,
        [](auto) {},
        tess::SimSchedulerOptions{
            DirtyTerrain,
            DirtyRender,
            tess::PathAgentTickOptions{1, {}},
            false,
        });
    visited.push_back(agents[0].position);
  }

  EXPECT_FALSE(agents[0].has_goal);
  EXPECT_EQ(agents[0].position, (tess::Coord3{4, 0, 0}));
  for (const auto coord : visited) {
    EXPECT_NE(coord, (tess::Coord3{2, 0, 0}));
  }
}

TEST(TessScheduler, RejectedPlanSkipsExecutionAndStillTicksAgents) {
  World world;
  fill_world(world);

  std::array<tess::PathAgentState, 1> agents{{
      {.position = tess::Coord3{0, 0, 0}},
  }};
  tess::set_path_agent_goal(agents[0], tess::Coord3{3, 0, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::SimSchedulerState scheduler;
  std::vector<tess::RenderTileDelta> render_deltas;

  tess::FrameOps ops;
  (void)ops.update_field(tess::DomainDesc::resident_chunks(),
                         tess::FieldAccessDesc{0, DirtyTerrain, DirtyTerrain},
                         tess::WritePolicy::ReadOnly);

  bool callback_ran = false;
  const auto stats = tess::tick_unit_scheduler<World, PassableTag,
                                               tess::WritePolicy::ReadOnly>(
      scheduler, world, ops, agents, runtime, render_deltas,
      [&callback_ran](auto) { callback_ran = true; },
      tess::SimSchedulerOptions{
          DirtyTerrain,
          DirtyRender,
          tess::PathAgentTickOptions{1, {}},
          false,
      });

  EXPECT_TRUE(stats.planned_ops);
  EXPECT_FALSE(stats.executed_ops);
  EXPECT_FALSE(stats.op_report.ok());
  EXPECT_EQ(stats.op_report.failed_count(), 1u);
  EXPECT_FALSE(callback_ran);

  std::size_t changed_tiles = 0;
  for (const auto& page : world.chunks()) {
    for (const auto value : page.field_span<TerrainTag>()) {
      changed_tiles += value != 0 ? 1u : 0u;
    }
    for (const auto value : page.field_span<PassableTag>()) {
      changed_tiles += value ? 0u : 1u;
    }
  }
  EXPECT_EQ(changed_tiles, 0u);
  EXPECT_TRUE(world.dirty_chunks(DirtyRender).empty());
  EXPECT_EQ(stats.render_delta_count, 0u);
  EXPECT_TRUE(render_deltas.empty());

  EXPECT_EQ(stats.tick, 1u);
  EXPECT_TRUE(stats.path_agents.processed_paths);
  EXPECT_EQ(stats.path_agents.pathing.found, 1u);
  EXPECT_EQ(agents[0].position, (tess::Coord3{1, 0, 0}));
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

TEST(TessScheduler, ClearRenderDirtyOptionClearsMetadataAfterCollection) {
  World world;
  fill_world(world);

  std::array<tess::PathAgentState, 1> agents{{
      {.position = tess::Coord3{0, 0, 0}},
  }};

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::SimSchedulerState scheduler;
  std::vector<tess::RenderTileDelta> render_deltas;

  const auto edit_chunk = world.resolve(tess::Coord3{2, 0, 0}).chunk_key;
  const std::vector<tess::ChunkKey> chunks{edit_chunk};
  tess::FrameOps ops;
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(chunks),
                         tess::FieldAccessDesc{0, DirtyTerrain, DirtyTerrain},
                         tess::WritePolicy::UniquePerChunk);

  auto stats = tess::tick_unit_scheduler<World, PassableTag,
                                         tess::WritePolicy::UniquePerChunk>(
      scheduler, world, ops, agents, runtime, render_deltas,
      [](auto view) {
        const auto local = tess::local_tile_id<Shape>(
            tess::local_coord<Shape>(tess::Coord3{2, 0, 0}));
        view.template field_span<TerrainTag>()[local.value] = 3u;
      },
      tess::SimSchedulerOptions{
          DirtyTerrain,
          DirtyRender,
          tess::PathAgentTickOptions{1, {}},
          true,
      });

  EXPECT_TRUE(stats.executed_ops);
  EXPECT_GE(stats.render_delta_count, 1u);
  EXPECT_EQ(world.meta(edit_chunk).field_dirty_flags & DirtyRender, 0u);
  EXPECT_TRUE(world.dirty_chunks(DirtyRender).empty());

  const auto collected = render_deltas.size();
  stats = tess::tick_unit_scheduler<World, PassableTag,
                                    tess::WritePolicy::UniquePerChunk>(
      scheduler, world, tess::FrameOps{}, agents, runtime, render_deltas,
      [](auto) {},
      tess::SimSchedulerOptions{
          DirtyTerrain,
          DirtyRender,
          tess::PathAgentTickOptions{1, {}},
          true,
      });
  EXPECT_EQ(stats.render_delta_count, 0u);
  EXPECT_EQ(render_deltas.size(), collected);
}

constexpr std::uint32_t WeightedMaxCost = 8;

void fill_weighted_detour_world(World& world) {
  fill_world(world);
  world.template field<CostTag>(tess::Coord3{1, 0, 0}) = 5u;
  world.template field<CostTag>(tess::Coord3{2, 0, 0}) = 5u;
  world.template field<CostTag>(tess::Coord3{3, 0, 0}) = 5u;
  world.template field<OccupancyTag>(tess::Coord3{0, 0, 0}) = true;
}

auto run_weighted_movement_to_arrival(World& world,
                                      std::span<tess::PathAgentState> agents,
                                      tess::PathRequestRuntime& runtime,
                                      std::vector<tess::RenderTileDelta>& out,
                                      std::uint32_t movement_dirty_mask)
    -> std::vector<tess::Coord3> {
  tess::SimSchedulerState scheduler;
  const auto options = tess::SimSchedulerOptions{
      DirtyTerrain, DirtyRender,         tess::PathAgentTickOptions{1, {}},
      false,        movement_dirty_mask,
  };

  std::vector<tess::Coord3> visited{agents[0].position};
  for (int tick = 0; tick < 32 && agents[0].has_goal; ++tick) {
    (void)tess::tick_weighted_movement_scheduler<
        World, PassableTag, CostTag, WeightedMaxCost, OccupancyTag,
        ReservationTag, tess::WritePolicy::ReadOnly>(
        scheduler, world, tess::FrameOps{}, agents, runtime, out, [](auto) {},
        options);
    visited.push_back(agents[0].position);
  }
  return visited;
}

TEST(TessScheduler, WeightedMovementSchedulerTakesCheapDetourAndCommits) {
  World world;
  fill_weighted_detour_world(world);

  std::array<tess::PathAgentState, 1> agents{{
      {.position = tess::Coord3{0, 0, 0}},
  }};
  tess::set_path_agent_goal(agents[0], tess::Coord3{4, 0, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  std::vector<tess::RenderTileDelta> render_deltas;

  const auto visited = run_weighted_movement_to_arrival(
      world, agents, runtime, render_deltas, DirtyOccupancy);

  EXPECT_FALSE(agents[0].has_goal);
  EXPECT_EQ(agents[0].position, (tess::Coord3{4, 0, 0}));

  bool used_detour_row = false;
  for (const auto coord : visited) {
    EXPECT_EQ(world.template field<CostTag>(coord), 1u)
        << "expensive tile entered at " << coord.x << "," << coord.y;
    used_detour_row = used_detour_row || coord.y != 0;
  }
  EXPECT_TRUE(used_detour_row);

  EXPECT_FALSE(world.template field<OccupancyTag>(tess::Coord3{0, 0, 0}));
  EXPECT_TRUE(world.template field<OccupancyTag>(tess::Coord3{4, 0, 0}));

  const auto route_chunk = world.resolve(tess::Coord3{0, 0, 0}).chunk_key;
  EXPECT_NE(world.meta(route_chunk).field_dirty_flags & DirtyOccupancy, 0u);
  for (std::uint64_t key = 0; key < World::chunk_count; ++key) {
    if ((tess::ChunkKey{key}) == route_chunk) {
      continue;
    }
    EXPECT_EQ(world.meta(tess::ChunkKey{key}).field_dirty_flags, 0u);
  }
  EXPECT_FALSE(render_deltas.empty());
}

TEST(TessScheduler, WeightedMovementSchedulerZeroMaskLeavesMetadataClean) {
  World world;
  fill_weighted_detour_world(world);

  std::array<tess::PathAgentState, 1> agents{{
      {.position = tess::Coord3{0, 0, 0}},
  }};
  tess::set_path_agent_goal(agents[0], tess::Coord3{4, 0, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  std::vector<tess::RenderTileDelta> render_deltas;

  const auto visited = run_weighted_movement_to_arrival(world, agents, runtime,
                                                        render_deltas, 0);

  EXPECT_FALSE(agents[0].has_goal);
  EXPECT_EQ(agents[0].position, (tess::Coord3{4, 0, 0}));
  EXPECT_GE(visited.size(), 2u);

  EXPECT_FALSE(world.template field<OccupancyTag>(tess::Coord3{0, 0, 0}));
  EXPECT_TRUE(world.template field<OccupancyTag>(tess::Coord3{4, 0, 0}));

  for (std::uint64_t key = 0; key < World::chunk_count; ++key) {
    EXPECT_EQ(world.meta(tess::ChunkKey{key}).field_dirty_flags, 0u);
  }
  EXPECT_TRUE(render_deltas.empty());
}

}  // namespace
