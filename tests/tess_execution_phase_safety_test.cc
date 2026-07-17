#include <gtest/gtest.h>
#include <tess/tess.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

struct TerrainTag {};

using PhaseShape =
    tess::Shape<tess::Extent3{64, 32, 1}, tess::Extent3{32, 32, 1}>;
using PhaseSchema = tess::FieldSchema<tess::Field<TerrainTag, std::uint16_t>>;
using PhaseWorld = tess::AlwaysResidentWorld<PhaseShape, PhaseSchema>;
using ForeignPhaseShape =
    tess::Shape<tess::Extent3{32, 64, 1}, tess::Extent3{32, 32, 1}>;
using ForeignPhaseWorld =
    tess::AlwaysResidentWorld<ForeignPhaseShape, PhaseSchema>;

static_assert(PhaseWorld::chunk_count == ForeignPhaseWorld::chunk_count);

static_assert(!std::is_aggregate_v<tess::ExecutionPhase>);
static_assert(!std::is_default_constructible_v<tess::ExecutionPhase>);
static_assert(
    !std::is_constructible_v<tess::ExecutionPhase, std::size_t, std::size_t>);

void enqueue_unique(tess::FrameOps& ops, tess::ChunkKey chunk) {
  const auto chunks = std::vector{chunk};
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(chunks),
                         tess::WritePolicy::UniquePerChunk);
}

TEST(TessExecutionPhaseSafety, PhaseFromAnotherPlanCannotDispatchOperations) {
  PhaseWorld world;

  tess::FrameOps safe_ops;
  enqueue_unique(safe_ops, tess::ChunkKey{0});
  enqueue_unique(safe_ops, tess::ChunkKey{1});
  const auto safe_report = tess::plan_operations(world, safe_ops);
  ASSERT_TRUE(safe_report.ok());
  const auto safe_phases =
      tess::plan_parallel_execution_phases(safe_report.plan());
  ASSERT_TRUE(safe_phases.ok());
  ASSERT_EQ(safe_phases.phases().size(), 1u);
  ASSERT_EQ(safe_phases.phases()[0].operation_count(), 2u);

  // Zero field masks let both same-chunk mutations pass hazard validation,
  // but parallel-phase planning must still separate their chunk ownership.
  tess::FrameOps conflicting_ops;
  enqueue_unique(conflicting_ops, tess::ChunkKey{0});
  enqueue_unique(conflicting_ops, tess::ChunkKey{0});
  const auto conflicting_report = tess::plan_operations(world, conflicting_ops);
  ASSERT_TRUE(conflicting_report.ok());
  const auto conflicting_phases =
      tess::plan_parallel_execution_phases(conflicting_report.plan());
  ASSERT_TRUE(conflicting_phases.ok());
  ASSERT_EQ(conflicting_phases.phases().size(), 2u);

  const auto foreign_phase = safe_phases.phases()[0];
  std::atomic_size_t callback_calls{0};

  tess::PlannedDirtyAccumulator dirty;
  const auto serial_result =
      tess::execute_phase_deferred_dirty<tess::WritePolicy::UniquePerChunk>(
          world, conflicting_report.plan(), foreign_phase, dirty, [&](auto) {
            callback_calls.fetch_add(1, std::memory_order_relaxed);
          });

  tess::ScopedThreadPhaseExecutor executor{2};
  tess::PlannedPhaseExecutionScratch scratch;
  const auto partitioned_result = tess::execute_phase_partitioned_dirty_with<
      tess::WritePolicy::UniquePerChunk>(
      executor, world, conflicting_report.plan(), foreign_phase, scratch,
      [&](auto) { callback_calls.fetch_add(1, std::memory_order_relaxed); });

  tess::PlannedPhaseExecutionScratch result_scratch;
  tess::ResultChannel<std::uint32_t> channel;
  const auto result_result = tess::execute_phase_partitioned_dirty_with_results<
      tess::WritePolicy::UniquePerChunk>(
      executor, world, conflicting_report.plan(), foreign_phase, result_scratch,
      channel, [&](auto, std::uint32_t&) {
        callback_calls.fetch_add(1, std::memory_order_relaxed);
      });

  EXPECT_EQ(serial_result.status, tess::PlannedExecutionStatus::InvalidPhase);
  EXPECT_EQ(partitioned_result.status,
            tess::PlannedExecutionStatus::InvalidPhase);
  EXPECT_EQ(result_result.status, tess::PlannedExecutionStatus::InvalidPhase);
  EXPECT_EQ(callback_calls.load(std::memory_order_relaxed), 0u);
  EXPECT_TRUE(dirty.records().empty());
  EXPECT_EQ(scratch.operation_count(), 0u);
  EXPECT_EQ(result_scratch.operation_count(), 0u);
  EXPECT_EQ(channel.size(), 0u);
}

