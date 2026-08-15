#include <gtest/gtest.h>
#include <tess/experimental/maintenance.h>
#include <tess/persistence/archive.h>
#include <tess/storage/world.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

#include "allocation_counter.h"

namespace {

namespace maintenance = tess::experimental::maintenance;

struct CountingTask final : maintenance::MaintenanceTask {
  maintenance::MaintenanceScheduler* scheduler = nullptr;
  std::uint64_t remaining = 1;
  std::uint64_t executions = 0;
  std::uint64_t processed = 0;

  void run(maintenance::MaintenanceBudget& budget) override {
    ++executions;
    while (remaining != 0 && budget.consume()) {
      --remaining;
      ++processed;
    }
    if (remaining != 0) {
      (void)scheduler->schedule(*this);
    }
  }
};

struct DirtyTask final : maintenance::MaintenanceTask {
  std::uint32_t dirty = 0;
  std::uint32_t handled = 0;
  std::uint32_t clear_mask = 0;

  void run(maintenance::MaintenanceBudget& budget) override {
    if (!budget.consume()) {
      return;
    }
    const auto observed = dirty & clear_mask;
    handled |= observed;
    dirty &= ~observed;
  }
};

struct OverlapTask final : maintenance::MaintenanceTask {
  maintenance::MaintenanceScheduler* scheduler = nullptr;
  std::atomic<int> active = 0;
  std::atomic<int> maximum_active = 0;
  std::uint32_t remaining = 100;

  void run(maintenance::MaintenanceBudget& budget) override {
    if (!budget.consume()) {
      return;
    }
    const auto now = active.fetch_add(1, std::memory_order_acq_rel) + 1;
    auto maximum = maximum_active.load(std::memory_order_relaxed);
    while (now > maximum && !maximum_active.compare_exchange_weak(
                                maximum, now, std::memory_order_relaxed)) {
    }
    std::this_thread::yield();
    active.fetch_sub(1, std::memory_order_acq_rel);
    --remaining;
    if (remaining != 0) {
      (void)scheduler->schedule(*this);
    }
  }
};

struct ZeroProgressTask final : maintenance::MaintenanceTask {
  maintenance::MaintenanceScheduler* scheduler = nullptr;
  std::uint32_t executions = 0;

  void run(maintenance::MaintenanceBudget&) override {
    ++executions;
    static_cast<void>(scheduler->schedule(*this));
  }
};

struct ZeroProgressHandoffTask final : maintenance::MaintenanceTask {
  maintenance::MaintenanceScheduler* scheduler = nullptr;
  ZeroProgressHandoffTask* next = nullptr;
  std::uint32_t* handoffs_remaining = nullptr;
  std::uint32_t executions = 0;

  void run(maintenance::MaintenanceBudget& budget) override {
    ++executions;
    if (*handoffs_remaining != 0) {
      --*handoffs_remaining;
      static_cast<void>(scheduler->schedule(*next));
      return;
    }
    static_cast<void>(budget.consume());
  }
};

struct ImmediateSelfSchedulingTask final : maintenance::MaintenanceTask {
  maintenance::MaintenanceScheduler* scheduler = nullptr;
  std::uint32_t remaining = 3;
  std::uint32_t executions = 0;
  std::uint32_t active_depth = 0;
  std::uint32_t maximum_active_depth = 0;

  void run(maintenance::MaintenanceBudget& budget) override {
    if (!budget.consume()) {
      return;
    }
    ++executions;
    ++active_depth;
    maximum_active_depth = std::max(maximum_active_depth, active_depth);
    --remaining;
    if (remaining != 0) {
      (void)scheduler->schedule(*this);
    }
    --active_depth;
  }
};

struct DuplicateImmediateSelfScheduleTask final : maintenance::MaintenanceTask {
  maintenance::MaintenanceScheduler* scheduler = nullptr;
  std::uint32_t executions = 0;

  void run(maintenance::MaintenanceBudget& budget) override {
    if (!budget.consume()) {
      return;
    }
    ++executions;
    if (executions == 1) {
      (void)scheduler->schedule(*this);
      (void)scheduler->schedule(*this);
    }
  }
};

struct BlockingImmediateTask final : maintenance::MaintenanceTask {
  std::atomic<bool> first_entered = false;
  std::atomic<bool> release_first = false;
  std::atomic<std::uint32_t> executions = 0;

  void run(maintenance::MaintenanceBudget&) override {
    const auto execution = executions.fetch_add(1, std::memory_order_acq_rel);
    if (execution != 0) {
      return;
    }
    first_entered.store(true, std::memory_order_release);
    while (!release_first.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
  }
};

#if TESS_HAS_EXCEPTIONS
struct ThrowOnceTask final : maintenance::MaintenanceTask {
  std::uint32_t executions = 0;

