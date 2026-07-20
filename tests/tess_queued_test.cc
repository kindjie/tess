#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

#include "allocation_counter.h"

namespace {

struct TerrainTag {};
struct CostTag {};

constexpr std::uint32_t DirtyTerrain = 1u << 0u;
constexpr std::uint32_t DirtyCost = 1u << 1u;
constexpr std::uint32_t ActiveFluid = 1u << 0u;
constexpr std::uint32_t ActiveFire = 1u << 1u;

using TopDown2D =
    tess::Shape<tess::Extent3{128, 64, 1}, tess::Extent3{32, 16, 1}>;
using Schema = tess::FieldSchema<tess::Field<TerrainTag, std::uint16_t>,
                                 tess::Field<CostTag, float>>;
using World = tess::AlwaysResidentWorld<TopDown2D, Schema>;
using OtherTopDown2D =
    tess::Shape<tess::Extent3{64, 128, 1}, tess::Extent3{16, 32, 1}>;
using OtherWorld = tess::AlwaysResidentWorld<OtherTopDown2D, Schema>;

static_assert(World::chunk_count == OtherWorld::chunk_count);
static_assert(!std::is_aggregate_v<tess::PlannedOperation>);

// Bounded rendezvous spin for concurrency tests: an unbounded
// `while (entered < expected) yield()` hangs for the whole ctest timeout on
// regression. Fails the test with a clear message and returns instead, so
// the worker callback completes and the executor can join. The failure path
// (never reached on a green run) reports from a worker thread, which gtest
// documents as thread-safe only on pthread-backed builds.
void await_rendezvous(const std::atomic<std::size_t>& entered,
                      std::size_t expected) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds{30};
  while (entered.load() < expected) {
    if (std::chrono::steady_clock::now() >= deadline) {
      ADD_FAILURE() << "rendezvous timed out: " << entered.load() << " of "
                    << expected << " workers entered within 30s";
      return;
    }
    std::this_thread::yield();
  }
}

auto planned_keys(const tess::ExecutionReport& report, std::size_t index)
    -> std::vector<tess::ChunkKey> {
  const auto ops = report.plan().operations();
  if (index >= ops.size()) {
    return {};
  }
  return {ops[index].chunks().begin(), ops[index].chunks().end()};
}

struct ReplayEntry {
  tess::ChunkKey key{};
  tess::FieldAccessDesc access{};
};

auto next_replay_value(std::uint32_t& state) -> std::uint32_t {
  state = (state * 1664525u) + 1013904223u;
  return state;
}

auto replay_entries(std::uint32_t seed) -> std::vector<ReplayEntry> {
  std::vector<ReplayEntry> entries;
  entries.reserve(static_cast<std::size_t>(World::chunk_count) * 2u);

  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      DirtyTerrain,
      DirtyTerrain,
  };
  constexpr auto writes_cost = tess::FieldAccessDesc{
      0,
      DirtyCost,
      DirtyCost,
  };

  for (std::uint64_t key = 0; key < World::chunk_count; ++key) {
    entries.push_back(ReplayEntry{tess::ChunkKey{key}, writes_terrain});
    entries.push_back(ReplayEntry{tess::ChunkKey{key}, writes_cost});
  }

  auto state = seed;
  for (std::size_t i = entries.size(); i > 1u; --i) {
    const auto j = static_cast<std::size_t>(next_replay_value(state) % i);
    std::swap(entries[i - 1u], entries[j]);
  }
  return entries;
}

auto replay_callback(std::uint32_t seed) {
  return [seed](auto view) {
    auto terrain = view.template field_span<TerrainTag>();
    auto cost = view.template field_span<CostTag>();
    terrain[0] = static_cast<std::uint16_t>((seed * 97u) + view.key().value);
    cost[0] = static_cast<float>(seed) +
              (static_cast<float>(view.key().value) * 0.25F);
  };
}

template <typename Executor>
void execute_replay_phases(Executor& executor, World& world,
                           const tess::ExecutionPlan& plan,
                           std::span<const tess::ExecutionPhase> phases,
                           tess::PlannedPhaseExecutionScratch& scratch,
                           std::uint32_t seed) {
  for (const auto phase : phases) {
    const auto result = tess::execute_phase_partitioned_dirty_with<
        tess::WritePolicy::UniquePerChunk>(executor, world, plan, phase,
                                           scratch, replay_callback(seed));
    ASSERT_EQ(result.status, tess::PlannedExecutionStatus::Executed);
    (void)tess::merge_planned_dirty(world, scratch);
  }
}

void expect_worlds_match(const World& lhs, const World& rhs) {
  for (std::uint64_t key_value = 0; key_value < World::chunk_count;
       ++key_value) {
    const auto key = tess::ChunkKey{key_value};
    EXPECT_EQ(lhs.meta(key).state, rhs.meta(key).state);
    EXPECT_EQ(lhs.meta(key).version, rhs.meta(key).version);
    EXPECT_EQ(lhs.meta(key).topology_version, rhs.meta(key).topology_version);
    EXPECT_EQ(lhs.dirty_flags(key), rhs.dirty_flags(key));
    EXPECT_EQ(lhs.active_flags(key), rhs.active_flags(key));
    EXPECT_EQ(lhs.dirty_bounds(key), rhs.dirty_bounds(key));

    const auto lhs_terrain = lhs.chunk(key).template field_span<TerrainTag>();
    const auto rhs_terrain = rhs.chunk(key).template field_span<TerrainTag>();
    const auto lhs_cost = lhs.chunk(key).template field_span<CostTag>();
    const auto rhs_cost = rhs.chunk(key).template field_span<CostTag>();
    ASSERT_EQ(lhs_terrain.size(), rhs_terrain.size());
    ASSERT_EQ(lhs_cost.size(), rhs_cost.size());
    EXPECT_TRUE(
        std::equal(lhs_terrain.begin(), lhs_terrain.end(), rhs_terrain.begin()))
        << "terrain fields differ in chunk " << key_value;
    EXPECT_TRUE(std::equal(lhs_cost.begin(), lhs_cost.end(), rhs_cost.begin()))
        << "cost fields differ in chunk " << key_value;
  }
}

struct RecordingPhaseExecutor {
  // Promises serialized callback invocation to the shared-accumulator
  // deferred-dirty helper (see tess::SerialExecutor).
  using serial_execution_tag = void;

  std::vector<std::size_t> indexes;

  // [phase-executor]
  template <typename Fn>
  auto for_each_operation(std::size_t first, std::size_t count, Fn&& fn)
      -> tess::PlannedExecutionResult {
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
  // [phase-executor]
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

struct ReverseRecordingPhaseExecutor {
  std::vector<std::size_t> indexes;

  template <typename Fn>
  auto for_each_operation(std::size_t first, std::size_t count, Fn&& fn)
      -> tess::PlannedExecutionResult {
    auto&& callback = fn;
    for (std::size_t remaining = count; remaining != 0; --remaining) {
      const auto index = first + remaining - 1;
      indexes.push_back(index);
      auto result = callback(index);
      if (result.status != tess::PlannedExecutionStatus::Executed) {
        return result;
      }
    }
    return tess::PlannedExecutionResult{};
  }
};

struct ThreadedRecordingPhaseExecutor {
  std::vector<std::size_t> indexes;

  template <typename Fn>
  auto for_each_operation(std::size_t first, std::size_t count, Fn&& fn)
      -> tess::PlannedExecutionResult {
    auto&& callback = fn;
    std::mutex indexes_mutex;
    std::vector<tess::PlannedExecutionResult> results(count);
    std::vector<std::thread> threads;
    threads.reserve(count);

    for (std::size_t offset = 0; offset < count; ++offset) {
      threads.emplace_back([&, offset] {
        const auto index = first + offset;
        {
          const std::lock_guard<std::mutex> lock(indexes_mutex);
          indexes.push_back(index);
        }
        results[offset] = callback(index);
      });
    }

    for (auto& thread : threads) {
      thread.join();
    }

    for (const auto result : results) {
      if (result.status != tess::PlannedExecutionStatus::Executed) {
        return result;
      }
    }
    return tess::PlannedExecutionResult{};
  }
};

// Custom serial executor that opts into the shared-accumulator
// deferred-dirty helper by declaring tess::SerialExecutor's tag.
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

struct ProbeChunkCallback {
  template <typename View>
  void operator()(View&&) const {}
};

template <typename Executor>
concept DeferredDirtyExecutorAccepted = requires(
    Executor& executor, World& world, const tess::ExecutionPlan& plan,
    tess::ExecutionPhase phase, tess::PlannedDirtyAccumulator& dirty,
    ProbeChunkCallback fn) {
  tess::execute_phase_deferred_dirty_with<tess::WritePolicy::UniquePerChunk>(
      executor, world, plan, phase, dirty, fn);
};

template <typename Executor>
concept PartitionedDirtyExecutorAccepted = requires(
    Executor& executor, World& world, const tess::ExecutionPlan& plan,
    tess::ExecutionPhase phase, tess::PlannedPhaseExecutionScratch& scratch,
    ProbeChunkCallback fn) {
  tess::execute_phase_partitioned_dirty_with<tess::WritePolicy::UniquePerChunk>(
      executor, world, plan, phase, scratch, fn);
};

// The serial-execution promise is structural: declaring the tag satisfies
// tess::SerialExecutor, and only tagged executors may drive the
// shared-accumulator deferred-dirty helper.
static_assert(tess::SerialExecutor<tess::SerialPhaseExecutor>);
static_assert(tess::SerialExecutor<TaggedCustomSerialExecutor>);
static_assert(!tess::SerialExecutor<tess::ScopedThreadPhaseExecutor>);
static_assert(!tess::SerialExecutor<ThreadedRecordingPhaseExecutor>);
static_assert(DeferredDirtyExecutorAccepted<tess::SerialPhaseExecutor>);
static_assert(DeferredDirtyExecutorAccepted<TaggedCustomSerialExecutor>);
static_assert(DeferredDirtyExecutorAccepted<RecordingPhaseExecutor>);
static_assert(!DeferredDirtyExecutorAccepted<tess::ScopedThreadPhaseExecutor>);
static_assert(!DeferredDirtyExecutorAccepted<ThreadedRecordingPhaseExecutor>);
// The partitioned helper gives every operation its own dirty partition and
// result slot, so it accepts serial and concurrent executors alike.
static_assert(PartitionedDirtyExecutorAccepted<tess::SerialPhaseExecutor>);
static_assert(PartitionedDirtyExecutorAccepted<TaggedCustomSerialExecutor>);
static_assert(
    PartitionedDirtyExecutorAccepted<tess::ScopedThreadPhaseExecutor>);
static_assert(PartitionedDirtyExecutorAccepted<ThreadedRecordingPhaseExecutor>);

TEST(TessQueued, EmptyFramePlansToEmptyReport) {
  World world;
  const tess::FrameOps ops;

  const auto report = tess::plan_operations(world, ops);

  EXPECT_TRUE(ops.empty());
  EXPECT_EQ(ops.size(), 0u);
  EXPECT_TRUE(report.ok());
  EXPECT_TRUE(report.operations().empty());
  EXPECT_TRUE(report.plan().empty());
  EXPECT_EQ(report.plan().size(), 0u);
}

TEST(TessQueued, StableHandlesAndIdsFollowEnqueueOrder) {
  tess::FrameOps ops;
  const auto first = ops.update_field(
      tess::DomainDesc::resident_chunks(), tess::WritePolicy::ReadOnly,
      tess::Priority::Immediate, tess::BudgetPolicy::MustRun);
  const auto second = ops.update_field(
      tess::DomainDesc::resident_chunks(), tess::WritePolicy::UniquePerChunk,
      tess::Priority::Background, tess::BudgetPolicy::CanDefer);

  ASSERT_NE(ops.operation(first), nullptr);
  ASSERT_NE(ops.operation(second), nullptr);
  EXPECT_EQ(first, (tess::OpHandle{0}));
  EXPECT_EQ(second, (tess::OpHandle{1}));
  EXPECT_EQ(ops.operation(first)->id, (tess::OpId{0}));
  EXPECT_EQ(ops.operation(second)->id, (tess::OpId{1}));
  EXPECT_EQ(ops.operation(first)->priority, tess::Priority::Immediate);
  EXPECT_EQ(ops.operation(second)->priority, tess::Priority::Background);
  EXPECT_EQ(ops.operation(second)->budget_policy, tess::BudgetPolicy::CanDefer);
  EXPECT_EQ(ops.operation(tess::OpHandle{2}), nullptr);
}

TEST(TessQueued, ExplicitChunkDomainExpandsInChunkKeyOrder) {
  World world;
  tess::FrameOps ops;
  const std::vector<tess::ChunkKey> requested{
      tess::ChunkKey{7},
      tess::ChunkKey{2},
      tess::ChunkKey{5},
  };

  const auto handle =
      ops.update_field(tess::DomainDesc::explicit_chunks(requested),
                       tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);

  ASSERT_TRUE(report.ok());
  ASSERT_EQ(report.operations().size(), 1u);
  ASSERT_EQ(report.plan().operations().size(), 1u);
  EXPECT_EQ(report.operations()[0].handle, handle);
  EXPECT_EQ(report.operations()[0].chunk_count, 3u);
  EXPECT_EQ(report.operations()[0].access.write_policy,
            tess::WritePolicy::UniquePerChunk);
  EXPECT_EQ(report.operations()[0].access.domain_kind,
            tess::DomainKind::ExplicitChunks);
  EXPECT_EQ(report.operations()[0].access.domain_mask, 0u);
  EXPECT_EQ(report.plan().operations()[0].access.write_policy,
            tess::WritePolicy::UniquePerChunk);
  EXPECT_EQ(planned_keys(report, 0), (std::vector<tess::ChunkKey>{
                                         tess::ChunkKey{2},
                                         tess::ChunkKey{5},
                                         tess::ChunkKey{7},
                                     }));
}

TEST(TessQueued, FieldAccessMetadataPropagatesToPlanAndReport) {
  World world;
  tess::FrameOps ops;
  constexpr auto field_access = tess::FieldAccessDesc{
      DirtyCost,
      DirtyTerrain,
      DirtyTerrain,
  };

  const auto handle = ops.update_field(
      tess::DomainDesc::resident_chunks(), field_access,
      tess::WritePolicy::UniquePerTile, tess::Priority::VisibleSoon,
      tess::BudgetPolicy::BudgetedIncremental);
  const auto report = tess::plan_operations(world, ops);

  ASSERT_TRUE(report.ok());
  ASSERT_NE(ops.operation(handle), nullptr);
  ASSERT_NE(report.find(handle), nullptr);
  ASSERT_EQ(report.plan().operations().size(), 1u);

  EXPECT_EQ(ops.operation(handle)->field_access, field_access);
  EXPECT_EQ(report.find(handle)->field_access, field_access);
  EXPECT_EQ(report.plan().operations()[0].field_access, field_access);
  EXPECT_EQ(report.plan().operations()[0].priority,
            tess::Priority::VisibleSoon);
  EXPECT_EQ(report.plan().operations()[0].budget_policy,
            tess::BudgetPolicy::BudgetedIncremental);
}

TEST(TessQueued, DirtyChunkDomainExpandsThroughWorldMetadata) {
  World world;
  tess::FrameOps ops;
  const auto bounds = tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}};

