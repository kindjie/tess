#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "allocation_counter.h"

namespace {

struct TerrainTag {};
struct CostTag {};

constexpr std::uint32_t DirtyTerrain = 1u << 0u;

using TopDown2D =
    tess::Shape<tess::Extent3{128, 64, 1}, tess::Extent3{32, 16, 1}>;
using Schema = tess::FieldSchema<tess::Field<TerrainTag, std::uint16_t>,
                                 tess::Field<CostTag, float>>;
using World = tess::AlwaysResidentWorld<TopDown2D, Schema>;

auto update_from_helper(tess::FrameOps& ops, tess::DomainDesc domain)
    -> tess::OpHandle {
  return ops.update_field(std::move(domain), tess::WritePolicy::ReadOnly);
}

auto planned_keys(const tess::ExecutionReport& report, std::size_t index)
    -> std::vector<tess::ChunkKey> {
  const auto ops = report.plan().operations();
  if (index >= ops.size()) {
    return {};
  }
  return {ops[index].chunks().begin(), ops[index].chunks().end()};
}

struct TaggedCustomSerialExecutor {
  using serial_execution_tag = void;

  template <typename Fn>
  auto for_each_operation(std::size_t first, std::size_t count, Fn&& fn) const
      -> tess::PlannedExecutionResult {
    auto&& callback = fn;
    const auto end = first + count;
    for (std::size_t i = first; i < end; ++i) {
      auto result = callback(i);
      if (result.status != tess::PlannedExecutionStatus::Executed) {
        return result;
      }
    }
    return tess::PlannedExecutionResult{};
  }
};

struct RangeRecordingPhaseExecutor {
  std::vector<tess::ExecutorPhaseRange> ranges;
  std::vector<std::size_t> indexes;

  template <typename Fn>
  auto for_each_operation(std::size_t first, std::size_t count, Fn&& fn)
      -> tess::PlannedExecutionResult {
    ranges.push_back(tess::ExecutorPhaseRange{first, count});
    auto&& callback = fn;
    const auto end = first + count;
    for (std::size_t i = first; i < end; ++i) {
      indexes.push_back(i);
      auto result = callback(i);
      if (result.status != tess::PlannedExecutionStatus::Executed) {
        return result;
      }
    }
    return tess::PlannedExecutionResult{};
  }
};

TEST(TessQueuedContract, PrebuiltPlannedBlockCtxIterationDoesNotAllocate) {
  World world;
  tess::FrameOps ops;

  (void)ops.update_field(tess::DomainDesc::resident_chunks(),
                         tess::WritePolicy::ReadOnly);
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());
  ASSERT_EQ(report.plan().operations().size(), 1u);

  tess_test::ScopedAllocationCounter counter;

  auto ctx = tess::try_planned_block_ctx<tess::WritePolicy::ReadOnly>(
      world, report.plan().operations()[0]);
  std::uint64_t key_sum = 0;
  std::size_t visited = 0;
  if (ctx) {
    ctx->for_each_chunk([&](auto view) {
      key_sum += view.key().value;
      ++visited;
    });
  }

  ASSERT_TRUE(ctx);
  EXPECT_EQ(visited, World::chunk_count);
  EXPECT_EQ(key_sum, 120u);
  EXPECT_EQ(counter.count(), 0u);
}

TEST(TessQueuedContract, SourceLocationIsCapturedForDiagnostics) {
  World world;
  tess::FrameOps ops;

  const auto handle =
      update_from_helper(ops, tess::DomainDesc::resident_chunks());
  const auto report = tess::plan_operations(world, ops);

  ASSERT_NE(ops.operation(handle), nullptr);
  ASSERT_EQ(report.operations().size(), 1u);
  const auto queued_source = ops.operation(handle)->source;
  const auto reported_source = report.operations()[0].source;

  EXPECT_GT(queued_source.line(), 0u);
  EXPECT_GT(reported_source.line(), 0u);
  EXPECT_EQ(queued_source.line(), reported_source.line());
  EXPECT_NE(std::string_view{queued_source.file_name()}.find(
                "tess_queued_contract_test.cc"),
            std::string_view::npos);
  EXPECT_NE(std::string_view{queued_source.function_name()}.find(
                "update_from_helper"),
            std::string_view::npos);
}

