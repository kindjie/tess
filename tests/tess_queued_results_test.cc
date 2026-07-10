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

}  // namespace