  world.mark_dirty(tess::ChunkKey{7}, DirtyTerrain, bounds);
  world.mark_dirty(tess::ChunkKey{2}, DirtyCost, bounds);
  world.mark_dirty(tess::ChunkKey{5}, DirtyTerrain | DirtyCost, bounds);

  (void)ops.update_field(tess::DomainDesc::dirty_chunks(DirtyTerrain),
                         tess::WritePolicy::UniquePerTile);
  const auto report = tess::plan_operations(world, ops);

  ASSERT_TRUE(report.ok());
  EXPECT_EQ(planned_keys(report, 0), (std::vector<tess::ChunkKey>{
                                         tess::ChunkKey{5},
                                         tess::ChunkKey{7},
                                     }));
}

TEST(TessQueued, ActiveChunkDomainExpandsThroughWorldMetadata) {
  World world;
  tess::FrameOps ops;

  world.mark_active(tess::ChunkKey{9}, ActiveFire);
  world.mark_active(tess::ChunkKey{1}, ActiveFluid);
  world.mark_active(tess::ChunkKey{4}, ActiveFluid | ActiveFire);

  (void)ops.update_field(tess::DomainDesc::active_chunks(ActiveFire),
                         tess::WritePolicy::UniquePerTile);
  const auto report = tess::plan_operations(world, ops);

  ASSERT_TRUE(report.ok());
  EXPECT_EQ(planned_keys(report, 0), (std::vector<tess::ChunkKey>{
                                         tess::ChunkKey{4},
                                         tess::ChunkKey{9},
                                     }));
}

TEST(TessQueued, ResidentChunkDomainExpandsAllAlwaysResidentChunks) {
  World world;
  tess::FrameOps ops;

  (void)ops.update_field(tess::DomainDesc::resident_chunks(),
                         tess::WritePolicy::ReadOnly);
  const auto report = tess::plan_operations(world, ops);

  ASSERT_TRUE(report.ok());
  ASSERT_EQ(report.plan().operations().size(), 1u);
  EXPECT_EQ(report.operations()[0].chunk_count, World::chunk_count);
  ASSERT_EQ(report.plan().operations()[0].chunks().size(), World::chunk_count);
  EXPECT_EQ(report.plan().operations()[0].chunks().front(),
            (tess::ChunkKey{0}));
  EXPECT_EQ(report.plan().operations()[0].chunks().back(),
            (tess::ChunkKey{15}));
}

TEST(TessQueued, InvalidWritePolicyIsRejectedWithoutPlanEntry) {
  World world;
  tess::FrameOps ops;

  (void)ops.update_field(
      tess::DomainDesc::resident_chunks(),
      // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
      static_cast<tess::WritePolicy>(255));
  const auto report = tess::plan_operations(world, ops);

  ASSERT_FALSE(report.ok());
  EXPECT_TRUE(report.failed());
  EXPECT_EQ(report.planned_count(), 0u);
  EXPECT_EQ(report.failed_count(), 1u);
  ASSERT_EQ(report.operations().size(), 1u);
  EXPECT_EQ(report.operations()[0].status,
            tess::OperationStatus::InvalidWritePolicy);
  EXPECT_EQ(report.operations()[0].failure,
            tess::OperationFailure::InvalidWritePolicyValue);
  EXPECT_FALSE(report.operations()[0].has_detail_chunk);
  EXPECT_TRUE(report.plan().empty());
}

TEST(TessQueued, ReadOnlyWriteMaskIsRejectedWithoutPlanEntry) {
  World world;
  tess::FrameOps ops;
  constexpr auto field_access = tess::FieldAccessDesc{
      DirtyCost,
      DirtyTerrain,
      DirtyTerrain,
  };

  (void)ops.update_field(tess::DomainDesc::resident_chunks(), field_access,
                         tess::WritePolicy::ReadOnly);
  const auto report = tess::plan_operations(world, ops);

  ASSERT_FALSE(report.ok());
  EXPECT_TRUE(report.failed());
  EXPECT_EQ(report.planned_count(), 0u);
  EXPECT_EQ(report.failed_count(), 1u);
  ASSERT_EQ(report.operations().size(), 1u);
  EXPECT_EQ(report.operations()[0].status,
            tess::OperationStatus::InvalidFieldAccess);
  EXPECT_EQ(report.operations()[0].failure,
            tess::OperationFailure::ReadOnlyWriteMask);
  EXPECT_EQ(report.operations()[0].field_access, field_access);
  EXPECT_TRUE(report.plan().empty());
}

TEST(TessQueued, OutOfRangeExplicitChunkIsRejectedWithoutPlanEntry) {
  World world;
  tess::FrameOps ops;
  const std::vector<tess::ChunkKey> requested{
      tess::ChunkKey{3},
      tess::ChunkKey{World::chunk_count},
  };

  (void)ops.update_field(tess::DomainDesc::explicit_chunks(requested),
                         tess::WritePolicy::ReadOnly);
  const auto report = tess::plan_operations(world, ops);

  ASSERT_FALSE(report.ok());
  EXPECT_TRUE(report.failed());
  EXPECT_EQ(report.planned_count(), 0u);
  EXPECT_EQ(report.failed_count(), 1u);
  ASSERT_EQ(report.operations().size(), 1u);
  EXPECT_EQ(report.operations()[0].status,
            tess::OperationStatus::InvalidDomain);
  EXPECT_EQ(report.operations()[0].failure,
            tess::OperationFailure::ExplicitChunkOutOfRange);
  EXPECT_TRUE(report.operations()[0].has_detail_chunk);
  EXPECT_EQ(report.operations()[0].detail_chunk,
            (tess::ChunkKey{World::chunk_count}));
  EXPECT_TRUE(report.plan().empty());
  EXPECT_EQ(report.operations()[0].chunk_count, 0u);
}

TEST(TessQueued, ReportFindsOperationsByHandle) {
  World world;
  tess::FrameOps ops;

  const auto first = ops.update_field(tess::DomainDesc::resident_chunks(),
                                      tess::WritePolicy::ReadOnly);
  const auto second = ops.update_field(
      tess::DomainDesc::resident_chunks(),
      // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
      static_cast<tess::WritePolicy>(255));

  const auto report = tess::plan_operations(world, ops);

  ASSERT_NE(report.find(first), nullptr);
  ASSERT_NE(report.find(second), nullptr);
  EXPECT_EQ(report.find(first)->status, tess::OperationStatus::Planned);
  EXPECT_EQ(report.find(second)->status,
            tess::OperationStatus::InvalidWritePolicy);
  EXPECT_EQ(report.find(tess::OpHandle{2}), nullptr);
}

TEST(TessQueued, MixedReportsPreserveOrderAndCountFailures) {
  World world;
  tess::FrameOps ops;
  const std::vector<tess::ChunkKey> invalid_keys{
      tess::ChunkKey{World::chunk_count},
  };

  const auto valid = ops.update_field(tess::DomainDesc::resident_chunks(),
                                      tess::WritePolicy::ReadOnly);
  const auto bad_policy = ops.update_field(
      tess::DomainDesc::resident_chunks(),
      // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
      static_cast<tess::WritePolicy>(255));
  const auto bad_domain =
      ops.update_field(tess::DomainDesc::explicit_chunks(invalid_keys),
                       tess::WritePolicy::ReadOnly);

  const auto report = tess::plan_operations(world, ops);

  ASSERT_FALSE(report.ok());
  EXPECT_TRUE(report.failed());
  EXPECT_EQ(report.planned_count(), 1u);
  EXPECT_EQ(report.failed_count(), 2u);
  ASSERT_EQ(report.operations().size(), 3u);
  EXPECT_EQ(report.operations()[0].handle, valid);
  EXPECT_EQ(report.operations()[0].status, tess::OperationStatus::Planned);
  EXPECT_EQ(report.operations()[1].handle, bad_policy);
  EXPECT_EQ(report.operations()[1].status,
            tess::OperationStatus::InvalidWritePolicy);
  EXPECT_EQ(report.operations()[2].handle, bad_domain);
  EXPECT_EQ(report.operations()[2].status,
            tess::OperationStatus::InvalidDomain);

  ASSERT_EQ(report.plan().operations().size(), 1u);
  EXPECT_EQ(report.plan().operations()[0].handle, valid);
}

TEST(TessQueued, DisjointChunksWithSameWriteMaskDoNotConflict) {
  World world;
  tess::FrameOps ops;
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

  EXPECT_TRUE(report.ok());
  EXPECT_EQ(report.planned_count(), 2u);
  ASSERT_EQ(report.plan().operations().size(), 2u);
  EXPECT_EQ(report.plan().operations()[0].chunks()[0], (tess::ChunkKey{1}));
  EXPECT_EQ(report.plan().operations()[1].chunks()[0], (tess::ChunkKey{2}));
}

