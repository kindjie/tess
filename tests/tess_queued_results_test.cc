#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "allocation_counter.h"

// S6.1: the ResultChannel core -- completion semantics, plan-time failure
// delivery through record_plan_completions, drain order and drain-once,
// paired-clear generations, and allocation-free warm reuse. Result-bearing
// EXECUTION (Ready slots with values) lands with the execute wrappers in the
// next slice and is covered by the conformance extension there.
namespace {

struct TerrainTag {};
struct CostTag {};

constexpr std::uint32_t DirtyTerrain = 1u << 0u;

using TopDown2D =
    tess::Shape<tess::Extent3{64, 32, 1}, tess::Extent3{32, 16, 1}>;
using Schema = tess::FieldSchema<tess::Field<TerrainTag, std::uint16_t>,
                                 tess::Field<CostTag, float>>;
using World = tess::AlwaysResidentWorld<TopDown2D, Schema>;

struct Ack {
  std::size_t tiles = 0;
};

// One valid op and one plan-time rejection (write mask under ReadOnly).
auto plan_one_good_one_rejected(World& world, tess::FrameOps& ops)
    -> tess::ExecutionReport {
  (void)ops.update_field(
      tess::DomainDesc::resident_chunks(),
      tess::FieldAccessDesc{DirtyTerrain, DirtyTerrain, DirtyTerrain},
      tess::WritePolicy::UniquePerChunk);
  (void)ops.update_field(
      tess::DomainDesc::resident_chunks(),
      tess::FieldAccessDesc{DirtyTerrain, DirtyTerrain, DirtyTerrain},
      tess::WritePolicy::ReadOnly);
  return tess::plan_operations(world, ops);
}

TEST(TessQueuedResults, DefaultCompletionIsNeverOk) {
  const tess::OpCompletion fresh{};
  EXPECT_FALSE(fresh.ok());
  // The success triple alone is not enough: the record must be stamped.
  tess::OpCompletion stamped{};
  stamped.completed = true;
  EXPECT_TRUE(stamped.ok());
  stamped.failure = tess::OperationFailure::ReadOnlyWriteMask;
  EXPECT_FALSE(stamped.ok());
}

TEST(TessQueuedResults, PlanRejectionsDeliverReasonsNotValues) {
  World world;
  tess::FrameOps ops;
  const auto report = plan_one_good_one_rejected(world, ops);
  ASSERT_EQ(report.operations().size(), 2u);

  tess::ResultChannel<Ack> channel;
  channel.reserve_operations(2);
  EXPECT_EQ(tess::record_plan_completions(report, channel), 1u);

  // The planned op has no slot; the rejected one is Failed with its report
  // reasons and enqueue-site source.
  EXPECT_EQ(channel.state(tess::OpHandle{0}), tess::OpResultState::Unbound);
  EXPECT_EQ(channel.state(tess::OpHandle{1}), tess::OpResultState::Failed);
  const auto completion = channel.completion(tess::OpHandle{1});
  EXPECT_TRUE(completion.completed);
  EXPECT_FALSE(completion.ok());
  EXPECT_EQ(completion.status, tess::OperationStatus::InvalidFieldAccess);
  EXPECT_EQ(completion.failure, tess::OperationFailure::ReadOnlyWriteMask);

  std::size_t visited = 0;
  const auto drained = channel.drain_results([&](tess::OpHandle handle,
                                                 const tess::OpCompletion& ack,
                                                 const Ack* value) {
    ++visited;
    EXPECT_EQ(handle.value, 1u);
    EXPECT_FALSE(ack.ok());
    EXPECT_EQ(value, nullptr);  // failures deliver reasons, not values
  });
  EXPECT_EQ(drained, 1u);
  EXPECT_EQ(visited, 1u);
}

TEST(TessQueuedResults, DrainVisitsHandleOrderExactlyOnce) {
  World world;
  tess::FrameOps ops;
  // Three rejected ops -> three Failed slots at handles 0..2.
  for (int i = 0; i < 3; ++i) {
    (void)ops.update_field(
        tess::DomainDesc::resident_chunks(),
        tess::FieldAccessDesc{DirtyTerrain, DirtyTerrain, DirtyTerrain},
        tess::WritePolicy::ReadOnly);
  }
  const auto report = tess::plan_operations(world, ops);

  tess::ResultChannel<Ack> channel;
  EXPECT_EQ(tess::record_plan_completions(report, channel), 3u);

  std::vector<std::uint64_t> order;
  (void)channel.drain_results(
      [&](tess::OpHandle handle, const tess::OpCompletion&, const Ack*) {
        order.push_back(handle.value);
      });
  ASSERT_EQ(order.size(), 3u);
  EXPECT_EQ(order[0], 0u);
  EXPECT_EQ(order[1], 1u);
  EXPECT_EQ(order[2], 2u);

  // Drain-once: a second drain visits nothing, but lookups stay readable.
  EXPECT_EQ(channel.drain_results(
                [](tess::OpHandle, const tess::OpCompletion&, const Ack*) {}),
            0u);
  EXPECT_EQ(channel.state(tess::OpHandle{2}), tess::OpResultState::Failed);
  EXPECT_TRUE(channel.completion(tess::OpHandle{2}).completed);
}

TEST(TessQueuedResults, ClearDropsSlotsAndBumpsTheGeneration) {
  World world;
  tess::FrameOps ops;
  const auto report = plan_one_good_one_rejected(world, ops);

  tess::ResultChannel<Ack> channel;
  (void)tess::record_plan_completions(report, channel);
  ASSERT_GT(channel.size(), 0u);
  const auto generation = channel.generation();

  channel.clear();
  EXPECT_EQ(channel.size(), 0u);
  EXPECT_EQ(channel.generation(), generation + 1u);
  EXPECT_EQ(channel.state(tess::OpHandle{1}), tess::OpResultState::Unbound);
  EXPECT_FALSE(channel.completion(tess::OpHandle{1}).completed);
}

TEST(TessQueuedResults, WarmReuseWithinCapacityIsAllocationFree) {
  World world;
  tess::FrameOps ops;
  const auto report = plan_one_good_one_rejected(world, ops);

  tess::ResultChannel<Ack> channel;
  channel.reserve_operations(4);
  // Warm the slot storage once, then reuse across "frames".
  (void)tess::record_plan_completions(report, channel);
  channel.clear();
  {
    tess_test::ScopedAllocationCounter counter;
    for (int frame = 0; frame < 4; ++frame) {
      (void)tess::record_plan_completions(report, channel);
      (void)channel.drain_results(
          [](tess::OpHandle, const tess::OpCompletion&, const Ack*) {});
      channel.clear();
    }
    EXPECT_EQ(counter.count(), 0u);
  }
}

// --- result-bearing execution (S6.3) -----------------------------------------

constexpr auto WritesTerrain = tess::FieldAccessDesc{
    0,
    DirtyTerrain,
    DirtyTerrain,
};

auto enqueue_chunk_write(
    tess::FrameOps& ops, std::uint64_t chunk,
    tess::WritePolicy policy = tess::WritePolicy::UniquePerChunk)
    -> tess::OpHandle {
  return ops.update_field(
      tess::DomainDesc::explicit_chunks(
          std::vector<tess::ChunkKey>{tess::ChunkKey{chunk}}),
      WritesTerrain, policy);
}

struct DrainedEntry {
  std::uint64_t handle = 0;
  bool ok = false;
  std::size_t chunk_count = 0;
  std::size_t tiles = 0;
  bool has_value = false;
};

auto drain_all(tess::ResultChannel<Ack>& channel) -> std::vector<DrainedEntry> {
  std::vector<DrainedEntry> entries;
  (void)channel.drain_results([&](tess::OpHandle handle,
                                  const tess::OpCompletion& completion,
                                  const Ack* value) {
    entries.push_back(DrainedEntry{
        handle.value,
        completion.ok(),
        completion.chunk_count,
        value != nullptr ? value->tiles : 0,
        value != nullptr,
    });
  });
  return entries;
}

TEST(TessQueuedResults, DeliveryIsIdenticalUnderSerialAndThreadedExecutors) {
  World serial_world;
  World threaded_world;
  tess::FrameOps ops;
  for (std::uint64_t chunk = 0; chunk < 4; ++chunk) {
    (void)enqueue_chunk_write(ops, chunk);
  }
  const auto report = tess::plan_operations(serial_world, ops);
  ASSERT_TRUE(report.ok());
  const auto phases = tess::plan_parallel_execution_phases(report.plan());
  ASSERT_TRUE(phases.ok());
  ASSERT_EQ(phases.phases().size(), 1u);
  const auto phase = phases.phases()[0];

  const auto run = [&](auto& executor, World& world,
                       tess::ResultChannel<Ack>& channel) {
    tess::PlannedPhaseExecutionScratch scratch;
    const auto result = tess::execute_phase_partitioned_dirty_with_results<
        tess::WritePolicy::UniquePerChunk>(
        executor, world, report.plan(), phase, scratch, channel,
        [](auto view, Ack& ack) {
          auto terrain = view.template field_span<TerrainTag>();
          terrain[0] = static_cast<std::uint16_t>(view.key().value + 7);
          ack.tiles += terrain.size();
        });
    EXPECT_EQ(result.status, tess::PlannedExecutionStatus::Executed);
    EXPECT_EQ(result.chunk_count, 4u);
  };

  tess::SerialPhaseExecutor serial_executor;
  tess::ResultChannel<Ack> serial_channel;
  run(serial_executor, serial_world, serial_channel);
  tess::ScopedThreadPhaseExecutor threaded_executor{2};
  tess::ResultChannel<Ack> threaded_channel;
  run(threaded_executor, threaded_world, threaded_channel);

  const auto serial_entries = drain_all(serial_channel);
  const auto threaded_entries = drain_all(threaded_channel);
  ASSERT_EQ(serial_entries.size(), 4u);
  ASSERT_EQ(threaded_entries.size(), 4u);
  for (std::size_t i = 0; i < 4; ++i) {
    EXPECT_EQ(serial_entries[i].handle, i);
    EXPECT_EQ(threaded_entries[i].handle, i);
    EXPECT_TRUE(serial_entries[i].ok);
    EXPECT_TRUE(threaded_entries[i].ok);
    EXPECT_EQ(serial_entries[i].chunk_count, 1u);
    EXPECT_EQ(serial_entries[i].tiles, threaded_entries[i].tiles);
    EXPECT_GT(serial_entries[i].tiles, 0u);
  }
  for (std::uint64_t chunk = 0; chunk < 4; ++chunk) {
    const auto key = tess::ChunkKey{chunk};
    EXPECT_EQ(serial_world.chunk(key).template field_span<TerrainTag>()[0],
              threaded_world.chunk(key).template field_span<TerrainTag>()[0]);
  }
}

TEST(TessQueuedResults, RuntimeFailuresDeliverReasonsAndSerialTailsPend) {
  // Mixed write policies plan fine; executing with UniquePerChunk turns the
  // UniquePerTile op into a runtime PolicyMismatch. The serial executor
  // aborts there (tail stays Pending); threaded executors drain the whole
  // range by contract.
  tess::FrameOps ops;
  World serial_world;
  (void)enqueue_chunk_write(ops, 0);
  (void)enqueue_chunk_write(ops, 1, tess::WritePolicy::UniquePerTile);
  (void)enqueue_chunk_write(ops, 2);
  const auto report = tess::plan_operations(serial_world, ops);
  ASSERT_TRUE(report.ok());
  const auto phase = tess::ExecutionPhase{0, 3};

  const auto run = [&](auto& executor, World& world,
                       tess::ResultChannel<Ack>& channel) {
    tess::PlannedPhaseExecutionScratch scratch;
    return tess::execute_phase_partitioned_dirty_with_results<
        tess::WritePolicy::UniquePerChunk>(
        executor, world, report.plan(), phase, scratch, channel,
        [](auto view, Ack& ack) {
          ack.tiles += view.template field_span<TerrainTag>().size();
        });
  };

  tess::SerialPhaseExecutor serial_executor;
  tess::ResultChannel<Ack> serial_channel;
  const auto serial_result = run(serial_executor, serial_world, serial_channel);
  EXPECT_EQ(serial_result.status, tess::PlannedExecutionStatus::PolicyMismatch);
  EXPECT_EQ(serial_channel.state(tess::OpHandle{0}),
            tess::OpResultState::Ready);
  EXPECT_EQ(serial_channel.state(tess::OpHandle{1}),
            tess::OpResultState::Failed);
  // Serial early-stop: the tail was prepared but never ran.
  EXPECT_EQ(serial_channel.state(tess::OpHandle{2}),
            tess::OpResultState::Pending);
  const auto failed = serial_channel.completion(tess::OpHandle{1});
  EXPECT_TRUE(failed.completed);
  EXPECT_FALSE(failed.ok());
  EXPECT_EQ(failed.execution, tess::PlannedExecutionStatus::PolicyMismatch);
  const auto serial_entries = drain_all(serial_channel);
  ASSERT_EQ(serial_entries.size(), 2u);  // Pending tail is not drained
  EXPECT_FALSE(serial_entries[1].has_value);

  World threaded_world;
  tess::ScopedThreadPhaseExecutor threaded_executor{2};
  tess::ResultChannel<Ack> threaded_channel;
  const auto threaded_result =
      run(threaded_executor, threaded_world, threaded_channel);
  EXPECT_EQ(threaded_result.status,
            tess::PlannedExecutionStatus::PolicyMismatch);
  // Threaded executors drain the whole range: the tail op completed.
  EXPECT_EQ(threaded_channel.state(tess::OpHandle{2}),
            tess::OpResultState::Ready);
  EXPECT_EQ(threaded_channel.state(tess::OpHandle{1}),
            tess::OpResultState::Failed);
}

TEST(TessQueuedResults, InvalidPhaseRangeFailsBeforeTouchingTheChannel) {
  tess::FrameOps ops;
  World world;
  (void)enqueue_chunk_write(ops, 0);
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());

