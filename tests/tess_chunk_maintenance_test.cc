#include <gtest/gtest.h>
#include <tess/experimental/chunk_maintenance.h>
#include <tess/persistence/archive.h>
#include <tess/storage/sparse_world.h>
#include <tess/storage/world.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

#include "allocation_counter.h"

namespace {

namespace maintenance = tess::experimental::maintenance;

struct ValueTag {};

using Schema = tess::FieldSchema<tess::Field<ValueTag, std::uint16_t>>;
using Shape = tess::Shape<tess::Extent3{12, 4, 1}, tess::Extent3{4, 4, 1}>;
using DenseWorld = tess::AlwaysResidentWorld<Shape, Schema>;
using SparseWorld = tess::SparseResidentWorld<Shape, Schema>;
using Archive = tess::PersistenceSchema<
    0x746573732d6d6e32ull, 1,
    tess::PersistedField<ValueTag, 0x76616c75652d6d32ull, 1>>;

constexpr auto kTerrain = std::uint32_t{0b0001};
constexpr auto kLighting = std::uint32_t{0b0010};
constexpr auto kForeign = std::uint32_t{0b0100};
constexpr auto kOwned = kTerrain | kLighting;

struct ChunkSummary {
  std::uint64_t sum = 0;
  std::uint32_t nonzero = 0;

  friend auto operator==(const ChunkSummary&, const ChunkSummary&)
      -> bool = default;
};

struct SummaryRebuilder {
  bool* throw_once = nullptr;
  DenseWorld* dense_intervening_world = nullptr;
  tess::ChunkKey intervening_key{};

  template <typename World>
  void operator()(const World& world, tess::ChunkKey key,
                  tess::DirtyObservation, ChunkSummary& product) {
#if TESS_HAS_EXCEPTIONS
    if (throw_once != nullptr && *throw_once) {
      *throw_once = false;
      throw std::runtime_error("chunk rebuild failed");
    }
#endif
    product = {};
    for (const auto value : world.template field_span<ValueTag>(key)) {
      product.sum += value;
      product.nonzero += value != 0 ? 1u : 0u;
    }
    if (dense_intervening_world != nullptr) {
      dense_intervening_world->mark_dirty(
          intervening_key, kTerrain,
          tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}});
      dense_intervening_world = nullptr;
    }
  }
};

template <typename World>
auto rescan(const World& world, tess::ChunkKey key) -> ChunkSummary {
  auto result = ChunkSummary{};
  for (const auto value : world.template field_span<ValueTag>(key)) {
    result.sum += value;
    result.nonzero += value != 0 ? 1u : 0u;
  }
  return result;
}

template <typename Adapter>
void drain_to_idle(Adapter& adapter) {
  for (;;) {
    const auto result = adapter.flush();
    if (result == maintenance::DrainResult::Idle) {
      return;
    }
    ASSERT_EQ(result, maintenance::DrainResult::Drained);
  }
}

template <typename Backend, typename World>
using Adapter = maintenance::ChunkMaintenanceAdapter<World, ChunkSummary,
                                                     SummaryRebuilder, Backend>;

auto one_tile_box(std::uint64_t key) -> tess::Box3 {
  return tess::Box3{tess::Coord3{static_cast<std::int64_t>(key * 4u), 0, 0},
                    tess::Extent3{1, 1, 1}};
}