TEST(TessQueuedContract, CommonTypedIntentsPreservePayloadAndPlanningMetadata) {
  const std::vector<tess::PathRequest> paths{
      {{1, 2, 0}, {8, 9, 0}},
      {{3, 4, 0}, {10, 11, 0}},
  };
  const std::vector<tess::PathRequest> nearest{
      {{7, 8, 0}, {12, 13, 0}},
  };
  const std::vector<tess::MovementIntent> moves{
      {{1, 1, 0}, {2, 1, 0}, {}},
  };
  const std::vector<tess::ChunkKey> residency{{2}, {3}};
  const std::vector<std::uint32_t> field_products{17, 23};
  const std::vector<std::uint64_t> render_batches{41};

  auto metadata = tess::IntentMetadata{};
  metadata.versions = tess::IntentVersions{3, 5, 7, 11};
  metadata.invalidations =
      tess::IntentInvalidations{DirtyTerrain, true, true, false};
  metadata.backend = tess::BackendEligibility::CpuOrGpu;
  metadata.exactness = tess::ExactnessRequirement::Exact;

  tess::FrameOps ops;
  const auto path_handle =
      ops.query_paths(tess::PathBatchDesc::from(std::span{paths}, metadata));
  const auto nearest_handle =
      ops.query_nearest(tess::NearestBatchDesc::from(std::span{nearest}));
  const auto field_handle = ops.build_field_product(
      tess::FieldProductDesc::from(std::span{field_products}));
  const auto move_handle =
      ops.move_entities(tess::MoveBatchDesc::from(std::span{moves}));
  const auto topology_handle =
      ops.rebuild_topology(tess::TopologyRebuildDesc{});
  const auto residency_handle =
      ops.ensure_resident(tess::ResidencyDesc::from(std::span{residency}));
  const auto dirty_handle = ops.mark_dirty(
      tess::MarkDirtyDesc{tess::DomainDesc::resident_chunks(), DirtyTerrain});
  const auto render_handle = ops.publish_render_deltas(
      tess::RenderDeltaDesc::from(std::span{render_batches}));

  ASSERT_EQ(ops.size(), 8u);
  const auto* path = ops.operation(path_handle.operation);
  ASSERT_NE(path, nullptr);
  EXPECT_EQ(path->kind, tess::OperationKind::QueryPaths);
  EXPECT_EQ(path->payload.as<tess::PathRequest>().size(), 2u);
  EXPECT_EQ(path->payload.as<tess::MovementIntent>().size(), 0u);
  EXPECT_EQ(path->versions, metadata.versions);
  EXPECT_EQ(path->invalidations, metadata.invalidations);
  EXPECT_EQ(path->backend, tess::BackendEligibility::CpuOrGpu);
  EXPECT_EQ(path->exactness, tess::ExactnessRequirement::Exact);

  EXPECT_EQ(ops.operation(nearest_handle.operation)->kind,
            tess::OperationKind::QueryNearest);
  EXPECT_EQ(ops.operation(field_handle)->kind,
            tess::OperationKind::BuildFieldProduct);
  EXPECT_EQ(ops.operation(move_handle)->kind,
            tess::OperationKind::MoveEntities);
  EXPECT_EQ(ops.operation(topology_handle)->kind,
            tess::OperationKind::RebuildTopology);
  EXPECT_EQ(ops.operation(residency_handle)->kind,
            tess::OperationKind::EnsureResident);
  EXPECT_EQ(ops.operation(residency_handle)->payload.as<tess::ChunkKey>()[1],
            tess::ChunkKey{3});
  EXPECT_EQ(ops.operation(dirty_handle)->kind, tess::OperationKind::MarkDirty);
  EXPECT_EQ(ops.operation(dirty_handle)->field_access.dirty_mask, DirtyTerrain);
  EXPECT_EQ(ops.operation(render_handle)->kind,
            tess::OperationKind::PublishRenderDeltas);

  World world;
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());
  EXPECT_EQ(report.operations()[0].versions, metadata.versions);
  EXPECT_EQ(report.operations()[0].backend, tess::BackendEligibility::CpuOrGpu);
  ASSERT_EQ(report.plan().operations().size(), 8u);
  EXPECT_EQ(report.plan().operations()[0].versions, metadata.versions);
  EXPECT_EQ(report.plan().operations()[0].invalidations,
            metadata.invalidations);
  EXPECT_EQ(
      report.plan().operations()[0].payload.as<tess::PathRequest>().size(), 2u);
}

