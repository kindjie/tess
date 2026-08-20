#include <gtest/gtest.h>
#include <tess/experimental/chunk_maintenance.h>
#include <tess/persistence/archive.h>
#include <tess/storage/sparse_world.h>
#include <tess/storage/world.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
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

constexpr auto kTerrain = tess::DirtyMask{0b0001};
constexpr auto kLighting = tess::DirtyMask{0b0010};
constexpr auto kForeign = tess::DirtyMask{0b0100};
constexpr auto kOwned = kTerrain | kLighting;

class SynchronousDebtBackend {
 public:
  explicit SynchronousDebtBackend(std::size_t) { active_ = this; }

  ~SynchronousDebtBackend() { active_ = nullptr; }

  [[nodiscard]] auto schedule(maintenance::MaintenanceTask& task)
      -> maintenance::ScheduleResult {
    auto synchronous = false;
    {
      const auto lock = std::scoped_lock{state_mutex_};
      ++metrics_.schedule_calls;
      synchronous = synchronous_;
      if (!synchronous) {
        if (pending_ != nullptr) {
          ++metrics_.capacity_failures;
          return maintenance::ScheduleResult::CapacityExhausted;
        }
        pending_ = &task;
        return maintenance::ScheduleResult::Accepted;
      }
    }
    return run_synchronously(task);
  }

  [[nodiscard]] auto run_some(maintenance::MaintenanceBudget budget)
      -> maintenance::BackendDrainResult {
    if (budget.remaining() == 0) {
      return maintenance::BackendDrainResult::Completed;
    }
    static_cast<void>(run_pending(budget));
    return maintenance::BackendDrainResult::Completed;
  }

  [[nodiscard]] auto flush() -> maintenance::BackendDrainResult {
    while (true) {
      auto budget = maintenance::MaintenanceBudget{};
      if (!run_pending(budget)) {
        break;
      }
    }
    return maintenance::BackendDrainResult::Completed;
  }

  [[nodiscard]] auto metrics() const noexcept
      -> maintenance::MaintenanceMetrics {
    const auto lock = std::scoped_lock{state_mutex_};
    return metrics_;
  }

  [[nodiscard]] auto has_pending() const noexcept -> bool {
    const auto lock = std::scoped_lock{state_mutex_};
    return pending_ != nullptr;
  }

  static void enable_synchronous_for_test() {
    ASSERT_NE(active_, nullptr);
    const auto lock = std::scoped_lock{active_->state_mutex_};
    ASSERT_EQ(active_->pending_, nullptr);
    active_->synchronous_ = true;
  }

 private:
  struct ActiveRun {
    maintenance::MaintenanceTask* task = nullptr;
  };

  [[nodiscard]] auto run_synchronously(maintenance::MaintenanceTask& task)
      -> maintenance::ScheduleResult {
    const auto run_lock = std::scoped_lock{run_mutex_};
    if (active_run_ != nullptr) {
      const auto lock = std::scoped_lock{state_mutex_};
      if (pending_ != nullptr) {
        ++metrics_.capacity_failures;
        return maintenance::ScheduleResult::CapacityExhausted;
      }
      pending_ = &task;
      return maintenance::ScheduleResult::Accepted;
    }

    auto budget = maintenance::MaintenanceBudget{};
    invoke(task, budget);
    return maintenance::ScheduleResult::Accepted;
  }

  void invoke(maintenance::MaintenanceTask& task,
              maintenance::MaintenanceBudget& budget) {
    const auto run_lock = std::scoped_lock{run_mutex_};
    ASSERT_EQ(active_run_, nullptr);
    auto active = ActiveRun{&task};
    struct RestoreActiveRun {
      ActiveRun*& current;
      ~RestoreActiveRun() { current = nullptr; }
    };
    active_run_ = &active;
    const auto restore = RestoreActiveRun{active_run_};
    {
      const auto lock = std::scoped_lock{state_mutex_};
      ++metrics_.executions;
    }
    task.run(budget);
  }