TEST(TessChunkMaintenance, DenseSlotsCoalesceAndOwnOnlyTheirMask) {
  DenseWorld world;
  Adapter<maintenance::DirtyBitScheduler, DenseWorld> adapter{
      world, kOwned, SummaryRebuilder{}};
  const auto key = tess::ChunkKey{1};
  world.field<ValueTag>(tess::Coord3{4, 0, 0}) = 7;

  EXPECT_EQ(adapter.mark_dirty(key, kTerrain, one_tile_box(1)),
            maintenance::ChunkMarkResult::Accepted);
  EXPECT_EQ(adapter.mark_dirty(key, kLighting, one_tile_box(1)),
            maintenance::ChunkMarkResult::Accepted);
  world.mark_dirty(key, kForeign, one_tile_box(1));
  EXPECT_EQ(adapter.flush(), maintenance::DrainResult::Drained);

  const auto view = adapter.product(key);
  ASSERT_EQ(view.state, maintenance::ChunkProductState::Current);
  ASSERT_NE(view.value, nullptr);
  EXPECT_EQ(*view.value, rescan(world, key));
  EXPECT_EQ(view.token.key, key);
  EXPECT_EQ(view.token.version, world.meta(key).version);
  EXPECT_EQ(view.token.residency_generation, 0u);
  EXPECT_TRUE(adapter.current(view.token));
  EXPECT_EQ(world.dirty_flags(key), kForeign);
  EXPECT_EQ(adapter.metrics().executions, 1u);
}

TEST(TessChunkMaintenance, InvalidMarksDoNotMutateOrSchedule) {
  DenseWorld world;
  Adapter<maintenance::DirtyBitScheduler, DenseWorld> adapter{
      world, kOwned, SummaryRebuilder{}};
  const auto key = tess::ChunkKey{0};

  EXPECT_EQ(adapter.mark_dirty(key, 0, one_tile_box(0)),
            maintenance::ChunkMarkResult::InvalidMask);
  EXPECT_EQ(adapter.mark_dirty(key, kForeign, one_tile_box(0)),
            maintenance::ChunkMarkResult::InvalidMask);
  EXPECT_EQ(adapter.mark_dirty(tess::ChunkKey{99}, kTerrain, one_tile_box(0)),
            maintenance::ChunkMarkResult::Missing);
  EXPECT_EQ(world.dirty_flags(key), 0u);
  EXPECT_EQ(adapter.metrics().schedule_calls, 0u);
}

TEST(TessChunkMaintenance, NewContentVersionMakesOldProductExplicitlyStale) {
  DenseWorld world;
  Adapter<maintenance::DirtyBitScheduler, DenseWorld> adapter{
      world, kOwned, SummaryRebuilder{}};
  const auto key = tess::ChunkKey{0};
  ASSERT_EQ(adapter.mark_dirty(key, kTerrain, one_tile_box(0)),
            maintenance::ChunkMarkResult::Accepted);
  drain_to_idle(adapter);
  const auto first = adapter.product(key);
  ASSERT_EQ(first.state, maintenance::ChunkProductState::Current);

  world.field<ValueTag>(tess::Coord3{0, 0}) = 6;
  ASSERT_EQ(adapter.mark_dirty(key, kTerrain, one_tile_box(0)),
            maintenance::ChunkMarkResult::Accepted);
  EXPECT_EQ(adapter.product(key).state, maintenance::ChunkProductState::Stale);
  EXPECT_FALSE(adapter.current(first.token));
  drain_to_idle(adapter);
  const auto second = adapter.product(key);
  EXPECT_EQ(second.state, maintenance::ChunkProductState::Current);
  EXPECT_GT(second.token.version, first.token.version);
}

TEST(TessChunkMaintenance, QueueCapacityFailureLeavesDirtyWorkRetryable) {
  DenseWorld world;
  Adapter<maintenance::FifoScheduler, DenseWorld> adapter{
      world, kOwned, SummaryRebuilder{}, 1};
  world.field<ValueTag>(tess::Coord3{0, 0, 0}) = 3;
  world.field<ValueTag>(tess::Coord3{4, 0, 0}) = 5;

  EXPECT_EQ(adapter.mark_dirty(tess::ChunkKey{0}, kTerrain, one_tile_box(0)),
            maintenance::ChunkMarkResult::Accepted);
  EXPECT_EQ(adapter.mark_dirty(tess::ChunkKey{1}, kTerrain, one_tile_box(1)),
            maintenance::ChunkMarkResult::CapacityExhausted);
  EXPECT_NE(world.dirty_flags(tess::ChunkKey{1}) & kTerrain, 0u);
  EXPECT_EQ(adapter.run_some(maintenance::MaintenanceBudget{1}),
            maintenance::DrainResult::Drained);
  ASSERT_EQ(adapter.retry(tess::ChunkKey{1}),
            maintenance::ScheduleResult::Accepted);
  drain_to_idle(adapter);

  EXPECT_EQ(adapter.product(tess::ChunkKey{0}).state,
            maintenance::ChunkProductState::Current);
  EXPECT_EQ(adapter.product(tess::ChunkKey{1}).state,
            maintenance::ChunkProductState::Current);
}