TEST(TessQueuedContract, InspectingQueuedAndPlannedOperationsDoesNotAllocate) {
  World world;
  tess::FrameOps ops;

  (void)ops.update_field(tess::DomainDesc::resident_chunks(),
                         tess::WritePolicy::ReadOnly);
  const auto report = tess::plan_operations(world, ops);

  tess_test::ScopedAllocationCounter counter;

  const auto queued = ops.operations();
  const auto* operation = ops.operation(tess::OpHandle{0});
  const auto reports = report.operations();
  const auto planned = report.plan().operations();

  ASSERT_NE(operation, nullptr);
  EXPECT_EQ(queued.size(), 1u);
  EXPECT_EQ(operation->id, (tess::OpId{0}));
  EXPECT_EQ(reports.size(), 1u);
  EXPECT_EQ(planned.size(), 1u);
  EXPECT_EQ(counter.count(), 0u);
}

TEST(TessQueuedContract, DefaultConstructedIdsAndHandlesCompareEqualToZero) {
  tess::OpId id;
  tess::OpHandle handle;

  EXPECT_EQ(id, (tess::OpId{0}));
  EXPECT_EQ(handle, (tess::OpHandle{0}));
  EXPECT_EQ(id.value, 0u);
  EXPECT_EQ(handle.value, 0u);
}

TEST(TessQueuedContract,
     ExecutePhaseDeferredDirtyWithRunsTaggedCustomExecutor) {
  World world;
  tess::FrameOps ops;
  tess::PlannedDirtyAccumulator dirty;
  const TaggedCustomSerialExecutor executor;
  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      DirtyTerrain,
      DirtyTerrain,
  };
  const std::vector<tess::ChunkKey> first_keys{tess::ChunkKey{1}};
  const std::vector<tess::ChunkKey> second_keys{tess::ChunkKey{2}};

  (void)ops.update_field(tess::DomainDesc::explicit_chunks(first_keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(second_keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());
  const auto phases = tess::plan_parallel_execution_phases(report.plan());
  ASSERT_TRUE(phases.ok());
  ASSERT_EQ(phases.phases().size(), 1u);

  const auto result = tess::execute_phase_deferred_dirty_with<
      tess::WritePolicy::UniquePerChunk>(
      executor, world, report.plan(), phases.phases()[0], dirty, [](auto view) {
        auto terrain = view.template field_span<TerrainTag>();
        terrain[0] = static_cast<std::uint16_t>(view.key().value + 140);
      });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::Executed);
  EXPECT_EQ(result.chunk_count, 2u);
  EXPECT_EQ(dirty.records().size(), 2u);
  EXPECT_EQ(world.chunk(tess::ChunkKey{1})
                .template field<TerrainTag>(tess::LocalTileId{0}),
            141u);
  EXPECT_EQ(world.chunk(tess::ChunkKey{2})
                .template field<TerrainTag>(tess::LocalTileId{0}),
            142u);
}