TEST(TessQueued, OverlappingWriteMasksConflictOnSameChunk) {
  World world;
  tess::FrameOps ops;
  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      DirtyTerrain,
      DirtyTerrain,
  };
  const std::vector<tess::ChunkKey> first_keys{
      tess::ChunkKey{1},
      tess::ChunkKey{3},
  };
  const std::vector<tess::ChunkKey> second_keys{tess::ChunkKey{3}};

  const auto first =
      ops.update_field(tess::DomainDesc::explicit_chunks(first_keys),
                       writes_terrain, tess::WritePolicy::UniquePerChunk);
  const auto second =
      ops.update_field(tess::DomainDesc::explicit_chunks(second_keys),
                       writes_terrain, tess::WritePolicy::UniquePerChunk);

  const auto report = tess::plan_operations(world, ops);

  ASSERT_FALSE(report.ok());
  EXPECT_EQ(report.planned_count(), 1u);
  EXPECT_EQ(report.failed_count(), 1u);
  ASSERT_NE(report.find(second), nullptr);
  EXPECT_EQ(report.find(second)->status, tess::OperationStatus::HazardConflict);
  EXPECT_EQ(report.find(second)->failure,
            tess::OperationFailure::FieldHazardConflict);
  EXPECT_TRUE(report.find(second)->has_conflict);
  EXPECT_EQ(report.find(second)->conflict_handle, first);
  EXPECT_EQ(report.find(second)->conflict_id, (tess::OpId{0}));
  EXPECT_EQ(report.find(second)->conflict_mask, DirtyTerrain);
  ASSERT_EQ(report.plan().operations().size(), 1u);
  EXPECT_EQ(report.plan().operations()[0].handle, first);
}

TEST(TessQueued, DisjointFieldMasksOnSameChunksDoNotConflict) {
  World world;
  tess::FrameOps ops;
  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      DirtyTerrain,
      DirtyTerrain,
  };
  constexpr auto writes_cost = tess::FieldAccessDesc{
      0,
      DirtyCost,
      DirtyCost,
  };

  (void)ops.update_field(tess::DomainDesc::resident_chunks(), writes_terrain,
                         tess::WritePolicy::UniquePerChunk);
  (void)ops.update_field(tess::DomainDesc::resident_chunks(), writes_cost,
                         tess::WritePolicy::UniquePerChunk);

  const auto report = tess::plan_operations(world, ops);

  EXPECT_TRUE(report.ok());
  EXPECT_EQ(report.planned_count(), 2u);
}

TEST(TessQueued, ReadWriteOverlapConflictsWithoutBarrier) {
  World world;
  tess::FrameOps ops;
  constexpr auto reads_terrain = tess::FieldAccessDesc{
      DirtyTerrain,
      0,
      0,
  };
  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      DirtyTerrain,
      DirtyTerrain,
  };

  const auto reader =
      ops.update_field(tess::DomainDesc::resident_chunks(), reads_terrain,
                       tess::WritePolicy::ReadOnly);
  const auto writer =
      ops.update_field(tess::DomainDesc::resident_chunks(), writes_terrain,
                       tess::WritePolicy::UniquePerTile);

  const auto report = tess::plan_operations(world, ops);

  ASSERT_FALSE(report.ok());
  EXPECT_EQ(report.planned_count(), 1u);
  EXPECT_EQ(report.failed_count(), 1u);
  ASSERT_NE(report.find(writer), nullptr);
  EXPECT_EQ(report.find(writer)->status, tess::OperationStatus::HazardConflict);
  EXPECT_EQ(report.find(writer)->conflict_handle, reader);
  EXPECT_EQ(report.find(writer)->conflict_mask, DirtyTerrain);
}

TEST(TessQueued, PlanOperationsPreserveEnqueueOrderAcrossDomains) {
  World world;
  tess::FrameOps ops;
  const std::vector<tess::ChunkKey> first_keys{tess::ChunkKey{6}};
  const std::vector<tess::ChunkKey> second_keys{tess::ChunkKey{1}};

  const auto first =
      ops.update_field(tess::DomainDesc::explicit_chunks(first_keys),
                       tess::WritePolicy::UniquePerChunk);
  const auto second =
      ops.update_field(tess::DomainDesc::explicit_chunks(second_keys),
                       tess::WritePolicy::ReadOnly);

  const auto report = tess::plan_operations(world, ops);

  ASSERT_TRUE(report.ok());
  ASSERT_EQ(report.plan().operations().size(), 2u);
  EXPECT_EQ(report.plan().operations()[0].handle, first);
  EXPECT_EQ(report.plan().operations()[0].id, (tess::OpId{0}));
  EXPECT_EQ(report.plan().operations()[0].chunks()[0], (tess::ChunkKey{6}));
  EXPECT_EQ(report.plan().operations()[1].handle, second);
  EXPECT_EQ(report.plan().operations()[1].id, (tess::OpId{1}));
  EXPECT_EQ(report.plan().operations()[1].chunks()[0], (tess::ChunkKey{1}));
}

TEST(TessQueued, PlannedChunkDomainAdaptsExpandedChunks) {
  World world;
  tess::FrameOps ops;
  const auto bounds = tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}};
  const std::vector<tess::ChunkKey> requested{
      tess::ChunkKey{4},
      tess::ChunkKey{1},
  };

  world.mark_dirty(tess::ChunkKey{7}, DirtyTerrain, bounds);

  (void)ops.update_field(tess::DomainDesc::explicit_chunks(requested),
                         tess::WritePolicy::ReadOnly);
  (void)ops.update_field(tess::DomainDesc::dirty_chunks(DirtyTerrain),
                         tess::WritePolicy::ReadOnly);
  (void)ops.update_field(tess::DomainDesc::resident_chunks(),
                         tess::WritePolicy::ReadOnly);

  const auto report = tess::plan_operations(world, ops);

  ASSERT_TRUE(report.ok());
  ASSERT_EQ(report.plan().operations().size(), 3u);

  const auto explicit_domain =
      tess::planned_chunk_domain(report.plan().operations()[0]);
  const auto dirty_domain =
      tess::planned_chunk_domain(report.plan().operations()[1]);
  const auto resident_domain =
      tess::planned_chunk_domain(report.plan().operations()[2]);

  EXPECT_EQ(explicit_domain.size(), 2u);
  EXPECT_EQ(explicit_domain.keys()[0], (tess::ChunkKey{1}));
  EXPECT_EQ(explicit_domain.keys()[1], (tess::ChunkKey{4}));
  EXPECT_EQ(dirty_domain.size(), 1u);
  EXPECT_EQ(dirty_domain.keys()[0], (tess::ChunkKey{7}));
  EXPECT_EQ(resident_domain.size(), World::chunk_count);
  EXPECT_EQ(resident_domain.keys().front(), (tess::ChunkKey{0}));
  EXPECT_EQ(resident_domain.keys().back(), (tess::ChunkKey{15}));
}

TEST(TessQueued, ParallelPhasesGroupDisjointChunkMutationsAfterReaders) {
  World world;
  tess::FrameOps ops;
  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      DirtyTerrain,
      DirtyTerrain,
  };
  const std::vector<tess::ChunkKey> first_keys{tess::ChunkKey{1}};
  const std::vector<tess::ChunkKey> second_keys{tess::ChunkKey{2}};

  (void)ops.update_field(tess::DomainDesc::resident_chunks(),
                         tess::WritePolicy::ReadOnly);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(first_keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(second_keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);

  ASSERT_TRUE(report.ok());
  const auto phases = tess::plan_parallel_execution_phases(report.plan());

  ASSERT_TRUE(phases.ok());
  ASSERT_EQ(phases.phases().size(), 2u);
  EXPECT_EQ(phases.phases()[0].first_operation(), 0u);
  EXPECT_EQ(phases.phases()[0].operation_count(), 1u);
  EXPECT_EQ(phases.phases()[1].first_operation(), 1u);
  EXPECT_EQ(phases.phases()[1].operation_count(), 2u);
}