TEST(TessChunkMaintenance, BudgetedDrainContinuesInSlotOrder) {
  DenseWorld world;
  Adapter<maintenance::DirtyBitScheduler, DenseWorld> adapter{
      world, kOwned, SummaryRebuilder{}};
  for (std::uint64_t key = 0; key < 3; ++key) {
    world.field<ValueTag>(
        tess::Coord3{static_cast<std::int64_t>(key * 4u), 0}) =
        static_cast<std::uint16_t>(key + 1u);
    ASSERT_EQ(
        adapter.mark_dirty(tess::ChunkKey{key}, kTerrain, one_tile_box(key)),
        maintenance::ChunkMarkResult::Accepted);
  }

  EXPECT_EQ(adapter.run_some(maintenance::MaintenanceBudget{1}),
            maintenance::DrainResult::BudgetExhausted);
  EXPECT_EQ(adapter.product(tess::ChunkKey{0}).state,
            maintenance::ChunkProductState::Current);
  EXPECT_EQ(adapter.product(tess::ChunkKey{1}).state,
            maintenance::ChunkProductState::Unavailable);
  EXPECT_EQ(adapter.run_some(maintenance::MaintenanceBudget{1}),
            maintenance::DrainResult::BudgetExhausted);
  EXPECT_EQ(adapter.run_some(maintenance::MaintenanceBudget{1}),
            maintenance::DrainResult::Drained);
  EXPECT_EQ(adapter.flush(), maintenance::DrainResult::Idle);
}

TEST(TessChunkMaintenance, InterveningMarkCannotBeClearedByOldObservation) {
  DenseWorld world;
  auto rebuilder = SummaryRebuilder{};
  rebuilder.dense_intervening_world = &world;
  rebuilder.intervening_key = tess::ChunkKey{0};
  Adapter<maintenance::DirtyBitScheduler, DenseWorld> adapter{world, kOwned,
                                                              rebuilder};

  ASSERT_EQ(adapter.mark_dirty(tess::ChunkKey{0}, kTerrain, one_tile_box(0)),
            maintenance::ChunkMarkResult::Accepted);
  drain_to_idle(adapter);

  EXPECT_EQ(world.dirty_flags(tess::ChunkKey{0}) & kTerrain, 0u);
  EXPECT_EQ(adapter.product(tess::ChunkKey{0}).token.version,
            world.meta(tess::ChunkKey{0}).version);
  EXPECT_EQ(adapter.metrics().executions, 2u);
}

#if TESS_HAS_EXCEPTIONS
TEST(TessChunkMaintenance, ExceptionLeavesDirtyAndRequiresExplicitRetry) {
  DenseWorld world;
  auto throw_once = true;
  Adapter<maintenance::DirtyBitScheduler, DenseWorld> adapter{
      world, kOwned, SummaryRebuilder{&throw_once}};
  const auto key = tess::ChunkKey{0};
  world.field<ValueTag>(tess::Coord3{0, 0}) = 9;
  ASSERT_EQ(adapter.mark_dirty(key, kTerrain, one_tile_box(0)),
            maintenance::ChunkMarkResult::Accepted);

  EXPECT_THROW(static_cast<void>(adapter.flush()), std::runtime_error);
  EXPECT_NE(world.dirty_flags(key) & kTerrain, 0u);
  EXPECT_EQ(adapter.product(key).state,
            maintenance::ChunkProductState::Unavailable);
  ASSERT_EQ(adapter.retry(key), maintenance::ScheduleResult::Accepted);
  drain_to_idle(adapter);
  EXPECT_EQ(adapter.product(key).state,
            maintenance::ChunkProductState::Current);
}
#endif

