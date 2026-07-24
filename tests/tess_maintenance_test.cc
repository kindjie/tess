#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <thread>

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
  }
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

}  // namespace