TEST(TessQueuedContract, FrameOpsClearRestartsIdsAndReusesCapacity) {
  World world;
  tess::FrameOps ops;
  const auto bounds = tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}};
  world.mark_dirty(tess::ChunkKey{2}, DirtyTerrain, bounds);

  const auto first = ops.update_field(tess::DomainDesc::resident_chunks(),
                                      tess::WritePolicy::ReadOnly);
  const auto second =
      ops.update_field(tess::DomainDesc::dirty_chunks(DirtyTerrain),
                       tess::WritePolicy::ReadOnly);
  const auto cold_report = tess::plan_operations(world, ops);

  ASSERT_TRUE(cold_report.ok());
  EXPECT_EQ(first, (tess::OpHandle{0}));
  EXPECT_EQ(second, (tess::OpHandle{1}));

  ops.clear();

  EXPECT_TRUE(ops.empty());
  EXPECT_EQ(ops.size(), 0u);
  EXPECT_EQ(ops.operation(tess::OpHandle{0}), nullptr);

  {
    tess_test::ScopedAllocationCounter counter;

    const auto warm_first = ops.update_field(
        tess::DomainDesc::resident_chunks(), tess::WritePolicy::ReadOnly);
    const auto warm_second =
        ops.update_field(tess::DomainDesc::dirty_chunks(DirtyTerrain),
                         tess::WritePolicy::ReadOnly);

#if !defined(_MSC_VER) || defined(NDEBUG)
    EXPECT_EQ(counter.count(), 0u);
#endif
    EXPECT_EQ(warm_first, (tess::OpHandle{0}));
    EXPECT_EQ(warm_second, (tess::OpHandle{1}));
  }

  ASSERT_NE(ops.operation(tess::OpHandle{0}), nullptr);
  ASSERT_NE(ops.operation(tess::OpHandle{1}), nullptr);
  EXPECT_EQ(ops.operation(tess::OpHandle{0})->id, (tess::OpId{0}));
  EXPECT_EQ(ops.operation(tess::OpHandle{1})->id, (tess::OpId{1}));

  const auto warm_report = tess::plan_operations(world, ops);
  ASSERT_TRUE(warm_report.ok());
  EXPECT_EQ(warm_report.planned_count(), 2u);
}

TEST(TessQueuedContract, WriteThenReadOverlapConflictsWithoutBarrier) {
  World world;
  tess::FrameOps ops;
  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      DirtyTerrain,
      DirtyTerrain,
  };
  constexpr auto reads_terrain = tess::FieldAccessDesc{
      DirtyTerrain,
      0,
      0,
  };
  const std::vector<tess::ChunkKey> keys{tess::ChunkKey{3}};

  const auto writer =
      ops.update_field(tess::DomainDesc::explicit_chunks(keys), writes_terrain,
                       tess::WritePolicy::UniquePerChunk);
  const auto reader =
      ops.update_field(tess::DomainDesc::explicit_chunks(keys), reads_terrain,
                       tess::WritePolicy::ReadOnly);

  const auto report = tess::plan_operations(world, ops);

  ASSERT_FALSE(report.ok());
  EXPECT_EQ(report.planned_count(), 1u);
  EXPECT_EQ(report.failed_count(), 1u);
  ASSERT_NE(report.find(reader), nullptr);
  EXPECT_EQ(report.find(reader)->status, tess::OperationStatus::HazardConflict);
  EXPECT_EQ(report.find(reader)->failure,
            tess::OperationFailure::FieldHazardConflict);
  EXPECT_EQ(report.find(reader)->conflict_handle, writer);
  EXPECT_EQ(report.find(reader)->conflict_mask, DirtyTerrain);
  ASSERT_EQ(report.plan().operations().size(), 1u);
  EXPECT_EQ(report.plan().operations()[0].handle, writer);
}

