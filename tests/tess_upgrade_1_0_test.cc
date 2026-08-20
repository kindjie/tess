#include <gtest/gtest.h>
#include <tess/tess.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace {

struct PassableTag {};
struct OccupancyTag {};
struct ReservationTag {};

using Shape = tess::Shape<tess::Extent3{4, 4, 1}, tess::Extent3{4, 4, 1}>;
using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                 tess::Field<OccupancyTag, bool>,
                                 tess::Field<ReservationTag, bool>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

struct MaintenanceUpgradeTask final : tess::maintenance::MaintenanceTask {
  void run(tess::maintenance::MaintenanceBudget& budget) override {
    static_cast<void>(budget.consume());
  }
};

void fill_passable(World& world) {
  for (auto& page : world.chunks()) {
    auto passable = page.field_span<PassableTag>();
    for (auto&& tile : passable) {
      tile = true;
    }
  }
}

template <typename T>
concept HasProductKey = requires(T value) { value.product_key; };

template <typename T>
concept HasCategoryStats = requires(T& value) { value.category_stats(); };

template <typename T>
concept HasCategory = requires(T& value) {
  value.category(tess::diagnostics::TraceCategory::Path);
};

template <typename Pipeline, typename Output>
concept HasToFrontier = requires(Pipeline& pipeline, Output& output) {
  pipeline.to_frontier(output);
};

TEST(TessUpgrade1_0, MaintenanceMovesToStableNamespace) {
  // [upgrade-maintenance]
  // Before:
  // #include <tess/experimental/registered_maintenance.h>
  // using Scheduler = tess::experimental::maintenance::RegisteredScheduler<
  //     tess::experimental::maintenance::ImmediateScheduler>;

  // After:
  using Scheduler = tess::maintenance::RegisteredScheduler<
      tess::maintenance::ImmediateScheduler>;
  Scheduler scheduler{1};
  // [upgrade-maintenance]

  MaintenanceUpgradeTask task;
  const auto handle = scheduler.register_task(task);
  ASSERT_TRUE(handle.has_value());
  scheduler.seal();
  EXPECT_EQ(scheduler.schedule(*handle),
            tess::maintenance::ScheduleResult::Accepted);
  EXPECT_EQ(scheduler.flush(), tess::maintenance::DrainResult::Idle);
  EXPECT_EQ(scheduler.try_release(*handle),
            tess::maintenance::ReleaseResult::Released);
}

TEST(TessUpgrade1_0, PathRequestReplacesStartGoalPairs) {
  World world;
  fill_passable(world);
  tess::LocalTopologyScratch local_scratch;
  tess::RegionGraph graph;
  static_cast<void>(tess::build_region_graph<World, PassableTag>(
      world, local_scratch, graph));
  tess::RegionGraphScratch graph_scratch;
  const auto start = tess::Coord3{0, 0, 0};
  const auto goal = tess::Coord3{3, 3, 0};

  // [upgrade-path-request]
  // Before:
  // auto result = tess::reachable<Shape>(graph, start, goal, graph_scratch);

  // After:
  const auto result = tess::reachable<Shape>(
      graph, tess::PathRequest{start, goal}, graph_scratch);
  // [upgrade-path-request]

  EXPECT_EQ(result.status, tess::ReachabilityStatus::Reachable);
}

TEST(TessUpgrade1_0, OptionsReplaceAdjacentCacheAndMovementIntegers) {
  constexpr auto max_entries = std::size_t{8};
  constexpr auto max_path_nodes = std::size_t{64};
  tess::UnitRouteCache cache;

  // [upgrade-route-cache-limits]
  // Before:
  // cache.set_caps(max_entries, max_path_nodes);

  // After:
  cache.set_caps(tess::UnitRouteCacheLimits{max_entries, max_path_nodes});
  // [upgrade-route-cache-limits]

  World world;
  std::array<tess::PathAgentState, 0> agents{};
  tess::PathRequestRuntime runtime;
  constexpr auto max_steps = std::size_t{2};
  constexpr auto movement_dirty_mask = tess::DirtyMask{4};

  // [upgrade-agent-options]
  // Before:
  // tess::advance_path_agents_with_movement<
  //     World, PassableTag, OccupancyTag, ReservationTag>(
  //     world, agents, runtime, max_steps, movement_dirty_mask);

  // After:
  const auto stats =
      tess::advance_path_agents_with_movement<World, PassableTag, OccupancyTag,
                                              ReservationTag>(
          world, agents, runtime,
          tess::PathAgentAdvanceOptions{max_steps, movement_dirty_mask});
  // [upgrade-agent-options]

  // [upgrade-scheduler-agent-options]
  // Before:
  // tess::SimSchedulerOptions options{
  //     .movement_dirty_mask = movement_dirty_mask,
  // };

  // After:
  const auto scheduler_options = tess::SimSchedulerOptions{
      .path_agent_options =
          tess::PathAgentTickOptions{
              .movement_dirty_mask = movement_dirty_mask,
          },
  };
  // [upgrade-scheduler-agent-options]

  // [upgrade-blocked-exhaustion-policy]
  auto options = tess::PathAgentTickOptions{};
  options.blocked_exhaustion_policy =
      tess::BlockedAgentExhaustionPolicy::MarkUnreachable;
  // [upgrade-blocked-exhaustion-policy]

  EXPECT_EQ(stats.advanced, 0u);
  EXPECT_EQ(scheduler_options.path_agent_options.movement_dirty_mask,
            movement_dirty_mask);
  EXPECT_EQ(options.blocked_exhaustion_policy,
            tess::BlockedAgentExhaustionPolicy::MarkUnreachable);
}