TEST(TessChunkMaintenance, SparseCapacityOneRejectsTransitionBeforeIdle) {
  SparseWorld world{tess::ResidencyConfig{SparseWorld::page_byte_size}};
  Adapter<maintenance::DirtyBitScheduler, SparseWorld> adapter{
      world, kOwned, SummaryRebuilder{}};

  EXPECT_EQ(adapter.ensure_resident(tess::ChunkKey{0}).status,
            maintenance::ChunkResidencyStatus::NotIdle);
  ASSERT_EQ(adapter.flush(), maintenance::DrainResult::Idle);
  const auto first = adapter.ensure_resident(tess::ChunkKey{0});
  ASSERT_EQ(first.status, maintenance::ChunkResidencyStatus::Ready);
  ASSERT_NE(first.handle.generation, 0u);
  world.field<ValueTag>(tess::Coord3{0, 0}) = 11;
  ASSERT_EQ(adapter.mark_dirty(tess::ChunkKey{0}, kTerrain, one_tile_box(0)),
            maintenance::ChunkMarkResult::Accepted);
  drain_to_idle(adapter);
  const auto old = adapter.product(tess::ChunkKey{0});
  ASSERT_EQ(old.state, maintenance::ChunkProductState::Current);

  const auto second = adapter.ensure_resident(tess::ChunkKey{1});
  ASSERT_EQ(second.status, maintenance::ChunkResidencyStatus::Ready);
  EXPECT_FALSE(world.is_resident(tess::ChunkKey{0}));
  EXPECT_FALSE(adapter.current(old.token));
  EXPECT_EQ(adapter.product(tess::ChunkKey{1}).state,
            maintenance::ChunkProductState::Unavailable);

  const auto reloaded = adapter.ensure_resident(tess::ChunkKey{0});
  ASSERT_EQ(reloaded.status, maintenance::ChunkResidencyStatus::Ready);
  EXPECT_GT(reloaded.handle.generation, first.handle.generation);
  EXPECT_EQ(adapter.product(tess::ChunkKey{0}).state,
            maintenance::ChunkProductState::Unavailable);
}

TEST(TessChunkMaintenance, SparseCapacityTwoFollowsWorldLruAndExplicitEvict) {
  SparseWorld world{tess::ResidencyConfig{2 * SparseWorld::page_byte_size}};
  Adapter<maintenance::ImmediateScheduler, SparseWorld> adapter{
      world, kOwned, SummaryRebuilder{}};
  ASSERT_EQ(adapter.flush(), maintenance::DrainResult::Idle);
  ASSERT_EQ(adapter.ensure_resident(tess::ChunkKey{0}).status,
            maintenance::ChunkResidencyStatus::Ready);
  ASSERT_EQ(adapter.ensure_resident(tess::ChunkKey{1}).status,
            maintenance::ChunkResidencyStatus::Ready);
  ASSERT_EQ(adapter.ensure_resident(tess::ChunkKey{0}).status,
            maintenance::ChunkResidencyStatus::Ready);
  ASSERT_EQ(adapter.ensure_resident(tess::ChunkKey{2}).status,
            maintenance::ChunkResidencyStatus::Ready);
  EXPECT_TRUE(world.is_resident(tess::ChunkKey{0}));
  EXPECT_FALSE(world.is_resident(tess::ChunkKey{1}));
  EXPECT_TRUE(world.is_resident(tess::ChunkKey{2}));
  EXPECT_EQ(adapter.evict(tess::ChunkKey{0}),
            maintenance::ChunkEvictionResult::Evicted);
  EXPECT_FALSE(world.is_resident(tess::ChunkKey{0}));
  EXPECT_EQ(adapter.evict(tess::ChunkKey{0}),
            maintenance::ChunkEvictionResult::Missing);
}