  [[nodiscard]] auto run_pending(maintenance::MaintenanceBudget& budget)
      -> bool {
    const auto run_lock = std::scoped_lock{run_mutex_};
    if (active_run_ != nullptr) {
      ADD_FAILURE() << "recursive backend drain";
      return false;
    }
    auto* task = static_cast<maintenance::MaintenanceTask*>(nullptr);
    {
      const auto lock = std::scoped_lock{state_mutex_};
      task = pending_;
      pending_ = nullptr;
    }
    if (task == nullptr) {
      return false;
    }
    invoke(*task, budget);
    return true;
  }

  static inline SynchronousDebtBackend* active_ = nullptr;
  mutable std::mutex state_mutex_;
  std::recursive_mutex run_mutex_;
  maintenance::MaintenanceTask* pending_ = nullptr;
  ActiveRun* active_run_ = nullptr;
  bool synchronous_ = false;
  maintenance::MaintenanceMetrics metrics_{};
};

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
  std::function<void()>* after_rebuild = nullptr;

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
    if (after_rebuild != nullptr) {
      (*after_rebuild)();
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
  EXPECT_EQ(view.token.content_version, world.meta(key).content_version);
  EXPECT_EQ(view.token.residency_generation, tess::ResidencyGeneration{});
  EXPECT_TRUE(adapter.current(view.token));
  EXPECT_EQ(world.dirty_mask(key), kForeign);
  EXPECT_EQ(adapter.metrics().executions, 1u);
}

TEST(TessChunkMaintenance, InvalidMarksDoNotMutateOrSchedule) {
  DenseWorld world;
  Adapter<maintenance::DirtyBitScheduler, DenseWorld> adapter{
      world, kOwned, SummaryRebuilder{}};
  const auto key = tess::ChunkKey{0};

  EXPECT_EQ(adapter.mark_dirty(key, tess::DirtyMask{}, one_tile_box(0)),
            maintenance::ChunkMarkResult::InvalidMask);
  EXPECT_EQ(adapter.mark_dirty(key, kForeign, one_tile_box(0)),
            maintenance::ChunkMarkResult::InvalidMask);
  EXPECT_EQ(adapter.mark_dirty(tess::ChunkKey{99}, kTerrain, one_tile_box(0)),
            maintenance::ChunkMarkResult::Missing);
  EXPECT_TRUE(world.dirty_mask(key).empty());
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
  EXPECT_GT(second.token.content_version.value,
            first.token.content_version.value);
}

TEST(TessChunkMaintenance, RetryRebuildsAcrossDisjointOwnerVersionAdvance) {
  DenseWorld world;
  Adapter<maintenance::DirtyBitScheduler, DenseWorld> adapter{
      world, kTerrain, SummaryRebuilder{}};
  const auto key = tess::ChunkKey{0};
  world.field<ValueTag>(tess::Coord3{0, 0}) = 6;
  ASSERT_EQ(adapter.mark_dirty(key, kTerrain, one_tile_box(0)),
            maintenance::ChunkMarkResult::Accepted);
  drain_to_idle(adapter);
  const auto first = adapter.product(key);
  ASSERT_EQ(first.state, maintenance::ChunkProductState::Current);

  world.mark_dirty(key, kLighting, one_tile_box(0));
  EXPECT_EQ(adapter.product(key).state, maintenance::ChunkProductState::Stale);
  ASSERT_EQ(adapter.retry(key), maintenance::ScheduleResult::Accepted);
  drain_to_idle(adapter);

  const auto repaired = adapter.product(key);
  EXPECT_EQ(repaired.state, maintenance::ChunkProductState::Current);
  EXPECT_EQ(repaired.token.content_version, world.meta(key).content_version);
  EXPECT_EQ(world.dirty_mask(key), kLighting);
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
  EXPECT_FALSE((world.dirty_mask(tess::ChunkKey{1}) & kTerrain).empty());
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

  EXPECT_TRUE((world.dirty_mask(tess::ChunkKey{0}) & kTerrain).empty());
  EXPECT_EQ(adapter.product(tess::ChunkKey{0}).token.content_version,
            world.meta(tess::ChunkKey{0}).content_version);
  EXPECT_EQ(adapter.metrics().executions, 2u);
}