TEST(TessUpgrade1_0, GpuDescriptorsRetainAProductHandle) {
  const auto handle = tess::gpu::GpuProductHandle{7, 3};

  // [upgrade-gpu-handle]
  // Before:
  // tess::gpu::DispatchDesc dispatch{
  //     .product_key = handle.key,
  //     .product_generation = handle.generation,
  // };

  // After:
  const auto dispatch = tess::gpu::DispatchDesc{.handle = handle};
  // [upgrade-gpu-handle]

  static_assert(!HasProductKey<tess::gpu::DispatchDesc>);
  static_assert(!HasProductKey<tess::gpu::ReadbackDesc>);
  EXPECT_EQ(dispatch.handle, handle);
}

TEST(TessUpgrade1_0, OutputsPrecedeScratchAndDirtyDestinationsPrecedeSources) {
  World world;
  fill_passable(world);
  tess::GoalSet goals;
  goals.add(tess::Coord3{3, 3, 0});
  tess::DistanceFieldScratch field_scratch;
  tess::DistanceFieldProduct product;
  tess::PlannedDirtyPartitions partitions;
  tess::PlannedDirtyAccumulator dirty_scratch;
  std::vector<tess::RenderTileDelta> render_deltas;
  constexpr auto dirty_mask = tess::DirtyMask{1};

  // [upgrade-parameter-order]
  // Before:
  // tess::build_distance_field_product<World, PassableTag>(
  //     world, goals, field_scratch, product);
  // tess::merge_planned_dirty(world, partitions, dirty_scratch);
  // tess::collect_render_tile_deltas(world, dirty_mask, render_deltas);

  // After:
  const auto field = tess::build_distance_field_product<World, PassableTag>(
      world, goals, product, field_scratch);
  const auto merged =
      tess::merge_planned_dirty(world, dirty_scratch, partitions);
  tess::collect_render_tile_deltas(render_deltas, world, dirty_mask);
  // [upgrade-parameter-order]

  EXPECT_EQ(field.status, tess::PathStatus::Found);
  EXPECT_EQ(merged.status, tess::PlannedDirtyMergeStatus::Merged);
}

TEST(TessUpgrade1_0, RemovedSpellingsAndStableIdentityContractsArePinned) {
  std::array<std::uint32_t, 2> input{1, 2};
  std::array<std::uint32_t, 2> output{};
  auto pipeline = tess::pipeline_from(std::span{input});
  static_assert(!HasToFrontier<decltype(pipeline), decltype(output)>);
  const auto collected = pipeline.collect_into(output);

  std::array<tess::diagnostics::TraceRecord, 1> trace_storage{};
  tess::diagnostics::TraceBuffer trace{trace_storage};
  static_assert(!HasCategoryStats<decltype(trace)>);
  EXPECT_EQ(trace.all_stats().size(), tess::diagnostics::trace_category_count);
  EXPECT_EQ(trace.stats(tess::diagnostics::TraceCategory::Path).samples, 0u);
  tess::diagnostics::TimingSnapshot timing;
  static_assert(!HasCategory<decltype(timing)>);
  EXPECT_EQ(timing.stats(tess::diagnostics::TraceCategory::Path).samples, 0u);

  static_assert(!std::is_copy_constructible_v<tess::ResumableWorkQueue<int>>);
  static_assert(!std::is_move_constructible_v<tess::ResumableWorkQueue<int>>);
  static_assert(!std::is_copy_constructible_v<tess::ResumableWorkTask<int>>);
  static_assert(!std::is_move_constructible_v<tess::ResumableWorkTask<int>>);
  EXPECT_EQ(collected.written, input.size());
}

}  // namespace