template <typename Backend>
auto run_dense_scenario() -> std::array<ChunkSummary, 3> {
  DenseWorld world;
  Adapter<Backend, DenseWorld> adapter{world, kOwned, SummaryRebuilder{}};
  for (std::uint64_t mutation = 0; mutation < 96; ++mutation) {
    const auto x = static_cast<std::int64_t>(mutation % 12u);
    const auto y = static_cast<std::int64_t>((mutation * 3u) % 4u);
    const auto key = tess::ChunkKey{mutation % 3u};
    world.field<ValueTag>(tess::Coord3{x, y, 0}) =
        static_cast<std::uint16_t>(mutation + 1u);
    EXPECT_EQ(adapter.mark_dirty(key, kTerrain, one_tile_box(key.value)),
              maintenance::ChunkMarkResult::Accepted);
  }
  drain_to_idle(adapter);
  auto result = std::array<ChunkSummary, 3>{};
  for (std::size_t index = 0; index < result.size(); ++index) {
    const auto key = tess::ChunkKey{index};
    const auto view = adapter.product(key);
    EXPECT_EQ(view.state, maintenance::ChunkProductState::Current);
    EXPECT_EQ(*view.value, rescan(world, key));
    result[index] = *view.value;
  }
  return result;
}

template <typename Backend>
auto run_archive_scenario() -> std::vector<std::byte> {
  DenseWorld world;
  Adapter<Backend, DenseWorld> adapter{world, kOwned, SummaryRebuilder{}};
  for (std::uint64_t key = 0; key < 3; ++key) {
    world.field<ValueTag>(
        tess::Coord3{static_cast<std::int64_t>(key * 4u), 0}) =
        static_cast<std::uint16_t>(key + 10u);
    EXPECT_EQ(
        adapter.mark_dirty(tess::ChunkKey{key}, kTerrain, one_tile_box(key)),
        maintenance::ChunkMarkResult::Accepted);
  }
  drain_to_idle(adapter);
  std::vector<std::byte> bytes;
  EXPECT_GT(tess::save_world_archive<Archive>(world, bytes).bytes_written, 0u);
  for (std::uint64_t key = 0; key < 3; ++key) {
    EXPECT_EQ(adapter.product(tess::ChunkKey{key}).state,
              maintenance::ChunkProductState::Current);
  }
  return bytes;
}

template <typename Backend>
auto run_sparse_archive_scenario() -> std::vector<std::byte> {
  SparseWorld world{tess::ResidencyConfig{2 * SparseWorld::page_byte_size}};
  Adapter<Backend, SparseWorld> adapter{world, kOwned, SummaryRebuilder{}};
  EXPECT_EQ(adapter.flush(), maintenance::DrainResult::Idle);
  constexpr auto keys = std::array{tess::ChunkKey{0}, tess::ChunkKey{2}};
  for (const auto key : keys) {
    EXPECT_EQ(adapter.ensure_resident(key).status,
              maintenance::ChunkResidencyStatus::Ready);
  }
  for (const auto key : keys) {
    world.field<ValueTag>(
        tess::Coord3{static_cast<std::int64_t>(key.value * 4u), 0}) =
        static_cast<std::uint16_t>(key.value + 30u);
    EXPECT_EQ(adapter.mark_dirty(key, kTerrain, one_tile_box(key.value)),
              maintenance::ChunkMarkResult::Accepted);
  }
  drain_to_idle(adapter);
  std::vector<std::byte> bytes;
  EXPECT_GT(tess::save_world_archive<Archive>(world, bytes).bytes_written, 0u);
  for (const auto key : world.resident_chunk_keys()) {
    const auto view = adapter.product(key);
    EXPECT_EQ(view.state, maintenance::ChunkProductState::Current);
    EXPECT_EQ(*view.value, rescan(world, key));
  }
  return bytes;
}