template <typename Backend>
void expect_capacity_blocked_follow_up_remains_retryable() {
  DenseWorld world;
  auto after_rebuild = std::function<void()>{};
  auto rebuilder = SummaryRebuilder{};
  rebuilder.dense_intervening_world = &world;
  rebuilder.intervening_key = tess::ChunkKey{0};
  rebuilder.after_rebuild = &after_rebuild;
  Adapter<Backend, DenseWorld> adapter{world, kTerrain, rebuilder, 1};
  auto inject_once = true;
  after_rebuild = [&] {
    if (!inject_once) {
      return;
    }
    inject_once = false;
    EXPECT_EQ(adapter.mark_dirty(tess::ChunkKey{1}, kTerrain, one_tile_box(1)),
              maintenance::ChunkMarkResult::Accepted);
  };

  ASSERT_EQ(adapter.mark_dirty(tess::ChunkKey{0}, kTerrain, one_tile_box(0)),
            maintenance::ChunkMarkResult::Accepted);
  EXPECT_EQ(adapter.flush(), maintenance::DrainResult::BudgetExhausted);
  EXPECT_FALSE((world.dirty_mask(tess::ChunkKey{0}) & kTerrain).empty());
  EXPECT_EQ(adapter.product(tess::ChunkKey{0}).state,
            maintenance::ChunkProductState::Stale);
  EXPECT_GE(adapter.metrics().capacity_failures, 1u);
  EXPECT_EQ(adapter.try_release(),
            maintenance::ChunkAdapterReleaseResult::NotIdle);

  drain_to_idle(adapter);
  EXPECT_EQ(adapter.product(tess::ChunkKey{0}).state,
            maintenance::ChunkProductState::Current);
  EXPECT_TRUE((world.dirty_mask(tess::ChunkKey{0}) & kTerrain).empty());
  EXPECT_EQ(adapter.product(tess::ChunkKey{1}).state,
            maintenance::ChunkProductState::Current);
}

TEST(TessChunkMaintenance, FifoCapacityFailureRetainsFollowUpDebt) {
  expect_capacity_blocked_follow_up_remains_retryable<
      maintenance::FifoScheduler>();
}

TEST(TessChunkMaintenance, CoalescingCapacityFailureRetainsFollowUpDebt) {
  expect_capacity_blocked_follow_up_remains_retryable<
      maintenance::CoalescingScheduler>();
}

TEST(TessChunkMaintenance, BudgetedDrainDoesNotSynchronouslyReofferDebt) {
  DenseWorld world;
  auto after_rebuild = std::function<void()>{};
  auto rebuilder = SummaryRebuilder{};
  rebuilder.dense_intervening_world = &world;
  rebuilder.intervening_key = tess::ChunkKey{0};
  rebuilder.after_rebuild = &after_rebuild;
  Adapter<SynchronousDebtBackend, DenseWorld> adapter{world, kTerrain,
                                                      rebuilder, 1};
  auto inject_once = true;
  after_rebuild = [&] {
    if (!inject_once) {
      return;
    }
    inject_once = false;
    EXPECT_EQ(adapter.mark_dirty(tess::ChunkKey{1}, kTerrain, one_tile_box(1)),
              maintenance::ChunkMarkResult::Accepted);
  };

  ASSERT_EQ(adapter.mark_dirty(tess::ChunkKey{0}, kTerrain, one_tile_box(0)),
            maintenance::ChunkMarkResult::Accepted);
  ASSERT_EQ(adapter.run_some(maintenance::MaintenanceBudget{1}),
            maintenance::DrainResult::BudgetExhausted);
  ASSERT_EQ(adapter.product(tess::ChunkKey{0}).state,
            maintenance::ChunkProductState::Stale);

  ASSERT_EQ(adapter.run_some(maintenance::MaintenanceBudget{1}),
            maintenance::DrainResult::BudgetExhausted);
  EXPECT_EQ(adapter.product(tess::ChunkKey{1}).state,
            maintenance::ChunkProductState::Current);
  SynchronousDebtBackend::enable_synchronous_for_test();
  EXPECT_EQ(adapter.run_some(maintenance::MaintenanceBudget{0}),
            maintenance::DrainResult::BudgetExhausted);
  EXPECT_EQ(adapter.product(tess::ChunkKey{0}).state,
            maintenance::ChunkProductState::Stale);

  drain_to_idle(adapter);
  EXPECT_EQ(adapter.product(tess::ChunkKey{0}).state,
            maintenance::ChunkProductState::Current);
}