TEST(TessQueued, ParallelPhasesSeparateSameChunkMutations) {
  World world;
  tess::FrameOps ops;
  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      DirtyTerrain,
      DirtyTerrain,
  };
  constexpr auto writes_cost = tess::FieldAccessDesc{
      0,
      DirtyCost,
      DirtyCost,
  };
  const std::vector<tess::ChunkKey> keys{tess::ChunkKey{3}};

  (void)ops.update_field(tess::DomainDesc::explicit_chunks(keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(keys), writes_cost,
                         tess::WritePolicy::UniquePerChunk);
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

TEST(TessQueued, ParallelPhasesRejectUniquePerTileUntilTileDomainsExist) {
  World world;
  tess::FrameOps ops;
  const std::vector<tess::ChunkKey> keys{tess::ChunkKey{3}};

  (void)ops.update_field(tess::DomainDesc::explicit_chunks(keys),
                         tess::WritePolicy::UniquePerTile);
  const auto report = tess::plan_operations(world, ops);

  ASSERT_TRUE(report.ok());
  const auto phases = tess::plan_parallel_execution_phases(report.plan());

  EXPECT_FALSE(phases.ok());
  EXPECT_EQ(phases.status(),
            tess::ExecutionPhaseStatus::UnsupportedWritePolicy);
  EXPECT_EQ(phases.failed_operation_index(), 0u);
  EXPECT_EQ(phases.failed_write_policy(), tess::WritePolicy::UniquePerTile);
  EXPECT_TRUE(phases.phases().empty());
}

TEST(TessQueued, PlannedBlockCtxRejectsPolicyMismatch) {
  World world;
  tess::FrameOps ops;

  (void)ops.update_field(tess::DomainDesc::resident_chunks(),
                         tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);

  ASSERT_TRUE(report.ok());
  ASSERT_EQ(report.plan().operations().size(), 1u);
  const auto& planned = report.plan().operations()[0];

  EXPECT_TRUE(
      tess::planned_policy_matches<tess::WritePolicy::UniquePerChunk>(planned));
  EXPECT_FALSE(
      tess::planned_policy_matches<tess::WritePolicy::ReadOnly>(planned));
  EXPECT_FALSE(
      tess::try_planned_block_ctx<tess::WritePolicy::ReadOnly>(world, planned));
}

TEST(TessQueued, PlannedBlockCtxIteratesWithExistingBlockApi) {
  World world;
  tess::FrameOps ops;
  const std::vector<tess::ChunkKey> requested{
      tess::ChunkKey{3},
      tess::ChunkKey{1},
  };

  (void)ops.update_field(tess::DomainDesc::explicit_chunks(requested),
                         tess::WritePolicy::ReadOnly);
  const auto report = tess::plan_operations(world, ops);

  ASSERT_TRUE(report.ok());
  ASSERT_EQ(report.plan().operations().size(), 1u);

  auto ctx = tess::try_planned_block_ctx<tess::WritePolicy::ReadOnly>(
      world, report.plan().operations()[0]);

  if (!ctx.has_value()) {
    FAIL() << "expected planned block context";
    return;
  }
  auto& block_ctx = ctx.value();
  EXPECT_EQ(block_ctx.policy(), tess::WritePolicy::ReadOnly);
  EXPECT_EQ(block_ctx.size(), 2u);

  std::vector<tess::ChunkKey> visited;
  block_ctx.for_each_chunk([&](auto view) { visited.push_back(view.key()); });

  EXPECT_EQ(visited, (std::vector<tess::ChunkKey>{
                         tess::ChunkKey{1},
                         tess::ChunkKey{3},
                     }));
}

TEST(TessQueued, CheckedPlannedOperationCreationRejectsInvalidChunks) {
  World world;
  tess::FrameOps ops;
  const auto handle = ops.update_field(tess::DomainDesc::resident_chunks(),
                                       tess::WritePolicy::ReadOnly);
  const std::vector<tess::ChunkKey> invalid_chunks{
      tess::ChunkKey{1},
      tess::ChunkKey{World::chunk_count},
  };

  const auto created = tess::PlannedOperation::create(
      world, *ops.operation(handle), invalid_chunks);

  EXPECT_EQ(created.status, tess::PlannedOperationCreateStatus::InvalidChunk);
  EXPECT_EQ(created.invalid_chunk, (tess::ChunkKey{World::chunk_count}));
  EXPECT_FALSE(created.operation.has_value());
}

TEST(TessQueued, CheckedPlannedOperationCreationSortsAndDeduplicatesChunks) {
  World world;
  tess::FrameOps ops;
  const auto handle = ops.update_field(tess::DomainDesc::resident_chunks(),
                                       tess::WritePolicy::ReadOnly);
  const std::vector<tess::ChunkKey> chunks{
      tess::ChunkKey{3},
      tess::ChunkKey{1},
      tess::ChunkKey{3},
  };

  auto created =
      tess::PlannedOperation::create(world, *ops.operation(handle), chunks);

  ASSERT_EQ(created.status, tess::PlannedOperationCreateStatus::Created);
  if (!created.operation.has_value()) {
    ADD_FAILURE() << "successful checked creation returned no operation";
    return;
  }
  const auto& operation = *created.operation;
  EXPECT_EQ(operation.chunks().size(), 2u);
  EXPECT_EQ(operation.chunks()[0], (tess::ChunkKey{1}));
  EXPECT_EQ(operation.chunks()[1], (tess::ChunkKey{3}));

  std::vector<tess::ChunkKey> visited;
  const auto executed =
      tess::execute_planned_operation<tess::WritePolicy::ReadOnly>(
          world, operation, [&](auto view) { visited.push_back(view.key()); });
  EXPECT_EQ(executed.status, tess::PlannedExecutionStatus::Executed);
  EXPECT_EQ(visited, (std::vector<tess::ChunkKey>{
                         tess::ChunkKey{1},
                         tess::ChunkKey{3},
                     }));
}

TEST(TessQueued, SpanPlannerRejectsNonDenseHandlesAndIdsSafely) {
  World world;
  tess::FrameOps source;
  (void)source.update_field(tess::DomainDesc::resident_chunks(),
                            tess::WritePolicy::ReadOnly);
  (void)source.update_field(tess::DomainDesc::resident_chunks(),
                            tess::WritePolicy::ReadOnly);
  auto operations = std::vector<tess::QueuedOperation>{
      source.operations().begin(), source.operations().end()};
  operations[0].handle = tess::OpHandle{~std::uint64_t{0}};
  operations[1].handle = tess::OpHandle{0};
  operations[1].id = tess::OpId{0};
  tess::ExecutionReport report;

  tess::plan_operations(world, operations, report);

  ASSERT_EQ(report.operations().size(), 2u);
  EXPECT_TRUE(report.plan().empty());
  EXPECT_EQ(report.operations()[0].handle, (tess::OpHandle{0}));
  EXPECT_EQ(report.operations()[0].id, (tess::OpId{0}));
  EXPECT_EQ(report.operations()[0].status,
            tess::OperationStatus::InvalidIdentity);
  EXPECT_EQ(report.operations()[0].failure,
            tess::OperationFailure::NonDenseHandle);
  EXPECT_EQ(report.operations()[1].handle, (tess::OpHandle{1}));
  EXPECT_EQ(report.operations()[1].id, (tess::OpId{1}));
  EXPECT_EQ(report.operations()[1].failure,
            tess::OperationFailure::NonDenseHandle);

  tess::ResultChannel<std::uint64_t> channel;
  EXPECT_EQ(tess::record_plan_completions(report, channel), 2u);
  EXPECT_EQ(channel.size(), 2u);
  EXPECT_EQ(channel.state(tess::OpHandle{0}), tess::OpResultState::Failed);
  EXPECT_EQ(channel.state(tess::OpHandle{1}), tess::OpResultState::Failed);
}

TEST(TessQueued, SpanPlannerRejectsNonDenseId) {
  World world;
  tess::FrameOps source;
  (void)source.update_field(tess::DomainDesc::resident_chunks(),
                            tess::WritePolicy::ReadOnly);
  auto operations = std::vector<tess::QueuedOperation>{
      source.operations().begin(), source.operations().end()};
  operations[0].id = tess::OpId{7};
  tess::ExecutionReport report;

  tess::plan_operations(world, operations, report);

  ASSERT_EQ(report.operations().size(), 1u);
  EXPECT_EQ(report.operations()[0].status,
            tess::OperationStatus::InvalidIdentity);
  EXPECT_EQ(report.operations()[0].failure, tess::OperationFailure::NonDenseId);
  EXPECT_TRUE(report.plan().empty());
}

TEST(TessQueued, PlannedExecutionRejectsDifferentShapeBeforeCallback) {
  World planning_world;
  OtherWorld execution_world;
  tess::FrameOps ops;

  (void)ops.update_field(tess::DomainDesc::resident_chunks(),
                         tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(planning_world, ops);
  ASSERT_TRUE(report.ok());

  auto called = false;
  const auto result =
      tess::execute_planned_operation<tess::WritePolicy::UniquePerChunk>(
          execution_world, report.plan().operations()[0],
          [&](auto) { called = true; });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::InvalidShape);
  EXPECT_EQ(result.chunk_count, 0u);
  EXPECT_FALSE(called);
  EXPECT_EQ(execution_world.meta(tess::ChunkKey{0}).version, 0u);
}

TEST(TessQueued, ExecutePlannedOperationRunsCallbackAndMarksDirtyChunks) {
  World world;
  tess::FrameOps ops;
  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      DirtyTerrain,
      DirtyTerrain,
  };
  const std::vector<tess::ChunkKey> requested{
      tess::ChunkKey{3},
      tess::ChunkKey{1},
  };

  (void)ops.update_field(tess::DomainDesc::explicit_chunks(requested),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);

  ASSERT_TRUE(report.ok());
  ASSERT_EQ(report.plan().operations().size(), 1u);

  // [execute-planned-operation]
  const auto result =
      tess::execute_planned_operation<tess::WritePolicy::UniquePerChunk>(
          world, report.plan().operations()[0], [](auto view) {
            auto terrain = view.template field_span<TerrainTag>();
            terrain[0] = static_cast<std::uint16_t>(view.key().value + 10);
          });
  // [execute-planned-operation]

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::Executed);
  EXPECT_EQ(result.chunk_count, 2u);
  EXPECT_EQ(world.chunk(tess::ChunkKey{1})
                .template field<TerrainTag>(tess::LocalTileId{0}),
            11u);
  EXPECT_EQ(world.chunk(tess::ChunkKey{3})
                .template field<TerrainTag>(tess::LocalTileId{0}),
            13u);
  EXPECT_EQ(world.dirty_flags(tess::ChunkKey{1}), DirtyTerrain);
  EXPECT_EQ(world.dirty_flags(tess::ChunkKey{3}), DirtyTerrain);
  EXPECT_EQ(world.meta(tess::ChunkKey{1}).version, 1u);
  EXPECT_EQ(world.meta(tess::ChunkKey{3}).version, 1u);
}

TEST(TessQueued, PlannedDirtyMergeCoalescesRecordsInChunkOrder) {
  World world;
  tess::PlannedDirtyAccumulator dirty;
  dirty.reserve(2);

  const auto first =
      tess::Box3{tess::Coord3{96, 48, 0}, tess::Extent3{2, 3, 1}};
  const auto second =
      tess::Box3{tess::Coord3{100, 50, 0}, tess::Extent3{4, 2, 1}};

  EXPECT_EQ(dirty.record(world, tess::ChunkKey{3}, DirtyTerrain, first),
            tess::PlannedDirtyRecordStatus::Recorded);
  EXPECT_EQ(dirty.record(world, tess::ChunkKey{3}, DirtyCost, second),
            tess::PlannedDirtyRecordStatus::Recorded);

  const auto merged = tess::merge_planned_dirty(world, dirty);

  EXPECT_EQ(merged.status, tess::PlannedDirtyMergeStatus::Merged);
  EXPECT_EQ(merged.merged_chunk_count, 1u);
  EXPECT_TRUE(dirty.records().empty());
  EXPECT_EQ(world.dirty_flags(tess::ChunkKey{3}), DirtyTerrain | DirtyCost);
  EXPECT_EQ(world.meta(tess::ChunkKey{3}).version, 1u);
  EXPECT_EQ(world.dirty_bounds(tess::ChunkKey{3}),
            (tess::Box3{tess::Coord3{96, 48, 0}, tess::Extent3{8, 4, 1}}));
}

TEST(TessQueued, PlannedDirtyRecordRejectsInvalidChunkWithoutMutation) {
  World world;
  tess::PlannedDirtyAccumulator dirty;
  const auto bounds = tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}};

  const auto recorded = dirty.record(world, tess::ChunkKey{World::chunk_count},
                                     DirtyTerrain, bounds);
  const auto merged = tess::merge_planned_dirty(world, dirty);

  EXPECT_EQ(recorded, tess::PlannedDirtyRecordStatus::InvalidChunk);
  EXPECT_TRUE(dirty.records().empty());
  EXPECT_EQ(merged.status, tess::PlannedDirtyMergeStatus::Merged);
  EXPECT_EQ(merged.merged_chunk_count, 0u);
  EXPECT_EQ(world.dirty_flags(tess::ChunkKey{0}), 0u);
  EXPECT_EQ(world.meta(tess::ChunkKey{0}).version, 0u);
}

TEST(TessQueued, PlannedDirtyMergeRejectsDifferentShapeWithoutMutation) {
  World source_world;
  OtherWorld target_world;
  tess::PlannedDirtyAccumulator dirty;
  const auto bounds = tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}};
  ASSERT_EQ(dirty.record(source_world, tess::ChunkKey{1}, DirtyTerrain, bounds),
            tess::PlannedDirtyRecordStatus::Recorded);

  const auto rejected = tess::merge_planned_dirty(target_world, dirty);

  EXPECT_EQ(rejected.status, tess::PlannedDirtyMergeStatus::InvalidShape);
  EXPECT_EQ(rejected.merged_chunk_count, 0u);
  EXPECT_EQ(dirty.records().size(), 1u);
  EXPECT_EQ(target_world.dirty_flags(tess::ChunkKey{1}), 0u);
  EXPECT_EQ(target_world.meta(tess::ChunkKey{1}).version, 0u);

  const auto accepted = tess::merge_planned_dirty(source_world, dirty);
  EXPECT_EQ(accepted.status, tess::PlannedDirtyMergeStatus::Merged);
  EXPECT_EQ(accepted.merged_chunk_count, 1u);
}

TEST(TessQueued, DeferredExecutionRejectsMismatchedDirtyAccumulatorFirst) {
  World world;
  OtherWorld other_world;
  tess::FrameOps ops;
  tess::PlannedDirtyAccumulator dirty;
  const auto bounds = tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}};
  ASSERT_EQ(dirty.record(other_world, tess::ChunkKey{0}, DirtyTerrain, bounds),
            tess::PlannedDirtyRecordStatus::Recorded);

  (void)ops.update_field(tess::DomainDesc::resident_chunks(),
                         tess::FieldAccessDesc{0, DirtyTerrain, DirtyTerrain},
                         tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());

  auto called = false;
  const auto result = tess::execute_planned_operation_deferred_dirty<
      tess::WritePolicy::UniquePerChunk>(world, report.plan().operations()[0],
                                         dirty, [&](auto) { called = true; });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::InvalidShape);
  EXPECT_EQ(result.chunk_count, 0u);
  EXPECT_FALSE(called);
  EXPECT_EQ(world.meta(tess::ChunkKey{0}).version, 0u);
  EXPECT_EQ(dirty.records().size(), 1u);
}

TEST(TessQueued, EmptyDirtyExecutionKeepsItsWorldBinding) {
  World source_world;
  OtherWorld target_world;
  tess::PlannedDirtyAccumulator dirty;
  tess::FrameOps empty_ops;
  const auto empty_domain =
      tess::DomainDesc::explicit_chunks(std::span<const tess::ChunkKey>{});
  (void)empty_ops.update_field(
      empty_domain, tess::FieldAccessDesc{0, DirtyTerrain, DirtyTerrain},
      tess::WritePolicy::UniquePerChunk);
  const auto empty_report = tess::plan_operations(source_world, empty_ops);
  ASSERT_TRUE(empty_report.ok());
  ASSERT_EQ(empty_report.plan().size(), 1u);
  const auto empty_result = tess::execute_planned_operation_deferred_dirty<
      tess::WritePolicy::UniquePerChunk>(
      source_world, empty_report.plan().operations()[0], dirty, [](auto) {});
  ASSERT_EQ(empty_result.status, tess::PlannedExecutionStatus::Executed);
  ASSERT_TRUE(dirty.records().empty());

  tess::FrameOps target_ops;
  const std::vector<tess::ChunkKey> target_chunks{tess::ChunkKey{0}};
  (void)target_ops.update_field(
      tess::DomainDesc::explicit_chunks(target_chunks),
      tess::FieldAccessDesc{0, DirtyTerrain, DirtyTerrain},
      tess::WritePolicy::UniquePerChunk);
  const auto target_report = tess::plan_operations(target_world, target_ops);
  auto called = false;

  const auto rejected = tess::execute_planned_operation_deferred_dirty<
      tess::WritePolicy::UniquePerChunk>(target_world,
                                         target_report.plan().operations()[0],
                                         dirty, [&](auto) { called = true; });

  EXPECT_EQ(rejected.status, tess::PlannedExecutionStatus::InvalidShape);
  EXPECT_FALSE(called);
  EXPECT_EQ(target_world.dirty_flags(tess::ChunkKey{0}), 0u);
  EXPECT_EQ(target_world.meta(tess::ChunkKey{0}).version, 0u);
}