  tess::PlannedPhaseExecutionScratch scratch;
  tess::ResultChannel<Ack> channel;
  tess::SerialPhaseExecutor executor;
  const auto result = tess::execute_phase_partitioned_dirty_with_results<
      tess::WritePolicy::UniquePerChunk>(executor, world, report.plan(),
                                         tess::ExecutionPhase{0, 5}, scratch,
                                         channel, [](auto, Ack&) {});
  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::InvalidPhase);
  EXPECT_EQ(channel.size(), 0u);
}

TEST(TessQueuedResults, SerialPlanWrapperPreparesTheWholePlanUpfront) {
  tess::FrameOps ops;
  World world;
  (void)enqueue_chunk_write(ops, 0);
  (void)enqueue_chunk_write(ops, 1, tess::WritePolicy::UniquePerTile);
  (void)enqueue_chunk_write(ops, 2);
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());

  tess::PlannedDirtyAccumulator dirty;
  tess::ResultChannel<Ack> channel;
  const auto result = tess::execute_plan_deferred_dirty_with_results<
      tess::WritePolicy::UniquePerChunk>(
      world, report.plan(), dirty, channel, [](auto view, Ack& ack) {
        ack.tiles += view.template field_span<TerrainTag>().size();
      });
  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::PolicyMismatch);
  EXPECT_EQ(channel.state(tess::OpHandle{0}), tess::OpResultState::Ready);
  EXPECT_EQ(channel.state(tess::OpHandle{1}), tess::OpResultState::Failed);
  EXPECT_EQ(channel.state(tess::OpHandle{2}), tess::OpResultState::Pending);
}