TEST(TessChunkMaintenance, SynchronousAcceptedCallCannotEraseNestedDebt) {
  DenseWorld world;
  auto after_rebuild = std::function<void()>{};
  auto rebuilder = SummaryRebuilder{};
  rebuilder.dense_intervening_world = &world;
  rebuilder.intervening_key = tess::ChunkKey{0};
  rebuilder.after_rebuild = &after_rebuild;
  Adapter<SynchronousDebtBackend, DenseWorld> adapter{world, kTerrain,
                                                      rebuilder, 1};
  auto inject_once = true;
  after_rebuild = [&] {
    if (!inject_once) {
      return;
    }
    inject_once = false;
    EXPECT_EQ(adapter.mark_dirty(tess::ChunkKey{1}, kTerrain, one_tile_box(1)),
              maintenance::ChunkMarkResult::Accepted);
  };
  SynchronousDebtBackend::enable_synchronous_for_test();

  ASSERT_EQ(adapter.mark_dirty(tess::ChunkKey{0}, kTerrain, one_tile_box(0)),
            maintenance::ChunkMarkResult::Accepted);
  EXPECT_EQ(adapter.product(tess::ChunkKey{0}).state,
            maintenance::ChunkProductState::Stale);
  EXPECT_GE(adapter.metrics().capacity_failures, 1u);

  EXPECT_EQ(adapter.run_some(maintenance::MaintenanceBudget{1}),
            maintenance::DrainResult::BudgetExhausted);
  EXPECT_EQ(adapter.product(tess::ChunkKey{1}).state,
            maintenance::ChunkProductState::Current);
  drain_to_idle(adapter);
  EXPECT_EQ(adapter.product(tess::ChunkKey{0}).state,
            maintenance::ChunkProductState::Current);
}

#if TESS_HAS_EXCEPTIONS
template <typename Backend>
void expect_exception_leaves_dirty_and_retryable() {
  DenseWorld world;
  auto throw_once = true;
  Adapter<Backend, DenseWorld> adapter{world, kOwned,
                                       SummaryRebuilder{&throw_once}};
  const auto key = tess::ChunkKey{0};
  world.field<ValueTag>(tess::Coord3{0, 0}) = 9;
  if constexpr (std::is_same_v<Backend, maintenance::ImmediateScheduler>) {
    EXPECT_THROW(
        static_cast<void>(adapter.mark_dirty(key, kTerrain, one_tile_box(0))),
        std::runtime_error);
  } else {
    ASSERT_EQ(adapter.mark_dirty(key, kTerrain, one_tile_box(0)),
              maintenance::ChunkMarkResult::Accepted);
    EXPECT_THROW(static_cast<void>(adapter.flush()), std::runtime_error);
  }
  EXPECT_FALSE((world.dirty_mask(key) & kTerrain).empty());
  EXPECT_EQ(adapter.product(key).state,
            maintenance::ChunkProductState::Unavailable);
  ASSERT_EQ(adapter.retry(key), maintenance::ScheduleResult::Accepted);
  drain_to_idle(adapter);
  EXPECT_EQ(adapter.product(key).state,
            maintenance::ChunkProductState::Current);
}