TEST(TessChunkMaintenance, BackendsMatchIndependentRescanAndArchiveOracle) {
  const auto immediate = run_dense_scenario<maintenance::ImmediateScheduler>();
  const auto dirty = run_dense_scenario<maintenance::DirtyBitScheduler>();
  EXPECT_EQ(dirty, immediate);
  EXPECT_EQ(run_archive_scenario<maintenance::ImmediateScheduler>(),
            run_archive_scenario<maintenance::DirtyBitScheduler>());
  EXPECT_EQ(run_sparse_archive_scenario<maintenance::ImmediateScheduler>(),
            run_sparse_archive_scenario<maintenance::DirtyBitScheduler>());
}

TEST(TessChunkMaintenance, ExplicitFlushIsDeterministicAcrossRuns) {
  const auto expected = run_dense_scenario<maintenance::DirtyBitScheduler>();
  for (int run = 0; run < 100; ++run) {
    EXPECT_EQ(run_dense_scenario<maintenance::DirtyBitScheduler>(), expected);
  }
}

TEST(TessChunkMaintenance, SparseArchiveLoadNeedsIdleReconciliation) {
  SparseWorld source{tess::ResidencyConfig{2 * SparseWorld::page_byte_size}};
  (void)source.ensure_resident(tess::ChunkKey{0});
  (void)source.ensure_resident(tess::ChunkKey{1});
  source.field<ValueTag>(tess::Coord3{0, 0}) = 21;
  source.field<ValueTag>(tess::Coord3{4, 0}) = 22;
  std::vector<std::byte> bytes;
  ASSERT_GT(tess::save_world_archive<Archive>(source, bytes).bytes_written, 0u);

  SparseWorld target{tess::ResidencyConfig{2 * SparseWorld::page_byte_size}};
  Adapter<maintenance::DirtyBitScheduler, SparseWorld> adapter{
      target, kOwned, SummaryRebuilder{}};
  EXPECT_EQ(adapter.reconcile_residency(),
            maintenance::ChunkResidencyStatus::NotIdle);
  ASSERT_EQ(adapter.flush(), maintenance::DrainResult::Idle);
  ASSERT_EQ(tess::load_world_archive<Archive>(target, bytes, kTerrain).status,
            tess::WorldArchiveStatus::Ok);
  ASSERT_EQ(adapter.reconcile_residency(),
            maintenance::ChunkResidencyStatus::Ready);
  for (const auto key : target.resident_chunk_keys()) {
    ASSERT_EQ(adapter.retry(key), maintenance::ScheduleResult::Accepted);
  }
  drain_to_idle(adapter);
  for (const auto key : target.resident_chunk_keys()) {
    const auto view = adapter.product(key);
    ASSERT_EQ(view.state, maintenance::ChunkProductState::Current);
    EXPECT_EQ(*view.value, rescan(target, key));
  }
}

TEST(TessChunkMaintenance, WarmDenseSchedulingAndDrainDoNotAllocate) {
  DenseWorld world;
  Adapter<maintenance::DirtyBitScheduler, DenseWorld> adapter{
      world, kOwned, SummaryRebuilder{}};
  ASSERT_EQ(adapter.mark_dirty(tess::ChunkKey{0}, kTerrain, one_tile_box(0)),
            maintenance::ChunkMarkResult::Accepted);
  drain_to_idle(adapter);

  {
    tess_test::ScopedAllocationCounter counter;
    for (int call = 0; call < 1'000; ++call) {
      ASSERT_EQ(
          adapter.mark_dirty(tess::ChunkKey{0}, kTerrain, one_tile_box(0)),
          maintenance::ChunkMarkResult::Accepted);
    }
    ASSERT_EQ(adapter.flush(), maintenance::DrainResult::Drained);
    ASSERT_EQ(adapter.flush(), maintenance::DrainResult::Idle);
    EXPECT_EQ(counter.count(), 0u);
  }
}