TEST(TessQueuedContract,
     ParallelPhasesSeparateMutationFromLaterReaderOnSameChunk) {
  World world;
  tess::FrameOps ops;
  constexpr auto reads_terrain = tess::FieldAccessDesc{
      DirtyTerrain,
      0,
      0,
  };
  const std::vector<tess::ChunkKey> keys{tess::ChunkKey{3}};

  (void)ops.update_field(tess::DomainDesc::explicit_chunks(keys),
                         tess::WritePolicy::UniquePerChunk);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(keys), reads_terrain,
                         tess::WritePolicy::ReadOnly);
  const auto report = tess::plan_operations(world, ops);

  ASSERT_TRUE(report.ok());
  const auto phases = tess::plan_parallel_execution_phases(report.plan());

  ASSERT_TRUE(phases.ok());
  ASSERT_EQ(phases.phases().size(), 2u);
  EXPECT_EQ(phases.phases()[0].first_operation(), 0u);
  EXPECT_EQ(phases.phases()[0].operation_count(), 1u);
  EXPECT_EQ(phases.phases()[1].first_operation(), 1u);
  EXPECT_EQ(phases.phases()[1].operation_count(), 1u);
}

TEST(TessQueuedContract, ExecutorPhaseRangeCopiesExecutionPhaseRange) {
  World world;
  tess::FrameOps ops;
  for (std::uint64_t chunk = 0; chunk < 4; ++chunk) {
    const auto keys = std::vector{tess::ChunkKey{chunk}};
    (void)ops.update_field(tess::DomainDesc::explicit_chunks(keys),
                           tess::WritePolicy::ReadOnly);
  }
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());
  const auto phases = tess::plan_parallel_execution_phases(report.plan());
  ASSERT_TRUE(phases.ok());
  ASSERT_EQ(phases.phases().size(), 1u);

  const auto range = tess::executor_phase_range(phases.phases()[0]);

  EXPECT_EQ(range.first_operation, 0u);
  EXPECT_EQ(range.operation_count, 4u);
}

TEST(TessQueuedContract, ExecuteOperationIndexRangeDeliversContiguousIndexes) {
  RangeRecordingPhaseExecutor executor;
  std::vector<std::size_t> visited;

  const auto result = tess::execute_operation_index_range(
      executor, tess::ExecutorPhaseRange{2, 3}, [&](std::size_t index) {
        visited.push_back(index);
        return tess::PlannedExecutionResult{};
      });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::Executed);
  ASSERT_EQ(executor.ranges.size(), 1u);
  EXPECT_EQ(executor.ranges[0].first_operation, 2u);
  EXPECT_EQ(executor.ranges[0].operation_count, 3u);
  EXPECT_EQ(executor.indexes, (std::vector<std::size_t>{2, 3, 4}));
  EXPECT_EQ(visited, (std::vector<std::size_t>{2, 3, 4}));
}