TEST(TessChunkMaintenance, ExceptionsLeaveEveryBackendDirtyAndRetryable) {
  expect_exception_leaves_dirty_and_retryable<
      maintenance::ImmediateScheduler>();
  expect_exception_leaves_dirty_and_retryable<maintenance::FifoScheduler>();
  expect_exception_leaves_dirty_and_retryable<
      maintenance::CoalescingScheduler>();
  expect_exception_leaves_dirty_and_retryable<maintenance::DirtyBitScheduler>();
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
  ASSERT_TRUE(first.handle.generation.valid());
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
  EXPECT_GT(reloaded.handle.generation.value, first.handle.generation.value);
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
  Adapter<Backend, DenseWorld> adapter{world, kOwned, SummaryRebuilder{}, 96};
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
    EXPECT_NE(view.value, nullptr);
    if (view.value == nullptr) {
      continue;
    }
    EXPECT_EQ(*view.value, rescan(world, key));
    EXPECT_EQ(view.token.key, key);
    EXPECT_EQ(view.token.version, world.meta(key).version);
    EXPECT_EQ(view.token.residency_generation, 0u);
    EXPECT_TRUE(adapter.current(view.token));
    EXPECT_EQ(world.dirty_flags(key) & kOwned, 0u);
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
    const auto chunk_key = tess::ChunkKey{key};
    const auto view = adapter.product(chunk_key);
    EXPECT_EQ(view.state, maintenance::ChunkProductState::Current);
    EXPECT_NE(view.value, nullptr);
    if (view.value == nullptr) {
      continue;
    }
    EXPECT_EQ(*view.value, rescan(world, chunk_key));
    EXPECT_EQ(view.token.key, chunk_key);
    EXPECT_EQ(view.token.version, world.meta(chunk_key).version);
    EXPECT_EQ(view.token.residency_generation, 0u);
    EXPECT_TRUE(adapter.current(view.token));
    EXPECT_EQ(world.dirty_flags(chunk_key) & kOwned, 0u);
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
    EXPECT_NE(view.value, nullptr);
    if (view.value == nullptr) {
      continue;
    }
    EXPECT_EQ(*view.value, rescan(world, key));
    EXPECT_EQ(view.token.key, key);
    EXPECT_EQ(view.token.version, world.meta(key).version);
    EXPECT_GT(view.token.residency_generation, 0u);
    EXPECT_TRUE(adapter.current(view.token));
    EXPECT_EQ(world.dirty_flags(key) & kOwned, 0u);
  }
  return bytes;
}

TEST(TessChunkMaintenance, BackendsMatchIndependentRescanAndArchiveOracle) {
  const auto immediate = run_dense_scenario<maintenance::ImmediateScheduler>();
  EXPECT_EQ(run_dense_scenario<maintenance::FifoScheduler>(), immediate);
  EXPECT_EQ(run_dense_scenario<maintenance::CoalescingScheduler>(), immediate);
  EXPECT_EQ(run_dense_scenario<maintenance::DirtyBitScheduler>(), immediate);

  const auto dense_archive =
      run_archive_scenario<maintenance::ImmediateScheduler>();
  EXPECT_EQ(run_archive_scenario<maintenance::FifoScheduler>(), dense_archive);
  EXPECT_EQ(run_archive_scenario<maintenance::CoalescingScheduler>(),
            dense_archive);
  EXPECT_EQ(run_archive_scenario<maintenance::DirtyBitScheduler>(),
            dense_archive);

  const auto sparse_archive =
      run_sparse_archive_scenario<maintenance::ImmediateScheduler>();
  EXPECT_EQ(run_sparse_archive_scenario<maintenance::FifoScheduler>(),
            sparse_archive);
  EXPECT_EQ(run_sparse_archive_scenario<maintenance::CoalescingScheduler>(),
            sparse_archive);
  EXPECT_EQ(run_sparse_archive_scenario<maintenance::DirtyBitScheduler>(),
            sparse_archive);
}