TEST(TessChunkMaintenance, ConcurrentOffersAreSafeWithoutWorldMutation) {
  DenseWorld world;
  Adapter<maintenance::DirtyBitScheduler, DenseWorld> adapter{
      world, kOwned, SummaryRebuilder{}};
  ASSERT_EQ(adapter.mark_dirty(tess::ChunkKey{0}, kTerrain, one_tile_box(0)),
            maintenance::ChunkMarkResult::Accepted);
  std::atomic<bool> start = false;
  std::array<std::thread, 4> producers;
  for (auto& producer : producers) {
    producer = std::thread([&] {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      for (int call = 0; call < 1'000; ++call) {
        EXPECT_EQ(adapter.retry(tess::ChunkKey{0}),
                  maintenance::ScheduleResult::Accepted);
      }
    });
  }
  start.store(true, std::memory_order_release);
  for (auto& producer : producers) {
    producer.join();
  }
  drain_to_idle(adapter);
  EXPECT_EQ(adapter.product(tess::ChunkKey{0}).state,
            maintenance::ChunkProductState::Current);
  EXPECT_EQ(adapter.metrics().executions, 1u);
}

TEST(TessChunkMaintenance, ReleaseRequiresFreshIdleAndInvalidatesAdapter) {
  DenseWorld world;
  Adapter<maintenance::DirtyBitScheduler, DenseWorld> adapter{
      world, kOwned, SummaryRebuilder{}};
  EXPECT_EQ(adapter.try_release(),
            maintenance::ChunkAdapterReleaseResult::NotIdle);

  ASSERT_EQ(adapter.mark_dirty(tess::ChunkKey{0}, kTerrain, one_tile_box(0)),
            maintenance::ChunkMarkResult::Accepted);
  EXPECT_EQ(adapter.flush(), maintenance::DrainResult::Drained);
  EXPECT_EQ(adapter.try_release(),
            maintenance::ChunkAdapterReleaseResult::NotIdle);
  ASSERT_EQ(adapter.flush(), maintenance::DrainResult::Idle);

  ASSERT_EQ(adapter.retry(tess::ChunkKey{0}),
            maintenance::ScheduleResult::Accepted);
  EXPECT_EQ(adapter.try_release(),
            maintenance::ChunkAdapterReleaseResult::NotIdle);
  EXPECT_EQ(adapter.flush(), maintenance::DrainResult::Drained);
  EXPECT_EQ(adapter.try_release(),
            maintenance::ChunkAdapterReleaseResult::NotIdle);
  ASSERT_EQ(adapter.flush(), maintenance::DrainResult::Idle);

  EXPECT_EQ(adapter.try_release(),
            maintenance::ChunkAdapterReleaseResult::Released);
  EXPECT_EQ(adapter.try_release(),
            maintenance::ChunkAdapterReleaseResult::AlreadyReleased);
  EXPECT_EQ(adapter.mark_dirty(tess::ChunkKey{0}, kTerrain, one_tile_box(0)),
            maintenance::ChunkMarkResult::Released);
}

TEST(TessChunkMaintenance, AdapterIsImmovableAndMayDropPendingAtDestruction) {
  using DenseAdapter = Adapter<maintenance::DirtyBitScheduler, DenseWorld>;
  static_assert(!std::is_copy_constructible_v<DenseAdapter>);
  static_assert(!std::is_move_constructible_v<DenseAdapter>);
  DenseWorld world;
  {
    DenseAdapter adapter{world, kOwned, SummaryRebuilder{}};
    ASSERT_EQ(adapter.mark_dirty(tess::ChunkKey{0}, kTerrain, one_tile_box(0)),
              maintenance::ChunkMarkResult::Accepted);
  }
  EXPECT_NE(world.dirty_flags(tess::ChunkKey{0}) & kTerrain, 0u);
}

}  // namespace