TEST(TessQueued, DeferredPlannedExecutionRecordsDirtyBeforeMerge) {
  World world;
  tess::FrameOps ops;
  tess::PlannedDirtyAccumulator dirty;
  dirty.reserve(2);
  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      DirtyTerrain,
      DirtyTerrain,
  };
  const std::vector<tess::ChunkKey> requested{
      tess::ChunkKey{3},
      tess::ChunkKey{1},
  };

  (void)ops.update_field(tess::DomainDesc::explicit_chunks(requested),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);

  ASSERT_TRUE(report.ok());
  ASSERT_EQ(report.plan().operations().size(), 1u);

  const auto result = tess::execute_planned_operation_deferred_dirty<
      tess::WritePolicy::UniquePerChunk>(
      world, report.plan().operations()[0], dirty, [](auto view) {
        auto terrain = view.template field_span<TerrainTag>();
        terrain[0] = static_cast<std::uint16_t>(view.key().value + 20);
      });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::Executed);
  EXPECT_EQ(result.chunk_count, 2u);
  EXPECT_EQ(dirty.records().size(), 2u);
  EXPECT_EQ(world.dirty_flags(tess::ChunkKey{1}), 0u);
  EXPECT_EQ(world.dirty_flags(tess::ChunkKey{3}), 0u);
  EXPECT_EQ(world.meta(tess::ChunkKey{1}).version, 0u);
  EXPECT_EQ(world.meta(tess::ChunkKey{3}).version, 0u);

  const auto merged = tess::merge_planned_dirty(world, dirty);

  EXPECT_EQ(merged.status, tess::PlannedDirtyMergeStatus::Merged);
  EXPECT_EQ(merged.merged_chunk_count, 2u);
  EXPECT_TRUE(dirty.records().empty());
  EXPECT_EQ(world.chunk(tess::ChunkKey{1})
                .template field<TerrainTag>(tess::LocalTileId{0}),
            21u);
  EXPECT_EQ(world.chunk(tess::ChunkKey{3})
                .template field<TerrainTag>(tess::LocalTileId{0}),
            23u);
  EXPECT_EQ(world.dirty_flags(tess::ChunkKey{1}), DirtyTerrain);
  EXPECT_EQ(world.dirty_flags(tess::ChunkKey{3}), DirtyTerrain);
  EXPECT_EQ(world.meta(tess::ChunkKey{1}).version, 1u);
  EXPECT_EQ(world.meta(tess::ChunkKey{3}).version, 1u);
}

TEST(TessQueued, ExecutePlanRejectsPolicyMismatchBeforeCallback) {
  World world;
  tess::FrameOps ops;

  (void)ops.update_field(tess::DomainDesc::resident_chunks(),
                         tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);

  ASSERT_TRUE(report.ok());
  bool called = false;
  const auto result = tess::execute_plan<tess::WritePolicy::ReadOnly>(
      world, report.plan(), [&](auto) { called = true; });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::PolicyMismatch);
  EXPECT_EQ(result.chunk_count, 0u);
  EXPECT_FALSE(called);
}

TEST(TessQueued, PrebuiltPlannedExecutionDoesNotAllocate) {
  World world;
  tess::FrameOps ops;

  (void)ops.update_field(tess::DomainDesc::resident_chunks(),
                         tess::FieldAccessDesc{},
                         tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());
  ASSERT_EQ(report.plan().operations().size(), 1u);

  tess_test::ScopedAllocationCounter counter;

  const auto result =
      tess::execute_planned_operation<tess::WritePolicy::UniquePerChunk>(
          world, report.plan().operations()[0], [](auto view) {
            auto terrain = view.template field_span<TerrainTag>();
            terrain[0] = static_cast<std::uint16_t>(view.key().value);
          });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::Executed);
  EXPECT_EQ(result.chunk_count, World::chunk_count);
  EXPECT_EQ(counter.count(), 0u);
}

TEST(TessQueued, PreReservedDeferredPlannedExecutionDoesNotAllocate) {
  World world;
  tess::FrameOps ops;
  tess::PlannedDirtyAccumulator dirty;
  dirty.reserve(World::chunk_count);
  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      DirtyTerrain,
      DirtyTerrain,
  };

  (void)ops.update_field(tess::DomainDesc::resident_chunks(), writes_terrain,
                         tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());
  ASSERT_EQ(report.plan().operations().size(), 1u);

  tess_test::ScopedAllocationCounter counter;

  const auto result =
      tess::execute_plan_deferred_dirty<tess::WritePolicy::UniquePerChunk>(
          world, report.plan(), dirty, [](auto view) {
            auto terrain = view.template field_span<TerrainTag>();
            terrain[0] = static_cast<std::uint16_t>(view.key().value);
          });
  const auto merged = tess::merge_planned_dirty(world, dirty);

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::Executed);
  EXPECT_EQ(result.chunk_count, World::chunk_count);
  EXPECT_EQ(merged.status, tess::PlannedDirtyMergeStatus::Merged);
  EXPECT_EQ(merged.merged_chunk_count, World::chunk_count);
  EXPECT_EQ(counter.count(), 0u);
}

TEST(TessQueued, ExecutePhaseDeferredDirtyVisitsOnlyPhaseRange) {
  World world;
  tess::FrameOps ops;
  tess::PlannedDirtyAccumulator dirty;
  dirty.reserve(2);
  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      DirtyTerrain,
      DirtyTerrain,
  };
  const std::vector<tess::ChunkKey> first_keys{tess::ChunkKey{1}};
  const std::vector<tess::ChunkKey> second_keys{tess::ChunkKey{2}};

  (void)ops.update_field(tess::DomainDesc::resident_chunks(),
                         tess::WritePolicy::ReadOnly);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(first_keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(second_keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());
  const auto phases = tess::plan_parallel_execution_phases(report.plan());
  ASSERT_TRUE(phases.ok());
  ASSERT_EQ(phases.phases().size(), 2u);

  const auto result =
      tess::execute_phase_deferred_dirty<tess::WritePolicy::UniquePerChunk>(
          world, report.plan(), phases.phases()[1], dirty, [](auto view) {
            auto terrain = view.template field_span<TerrainTag>();
            terrain[0] = static_cast<std::uint16_t>(view.key().value + 30);
          });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::Executed);
  EXPECT_EQ(result.chunk_count, 2u);
  EXPECT_EQ(dirty.records().size(), 2u);
  EXPECT_EQ(world.chunk(tess::ChunkKey{1})
                .template field<TerrainTag>(tess::LocalTileId{0}),
            31u);
  EXPECT_EQ(world.chunk(tess::ChunkKey{2})
                .template field<TerrainTag>(tess::LocalTileId{0}),
            32u);
  EXPECT_EQ(world.chunk(tess::ChunkKey{0})
                .template field<TerrainTag>(tess::LocalTileId{0}),
            0u);
  EXPECT_EQ(world.meta(tess::ChunkKey{1}).version, 0u);

  const auto merged = tess::merge_planned_dirty(world, dirty);
  EXPECT_EQ(merged.status, tess::PlannedDirtyMergeStatus::Merged);
  EXPECT_EQ(merged.merged_chunk_count, 2u);
  EXPECT_EQ(world.meta(tess::ChunkKey{1}).version, 1u);
  EXPECT_EQ(world.meta(tess::ChunkKey{2}).version, 1u);
}

TEST(TessQueued, ExecutePhaseDeferredDirtyWithUsesExecutorRange) {
  World world;
  tess::FrameOps ops;
  tess::PlannedDirtyAccumulator dirty;
  RecordingPhaseExecutor executor;
  dirty.reserve(2);
  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      DirtyTerrain,
      DirtyTerrain,
  };
  const std::vector<tess::ChunkKey> first_keys{tess::ChunkKey{1}};
  const std::vector<tess::ChunkKey> second_keys{tess::ChunkKey{2}};

  (void)ops.update_field(tess::DomainDesc::resident_chunks(),
                         tess::WritePolicy::ReadOnly);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(first_keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(second_keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());
  const auto phases = tess::plan_parallel_execution_phases(report.plan());
  ASSERT_TRUE(phases.ok());
  ASSERT_EQ(phases.phases().size(), 2u);

  const auto result = tess::execute_phase_deferred_dirty_with<
      tess::WritePolicy::UniquePerChunk>(
      executor, world, report.plan(), phases.phases()[1], dirty, [](auto view) {
        auto terrain = view.template field_span<TerrainTag>();
        terrain[0] = static_cast<std::uint16_t>(view.key().value + 50);
      });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::Executed);
  EXPECT_EQ(result.chunk_count, 2u);
  EXPECT_EQ(executor.indexes, (std::vector<std::size_t>{1, 2}));
  EXPECT_EQ(dirty.records().size(), 2u);
  EXPECT_EQ(world.chunk(tess::ChunkKey{1})
                .template field<TerrainTag>(tess::LocalTileId{0}),
            51u);
  EXPECT_EQ(world.chunk(tess::ChunkKey{2})
                .template field<TerrainTag>(tess::LocalTileId{0}),
            52u);
  EXPECT_EQ(world.chunk(tess::ChunkKey{0})
                .template field<TerrainTag>(tess::LocalTileId{0}),
            0u);
}

TEST(TessQueued, PlannedDirtyPartitionsMergeDeterministically) {
  World world;
  tess::PlannedDirtyPartitions partitions;
  tess::PlannedDirtyAccumulator scratch;
  partitions.resize(2);
  scratch.reserve(2);

  const auto first =
      tess::Box3{tess::Coord3{96, 48, 0}, tess::Extent3{2, 3, 1}};
  const auto second =
      tess::Box3{tess::Coord3{100, 50, 0}, tess::Extent3{4, 2, 1}};

  EXPECT_EQ(partitions.partition(1).record(world, tess::ChunkKey{3},
                                           DirtyTerrain, first),
            tess::PlannedDirtyRecordStatus::Recorded);
  EXPECT_EQ(partitions.partition(0).record(world, tess::ChunkKey{3}, DirtyCost,
                                           second),
            tess::PlannedDirtyRecordStatus::Recorded);

  const auto merged = tess::merge_planned_dirty(world, partitions, scratch);

  EXPECT_EQ(merged.status, tess::PlannedDirtyMergeStatus::Merged);
  EXPECT_EQ(merged.merged_chunk_count, 1u);
  EXPECT_TRUE(partitions.partition(0).records().empty());
  EXPECT_TRUE(partitions.partition(1).records().empty());
  EXPECT_TRUE(scratch.records().empty());
  EXPECT_EQ(world.dirty_flags(tess::ChunkKey{3}), DirtyTerrain | DirtyCost);
  EXPECT_EQ(world.dirty_bounds(tess::ChunkKey{3}),
            (tess::Box3{tess::Coord3{96, 48, 0}, tess::Extent3{8, 4, 1}}));
}