  void run(maintenance::MaintenanceBudget& budget) override {
    ++executions;
    static_cast<void>(budget.consume());
    if (executions == 1) {
      throw std::runtime_error{"maintenance failure"};
    }
  }
};
#endif

struct MaintenanceFieldTag {};
using MaintenanceShape =
    tess::Shape<tess::Extent3{64, 64, 1}, tess::Extent3{16, 16, 1}>;
using MaintenanceSchema =
    tess::FieldSchema<tess::Field<MaintenanceFieldTag, std::uint8_t>>;
using MaintenanceWorld =
    tess::AlwaysResidentWorld<MaintenanceShape, MaintenanceSchema>;
using MaintenanceArchive = tess::PersistenceSchema<
    0x6d61696e74656e61ULL, 1,
    tess::PersistedField<MaintenanceFieldTag, 0x6465726976656400ULL, 1>>;

constexpr auto kDerivedDirty = std::uint32_t{1u << 3u};

struct WorldDirtyTask final : maintenance::MaintenanceTask {
  MaintenanceWorld* world = nullptr;
  maintenance::MaintenanceScheduler* scheduler = nullptr;
  tess::ChunkKey key{};
  std::uint32_t derived_version = 0;
  bool inject_intervening_mark = false;

  void run(maintenance::MaintenanceBudget& budget) override {
    if (!budget.consume()) {
      return;
    }
    const auto observed = world->observe_dirty(key, kDerivedDirty);
    if (observed.flags == 0) {
      return;
    }
    derived_version = observed.version;
    if (inject_intervening_mark) {
      inject_intervening_mark = false;
      world->mark_dirty(key, kDerivedDirty, observed.bounds);
    }
    if (!world->clear_dirty_observed(key, observed)) {
      static_cast<void>(scheduler->schedule(*this));
    }
  }
};

template <typename Scheduler>
void register_maintenance_task(Scheduler& scheduler,
                               maintenance::MaintenanceTask& task) {
  if constexpr (std::is_same_v<Scheduler, maintenance::DirtyBitScheduler>) {
    ASSERT_TRUE(scheduler.register_task(task));
  }
}

template <typename Scheduler>
void seal_maintenance_scheduler(Scheduler& scheduler) {
  if constexpr (std::is_same_v<Scheduler, maintenance::DirtyBitScheduler>) {
    scheduler.seal();
  }
}

template <typename Scheduler>
auto world_maintenance_hash() -> std::uint64_t {
  Scheduler scheduler(128);
  MaintenanceWorld world;
  std::array<WorldDirtyTask, 8> tasks;
  for (std::size_t index = 0; index < tasks.size(); ++index) {
    tasks[index].world = &world;
    tasks[index].scheduler = &scheduler;
    tasks[index].key = tess::ChunkKey{index};
    register_maintenance_task(scheduler, tasks[index]);
  }
  seal_maintenance_scheduler(scheduler);

  for (std::uint64_t mutation = 0; mutation < 128; ++mutation) {
    const auto index =
        static_cast<std::size_t>((mutation * 17u) % tasks.size());
    const auto origin = tess::Coord3{static_cast<std::int64_t>(index * 2u),
                                     static_cast<std::int64_t>(index), 0};
    world.mark_dirty(tasks[index].key, kDerivedDirty,
                     tess::Box3{origin, tess::Extent3{1, 1, 1}});
    EXPECT_TRUE(scheduler.schedule(tasks[index]));
  }
  EXPECT_TRUE(scheduler.flush());

  auto hash = std::uint64_t{1469598103934665603ull};
  for (const auto& task : tasks) {
    EXPECT_EQ(world.dirty_flags(task.key) & kDerivedDirty, 0u);
    EXPECT_EQ(task.derived_version, world.meta(task.key).version);
    hash ^= task.derived_version;
    hash *= 1099511628211ull;
  }
  return hash;
}

template <typename Scheduler>
auto world_maintenance_archive() -> std::vector<std::byte> {
  Scheduler scheduler(128);
  MaintenanceWorld world;
  std::array<WorldDirtyTask, 8> tasks;
  for (std::size_t index = 0; index < tasks.size(); ++index) {
    tasks[index].world = &world;
    tasks[index].scheduler = &scheduler;
    tasks[index].key = tess::ChunkKey{index};
    register_maintenance_task(scheduler, tasks[index]);
  }
  seal_maintenance_scheduler(scheduler);

  for (std::uint64_t mutation = 0; mutation < 128; ++mutation) {
    const auto index =
        static_cast<std::size_t>((mutation * 17u) % tasks.size());
    const auto coord =
        tess::Coord3{static_cast<std::int64_t>(mutation % 64u),
                     static_cast<std::int64_t>((mutation * 7u) % 64u), 0};
    world.field<MaintenanceFieldTag>(coord) =
        static_cast<std::uint8_t>(mutation);
    world.mark_dirty(tasks[index].key, kDerivedDirty,
                     tess::Box3{coord, tess::Extent3{1, 1, 1}});
    EXPECT_TRUE(scheduler.schedule(tasks[index]));
  }
  EXPECT_TRUE(scheduler.flush());

  std::vector<std::byte> bytes;
  EXPECT_GT(
      tess::save_world_archive<MaintenanceArchive>(world, bytes).bytes_written,
      0u);
  return bytes;
}

template <typename Scheduler>
void check_intervening_world_mark() {
  Scheduler scheduler(2);
  MaintenanceWorld world;
  WorldDirtyTask task;
  task.world = &world;
  task.scheduler = &scheduler;
  task.key = tess::ChunkKey{3};
  task.inject_intervening_mark = true;
  register_maintenance_task(scheduler, task);
  seal_maintenance_scheduler(scheduler);
  const auto bounds = tess::Box3{tess::Coord3{4, 4, 0}, tess::Extent3{1, 1, 1}};
  world.mark_dirty(task.key, kDerivedDirty, bounds);

  ASSERT_TRUE(scheduler.schedule(task));
  EXPECT_TRUE(scheduler.flush());
  EXPECT_EQ(world.dirty_flags(task.key) & kDerivedDirty, 0u);
  EXPECT_EQ(task.derived_version, world.meta(task.key).version);
  EXPECT_EQ(task.derived_version, 2u);
}

TEST(TessMaintenance, ImmediateSelfScheduleUsesConstantStackDepth) {
  maintenance::ImmediateScheduler scheduler;
  ImmediateSelfSchedulingTask task;
  task.scheduler = &scheduler;

  EXPECT_TRUE(scheduler.schedule(task));
  EXPECT_EQ(task.executions, 3u);
  EXPECT_EQ(task.maximum_active_depth, 1u);
}

TEST(TessMaintenance, ImmediatePreservesDuplicateSelfScheduleRequests) {
  maintenance::ImmediateScheduler scheduler;
  DuplicateImmediateSelfScheduleTask task;
  task.scheduler = &scheduler;

  EXPECT_TRUE(scheduler.schedule(task));
  EXPECT_EQ(task.executions, 3u);
}

TEST(TessMaintenance, ImmediateStopsZeroProgressSelfSchedule) {
  maintenance::ImmediateScheduler scheduler;
  ZeroProgressTask task;
  task.scheduler = &scheduler;

  EXPECT_FALSE(scheduler.schedule(task));
  EXPECT_EQ(task.executions, 1u);
}

TEST(TessMaintenance, ImmediateSelfScheduleTrampolineDoesNotAllocate) {
  maintenance::ImmediateScheduler scheduler;
  ImmediateSelfSchedulingTask task;
  task.scheduler = &scheduler;

  {
    tess_test::ScopedAllocationCounter counter;
    for (int run = 0; run < 100; ++run) {
      task.remaining = 3;
      EXPECT_TRUE(scheduler.schedule(task));
    }
    EXPECT_EQ(counter.count(), 0u);
  }
  EXPECT_EQ(task.executions, 300u);
}

TEST(TessMaintenance, ConcurrentImmediateScheduleReturnsAfterItsExecution) {
  maintenance::ImmediateScheduler scheduler;
  BlockingImmediateTask task;
  std::atomic<bool> first_result = false;
  std::atomic<bool> second_started = false;
  std::atomic<bool> second_returned = false;
  std::atomic<bool> second_result = false;

  std::thread first([&] {
    first_result.store(scheduler.schedule(task), std::memory_order_release);
  });
  while (!task.first_entered.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  std::thread second([&] {
    second_started.store(true, std::memory_order_release);
    second_result.store(scheduler.schedule(task), std::memory_order_release);
    second_returned.store(true, std::memory_order_release);
  });
  while (!second_started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  for (int attempt = 0; attempt < 10'000; ++attempt) {
    if (second_returned.load(std::memory_order_acquire)) {
      break;
    }
    std::this_thread::yield();
  }
  EXPECT_FALSE(second_returned.load(std::memory_order_acquire));

  task.release_first.store(true, std::memory_order_release);
  first.join();
  second.join();
  EXPECT_TRUE(first_result.load(std::memory_order_acquire));
  EXPECT_TRUE(second_result.load(std::memory_order_acquire));
  EXPECT_EQ(task.executions.load(std::memory_order_acquire), 2u);
}

TEST(TessMaintenance, ConcurrentProducerDoesNotLookLikeTaskFollowUp) {
  maintenance::CoalescingScheduler scheduler(2);
  BlockingImmediateTask blocking;
  DirtyTask follow_up;
  follow_up.dirty = 1;
  follow_up.clear_mask = 1;
  ASSERT_TRUE(scheduler.schedule(blocking));
  std::atomic<bool> drain_result = false;

  std::thread drain([&] {
    drain_result.store(scheduler.run_some(maintenance::MaintenanceBudget{1}),
                       std::memory_order_release);
  });
  while (!blocking.first_entered.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  EXPECT_TRUE(scheduler.schedule(follow_up));
  blocking.release_first.store(true, std::memory_order_release);
  drain.join();

  EXPECT_TRUE(drain_result.load(std::memory_order_acquire));
  EXPECT_EQ(follow_up.handled, 1u);
}

TEST(TessMaintenance, CoalescingCollapsesDuplicateSchedules) {
  maintenance::CoalescingScheduler scheduler(4);
  CountingTask task;
  task.scheduler = &scheduler;

  for (int i = 0; i < 1'000; ++i) {
    EXPECT_TRUE(scheduler.schedule(task));
  }
  EXPECT_TRUE(scheduler.flush());

  EXPECT_EQ(task.executions, 1u);
  EXPECT_EQ(task.processed, 1u);
  EXPECT_EQ(scheduler.metrics().schedule_calls, 1'000u);
  EXPECT_EQ(scheduler.metrics().coalesced_calls, 999u);
}

TEST(TessMaintenance, CoalescingSurvivesRepeatedFullCapacityChurn) {
  constexpr auto task_count = std::size_t{8};
  maintenance::CoalescingScheduler scheduler(task_count);
  std::array<CountingTask, task_count> tasks;
  for (auto& task : tasks) {
    task.scheduler = &scheduler;
  }

  for (int cycle = 0; cycle < 1'000; ++cycle) {
    for (auto& task : tasks) {
      task.remaining = 1;
      ASSERT_TRUE(scheduler.schedule(task));
      EXPECT_TRUE(scheduler.schedule(task));
    }
    ASSERT_TRUE(scheduler.flush());
  }

  for (const auto& task : tasks) {
    EXPECT_EQ(task.executions, 1'000u);
  }
  EXPECT_EQ(scheduler.metrics().coalesced_calls, 8'000u);
  EXPECT_EQ(scheduler.metrics().capacity_failures, 0u);
}

TEST(TessMaintenance, CoalescingTracksWrappedPendingEntries) {
  maintenance::CoalescingScheduler scheduler(3);
  std::array<CountingTask, 4> tasks;
  for (auto& task : tasks) {
    task.scheduler = &scheduler;
  }

  ASSERT_TRUE(scheduler.schedule(tasks[0]));
  ASSERT_TRUE(scheduler.schedule(tasks[1]));
  ASSERT_TRUE(scheduler.schedule(tasks[2]));
  ASSERT_TRUE(scheduler.run_some(maintenance::MaintenanceBudget{1}));
  ASSERT_TRUE(scheduler.schedule(tasks[3]));
  EXPECT_TRUE(scheduler.schedule(tasks[1]));
  EXPECT_TRUE(scheduler.schedule(tasks[3]));
  ASSERT_TRUE(scheduler.flush());

  for (const auto& task : tasks) {
    EXPECT_EQ(task.executions, 1u);
  }
  EXPECT_EQ(scheduler.metrics().coalesced_calls, 2u);
  EXPECT_EQ(scheduler.metrics().capacity_failures, 0u);
}

TEST(TessMaintenance, FifoPreservesEveryScheduleForBaselineComparison) {
  maintenance::FifoScheduler scheduler(8);
  CountingTask task;
  task.scheduler = &scheduler;

  for (int i = 0; i < 8; ++i) {
    EXPECT_TRUE(scheduler.schedule(task));
  }
  EXPECT_TRUE(scheduler.flush());
  EXPECT_EQ(task.executions, 8u);
}

TEST(TessMaintenance, BudgetedTaskSelfSchedulesUntilComplete) {
  maintenance::CoalescingScheduler scheduler(4);
  CountingTask task;
  task.scheduler = &scheduler;
  task.remaining = 10;

  EXPECT_TRUE(scheduler.schedule(task));
  while (task.remaining != 0) {
    EXPECT_TRUE(scheduler.run_some(maintenance::MaintenanceBudget{3}));
  }

  EXPECT_EQ(task.executions, 4u);
  EXPECT_EQ(task.processed, 10u);
  EXPECT_TRUE(scheduler.flush());
}

TEST(TessMaintenance, ZeroProgressRescheduleStopsDrain) {
  maintenance::CoalescingScheduler scheduler(2);
  ZeroProgressTask task;
  task.scheduler = &scheduler;
  EXPECT_TRUE(scheduler.schedule(task));

  EXPECT_FALSE(scheduler.run_some(maintenance::MaintenanceBudget{3}));
  EXPECT_EQ(task.executions, 1u);
  EXPECT_FALSE(scheduler.flush());
  EXPECT_EQ(task.executions, 2u);
}

template <typename Scheduler>
void check_cross_task_zero_progress_handoff() {
  Scheduler scheduler(2);
  auto handoffs_remaining = std::uint32_t{4};
  ZeroProgressHandoffTask first;
  ZeroProgressHandoffTask second;
  first.scheduler = &scheduler;
  first.next = &second;
  first.handoffs_remaining = &handoffs_remaining;
  second.scheduler = &scheduler;
  second.next = &first;
  second.handoffs_remaining = &handoffs_remaining;
  register_maintenance_task(scheduler, first);
  register_maintenance_task(scheduler, second);
  seal_maintenance_scheduler(scheduler);
  ASSERT_TRUE(scheduler.schedule(first));

  EXPECT_FALSE(scheduler.run_some(maintenance::MaintenanceBudget{1}));
  EXPECT_EQ(first.executions, 1u);
  EXPECT_EQ(second.executions, 0u);
  EXPECT_FALSE(scheduler.flush());
  EXPECT_FALSE(scheduler.flush());
  EXPECT_FALSE(scheduler.flush());
  EXPECT_TRUE(scheduler.flush());
  EXPECT_EQ(first.executions, 3u);
  EXPECT_EQ(second.executions, 2u);
}

TEST(TessMaintenance, CoalescingCrossTaskZeroProgressStopsEachDrain) {
  check_cross_task_zero_progress_handoff<maintenance::CoalescingScheduler>();
}

TEST(TessMaintenance, FifoCrossTaskZeroProgressStopsEachDrain) {
  check_cross_task_zero_progress_handoff<maintenance::FifoScheduler>();
}

TEST(TessMaintenance, DirtyBitCrossTaskZeroProgressStopsEachDrain) {
  check_cross_task_zero_progress_handoff<maintenance::DirtyBitScheduler>();
}

TEST(TessMaintenance, PartialClearPreservesUnrelatedDirtyFlags) {
  maintenance::CoalescingScheduler scheduler(2);
  DirtyTask task;
  task.dirty = 0b1111u;
  task.clear_mask = 0b0101u;

  EXPECT_TRUE(scheduler.schedule(task));
  EXPECT_TRUE(scheduler.flush());
  EXPECT_EQ(task.handled, 0b0101u);
  EXPECT_EQ(task.dirty, 0b1010u);
}

TEST(TessMaintenance, ConcurrentSchedulesNeverOverlapTaskExecution) {
  maintenance::CoalescingScheduler scheduler(8);
  CountingTask task;
  task.scheduler = &scheduler;
  std::atomic<bool> start = false;
  std::array<std::thread, 8> workers;
  for (auto& worker : workers) {
    worker = std::thread([&] {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      for (int i = 0; i < 10'000; ++i) {
        EXPECT_TRUE(scheduler.schedule(task));
      }
    });
  }
  start.store(true, std::memory_order_release);
  for (auto& worker : workers) {
    worker.join();
  }
  EXPECT_TRUE(scheduler.flush());

  EXPECT_EQ(task.executions, 1u);
  EXPECT_EQ(scheduler.metrics().schedule_calls, 80'000u);
}

TEST(TessMaintenance, ConcurrentDrainsNeverRunATaskAgainstItself) {
  maintenance::CoalescingScheduler scheduler(8);
  OverlapTask task;
  task.scheduler = &scheduler;
  EXPECT_TRUE(scheduler.schedule(task));

  std::array<std::thread, 4> workers;
  for (auto& worker : workers) {
    worker = std::thread([&] {
      EXPECT_TRUE(scheduler.run_some(maintenance::MaintenanceBudget{}));
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }

  EXPECT_EQ(task.remaining, 0u);
  EXPECT_EQ(task.maximum_active.load(), 1);
}

template <typename Scheduler>
auto deterministic_hash() -> std::uint64_t {
  Scheduler scheduler(128);
  std::array<CountingTask, 8> tasks;
  for (std::size_t i = 0; i < tasks.size(); ++i) {
    tasks[i].scheduler = &scheduler;
    tasks[i].remaining = i + 1;
    register_maintenance_task(scheduler, tasks[i]);
  }
  seal_maintenance_scheduler(scheduler);
  for (std::uint64_t mutation = 0; mutation < 128; ++mutation) {
    EXPECT_TRUE(scheduler.schedule(tasks[(mutation * 17u) % tasks.size()]));
  }
  EXPECT_TRUE(scheduler.flush());
  auto hash = std::uint64_t{1469598103934665603ull};
  for (const auto& task : tasks) {
    hash ^= task.processed;
    hash *= 1099511628211ull;
  }
  return hash;
}

TEST(TessMaintenance, ExplicitFlushIsDeterministicAcrossBackendsAndRuns) {
  const auto expected = deterministic_hash<maintenance::ImmediateScheduler>();
  for (int run = 0; run < 1'000; ++run) {
    EXPECT_EQ(deterministic_hash<maintenance::ImmediateScheduler>(), expected);
    EXPECT_EQ(deterministic_hash<maintenance::FifoScheduler>(), expected);
    EXPECT_EQ(deterministic_hash<maintenance::CoalescingScheduler>(), expected);
    EXPECT_EQ(deterministic_hash<maintenance::DirtyBitScheduler>(), expected);
  }
}

TEST(TessMaintenance, CapacityFailureDoesNotLoseQueuedWork) {
  maintenance::FifoScheduler scheduler(1);
  CountingTask first;
  CountingTask second;
  first.scheduler = &scheduler;
  second.scheduler = &scheduler;

  EXPECT_TRUE(scheduler.schedule(first));
  EXPECT_FALSE(scheduler.schedule(second));
  EXPECT_EQ(scheduler.metrics().capacity_failures, 1u);
  EXPECT_TRUE(scheduler.flush());
  EXPECT_EQ(first.executions, 1u);
  EXPECT_EQ(second.executions, 0u);
}

TEST(TessMaintenance, PendingWorkIsReleasedWithoutExecutionAtShutdown) {
  CountingTask task;
  {
    maintenance::CoalescingScheduler scheduler(2);
    task.scheduler = &scheduler;
    EXPECT_TRUE(scheduler.schedule(task));
  }
  EXPECT_EQ(task.executions, 0u);

  maintenance::CoalescingScheduler next(2);
  task.scheduler = &next;
  EXPECT_TRUE(next.schedule(task));
  EXPECT_TRUE(next.flush());
  EXPECT_EQ(task.executions, 1u);
}

TEST(TessMaintenance, CoalescingScheduleDoesNotAllocateAfterConstruction) {
  maintenance::CoalescingScheduler scheduler(4);
  CountingTask task;
  task.scheduler = &scheduler;

  {
    tess_test::ScopedAllocationCounter counter;
    for (int i = 0; i < 1'000; ++i) {
      EXPECT_TRUE(scheduler.schedule(task));
    }
    EXPECT_EQ(counter.count(), 0u);
  }
  EXPECT_TRUE(scheduler.flush());
}

TEST(TessMaintenance, DirtyBitRequiresRegistrationAndHonorsCapacity) {
  maintenance::DirtyBitScheduler scheduler(1);
  CountingTask first;
  CountingTask second;
  first.scheduler = &scheduler;
  second.scheduler = &scheduler;

  EXPECT_FALSE(scheduler.schedule(first));
  EXPECT_TRUE(scheduler.register_task(first));
  EXPECT_TRUE(scheduler.register_task(first));
  EXPECT_FALSE(scheduler.register_task(second));
  scheduler.seal();
  EXPECT_FALSE(scheduler.register_task(first));
  EXPECT_FALSE(scheduler.schedule(second));
  EXPECT_TRUE(scheduler.schedule(first));
  EXPECT_TRUE(scheduler.flush());
  EXPECT_EQ(first.executions, 1u);
  EXPECT_EQ(second.executions, 0u);
}

TEST(TessMaintenance, DirtyBitCollapsesDuplicateSchedules) {
  maintenance::DirtyBitScheduler scheduler(1);
  CountingTask task;
  task.scheduler = &scheduler;
  ASSERT_TRUE(scheduler.register_task(task));
  scheduler.seal();

  for (int call = 0; call < 1'000; ++call) {
    EXPECT_TRUE(scheduler.schedule(task));
  }
  EXPECT_TRUE(scheduler.flush());

  EXPECT_EQ(task.executions, 1u);
  EXPECT_EQ(scheduler.metrics().schedule_calls, 1'000u);
  EXPECT_EQ(scheduler.metrics().coalesced_calls, 999u);
}

TEST(TessMaintenance, DirtyBitBudgetedTaskSelfSchedulesUntilComplete) {
  maintenance::DirtyBitScheduler scheduler(1);
  CountingTask task;
  task.scheduler = &scheduler;
  task.remaining = 10;
  ASSERT_TRUE(scheduler.register_task(task));
  scheduler.seal();
  ASSERT_TRUE(scheduler.schedule(task));

  while (task.remaining != 0) {
    EXPECT_TRUE(scheduler.run_some(maintenance::MaintenanceBudget{3}));
  }

  EXPECT_EQ(task.executions, 4u);
  EXPECT_EQ(task.processed, 10u);
  EXPECT_TRUE(scheduler.flush());
}

TEST(TessMaintenance, DirtyBitStopsZeroProgressReschedule) {
  maintenance::DirtyBitScheduler scheduler(1);
  ZeroProgressTask task;
  task.scheduler = &scheduler;
  ASSERT_TRUE(scheduler.register_task(task));
  scheduler.seal();
  ASSERT_TRUE(scheduler.schedule(task));

  EXPECT_FALSE(scheduler.run_some(maintenance::MaintenanceBudget{3}));
  EXPECT_EQ(task.executions, 1u);
  EXPECT_FALSE(scheduler.flush());
  EXPECT_EQ(task.executions, 2u);
}

TEST(TessMaintenance, DirtyBitConcurrentSchedulesCollapse) {
  maintenance::DirtyBitScheduler scheduler(1);
  CountingTask task;
  task.scheduler = &scheduler;
  ASSERT_TRUE(scheduler.register_task(task));
  scheduler.seal();
  std::atomic<bool> start = false;
  std::array<std::thread, 8> workers;
  for (auto& worker : workers) {
    worker = std::thread([&] {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      for (int call = 0; call < 10'000; ++call) {
        EXPECT_TRUE(scheduler.schedule(task));
      }
    });
  }
  start.store(true, std::memory_order_release);
  for (auto& worker : workers) {
    worker.join();
  }
  EXPECT_TRUE(scheduler.flush());

  EXPECT_EQ(task.executions, 1u);
  EXPECT_EQ(scheduler.metrics().schedule_calls, 80'000u);
  EXPECT_EQ(scheduler.metrics().coalesced_calls, 79'999u);
}

TEST(TessMaintenance, DirtyBitSealPublishesOnlyCompletedRegistrations) {
  maintenance::DirtyBitScheduler scheduler(16);
  std::array<CountingTask, 16> tasks;
  std::array<std::atomic<bool>, 16> registered{};
  std::atomic<bool> start = false;
  std::array<std::thread, 16> workers;
  for (std::size_t index = 0; index < workers.size(); ++index) {
    tasks[index].scheduler = &scheduler;
    workers[index] = std::thread([&, index] {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      registered[index].store(scheduler.register_task(tasks[index]),
                              std::memory_order_release);
    });
  }
  std::thread sealer([&] {
    while (!start.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    scheduler.seal();
  });

  start.store(true, std::memory_order_release);
  for (auto& worker : workers) {
    worker.join();
  }
  sealer.join();

  for (std::size_t index = 0; index < tasks.size(); ++index) {
    const auto accepted = registered[index].load(std::memory_order_acquire);
    EXPECT_EQ(scheduler.schedule(tasks[index]), accepted);
  }
  EXPECT_TRUE(scheduler.flush());
  for (std::size_t index = 0; index < tasks.size(); ++index) {
    const auto expected = registered[index].load(std::memory_order_acquire);
    EXPECT_EQ(tasks[index].executions, expected ? 1u : 0u);
  }
}

TEST(TessMaintenance, DirtyBitConcurrentProducerDuringRunIsPreserved) {
  maintenance::DirtyBitScheduler scheduler(2);
  BlockingImmediateTask blocking;
  DirtyTask follow_up;
  follow_up.dirty = 1;
  follow_up.clear_mask = 1;
  ASSERT_TRUE(scheduler.register_task(blocking));
  ASSERT_TRUE(scheduler.register_task(follow_up));
  scheduler.seal();
  ASSERT_TRUE(scheduler.schedule(blocking));
  std::atomic<bool> drain_result = false;

  std::thread drain([&] {
    drain_result.store(scheduler.run_some(maintenance::MaintenanceBudget{1}),
                       std::memory_order_release);
  });
  while (!blocking.first_entered.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  EXPECT_TRUE(scheduler.schedule(follow_up));
  blocking.release_first.store(true, std::memory_order_release);
  drain.join();

  EXPECT_TRUE(drain_result.load(std::memory_order_acquire));
  EXPECT_EQ(follow_up.handled, 1u);
}

TEST(TessMaintenance, DirtyBitConcurrentDrainsNeverOverlapTaskExecution) {
  maintenance::DirtyBitScheduler scheduler(1);
  OverlapTask task;
  task.scheduler = &scheduler;
  ASSERT_TRUE(scheduler.register_task(task));
  scheduler.seal();
  ASSERT_TRUE(scheduler.schedule(task));

  std::array<std::thread, 4> workers;
  for (auto& worker : workers) {
    worker = std::thread([&] {
      EXPECT_TRUE(scheduler.run_some(maintenance::MaintenanceBudget{}));
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }

  EXPECT_EQ(task.remaining, 0u);
  EXPECT_EQ(task.maximum_active.load(), 1);
}

TEST(TessMaintenance, DirtyBitWarmScheduleDoesNotAllocate) {
  maintenance::DirtyBitScheduler scheduler(1);
  CountingTask task;
  task.scheduler = &scheduler;
  ASSERT_TRUE(scheduler.register_task(task));
  scheduler.seal();
  ASSERT_TRUE(scheduler.schedule(task));
  ASSERT_TRUE(scheduler.flush());

  {
    tess_test::ScopedAllocationCounter counter;
    for (int call = 0; call < 1'000; ++call) {
      EXPECT_TRUE(scheduler.schedule(task));
    }
    EXPECT_EQ(counter.count(), 0u);
  }
  EXPECT_TRUE(scheduler.flush());
  EXPECT_EQ(task.executions, 2u);
  EXPECT_EQ(scheduler.metrics().schedule_calls, 1'001u);
  EXPECT_EQ(scheduler.metrics().coalesced_calls, 999u);
}

TEST(TessMaintenance, DirtyBitPendingWorkIsReleasedAtShutdown) {
  CountingTask task;
  {
    maintenance::DirtyBitScheduler scheduler(1);
    task.scheduler = &scheduler;
    ASSERT_TRUE(scheduler.register_task(task));
    scheduler.seal();
    ASSERT_TRUE(scheduler.schedule(task));
  }
  EXPECT_EQ(task.executions, 0u);
}

#if TESS_HAS_EXCEPTIONS
TEST(TessMaintenance, DirtyBitExceptionAllowsCallerControlledRetry) {
  maintenance::DirtyBitScheduler scheduler(1);
  ThrowOnceTask task;
  ASSERT_TRUE(scheduler.register_task(task));
  scheduler.seal();
  ASSERT_TRUE(scheduler.schedule(task));

  EXPECT_THROW(static_cast<void>(scheduler.flush()), std::runtime_error);
  ASSERT_TRUE(scheduler.schedule(task));
  EXPECT_TRUE(scheduler.flush());
  EXPECT_EQ(task.executions, 2u);
}

TEST(TessMaintenance, DirtyBitExceptionPreservesLaterClaimedTasks) {
  maintenance::DirtyBitScheduler scheduler(2);
  ThrowOnceTask throwing;
  CountingTask later;
  later.scheduler = &scheduler;
  ASSERT_TRUE(scheduler.register_task(throwing));
  ASSERT_TRUE(scheduler.register_task(later));
  scheduler.seal();
  ASSERT_TRUE(scheduler.schedule(throwing));
  ASSERT_TRUE(scheduler.schedule(later));

  EXPECT_THROW(static_cast<void>(scheduler.flush()), std::runtime_error);
  EXPECT_EQ(later.executions, 0u);
  EXPECT_TRUE(scheduler.flush());
  EXPECT_EQ(later.executions, 1u);
}
#endif

TEST(TessMaintenance, WorldDerivedStateMatchesAcrossBackends) {
  const auto expected =
      world_maintenance_hash<maintenance::ImmediateScheduler>();
  EXPECT_EQ(world_maintenance_hash<maintenance::FifoScheduler>(), expected);
  EXPECT_EQ(world_maintenance_hash<maintenance::CoalescingScheduler>(),
            expected);
  EXPECT_EQ(world_maintenance_hash<maintenance::DirtyBitScheduler>(), expected);
}

TEST(TessMaintenance, FlushedWorldArchiveMatchesAcrossBackends) {
  const auto expected =
      world_maintenance_archive<maintenance::ImmediateScheduler>();
  EXPECT_EQ(world_maintenance_archive<maintenance::FifoScheduler>(), expected);
  EXPECT_EQ(world_maintenance_archive<maintenance::CoalescingScheduler>(),
            expected);
  const auto dirty_bit =
      world_maintenance_archive<maintenance::DirtyBitScheduler>();
  EXPECT_EQ(dirty_bit, expected);

  MaintenanceWorld restored;
  EXPECT_EQ(tess::load_world_archive<MaintenanceArchive>(restored, dirty_bit, 0)
                .status,
            tess::WorldArchiveStatus::Ok);
  EXPECT_EQ(restored.field<MaintenanceFieldTag>({63, 57, 0}), 127u);
}

TEST(TessMaintenance, ImmediatePreservesInterveningWorldMark) {
  check_intervening_world_mark<maintenance::ImmediateScheduler>();
}

TEST(TessMaintenance, FifoPreservesInterveningWorldMark) {
  check_intervening_world_mark<maintenance::FifoScheduler>();
}

TEST(TessMaintenance, CoalescingPreservesInterveningWorldMark) {
  check_intervening_world_mark<maintenance::CoalescingScheduler>();
}

TEST(TessMaintenance, DirtyBitPreservesInterveningWorldMark) {
  check_intervening_world_mark<maintenance::DirtyBitScheduler>();
}

}  // namespace