TEST(TessQueuedContract, DeferredPhaseHelperExecutesPlannerIssuedFullPhase) {
  World world;
  tess::FrameOps ops;
  tess::PlannedDirtyAccumulator dirty;
  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      DirtyTerrain,
      DirtyTerrain,
  };
  const std::vector<tess::ChunkKey> first_keys{tess::ChunkKey{1}};
  const std::vector<tess::ChunkKey> second_keys{tess::ChunkKey{2}};

  (void)ops.update_field(tess::DomainDesc::explicit_chunks(first_keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(second_keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());
  const auto phases = tess::plan_parallel_execution_phases(report.plan());
  ASSERT_TRUE(phases.ok());
  ASSERT_EQ(phases.phases().size(), 1u);
  EXPECT_EQ(phases.phases()[0].first_operation(), 0u);
  EXPECT_EQ(phases.phases()[0].operation_count(), 2u);

  const auto full =
      tess::execute_phase_deferred_dirty<tess::WritePolicy::UniquePerChunk>(
          world, report.plan(), phases.phases()[0], dirty, [](auto view) {
            auto terrain = view.template field_span<TerrainTag>();
            terrain[0] = static_cast<std::uint16_t>(view.key().value + 150);
          });

  EXPECT_EQ(full.status, tess::PlannedExecutionStatus::Executed);
  EXPECT_EQ(full.chunk_count, 2u);
  EXPECT_EQ(dirty.records().size(), 2u);
}

TEST(TessQueuedContract, PartitionedPhaseHelperExecutesPlannerIssuedFullPhase) {
  World world;
  tess::FrameOps ops;
  tess::PlannedPhaseExecutionScratch scratch;
  const tess::SerialPhaseExecutor executor;
  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      DirtyTerrain,
      DirtyTerrain,
  };
  const std::vector<tess::ChunkKey> first_keys{tess::ChunkKey{1}};
  const std::vector<tess::ChunkKey> second_keys{tess::ChunkKey{2}};

  (void)ops.update_field(tess::DomainDesc::explicit_chunks(first_keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(second_keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());
  const auto phases = tess::plan_parallel_execution_phases(report.plan());
  ASSERT_TRUE(phases.ok());
  ASSERT_EQ(phases.phases().size(), 1u);
  EXPECT_EQ(phases.phases()[0].first_operation(), 0u);
  EXPECT_EQ(phases.phases()[0].operation_count(), 2u);

  const auto full = tess::execute_phase_partitioned_dirty_with<
      tess::WritePolicy::UniquePerChunk>(
      executor, world, report.plan(), phases.phases()[0], scratch,
      [](auto view) {
        auto terrain = view.template field_span<TerrainTag>();
        terrain[0] = static_cast<std::uint16_t>(view.key().value + 160);
      });

  EXPECT_EQ(full.status, tess::PlannedExecutionStatus::Executed);
  EXPECT_EQ(full.chunk_count, 2u);
  EXPECT_EQ(scratch.operation_count(), 2u);

  const auto merged = tess::merge_planned_dirty(world, scratch);
  EXPECT_EQ(merged.status, tess::PlannedDirtyMergeStatus::Merged);
  EXPECT_EQ(merged.merged_chunk_count, 2u);
}

TEST(TessQueuedContract, EmptyPlanProducesNoPhasesAndRawRangeExecutesNothing) {
  World world;
  const tess::FrameOps ops;

  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());
  ASSERT_TRUE(report.plan().empty());

  const auto phases = tess::plan_parallel_execution_phases(report.plan());

  EXPECT_TRUE(phases.ok());
  EXPECT_EQ(phases.status(), tess::ExecutionPhaseStatus::Ready);
  EXPECT_TRUE(phases.phases().empty());

  auto called = false;
  const auto result = tess::execute_operation_index_range(
      tess::SerialPhaseExecutor{}, tess::ExecutorPhaseRange{0, 0},
      [&](std::size_t) {
        called = true;
        return tess::PlannedExecutionResult{};
      });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::Executed);
  EXPECT_EQ(result.chunk_count, 0u);
  EXPECT_FALSE(called);
}

TEST(TessQueuedContract, DuplicateExplicitChunkKeysCollapseToOnePlannedVisit) {
  World world;
  tess::FrameOps ops;
  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      DirtyTerrain,
      DirtyTerrain,
  };
  const std::vector<tess::ChunkKey> requested{
      tess::ChunkKey{3},
      tess::ChunkKey{3},
      tess::ChunkKey{1},
      tess::ChunkKey{3},
  };

  (void)ops.update_field(tess::DomainDesc::explicit_chunks(requested),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);

  ASSERT_TRUE(report.ok());
  ASSERT_EQ(report.plan().operations().size(), 1u);
  EXPECT_EQ(report.operations()[0].chunk_count, 2u);
  EXPECT_EQ(planned_keys(report, 0), (std::vector<tess::ChunkKey>{
                                         tess::ChunkKey{1},
                                         tess::ChunkKey{3},
                                     }));

  std::vector<tess::ChunkKey> visited;
  const auto result =
      tess::execute_planned_operation<tess::WritePolicy::UniquePerChunk>(
          world, report.plan().operations()[0],
          [&](auto view) { visited.push_back(view.key()); });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::Executed);
  EXPECT_EQ(result.chunk_count, 2u);
  EXPECT_EQ(visited, (std::vector<tess::ChunkKey>{
                         tess::ChunkKey{1},
                         tess::ChunkKey{3},
                     }));
}