TEST(TessQueued, PlannedDirtyPartitionMergePreflightsEveryShape) {
  World world;
  OtherWorld other_world;
  tess::PlannedDirtyPartitions partitions;
  tess::PlannedDirtyAccumulator scratch;
  partitions.resize(2);
  const auto bounds = tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}};
  ASSERT_EQ(partitions.partition(0).record(world, tess::ChunkKey{0},
                                           DirtyTerrain, bounds),
            tess::PlannedDirtyRecordStatus::Recorded);
  ASSERT_EQ(partitions.partition(1).record(other_world, tess::ChunkKey{1},
                                           DirtyTerrain, bounds),
            tess::PlannedDirtyRecordStatus::Recorded);

  const auto merged = tess::merge_planned_dirty(world, partitions, scratch);

  EXPECT_EQ(merged.status, tess::PlannedDirtyMergeStatus::InvalidShape);
  EXPECT_EQ(merged.merged_chunk_count, 0u);
  EXPECT_EQ(partitions.partition(0).records().size(), 1u);
  EXPECT_EQ(partitions.partition(1).records().size(), 1u);
  EXPECT_TRUE(scratch.records().empty());
  EXPECT_EQ(world.dirty_flags(tess::ChunkKey{0}), 0u);
  EXPECT_EQ(world.meta(tess::ChunkKey{0}).version, 0u);
}

TEST(TessQueued, PlannedDirtyCollectReportsMixedShapesWithoutMutation) {
  World world;
  OtherWorld other_world;
  tess::PlannedDirtyAccumulator dirty;
  tess::PlannedDirtyPartitions partitions;
  partitions.resize(1);
  const auto bounds = tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}};
  ASSERT_EQ(dirty.record(world, tess::ChunkKey{0}, DirtyTerrain, bounds),
            tess::PlannedDirtyRecordStatus::Recorded);
  ASSERT_EQ(partitions.partition(0).record(other_world, tess::ChunkKey{1},
                                           DirtyTerrain, bounds),
            tess::PlannedDirtyRecordStatus::Recorded);

  const auto collected = tess::collect_planned_dirty(dirty, partitions);

  EXPECT_EQ(collected.status, tess::PlannedDirtyCollectStatus::InvalidShape);
  EXPECT_EQ(collected.record_count, 0u);
  EXPECT_EQ(dirty.records().size(), 1u);
  EXPECT_EQ(partitions.partition(0).records().size(), 1u);
}

TEST(TessQueued, PlannedDirtyCollectRejectsEmptyBoundMixedShapes) {
  World world;
  OtherWorld other_world;
  tess::FrameOps ops;
  const auto no_chunks = std::vector<tess::ChunkKey>{};
  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      DirtyTerrain,
      DirtyTerrain,
  };
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(no_chunks),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  const auto world_report = tess::plan_operations(world, ops);
  const auto other_report = tess::plan_operations(other_world, ops);
  ASSERT_TRUE(world_report.ok());
  ASSERT_TRUE(other_report.ok());

  tess::PlannedDirtyAccumulator dirty;
  tess::PlannedDirtyPartitions partitions;
  partitions.resize(1);
  const auto world_result = tess::execute_planned_operation_deferred_dirty<
      tess::WritePolicy::UniquePerChunk>(
      world, world_report.plan().operations()[0], dirty, [](auto) {});
  const auto other_result = tess::execute_planned_operation_deferred_dirty<
      tess::WritePolicy::UniquePerChunk>(other_world,
                                         other_report.plan().operations()[0],
                                         partitions.partition(0), [](auto) {});
  ASSERT_EQ(world_result.status, tess::PlannedExecutionStatus::Executed);
  ASSERT_EQ(other_result.status, tess::PlannedExecutionStatus::Executed);
  ASSERT_TRUE(dirty.records().empty());
  ASSERT_TRUE(partitions.partition(0).records().empty());

  const auto collected = tess::collect_planned_dirty(dirty, partitions);

  EXPECT_EQ(collected.status, tess::PlannedDirtyCollectStatus::InvalidShape);
  EXPECT_EQ(collected.record_count, 0u);
  EXPECT_TRUE(dirty.records().empty());
  EXPECT_TRUE(partitions.partition(0).records().empty());
}

TEST(TessQueued, ExecutePhasePartitionedDirtyUsesOperationLocalDirty) {
  World world;
  tess::FrameOps ops;
  tess::PlannedPhaseExecutionScratch scratch;
  ReverseRecordingPhaseExecutor executor;
  scratch.reserve_operations(2);
  scratch.reserve_dirty_records_per_operation(1);
  scratch.reserve_merged_dirty_records(2);
  scratch.prepare_for_operation_count(2);
  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      DirtyTerrain,
      DirtyTerrain,
  };
  const std::vector<tess::ChunkKey> first_keys{tess::ChunkKey{1}};
  const std::vector<tess::ChunkKey> second_keys{tess::ChunkKey{2}};

  (void)ops.update_field(tess::DomainDesc::resident_chunks(),
                         tess::WritePolicy::ReadOnly);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(first_keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(second_keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());
  const auto phases = tess::plan_parallel_execution_phases(report.plan());
  ASSERT_TRUE(phases.ok());
  ASSERT_EQ(phases.phases().size(), 2u);

  const auto result = tess::execute_phase_partitioned_dirty_with<
      tess::WritePolicy::UniquePerChunk>(
      executor, world, report.plan(), phases.phases()[1], scratch,
      [](auto view) {
        auto terrain = view.template field_span<TerrainTag>();
        terrain[0] = static_cast<std::uint16_t>(view.key().value + 60);
      });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::Executed);
  EXPECT_EQ(result.chunk_count, 2u);
  EXPECT_EQ(executor.indexes, (std::vector<std::size_t>{2, 1}));
  ASSERT_EQ(scratch.dirty_partitions().size(), 2u);
  ASSERT_EQ(scratch.dirty_partitions()[0].records().size(), 1u);
  ASSERT_EQ(scratch.dirty_partitions()[1].records().size(), 1u);
  EXPECT_EQ(scratch.dirty_partitions()[0].records()[0].chunk,
            tess::ChunkKey{1});
  EXPECT_EQ(scratch.dirty_partitions()[1].records()[0].chunk,
            tess::ChunkKey{2});
  EXPECT_EQ(world.meta(tess::ChunkKey{1}).version, 0u);
  EXPECT_EQ(world.meta(tess::ChunkKey{2}).version, 0u);

  const auto merged = tess::merge_planned_dirty(world, scratch);

  EXPECT_EQ(merged.status, tess::PlannedDirtyMergeStatus::Merged);
  EXPECT_EQ(merged.merged_chunk_count, 2u);
  EXPECT_EQ(world.chunk(tess::ChunkKey{1})
                .template field<TerrainTag>(tess::LocalTileId{0}),
            61u);
  EXPECT_EQ(world.chunk(tess::ChunkKey{2})
                .template field<TerrainTag>(tess::LocalTileId{0}),
            62u);
  EXPECT_EQ(world.meta(tess::ChunkKey{1}).version, 1u);
  EXPECT_EQ(world.meta(tess::ChunkKey{2}).version, 1u);
}

TEST(TessQueued, ExecutePhasePartitionedDirtyDeliversExactExecutorRange) {
  World world;
  tess::FrameOps ops;
  tess::PlannedPhaseExecutionScratch scratch;
  RangeRecordingPhaseExecutor executor;
  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      DirtyTerrain,
      DirtyTerrain,
  };
  const std::vector<tess::ChunkKey> first_keys{tess::ChunkKey{1}};
  const std::vector<tess::ChunkKey> second_keys{tess::ChunkKey{2}};

  (void)ops.update_field(tess::DomainDesc::resident_chunks(),
                         tess::WritePolicy::ReadOnly);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(first_keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(second_keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());
  const auto phases = tess::plan_parallel_execution_phases(report.plan());
  ASSERT_TRUE(phases.ok());
  ASSERT_EQ(phases.phases().size(), 2u);

  const auto result = tess::execute_phase_partitioned_dirty_with<
      tess::WritePolicy::UniquePerChunk>(
      executor, world, report.plan(), phases.phases()[1], scratch,
      [](auto view) {
        auto terrain = view.template field_span<TerrainTag>();
        terrain[0] = static_cast<std::uint16_t>(view.key().value + 90);
      });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::Executed);
  EXPECT_EQ(result.chunk_count, 2u);
  ASSERT_EQ(executor.ranges.size(), 1u);
  EXPECT_EQ(executor.ranges[0].first_operation, 1u);
  EXPECT_EQ(executor.ranges[0].operation_count, 2u);
  EXPECT_EQ(executor.indexes, (std::vector<std::size_t>{1, 2}));
}

TEST(TessQueued, ExecutePhasePartitionedDirtySupportsThreadedExecutor) {
  World world;
  tess::FrameOps ops;
  tess::PlannedPhaseExecutionScratch scratch;
  ThreadedRecordingPhaseExecutor executor;
  scratch.reserve_operations(2);
  scratch.reserve_dirty_records_per_operation(1);
  scratch.reserve_merged_dirty_records(2);
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
  ASSERT_EQ(phases.phases()[0].operation_count(), 2u);
  scratch.prepare_for_operation_count(phases.phases()[0].operation_count());

  std::atomic<std::size_t> entered = 0;
  std::atomic<std::size_t> max_concurrent = 0;
  std::atomic<std::size_t> active = 0;
  const auto expected_workers = phases.phases()[0].operation_count();

  const auto result = tess::execute_phase_partitioned_dirty_with<
      tess::WritePolicy::UniquePerChunk>(
      executor, world, report.plan(), phases.phases()[0], scratch,
      [&](auto view) {
        const auto now_active = active.fetch_add(1) + 1;
        auto observed = max_concurrent.load();
        while (observed < now_active &&
               !max_concurrent.compare_exchange_weak(observed, now_active)) {
        }

        entered.fetch_add(1);
        await_rendezvous(entered, expected_workers);

        auto terrain = view.template field_span<TerrainTag>();
        terrain[0] = static_cast<std::uint16_t>(view.key().value + 80);
        active.fetch_sub(1);
      });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::Executed);
  EXPECT_EQ(result.chunk_count, 2u);
  EXPECT_EQ(executor.indexes.size(), 2u);
  EXPECT_GE(max_concurrent.load(), 2u);
  ASSERT_EQ(scratch.dirty_partitions().size(), 2u);
  EXPECT_EQ(scratch.dirty_partitions()[0].records()[0].chunk,
            tess::ChunkKey{1});
  EXPECT_EQ(scratch.dirty_partitions()[1].records()[0].chunk,
            tess::ChunkKey{2});

  const auto merged = tess::merge_planned_dirty(world, scratch);

  EXPECT_EQ(merged.status, tess::PlannedDirtyMergeStatus::Merged);
  EXPECT_EQ(merged.merged_chunk_count, 2u);
  EXPECT_EQ(world.chunk(tess::ChunkKey{1})
                .template field<TerrainTag>(tess::LocalTileId{0}),
            81u);
  EXPECT_EQ(world.chunk(tess::ChunkKey{2})
                .template field<TerrainTag>(tess::LocalTileId{0}),
            82u);
  EXPECT_EQ(world.meta(tess::ChunkKey{1}).version, 1u);
  EXPECT_EQ(world.meta(tess::ChunkKey{2}).version, 1u);
}