TEST(TessExecutionPhaseSafety,
     WrongWorldRejectsBeforeScratchChannelOrCallbackMutation) {
  PhaseWorld planning_world;
  ForeignPhaseWorld execution_world;
  tess::FrameOps ops;
  enqueue_unique(ops, tess::ChunkKey{0});
  const auto report = tess::plan_operations(planning_world, ops);
  ASSERT_TRUE(report.ok());
  const auto phases = tess::plan_parallel_execution_phases(report.plan());
  ASSERT_TRUE(phases.ok());
  ASSERT_EQ(phases.phases().size(), 1u);

  std::atomic_size_t callback_calls{0};
  tess::SerialPhaseExecutor executor;
  tess::PlannedPhaseExecutionScratch scratch;
  tess::ResultChannel<std::uint32_t> channel;
  const auto result = tess::execute_phase_partitioned_dirty_with_results<
      tess::WritePolicy::UniquePerChunk>(
      executor, execution_world, report.plan(), phases.phases()[0], scratch,
      channel, [&](auto, std::uint32_t&) {
        callback_calls.fetch_add(1, std::memory_order_relaxed);
      });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::InvalidShape);
  EXPECT_EQ(result.chunk_count, 0u);
  EXPECT_EQ(callback_calls.load(std::memory_order_relaxed), 0u);
  EXPECT_EQ(scratch.operation_count(), 0u);
  EXPECT_EQ(channel.size(), 0u);
  EXPECT_EQ(execution_world.chunk(tess::ChunkKey{0})
                .template field<TerrainTag>(tess::LocalTileId{0}),
            0u);
}

TEST(TessExecutionPhaseSafety,
     ForeignDirtyAccumulatorRejectsBeforeSerialPhaseCallback) {
  PhaseWorld world;
  ForeignPhaseWorld foreign_world;
  tess::FrameOps ops;
  enqueue_unique(ops, tess::ChunkKey{0});
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());
  const auto phases = tess::plan_parallel_execution_phases(report.plan());
  ASSERT_TRUE(phases.ok());
  ASSERT_EQ(phases.phases().size(), 1u);

  tess::PlannedDirtyAccumulator dirty;
  ASSERT_EQ(
      dirty.record(foreign_world, tess::ChunkKey{0}, 1u,
                   tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}}),
      tess::PlannedDirtyRecordStatus::Recorded);
  std::atomic_size_t callback_calls{0};
  const auto result =
      tess::execute_phase_deferred_dirty<tess::WritePolicy::UniquePerChunk>(
          world, report.plan(), phases.phases()[0], dirty, [&](auto) {
            callback_calls.fetch_add(1, std::memory_order_relaxed);
          });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::InvalidShape);
  EXPECT_EQ(result.chunk_count, 0u);
  EXPECT_EQ(callback_calls.load(std::memory_order_relaxed), 0u);
  EXPECT_EQ(dirty.records().size(), 1u);
  EXPECT_EQ(world.chunk(tess::ChunkKey{0})
                .template field<TerrainTag>(tess::LocalTileId{0}),
            0u);
}