TEST(TessQueuedContract, EmptyExplicitChunkDomainPlansAndExecutesZeroChunks) {
  World world;
  tess::FrameOps ops;

  (void)ops.update_field(
      tess::DomainDesc::explicit_chunks(std::span<const tess::ChunkKey>{}),
      tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);

  ASSERT_TRUE(report.ok());
  ASSERT_EQ(report.plan().operations().size(), 1u);
  EXPECT_EQ(report.operations()[0].chunk_count, 0u);
  EXPECT_TRUE(report.plan().operations()[0].chunks().empty());

  const auto phases = tess::plan_parallel_execution_phases(report.plan());
  ASSERT_TRUE(phases.ok());
  ASSERT_EQ(phases.phases().size(), 1u);

  auto called = false;
  const auto result =
      tess::execute_planned_operation<tess::WritePolicy::UniquePerChunk>(
          world, report.plan().operations()[0], [&](auto) { called = true; });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::Executed);
  EXPECT_EQ(result.chunk_count, 0u);
  EXPECT_FALSE(called);
}

TEST(TessQueuedContract,
     ScopedThreadPhaseExecutorClampsWorkersAndSkipsEmptyRanges) {
  const tess::ScopedThreadPhaseExecutor zero_workers{0};
  EXPECT_EQ(zero_workers.worker_count(), 1u);

  const tess::ScopedThreadPhaseExecutor defaulted;
  EXPECT_GE(defaulted.worker_count(), 1u);

  const tess::ScopedThreadPhaseExecutor executor{2};
  auto called = false;
  const auto result = tess::execute_operation_index_range(
      executor, tess::ExecutorPhaseRange{5, 0}, [&](std::size_t) {
        called = true;
        return tess::PlannedExecutionResult{};
      });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::Executed);
  EXPECT_EQ(result.chunk_count, 0u);
  EXPECT_FALSE(called);
}

TEST(TessQueuedContract, ThrowingImmediateCallbackMarksDirtyBeforePropagation) {
  World world;
  tess::FrameOps ops;
  constexpr auto key = tess::ChunkKey{1};
  const std::vector<tess::ChunkKey> requested{key};

  (void)ops.update_field(tess::DomainDesc::explicit_chunks(requested),
                         tess::FieldAccessDesc{0, DirtyTerrain, DirtyTerrain},
                         tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());

  EXPECT_THROW(
      (tess::execute_planned_operation<tess::WritePolicy::UniquePerChunk>(
          world, report.plan().operations()[0],
          [](auto) { throw std::runtime_error{"callback failed"}; })),
      std::runtime_error);

  EXPECT_EQ(world.dirty_flags(key), DirtyTerrain);
  EXPECT_EQ(world.meta(key).version, 1u);
  EXPECT_EQ(world.dirty_bounds(key),
            (tess::Box3{tess::Coord3{32, 0, 0}, tess::Extent3{32, 16, 1}}));
}

TEST(TessQueuedContract,
     ThrowingDeferredCallbackRetainsConservativeDirtyRecord) {
  World world;
  tess::FrameOps ops;
  tess::PlannedDirtyAccumulator dirty;
  constexpr auto key = tess::ChunkKey{1};
  const std::vector<tess::ChunkKey> requested{key};

  (void)ops.update_field(tess::DomainDesc::explicit_chunks(requested),
                         tess::FieldAccessDesc{0, DirtyTerrain, DirtyTerrain},
                         tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());

  EXPECT_THROW((tess::execute_planned_operation_deferred_dirty<
                   tess::WritePolicy::UniquePerChunk>(
                   world, report.plan().operations()[0], dirty,
                   [](auto) { throw std::runtime_error{"callback failed"}; })),
               std::runtime_error);

  ASSERT_EQ(dirty.records().size(), 1u);
  EXPECT_EQ(dirty.records()[0].chunk, key);
  EXPECT_EQ(world.meta(key).version, 0u);

  const auto merged = tess::merge_planned_dirty(world, dirty);
  EXPECT_EQ(merged.status, tess::PlannedDirtyMergeStatus::Merged);
  EXPECT_EQ(merged.merged_chunk_count, 1u);
  EXPECT_EQ(world.dirty_flags(key), DirtyTerrain);
  EXPECT_EQ(world.meta(key).version, 1u);
  EXPECT_EQ(world.dirty_bounds(key),
            (tess::Box3{tess::Coord3{32, 0, 0}, tess::Extent3{32, 16, 1}}));
}