TEST(TessQueued, ThreadedReadOnlyPhaseAllowsOverlappingChunksWithConstViews) {
  World world;
  tess::FrameOps ops;
  tess::PlannedPhaseExecutionScratch scratch;
  ThreadedRecordingPhaseExecutor executor;
  constexpr auto key = tess::ChunkKey{1};
  const std::vector<tess::ChunkKey> keys{key};
  world.chunk(key).template field<TerrainTag>(tess::LocalTileId{0}) = 41;

  (void)ops.update_field(tess::DomainDesc::explicit_chunks(keys),
                         tess::WritePolicy::ReadOnly);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(keys),
                         tess::WritePolicy::ReadOnly);
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());
  const auto phases = tess::plan_parallel_execution_phases(report.plan());
  ASSERT_TRUE(phases.ok());
  ASSERT_EQ(phases.phases().size(), 1u);
  ASSERT_EQ(phases.phases()[0].operation_count(), 2u);
  scratch.prepare_for_operation_count(phases.phases()[0].operation_count());

  std::atomic<std::size_t> entered = 0;
  std::atomic<std::size_t> max_concurrent = 0;
  std::atomic<std::size_t> active = 0;
  std::atomic<std::uint32_t> observed_sum = 0;
  const auto expected_workers = phases.phases()[0].operation_count();

  const auto result = tess::execute_phase_partitioned_dirty_with<
      tess::WritePolicy::ReadOnly>(
      executor, world, report.plan(), phases.phases()[0], scratch,
      [&](auto view) {
        auto terrain = view.template field_span<TerrainTag>();
        decltype(auto) page = view.page();
        decltype(auto) meta = view.meta();

        static_assert(
            std::is_same_v<decltype(terrain), std::span<const std::uint16_t>>);
        static_assert(std::is_same_v<decltype(page), const World::page_type&>);
        static_assert(std::is_same_v<decltype(meta), const tess::ChunkMeta&>);
        static_assert(
            !std::is_assignable_v<decltype(terrain[0]), std::uint16_t>);

        const auto now_active = active.fetch_add(1) + 1;
        auto observed = max_concurrent.load();
        while (observed < now_active &&
               !max_concurrent.compare_exchange_weak(observed, now_active)) {
        }

        entered.fetch_add(1);
        await_rendezvous(entered, expected_workers);

        observed_sum.fetch_add(terrain[0]);
        active.fetch_sub(1);
      });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::Executed);
  EXPECT_EQ(result.chunk_count, 2u);
  EXPECT_EQ(executor.indexes.size(), 2u);
  EXPECT_GE(max_concurrent.load(), 2u);
  EXPECT_EQ(observed_sum.load(), 82u);
  ASSERT_EQ(scratch.dirty_partitions().size(), 2u);
  EXPECT_TRUE(scratch.dirty_partitions()[0].records().empty());
  EXPECT_TRUE(scratch.dirty_partitions()[1].records().empty());
  EXPECT_EQ(world.meta(key).version, 0u);
  EXPECT_EQ(world.chunk(key).template field<TerrainTag>(tess::LocalTileId{0}),
            41u);
}

TEST(TessQueued, ScopedThreadPhaseExecutorMatchesSerialPhaseExecution) {
  World serial_world;
  World threaded_world;
  tess::FrameOps ops;
  tess::PlannedPhaseExecutionScratch serial_scratch;
  tess::PlannedPhaseExecutionScratch threaded_scratch;
  tess::SerialPhaseExecutor serial_executor;
  tess::ScopedThreadPhaseExecutor threaded_executor{2};
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
  const auto report = tess::plan_operations(serial_world, ops);
  ASSERT_TRUE(report.ok());
  const auto phases = tess::plan_parallel_execution_phases(report.plan());
  ASSERT_TRUE(phases.ok());
  ASSERT_EQ(phases.phases().size(), 1u);
  const auto phase = phases.phases()[0];
  serial_scratch.prepare_for_operation_count(phase.operation_count());
  threaded_scratch.prepare_for_operation_count(phase.operation_count());

  const auto serial_result = tess::execute_phase_partitioned_dirty_with<
      tess::WritePolicy::UniquePerChunk>(
      serial_executor, serial_world, report.plan(), phase, serial_scratch,
      [](auto view) {
        auto terrain = view.template field_span<TerrainTag>();
        terrain[0] = static_cast<std::uint16_t>(view.key().value + 120);
      });
  const auto threaded_result = tess::execute_phase_partitioned_dirty_with<
      tess::WritePolicy::UniquePerChunk>(
      threaded_executor, threaded_world, report.plan(), phase, threaded_scratch,
      [](auto view) {
        auto terrain = view.template field_span<TerrainTag>();
        terrain[0] = static_cast<std::uint16_t>(view.key().value + 120);
      });
  (void)tess::merge_planned_dirty(serial_world, serial_scratch);
  (void)tess::merge_planned_dirty(threaded_world, threaded_scratch);

  EXPECT_EQ(threaded_executor.worker_count(), 2u);
  EXPECT_EQ(serial_result.status, tess::PlannedExecutionStatus::Executed);
  EXPECT_EQ(threaded_result.status, tess::PlannedExecutionStatus::Executed);
  EXPECT_EQ(serial_result.chunk_count, threaded_result.chunk_count);
  EXPECT_EQ(serial_world.meta(tess::ChunkKey{1}).version,
            threaded_world.meta(tess::ChunkKey{1}).version);
  EXPECT_EQ(serial_world.meta(tess::ChunkKey{2}).version,
            threaded_world.meta(tess::ChunkKey{2}).version);
  EXPECT_EQ(serial_world.chunk(tess::ChunkKey{1})
                .template field<TerrainTag>(tess::LocalTileId{0}),
            threaded_world.chunk(tess::ChunkKey{1})
                .template field<TerrainTag>(tess::LocalTileId{0}));
  EXPECT_EQ(serial_world.chunk(tess::ChunkKey{2})
                .template field<TerrainTag>(tess::LocalTileId{0}),
            threaded_world.chunk(tess::ChunkKey{2})
                .template field<TerrainTag>(tess::LocalTileId{0}));
}

TEST(TessQueued, ScopedThreadPhaseExecutorReplaysStressPlansLikeSerial) {
  for (std::uint32_t seed = 1; seed <= 24u; ++seed) {
    World serial_world;
    World threaded_world;
    tess::FrameOps ops;
    tess::PlannedPhaseExecutionScratch serial_scratch;
    tess::PlannedPhaseExecutionScratch threaded_scratch;
    tess::SerialPhaseExecutor serial_executor;
    tess::ScopedThreadPhaseExecutor threaded_executor{4};

    for (const auto entry : replay_entries(seed)) {
      const std::vector<tess::ChunkKey> keys{entry.key};
      (void)ops.update_field(tess::DomainDesc::explicit_chunks(keys),
                             entry.access, tess::WritePolicy::UniquePerChunk);
    }

    const auto report = tess::plan_operations(serial_world, ops);
    ASSERT_TRUE(report.ok()) << "seed " << seed;
    ASSERT_EQ(report.plan().operations().size(), ops.size());

    const auto phase_plan = tess::plan_parallel_execution_phases(report.plan());
    ASSERT_TRUE(phase_plan.ok()) << "seed " << seed;
    ASSERT_GT(phase_plan.phases().size(), 1u) << "seed " << seed;

    serial_scratch.reserve_operations(ops.size());
    serial_scratch.reserve_dirty_records_per_operation(1);
    serial_scratch.reserve_merged_dirty_records(World::chunk_count);
    threaded_scratch.reserve_operations(ops.size());
    threaded_scratch.reserve_dirty_records_per_operation(1);
    threaded_scratch.reserve_merged_dirty_records(World::chunk_count);

    execute_replay_phases(serial_executor, serial_world, report.plan(),
                          phase_plan.phases(), serial_scratch, seed);
    execute_replay_phases(threaded_executor, threaded_world, report.plan(),
                          phase_plan.phases(), threaded_scratch, seed);

    expect_worlds_match(serial_world, threaded_world);
  }
}

TEST(TessQueued, WorkerPoolPhaseExecutorReplaysStressPlansLikeSerial) {
  tess::WorkerPoolPhaseExecutor pool_executor{4};

  for (std::uint32_t seed = 1; seed <= 24u; ++seed) {
    World serial_world;
    World pool_world;
    tess::FrameOps ops;
    tess::PlannedPhaseExecutionScratch serial_scratch;
    tess::PlannedPhaseExecutionScratch pool_scratch;
    tess::SerialPhaseExecutor serial_executor;

    for (const auto entry : replay_entries(seed)) {
      const std::vector<tess::ChunkKey> keys{entry.key};
      (void)ops.update_field(tess::DomainDesc::explicit_chunks(keys),
                             entry.access, tess::WritePolicy::UniquePerChunk);
    }

    const auto report = tess::plan_operations(serial_world, ops);
    ASSERT_TRUE(report.ok()) << "seed " << seed;

    const auto phase_plan = tess::plan_parallel_execution_phases(report.plan());
    ASSERT_TRUE(phase_plan.ok()) << "seed " << seed;

    serial_scratch.reserve_operations(ops.size());
    serial_scratch.reserve_dirty_records_per_operation(1);
    serial_scratch.reserve_merged_dirty_records(World::chunk_count);
    pool_scratch.reserve_operations(ops.size());
    pool_scratch.reserve_dirty_records_per_operation(1);
    pool_scratch.reserve_merged_dirty_records(World::chunk_count);

    execute_replay_phases(serial_executor, serial_world, report.plan(),
                          phase_plan.phases(), serial_scratch, seed);
    execute_replay_phases(pool_executor, pool_world, report.plan(),
                          phase_plan.phases(), pool_scratch, seed);

    expect_worlds_match(serial_world, pool_world);
  }
}

TEST(TessQueued, MixedPolicyPhaseRejectsBeforeSerialExecution) {
  World world;
  tess::FrameOps ops;
  tess::PlannedPhaseExecutionScratch scratch;
  tess::SerialPhaseExecutor executor;
  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      DirtyTerrain,
      DirtyTerrain,
  };
  const std::vector<tess::ChunkKey> first_keys{tess::ChunkKey{1}};
  const std::vector<tess::ChunkKey> mismatch_keys{tess::ChunkKey{2}};
  const std::vector<tess::ChunkKey> third_keys{tess::ChunkKey{3}};

  (void)ops.update_field(tess::DomainDesc::explicit_chunks(first_keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(mismatch_keys),
                         tess::WritePolicy::ReadOnly);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(third_keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());
  const auto phases = tess::plan_parallel_execution_phases(report.plan());
  ASSERT_TRUE(phases.ok());
  ASSERT_EQ(phases.phases().size(), 1u);

  const auto result = tess::execute_phase_partitioned_dirty_with<
      tess::WritePolicy::UniquePerChunk>(
      executor, world, report.plan(), phases.phases()[0], scratch,
      [](auto view) {
        auto terrain = view.template field_span<TerrainTag>();
        terrain[0] = static_cast<std::uint16_t>(view.key().value + 130);
      });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::PolicyMismatch);
  EXPECT_EQ(result.chunk_count, 0u);
  EXPECT_EQ(scratch.operation_count(), 0u);
  EXPECT_EQ(world.chunk(tess::ChunkKey{1})
                .template field<TerrainTag>(tess::LocalTileId{0}),
            0u);
  EXPECT_EQ(world.chunk(tess::ChunkKey{3})
                .template field<TerrainTag>(tess::LocalTileId{0}),
            0u);
  EXPECT_EQ(world.meta(tess::ChunkKey{1}).version, 0u);
  EXPECT_EQ(world.meta(tess::ChunkKey{3}).version, 0u);
}

TEST(TessQueued, MixedPolicyPhaseRejectsBeforeThreadedExecution) {
  World world;
  tess::FrameOps ops;
  tess::PlannedPhaseExecutionScratch scratch;
  ThreadedRecordingPhaseExecutor executor;
  scratch.reserve_operations(3);
  scratch.reserve_dirty_records_per_operation(1);
  scratch.reserve_merged_dirty_records(2);
  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      DirtyTerrain,
      DirtyTerrain,
  };
  const std::vector<tess::ChunkKey> first_keys{tess::ChunkKey{1}};
  const std::vector<tess::ChunkKey> mismatch_keys{tess::ChunkKey{2}};
  const std::vector<tess::ChunkKey> third_keys{tess::ChunkKey{3}};

  (void)ops.update_field(tess::DomainDesc::explicit_chunks(first_keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(mismatch_keys),
                         tess::WritePolicy::ReadOnly);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(third_keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());
  ASSERT_EQ(report.plan().operations().size(), 3u);
  const auto phases = tess::plan_parallel_execution_phases(report.plan());
  ASSERT_TRUE(phases.ok());
  ASSERT_EQ(phases.phases().size(), 1u);
  const auto phase = phases.phases()[0];

  std::atomic<std::size_t> callbacks = 0;
  const auto result = tess::execute_phase_partitioned_dirty_with<
      tess::WritePolicy::UniquePerChunk>(
      executor, world, report.plan(), phase, scratch, [&](auto view) {
        callbacks.fetch_add(1);
        auto terrain = view.template field_span<TerrainTag>();
        terrain[0] = static_cast<std::uint16_t>(view.key().value + 100);
      });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::PolicyMismatch);
  EXPECT_EQ(result.chunk_count, 0u);
  EXPECT_EQ(callbacks.load(), 0u);
  EXPECT_TRUE(executor.indexes.empty());
  EXPECT_EQ(scratch.operation_count(), 0u);
  EXPECT_EQ(world.meta(tess::ChunkKey{1}).version, 0u);
  EXPECT_EQ(world.meta(tess::ChunkKey{2}).version, 0u);
  EXPECT_EQ(world.meta(tess::ChunkKey{3}).version, 0u);

  EXPECT_EQ(world.chunk(tess::ChunkKey{1})
                .template field<TerrainTag>(tess::LocalTileId{0}),
            0u);
  EXPECT_EQ(world.chunk(tess::ChunkKey{2})
                .template field<TerrainTag>(tess::LocalTileId{0}),
            0u);
  EXPECT_EQ(world.chunk(tess::ChunkKey{3})
                .template field<TerrainTag>(tess::LocalTileId{0}),
            0u);
  EXPECT_EQ(world.meta(tess::ChunkKey{1}).version, 0u);
  EXPECT_EQ(world.meta(tess::ChunkKey{2}).version, 0u);
  EXPECT_EQ(world.meta(tess::ChunkKey{3}).version, 0u);
}