TEST(TessChunkMaintenance, ExplicitFlushIsDeterministicAcrossRuns) {
  const auto expected = run_dense_scenario<maintenance::DirtyBitScheduler>();
  for (int run = 0; run < 1'000; ++run) {
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

template <typename Backend>
void expect_warm_dense_scheduling_and_drain_do_not_allocate() {
  DenseWorld world;
  Adapter<Backend, DenseWorld> adapter{world, kOwned, SummaryRebuilder{},
                                       1'000};
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
    drain_to_idle(adapter);
    EXPECT_EQ(counter.count(), 0u);
  }
}

template <typename Backend>
void expect_warm_sparse_scheduling_and_drain_do_not_allocate() {
  SparseWorld world{tess::ResidencyConfig{SparseWorld::page_byte_size}};
  ASSERT_NE(world.ensure_resident(tess::ChunkKey{0}).generation, 0u);
  Adapter<Backend, SparseWorld> adapter{world, kOwned, SummaryRebuilder{},
                                        1'000};
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
    drain_to_idle(adapter);
    EXPECT_EQ(counter.count(), 0u);
  }
}

TEST(TessChunkMaintenance, WarmAdaptersDoNotAllocateAcrossBackendsOrStorage) {
  expect_warm_dense_scheduling_and_drain_do_not_allocate<
      maintenance::ImmediateScheduler>();
  expect_warm_dense_scheduling_and_drain_do_not_allocate<
      maintenance::FifoScheduler>();
  expect_warm_dense_scheduling_and_drain_do_not_allocate<
      maintenance::CoalescingScheduler>();
  expect_warm_dense_scheduling_and_drain_do_not_allocate<
      maintenance::DirtyBitScheduler>();
  expect_warm_sparse_scheduling_and_drain_do_not_allocate<
      maintenance::ImmediateScheduler>();
  expect_warm_sparse_scheduling_and_drain_do_not_allocate<
      maintenance::FifoScheduler>();
  expect_warm_sparse_scheduling_and_drain_do_not_allocate<
      maintenance::CoalescingScheduler>();
  expect_warm_sparse_scheduling_and_drain_do_not_allocate<
      maintenance::DirtyBitScheduler>();
}

TEST(TessChunkMaintenance, ConcurrentOffersAndDrainAreSafeWithoutMutation) {
  DenseWorld world;
  Adapter<maintenance::DirtyBitScheduler, DenseWorld> adapter{
      world, kOwned, SummaryRebuilder{}};
  ASSERT_EQ(adapter.mark_dirty(tess::ChunkKey{0}, kTerrain, one_tile_box(0)),
            maintenance::ChunkMarkResult::Accepted);
  std::atomic<bool> start = false;
  std::atomic<int> finished = 0;
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
      finished.fetch_add(1, std::memory_order_release);
    });
  }
  start.store(true, std::memory_order_release);
  while (finished.load(std::memory_order_acquire) !=
         static_cast<int>(producers.size())) {
    const auto result = adapter.run_some(maintenance::MaintenanceBudget{1});
    EXPECT_NE(result, maintenance::DrainResult::Stalled);
    std::this_thread::yield();
  }
  for (auto& producer : producers) {
    producer.join();
  }
  drain_to_idle(adapter);
  EXPECT_EQ(adapter.product(tess::ChunkKey{0}).state,
            maintenance::ChunkProductState::Current);
  EXPECT_EQ(adapter.metrics().schedule_calls, 4'001u);
  EXPECT_EQ(adapter.metrics().capacity_failures, 0u);
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
  EXPECT_FALSE((world.dirty_mask(tess::ChunkKey{0}) & kTerrain).empty());
}

}  // namespace