TEST(TessExecutionPhaseSafety, ReplanningAReportExpiresItsIssuedPhases) {
  PhaseWorld world;
  tess::ExecutionReport report;

  tess::FrameOps safe_ops;
  enqueue_unique(safe_ops, tess::ChunkKey{0});
  enqueue_unique(safe_ops, tess::ChunkKey{1});
  tess::plan_operations(world, safe_ops, report);
  ASSERT_TRUE(report.ok());
  const auto safe_phases = tess::plan_parallel_execution_phases(report.plan());
  ASSERT_TRUE(safe_phases.ok());
  ASSERT_EQ(safe_phases.phases().size(), 1u);
  const auto expired_phase = safe_phases.phases()[0];

  tess::FrameOps conflicting_ops;
  enqueue_unique(conflicting_ops, tess::ChunkKey{0});
  enqueue_unique(conflicting_ops, tess::ChunkKey{0});
  tess::plan_operations(world, conflicting_ops, report);
  ASSERT_TRUE(report.ok());
  const auto conflicting_phases =
      tess::plan_parallel_execution_phases(report.plan());
  ASSERT_TRUE(conflicting_phases.ok());
  ASSERT_EQ(conflicting_phases.phases().size(), 2u);

  std::atomic_size_t callback_calls{0};
  tess::ScopedThreadPhaseExecutor executor{2};
  tess::PlannedPhaseExecutionScratch scratch;
  const auto result = tess::execute_phase_partitioned_dirty_with<
      tess::WritePolicy::UniquePerChunk>(
      executor, world, report.plan(), expired_phase, scratch,
      [&](auto) { callback_calls.fetch_add(1, std::memory_order_relaxed); });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::InvalidPhase);
  EXPECT_EQ(callback_calls.load(std::memory_order_relaxed), 0u);
  EXPECT_EQ(scratch.operation_count(), 0u);
}

TEST(TessExecutionPhaseSafety, ReplacingAReportExpiresItsIssuedPhases) {
  PhaseWorld world;

  tess::FrameOps safe_ops;
  enqueue_unique(safe_ops, tess::ChunkKey{0});
  enqueue_unique(safe_ops, tess::ChunkKey{1});
  auto report = tess::plan_operations(world, safe_ops);
  ASSERT_TRUE(report.ok());
  const auto safe_phases = tess::plan_parallel_execution_phases(report.plan());
  ASSERT_TRUE(safe_phases.ok());
  ASSERT_EQ(safe_phases.phases().size(), 1u);
  const auto expired_phase = safe_phases.phases()[0];

  tess::FrameOps conflicting_ops;
  enqueue_unique(conflicting_ops, tess::ChunkKey{0});
  enqueue_unique(conflicting_ops, tess::ChunkKey{0});
  auto replacement = tess::plan_operations(world, conflicting_ops);
  ASSERT_TRUE(replacement.ok());
  report = std::move(replacement);
  const auto conflicting_phases =
      tess::plan_parallel_execution_phases(report.plan());
  ASSERT_TRUE(conflicting_phases.ok());
  ASSERT_EQ(conflicting_phases.phases().size(), 2u);

  std::atomic_size_t callback_calls{0};
  tess::ScopedThreadPhaseExecutor executor{2};
  tess::PlannedPhaseExecutionScratch scratch;
  const auto result = tess::execute_phase_partitioned_dirty_with<
      tess::WritePolicy::UniquePerChunk>(
      executor, world, report.plan(), expired_phase, scratch,
      [&](auto) { callback_calls.fetch_add(1, std::memory_order_relaxed); });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::InvalidPhase);
  EXPECT_EQ(callback_calls.load(std::memory_order_relaxed), 0u);
  EXPECT_EQ(scratch.operation_count(), 0u);
}

}  // namespace