TEST(TessQueuedResults, WarmResultBearingExecutionIsAllocationFree) {
  tess::FrameOps ops;
  World world;
  for (std::uint64_t chunk = 0; chunk < 4; ++chunk) {
    (void)enqueue_chunk_write(ops, chunk);
  }
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());
  const auto phases = tess::plan_parallel_execution_phases(report.plan());
  const auto phase = phases.phases()[0];

  tess::PlannedPhaseExecutionScratch scratch;
  scratch.reserve_operations(4);
  scratch.reserve_dirty_records_per_operation(4);
  scratch.reserve_merged_dirty_records(16);
  tess::ResultChannel<Ack> channel;
  channel.reserve_operations(4);
  tess::SerialPhaseExecutor executor;
  const auto run = [&] {
    (void)tess::execute_phase_partitioned_dirty_with_results<
        tess::WritePolicy::UniquePerChunk>(
        executor, world, report.plan(), phase, scratch, channel,
        [](auto view, Ack& ack) {
          ack.tiles += view.template field_span<TerrainTag>().size();
        });
    (void)channel.drain_results(
        [](tess::OpHandle, const tess::OpCompletion&, const Ack*) {});
    channel.clear();
  };
  run();  // warm
  {
    tess_test::ScopedAllocationCounter counter;
    for (int frame = 0; frame < 4; ++frame) {
      run();
    }
    EXPECT_EQ(counter.count(), 0u);
  }
}

}  // namespace