TEST(TessQueued, ExecutePhaseDeferredDirtyRejectsPolicyMismatchBeforeCallback) {
  World world;
  tess::FrameOps ops;
  tess::PlannedDirtyAccumulator dirty;
  const std::vector<tess::ChunkKey> keys{tess::ChunkKey{1}};

  (void)ops.update_field(tess::DomainDesc::explicit_chunks(keys),
                         tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());
  const auto phases = tess::plan_parallel_execution_phases(report.plan());
  ASSERT_TRUE(phases.ok());
  ASSERT_EQ(phases.phases().size(), 1u);

  auto called = false;
  const auto result =
      tess::execute_phase_deferred_dirty<tess::WritePolicy::ReadOnly>(
          world, report.plan(), phases.phases()[0], dirty,
          [&](auto) { called = true; });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::PolicyMismatch);
  EXPECT_EQ(result.chunk_count, 0u);
  EXPECT_FALSE(called);
  EXPECT_TRUE(dirty.records().empty());
}

TEST(TessQueued, ExecutePhaseDeferredDirtyWithStopsOnPolicyMismatch) {
  World world;
  tess::FrameOps ops;
  tess::PlannedDirtyAccumulator dirty;
  RecordingPhaseExecutor executor;
  const std::vector<tess::ChunkKey> keys{tess::ChunkKey{1}};

  (void)ops.update_field(tess::DomainDesc::explicit_chunks(keys),
                         tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());
  const auto phases = tess::plan_parallel_execution_phases(report.plan());
  ASSERT_TRUE(phases.ok());
  ASSERT_EQ(phases.phases().size(), 1u);

  auto called = false;
  const auto result =
      tess::execute_phase_deferred_dirty_with<tess::WritePolicy::ReadOnly>(
          executor, world, report.plan(), phases.phases()[0], dirty,
          [&](auto) { called = true; });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::PolicyMismatch);
  EXPECT_EQ(result.chunk_count, 0u);
  EXPECT_TRUE(executor.indexes.empty());
  EXPECT_FALSE(called);
  EXPECT_TRUE(dirty.records().empty());
}

TEST(TessQueued, ExecutePhasePartitionedDirtyStopsOnPolicyMismatch) {
  World world;
  tess::FrameOps ops;
  tess::PlannedPhaseExecutionScratch scratch;
  RecordingPhaseExecutor executor;
  const std::vector<tess::ChunkKey> keys{tess::ChunkKey{1}};

  (void)ops.update_field(tess::DomainDesc::explicit_chunks(keys),
                         tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());
  const auto phases = tess::plan_parallel_execution_phases(report.plan());
  ASSERT_TRUE(phases.ok());
  ASSERT_EQ(phases.phases().size(), 1u);

  auto called = false;
  const auto result =
      tess::execute_phase_partitioned_dirty_with<tess::WritePolicy::ReadOnly>(
          executor, world, report.plan(), phases.phases()[0], scratch,
          [&](auto) { called = true; });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::PolicyMismatch);
  EXPECT_EQ(result.chunk_count, 0u);
  EXPECT_TRUE(executor.indexes.empty());
  EXPECT_FALSE(called);
  EXPECT_EQ(scratch.operation_count(), 0u);
}

TEST(TessQueued, ExecutePhaseDeferredDirtyRejectsForeignPhase) {
  World world;
  tess::FrameOps ops;
  tess::FrameOps foreign_ops;
  tess::PlannedDirtyAccumulator dirty;

  (void)ops.update_field(tess::DomainDesc::resident_chunks(),
                         tess::WritePolicy::ReadOnly);
  (void)foreign_ops.update_field(tess::DomainDesc::resident_chunks(),
                                 tess::WritePolicy::ReadOnly);
  const auto report = tess::plan_operations(world, ops);
  const auto foreign_report = tess::plan_operations(world, foreign_ops);
  ASSERT_TRUE(report.ok());
  ASSERT_TRUE(foreign_report.ok());
  const auto foreign_phases =
      tess::plan_parallel_execution_phases(foreign_report.plan());
  ASSERT_TRUE(foreign_phases.ok());
  ASSERT_EQ(foreign_phases.phases().size(), 1u);

  auto called = false;
  const auto result =
      tess::execute_phase_deferred_dirty<tess::WritePolicy::ReadOnly>(
          world, report.plan(), foreign_phases.phases()[0], dirty,
          [&](auto) { called = true; });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::InvalidPhase);
  EXPECT_EQ(result.chunk_count, 0u);
  EXPECT_FALSE(called);
  EXPECT_TRUE(dirty.records().empty());
}

TEST(TessQueued, ExecutePhaseDeferredDirtyWithRejectsForeignPhase) {
  World world;
  tess::FrameOps ops;
  tess::FrameOps foreign_ops;
  tess::PlannedDirtyAccumulator dirty;
  RecordingPhaseExecutor executor;

  (void)ops.update_field(tess::DomainDesc::resident_chunks(),
                         tess::WritePolicy::ReadOnly);
  (void)foreign_ops.update_field(tess::DomainDesc::resident_chunks(),
                                 tess::WritePolicy::ReadOnly);
  const auto report = tess::plan_operations(world, ops);
  const auto foreign_report = tess::plan_operations(world, foreign_ops);
  ASSERT_TRUE(report.ok());
  ASSERT_TRUE(foreign_report.ok());
  const auto foreign_phases =
      tess::plan_parallel_execution_phases(foreign_report.plan());
  ASSERT_TRUE(foreign_phases.ok());
  ASSERT_EQ(foreign_phases.phases().size(), 1u);

  auto called = false;
  const auto result =
      tess::execute_phase_deferred_dirty_with<tess::WritePolicy::ReadOnly>(
          executor, world, report.plan(), foreign_phases.phases()[0], dirty,
          [&](auto) { called = true; });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::InvalidPhase);
  EXPECT_EQ(result.chunk_count, 0u);
  EXPECT_TRUE(executor.indexes.empty());
  EXPECT_FALSE(called);
  EXPECT_TRUE(dirty.records().empty());
}

TEST(TessQueued, ExecutePhasePartitionedDirtyRejectsForeignPhase) {
  World world;
  tess::FrameOps ops;
  tess::FrameOps foreign_ops;
  tess::PlannedPhaseExecutionScratch scratch;
  RecordingPhaseExecutor executor;

  (void)ops.update_field(tess::DomainDesc::resident_chunks(),
                         tess::WritePolicy::ReadOnly);
  (void)foreign_ops.update_field(tess::DomainDesc::resident_chunks(),
                                 tess::WritePolicy::ReadOnly);
  const auto report = tess::plan_operations(world, ops);
  const auto foreign_report = tess::plan_operations(world, foreign_ops);
  ASSERT_TRUE(report.ok());
  ASSERT_TRUE(foreign_report.ok());
  const auto foreign_phases =
      tess::plan_parallel_execution_phases(foreign_report.plan());
  ASSERT_TRUE(foreign_phases.ok());
  ASSERT_EQ(foreign_phases.phases().size(), 1u);

  auto called = false;
  const auto result =
      tess::execute_phase_partitioned_dirty_with<tess::WritePolicy::ReadOnly>(
          executor, world, report.plan(), foreign_phases.phases()[0], scratch,
          [&](auto) { called = true; });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::InvalidPhase);
  EXPECT_EQ(result.chunk_count, 0u);
  EXPECT_TRUE(executor.indexes.empty());
  EXPECT_FALSE(called);
  EXPECT_EQ(scratch.operation_count(), 0u);
  EXPECT_TRUE(scratch.dirty_partitions().empty());
}

TEST(TessQueued, PreReservedPartitionedPhaseExecutionDoesNotAllocate) {
  World world;
  tess::FrameOps ops;
  tess::PlannedPhaseExecutionScratch scratch;
  tess::SerialPhaseExecutor executor;
  scratch.reserve_operations(2);
  scratch.reserve_dirty_records_per_operation(1);
  scratch.reserve_merged_dirty_records(2);
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
  scratch.prepare_for_operation_count(phases.phases()[0].operation_count());

  tess_test::ScopedAllocationCounter counter;

  const auto result = tess::execute_phase_partitioned_dirty_with<
      tess::WritePolicy::UniquePerChunk>(
      executor, world, report.plan(), phases.phases()[0], scratch,
      [](auto view) {
        auto terrain = view.template field_span<TerrainTag>();
        terrain[0] = static_cast<std::uint16_t>(view.key().value + 70);
      });
  const auto merged = tess::merge_planned_dirty(world, scratch);

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::Executed);
  EXPECT_EQ(result.chunk_count, 2u);
  EXPECT_EQ(merged.status, tess::PlannedDirtyMergeStatus::Merged);
  EXPECT_EQ(merged.merged_chunk_count, 2u);
  EXPECT_EQ(counter.count(), 0u);
}

TEST(TessQueued, ExecutingPhasesMatchesDeferredPlanExecution) {
  World phase_world;
  World plan_world;
  tess::FrameOps ops;
  tess::PlannedDirtyAccumulator phase_dirty;
  tess::PlannedDirtyAccumulator plan_dirty;
  phase_dirty.reserve(2);
  plan_dirty.reserve(2);
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
  const auto report = tess::plan_operations(phase_world, ops);
  ASSERT_TRUE(report.ok());
  const auto phases = tess::plan_parallel_execution_phases(report.plan());
  ASSERT_TRUE(phases.ok());

  for (const auto phase : phases.phases()) {
    const auto result =
        tess::execute_phase_deferred_dirty<tess::WritePolicy::UniquePerChunk>(
            phase_world, report.plan(), phase, phase_dirty, [](auto view) {
              auto terrain = view.template field_span<TerrainTag>();
              terrain[0] = static_cast<std::uint16_t>(view.key().value + 40);
            });
    ASSERT_EQ(result.status, tess::PlannedExecutionStatus::Executed);
    (void)tess::merge_planned_dirty(phase_world, phase_dirty);
  }

  const auto plan_result =
      tess::execute_plan_deferred_dirty<tess::WritePolicy::UniquePerChunk>(
          plan_world, report.plan(), plan_dirty, [](auto view) {
            auto terrain = view.template field_span<TerrainTag>();
            terrain[0] = static_cast<std::uint16_t>(view.key().value + 40);
          });
  ASSERT_EQ(plan_result.status, tess::PlannedExecutionStatus::Executed);
  (void)tess::merge_planned_dirty(plan_world, plan_dirty);

  EXPECT_EQ(phase_world.chunk(tess::ChunkKey{1})
                .template field<TerrainTag>(tess::LocalTileId{0}),
            plan_world.chunk(tess::ChunkKey{1})
                .template field<TerrainTag>(tess::LocalTileId{0}));
  EXPECT_EQ(phase_world.chunk(tess::ChunkKey{2})
                .template field<TerrainTag>(tess::LocalTileId{0}),
            plan_world.chunk(tess::ChunkKey{2})
                .template field<TerrainTag>(tess::LocalTileId{0}));
  EXPECT_EQ(phase_world.meta(tess::ChunkKey{1}).version,
            plan_world.meta(tess::ChunkKey{1}).version);
  EXPECT_EQ(phase_world.meta(tess::ChunkKey{2}).version,
            plan_world.meta(tess::ChunkKey{2}).version);
}

}  // namespace