TEST(TessQueuedContract,
     ThrowingPhaseCallbackRetainsConservativeDirtyPartition) {
  World world;
  tess::FrameOps ops;
  tess::PlannedPhaseExecutionScratch scratch;
  constexpr auto key = tess::ChunkKey{1};
  const std::vector<tess::ChunkKey> requested{key};

  (void)ops.update_field(tess::DomainDesc::explicit_chunks(requested),
                         tess::FieldAccessDesc{0, DirtyTerrain, DirtyTerrain},
                         tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());
  const auto phases = tess::plan_parallel_execution_phases(report.plan());
  ASSERT_TRUE(phases.ok());
  ASSERT_EQ(phases.phases().size(), 1u);

  EXPECT_THROW(
      (tess::execute_phase_partitioned_dirty_with<
          tess::WritePolicy::UniquePerChunk>(
          tess::SerialPhaseExecutor{}, world, report.plan(), phases.phases()[0],
          scratch, [](auto) { throw std::runtime_error{"callback failed"}; })),
      std::runtime_error);

  ASSERT_EQ(scratch.dirty_partitions().size(), 1u);
  ASSERT_EQ(scratch.dirty_partitions()[0].records().size(), 1u);
  EXPECT_EQ(scratch.dirty_partitions()[0].records()[0].chunk, key);
  EXPECT_EQ(world.meta(key).version, 0u);

  const auto merged = tess::merge_planned_dirty(world, scratch);
  EXPECT_EQ(merged.status, tess::PlannedDirtyMergeStatus::Merged);
  EXPECT_EQ(merged.merged_chunk_count, 1u);
  EXPECT_EQ(world.dirty_flags(key), DirtyTerrain);
  EXPECT_EQ(world.meta(key).version, 1u);
  EXPECT_EQ(world.dirty_bounds(key),
            (tess::Box3{tess::Coord3{32, 0, 0}, tess::Extent3{32, 16, 1}}));
}

TEST(TessQueuedContract, OverlappingReadOnlyDirtyRecordsCoalesceOnce) {
  World world;
  tess::FrameOps ops;
  tess::PlannedPhaseExecutionScratch scratch;
  constexpr auto key = tess::ChunkKey{1};
  constexpr auto marks_terrain = tess::FieldAccessDesc{
      0,
      0,
      DirtyTerrain,
  };
  const std::vector<tess::ChunkKey> requested{key};

  (void)ops.update_field(tess::DomainDesc::explicit_chunks(requested),
                         marks_terrain, tess::WritePolicy::ReadOnly);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(requested),
                         marks_terrain, tess::WritePolicy::ReadOnly);
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());
  const auto phases = tess::plan_parallel_execution_phases(report.plan());
  ASSERT_TRUE(phases.ok());
  ASSERT_EQ(phases.phases().size(), 1u);
  ASSERT_EQ(phases.phases()[0].operation_count(), 2u);

  const auto result =
      tess::execute_phase_partitioned_dirty_with<tess::WritePolicy::ReadOnly>(
          tess::SerialPhaseExecutor{}, world, report.plan(), phases.phases()[0],
          scratch, [](auto) {});
  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::Executed);

  const auto merged = tess::merge_planned_dirty(world, scratch);
  EXPECT_EQ(merged.status, tess::PlannedDirtyMergeStatus::Merged);
  EXPECT_EQ(merged.merged_chunk_count, 1u);
  EXPECT_EQ(world.dirty_flags(key), DirtyTerrain);
  EXPECT_EQ(world.meta(key).version, 1u);
  EXPECT_EQ(world.dirty_bounds(key),
            (tess::Box3{tess::Coord3{32, 0, 0}, tess::Extent3{32, 16, 1}}));
}

}  // namespace
