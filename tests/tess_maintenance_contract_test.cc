#include <gtest/gtest.h>
#include <tess/experimental/registered_maintenance.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

#include "allocation_counter.h"

namespace {

namespace maintenance = tess::experimental::maintenance;

template <typename Backend>
using Registered = maintenance::RegisteredScheduler<Backend>;

[[nodiscard]] auto require_handle(
    const std::optional<maintenance::MaintenanceHandle>& handle)
    -> maintenance::MaintenanceHandle {
  if (!handle.has_value()) {
    ADD_FAILURE() << "expected maintenance registration to succeed";
    return {};
  }
  return *handle;
}

struct CountingTask final : maintenance::MaintenanceTask {
  std::uint64_t remaining = 1;
  std::uint64_t executions = 0;

  void run(maintenance::MaintenanceBudget& budget) override {
    ++executions;
    while (remaining != 0 && budget.consume()) {
      --remaining;
    }
  }
};

struct BlockingTask final : maintenance::MaintenanceTask {
  std::atomic<bool> entered = false;
  std::atomic<bool> release = false;

  void run(maintenance::MaintenanceBudget& budget) override {
    static_cast<void>(budget.consume());
    entered.store(true, std::memory_order_release);
    while (!release.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
  }
};

struct ConcurrentTask final : maintenance::MaintenanceTask {
  std::atomic<std::uint64_t> executions = 0;
  std::atomic<std::uint64_t> active = 0;
  std::atomic<bool> overlapped = false;

  void run(maintenance::MaintenanceBudget& budget) override {
    if (active.fetch_add(1, std::memory_order_acq_rel) != 0) {
      overlapped.store(true, std::memory_order_release);
    }
    static_cast<void>(budget.consume());
    std::this_thread::yield();
    executions.fetch_add(1, std::memory_order_relaxed);
    active.fetch_sub(1, std::memory_order_acq_rel);
  }
};

class StructuralBackend {
 public:
  explicit StructuralBackend(std::size_t capacity) : queue_(capacity) {}

  [[nodiscard]] auto schedule(maintenance::MaintenanceTask& task)
      -> maintenance::ScheduleResult {
    schedule_calls_.fetch_add(1, std::memory_order_relaxed);
    const auto lock = std::scoped_lock{queue_mutex_};
    if (size_ == queue_.size()) {
      capacity_failures_.fetch_add(1, std::memory_order_relaxed);
      return maintenance::ScheduleResult::CapacityExhausted;
    }
    const auto tail = (head_ + size_) % queue_.size();
    queue_[tail] = &task;
    ++size_;
    return maintenance::ScheduleResult::Accepted;
  }

  [[nodiscard]] auto run_some(maintenance::MaintenanceBudget budget)
      -> maintenance::BackendDrainResult {
    while (budget.remaining() != 0) {
      auto* task = pop();
      if (task == nullptr) {
        break;
      }
      executions_.fetch_add(1, std::memory_order_relaxed);
      task->run(budget);
    }
    return maintenance::BackendDrainResult::Completed;
  }

  [[nodiscard]] auto flush() -> maintenance::BackendDrainResult {
    return run_some(maintenance::MaintenanceBudget{});
  }

  [[nodiscard]] auto metrics() const noexcept
      -> maintenance::MaintenanceMetrics {
    return maintenance::MaintenanceMetrics{
        schedule_calls_.load(std::memory_order_relaxed), 0,
        executions_.load(std::memory_order_relaxed),
        capacity_failures_.load(std::memory_order_relaxed)};
  }

  [[nodiscard]] auto has_pending() const noexcept -> bool {
    const auto lock = std::scoped_lock{queue_mutex_};
    return size_ != 0;
  }

 private:
  [[nodiscard]] auto pop() noexcept -> maintenance::MaintenanceTask* {
    const auto lock = std::scoped_lock{queue_mutex_};
    if (size_ == 0) {
      return nullptr;
    }
    auto* task = queue_[head_];
    queue_[head_] = nullptr;
    head_ = (head_ + 1) % queue_.size();
    --size_;
    return task;
  }

  std::vector<maintenance::MaintenanceTask*> queue_;
  mutable std::mutex queue_mutex_;
  std::size_t head_ = 0;
  std::size_t size_ = 0;
  std::atomic<std::uint64_t> schedule_calls_ = 0;
  std::atomic<std::uint64_t> executions_ = 0;
  std::atomic<std::uint64_t> capacity_failures_ = 0;
};

class FixedHookBackend {
 public:
  explicit FixedHookBackend(std::size_t capacity)
      : registrations_(capacity), queue_(capacity) {}

  static void reset_probe() noexcept {
    registration_calls_.store(0, std::memory_order_relaxed);
    seal_calls_.store(0, std::memory_order_relaxed);
    seal_saw_all_registrations_.store(false, std::memory_order_relaxed);
  }

  [[nodiscard]] static auto registration_calls() noexcept -> std::uint64_t {
    return registration_calls_.load(std::memory_order_relaxed);
  }

  [[nodiscard]] static auto seal_calls() noexcept -> std::uint64_t {
    return seal_calls_.load(std::memory_order_relaxed);
  }

  [[nodiscard]] static auto seal_saw_all_registrations() noexcept -> bool {
    return seal_saw_all_registrations_.load(std::memory_order_relaxed);
  }

  [[nodiscard]] auto register_task(maintenance::MaintenanceTask& task) noexcept
      -> bool {
    registration_calls_.fetch_add(1, std::memory_order_relaxed);
    if (registration_size_ == registrations_.size()) {
      return false;
    }
    registrations_[registration_size_++] = &task;
    return true;
  }

  void seal() noexcept {
    seal_saw_all_registrations_.store(
        registration_size_ == registrations_.size(), std::memory_order_relaxed);
    seal_calls_.fetch_add(1, std::memory_order_relaxed);
    sealed_ = true;
  }

  [[nodiscard]] auto schedule(maintenance::MaintenanceTask& task)
      -> maintenance::ScheduleResult {
    if (!sealed_ || !registered(task)) {
      return maintenance::ScheduleResult::Stalled;
    }
    return queue_.schedule(task);
  }

  [[nodiscard]] auto run_some(maintenance::MaintenanceBudget budget)
      -> maintenance::BackendDrainResult {
    return queue_.run_some(budget);
  }

  [[nodiscard]] auto flush() -> maintenance::BackendDrainResult {
    return queue_.flush();
  }

  [[nodiscard]] auto metrics() const noexcept
      -> maintenance::MaintenanceMetrics {
    return queue_.metrics();
  }

  [[nodiscard]] auto has_pending() const noexcept -> bool {
    return queue_.has_pending();
  }

 private:
  [[nodiscard]] auto registered(
      const maintenance::MaintenanceTask& task) const noexcept -> bool {
    for (std::size_t index = 0; index < registration_size_; ++index) {
      if (registrations_[index] == &task) {
        return true;
      }
    }
    return false;
  }

  std::vector<maintenance::MaintenanceTask*> registrations_;
  std::size_t registration_size_ = 0;
  StructuralBackend queue_;
  bool sealed_ = false;
  inline static std::atomic<std::uint64_t> registration_calls_ = 0;
  inline static std::atomic<std::uint64_t> seal_calls_ = 0;
  inline static std::atomic<bool> seal_saw_all_registrations_ = false;
};

class ThrowingFixedHookBackend : public StructuralBackend {
 public:
  using StructuralBackend::StructuralBackend;

  [[nodiscard]] auto register_task(maintenance::MaintenanceTask&) -> bool {
    return true;
  }
  void seal() {}
};

template <typename Scheduler>
struct SelfSchedulingTask final : maintenance::MaintenanceTask {
  Scheduler* scheduler = nullptr;
  maintenance::MaintenanceHandle handle{};
  std::uint32_t remaining = 2;
  std::uint32_t executions = 0;
  bool consume = true;

  void run(maintenance::MaintenanceBudget& budget) override {
    ++executions;
    if (consume) {
      static_cast<void>(budget.consume());
    }
    --remaining;
    if (remaining != 0) {
      EXPECT_EQ(scheduler->schedule(handle),
                maintenance::ScheduleResult::Accepted);
    }
  }
};

template <typename Scheduler>
struct ChainedSchedulingTask final : maintenance::MaintenanceTask {
  Scheduler* scheduler = nullptr;
  maintenance::MaintenanceHandle next{};
  std::uint32_t executions = 0;

  void run(maintenance::MaintenanceBudget& budget) override {
    static_cast<void>(budget.consume());
    ++executions;
    if (executions == 1) {
      EXPECT_EQ(scheduler->schedule(next),
                maintenance::ScheduleResult::Accepted);
    }
  }
};

template <typename Scheduler>
struct ObserveOtherOwnerMetricsTask final : maintenance::MaintenanceTask {
  const Scheduler* scheduler = nullptr;
  std::uint64_t observed_schedule_calls = 0;

  void run(maintenance::MaintenanceBudget& budget) override {
    static_cast<void>(budget.consume());
    observed_schedule_calls = scheduler->metrics().schedule_calls;
  }
};

#if TESS_HAS_EXCEPTIONS
struct ThrowingTask final : maintenance::MaintenanceTask {
  std::uint32_t executions = 0;

  void run(maintenance::MaintenanceBudget& budget) override {
    ++executions;
    static_cast<void>(budget.consume());
    throw std::runtime_error{"maintenance failure"};
  }
};

template <typename Scheduler>
struct SelfSchedulingThrowingTask final : maintenance::MaintenanceTask {
  Scheduler* scheduler = nullptr;
  maintenance::MaintenanceHandle handle{};
  std::uint32_t executions = 0;
  bool dirty = true;

  void run(maintenance::MaintenanceBudget& budget) override {
    ++executions;
    static_cast<void>(budget.consume());
    if (executions != 1) {
      dirty = false;
      return;
    }
    EXPECT_EQ(scheduler->schedule(handle),
              maintenance::ScheduleResult::Accepted);
    throw std::runtime_error{"maintenance failure after follow-up"};
  }
};
#endif

using Immediate = Registered<maintenance::ImmediateScheduler>;
using Fifo = Registered<maintenance::FifoScheduler>;
using Coalescing = Registered<maintenance::CoalescingScheduler>;
using DirtyBit = Registered<maintenance::DirtyBitScheduler>;
using Structural = Registered<StructuralBackend>;
using FixedHook = Registered<FixedHookBackend>;

static_assert(maintenance::MaintenanceBackend<StructuralBackend>);
static_assert(maintenance::FixedRegistrationBackend<FixedHookBackend>);
static_assert(!maintenance::FixedRegistrationBackend<ThrowingFixedHookBackend>);
static_assert(
    !std::is_base_of_v<maintenance::MaintenanceScheduler, StructuralBackend>);
static_assert(
    !std::is_base_of_v<maintenance::MaintenanceScheduler, FixedHookBackend>);
static_assert(std::is_copy_constructible_v<maintenance::MaintenanceHandle>);
static_assert(!std::is_move_constructible_v<maintenance::MaintenanceTask>);
static_assert(!std::is_move_constructible_v<Immediate>);

TEST(TessMaintenanceContract, StructuralBackendNeedsNoVirtualBase) {
  CountingTask first;
  CountingTask second;
  CountingTask retry;
  Structural scheduler(3, 2);
  const auto first_handle = require_handle(scheduler.register_task(first));
  const auto second_handle = require_handle(scheduler.register_task(second));
  const auto retry_handle = require_handle(scheduler.register_task(retry));
  scheduler.seal();

  EXPECT_EQ(scheduler.schedule(first_handle),
            maintenance::ScheduleResult::Accepted);
  EXPECT_EQ(scheduler.schedule(second_handle),
            maintenance::ScheduleResult::Accepted);
  EXPECT_EQ(scheduler.schedule(retry_handle),
            maintenance::ScheduleResult::CapacityExhausted);
  EXPECT_EQ(scheduler.metrics().schedule_calls, 3u);
  EXPECT_EQ(scheduler.metrics().capacity_failures, 1u);
  EXPECT_EQ(scheduler.run_some(maintenance::MaintenanceBudget{1}),
            maintenance::DrainResult::BudgetExhausted);
  EXPECT_EQ(scheduler.schedule(retry_handle),
            maintenance::ScheduleResult::Accepted);
  EXPECT_EQ(scheduler.flush(), maintenance::DrainResult::Drained);
  EXPECT_EQ(scheduler.flush(), maintenance::DrainResult::Idle);
  EXPECT_EQ(first.executions, 1u);
  EXPECT_EQ(second.executions, 1u);
  EXPECT_EQ(retry.executions, 1u);
  EXPECT_EQ(scheduler.metrics().executions, 3u);
}

TEST(TessMaintenanceContract, FixedBackendHooksPublishBeforeScheduling) {
  FixedHookBackend::reset_probe();
  CountingTask first;
  CountingTask second;
  FixedHook scheduler(2);
  const auto first_handle = require_handle(scheduler.register_task(first));
  const auto second_handle = require_handle(scheduler.register_task(second));
  scheduler.seal();

  EXPECT_EQ(FixedHookBackend::registration_calls(), 2u);
  EXPECT_EQ(FixedHookBackend::seal_calls(), 1u);
  EXPECT_TRUE(FixedHookBackend::seal_saw_all_registrations());
  EXPECT_EQ(scheduler.schedule(second_handle),
            maintenance::ScheduleResult::Accepted);
  EXPECT_EQ(scheduler.schedule(first_handle),
            maintenance::ScheduleResult::Accepted);
  EXPECT_EQ(scheduler.run_some(maintenance::MaintenanceBudget{1}),
            maintenance::DrainResult::BudgetExhausted);
  EXPECT_EQ(second.executions, 1u);
  EXPECT_EQ(first.executions, 0u);
  EXPECT_EQ(scheduler.flush(), maintenance::DrainResult::Drained);
  EXPECT_EQ(first.executions, 1u);
}

TEST(TessMaintenanceContract, HandleIsOpaqueAndCheckedLookupRejectsForeign) {
  CountingTask task;
  Fifo first(1);
  Fifo second(1);
  const auto handle = require_handle(first.register_task(task));
  first.seal();
  second.seal();

  EXPECT_TRUE(first.valid(handle));
  EXPECT_FALSE(second.valid(handle));
  EXPECT_FALSE(second.try_schedule(handle).has_value());
  EXPECT_EQ(second.metrics().schedule_calls, 0u);
  EXPECT_EQ(second.flush(), maintenance::DrainResult::Idle);
}

TEST(TessMaintenanceContract, CheckedStaleHandleDoesNotMutateWorkOrMetrics) {
  CountingTask task;
  Fifo scheduler(1);
  const auto handle = require_handle(scheduler.register_task(task));
  EXPECT_EQ(scheduler.try_release(handle),
            maintenance::ReleaseResult::Released);
  scheduler.seal();

  EXPECT_FALSE(scheduler.valid(handle));
  EXPECT_FALSE(scheduler.try_schedule(handle).has_value());
  EXPECT_EQ(scheduler.metrics().schedule_calls, 0u);
  EXPECT_EQ(scheduler.flush(), maintenance::DrainResult::Idle);
  EXPECT_EQ(task.executions, 0u);
}

TEST(TessMaintenanceContract, OwnerEpochRejectsAHandleAfterAddressReuse) {
  CountingTask first_task;
  CountingTask second_task;
  std::optional<Fifo> storage;
  storage.emplace(1);
  const auto stale = require_handle(storage->register_task(first_task));
  storage->seal();
  storage.reset();

  storage.emplace(1);
  const auto current = require_handle(storage->register_task(second_task));
  storage->seal();
  EXPECT_FALSE(storage->valid(stale));
  EXPECT_FALSE(storage->try_schedule(stale).has_value());
  EXPECT_TRUE(storage->valid(current));
}

TEST(TessMaintenanceContract, CapacityAndRegistrationLifecycleAreExplicit) {
  CountingTask first;
  CountingTask second;
  Fifo scheduler(1);
  const auto first_handle = require_handle(scheduler.register_task(first));
  EXPECT_EQ(scheduler.register_task(first), first_handle);
  EXPECT_FALSE(scheduler.register_task(second).has_value());
  scheduler.seal();
}

TEST(TessMaintenanceContract, ScheduleAndDrainResultsRemainDistinct) {
  CountingTask first;
  CountingTask second;
  Fifo scheduler(2);
  const auto first_handle = require_handle(scheduler.register_task(first));
  const auto second_handle = require_handle(scheduler.register_task(second));
  scheduler.seal();

  EXPECT_EQ(scheduler.flush(), maintenance::DrainResult::Idle);
  EXPECT_EQ(scheduler.schedule(first_handle),
            maintenance::ScheduleResult::Accepted);
  EXPECT_EQ(scheduler.schedule(second_handle),
            maintenance::ScheduleResult::Accepted);
  EXPECT_EQ(scheduler.run_some(maintenance::MaintenanceBudget{1}),
            maintenance::DrainResult::BudgetExhausted);
  EXPECT_EQ(scheduler.flush(), maintenance::DrainResult::Drained);
  EXPECT_EQ(scheduler.flush(), maintenance::DrainResult::Idle);
}

TEST(TessMaintenanceContract, CapacityRejectionIsRetryable) {
  CountingTask first;
  CountingTask second;
  Fifo scheduler(2, 1);
  const auto first_handle = require_handle(scheduler.register_task(first));
  const auto second_handle = require_handle(scheduler.register_task(second));
  scheduler.seal();

  EXPECT_EQ(scheduler.schedule(first_handle),
            maintenance::ScheduleResult::Accepted);
  EXPECT_EQ(scheduler.schedule(second_handle),
            maintenance::ScheduleResult::CapacityExhausted);
  EXPECT_EQ(scheduler.flush(), maintenance::DrainResult::Drained);
  EXPECT_EQ(scheduler.schedule(second_handle),
            maintenance::ScheduleResult::Accepted);
}

TEST(TessMaintenanceContract, SameOwnerTaskChainMayReturnToActiveTask) {
  ChainedSchedulingTask<Immediate> first;
  ChainedSchedulingTask<Immediate> second;
  Immediate scheduler(2);
  first.scheduler = &scheduler;
  second.scheduler = &scheduler;
  const auto first_handle = require_handle(scheduler.register_task(first));
  const auto second_handle = require_handle(scheduler.register_task(second));
  first.next = second_handle;
  second.next = first_handle;
  scheduler.seal();

  EXPECT_EQ(scheduler.schedule(first_handle),
            maintenance::ScheduleResult::Accepted);
  EXPECT_EQ(first.executions, 2u);
  EXPECT_EQ(second.executions, 1u);
}

TEST(TessMaintenanceContract, StructuralBackendSupportsCallbackReschedule) {
  SelfSchedulingTask<Structural> task;
  Structural scheduler(1);
  task.scheduler = &scheduler;
  const auto handle = require_handle(scheduler.register_task(task));
  task.handle = handle;
  scheduler.seal();

  EXPECT_EQ(scheduler.schedule(handle), maintenance::ScheduleResult::Accepted);
  EXPECT_EQ(scheduler.flush(), maintenance::DrainResult::Drained);
  EXPECT_EQ(task.executions, 2u);
}

TEST(TessMaintenanceContract, StalledWorkRemainsReachableForCallerRetry) {
  SelfSchedulingTask<Coalescing> task;
  Coalescing scheduler(1);
  task.scheduler = &scheduler;
  task.consume = false;
  const auto handle = require_handle(scheduler.register_task(task));
  task.handle = handle;
  scheduler.seal();

  ASSERT_EQ(scheduler.schedule(handle), maintenance::ScheduleResult::Accepted);
  EXPECT_EQ(scheduler.flush(), maintenance::DrainResult::Stalled);
  EXPECT_EQ(task.executions, 1u);
  EXPECT_EQ(scheduler.flush(), maintenance::DrainResult::Drained);
  EXPECT_EQ(task.executions, 2u);
  EXPECT_EQ(scheduler.flush(), maintenance::DrainResult::Idle);
}

#if TESS_HAS_EXCEPTIONS
TEST(TessMaintenanceContract,
     TaskExceptionPropagatesVerbatimAndLaterWorkRemainsPending) {
  ThrowingTask throwing;
  CountingTask later;
  DirtyBit scheduler(2);
  const auto throwing_handle =
      require_handle(scheduler.register_task(throwing));
  const auto later_handle = require_handle(scheduler.register_task(later));
  scheduler.seal();
  ASSERT_EQ(scheduler.schedule(throwing_handle),
            maintenance::ScheduleResult::Accepted);
  ASSERT_EQ(scheduler.schedule(later_handle),
            maintenance::ScheduleResult::Accepted);

  try {
    static_cast<void>(scheduler.flush());
    FAIL() << "expected the original callback exception";
  } catch (const std::runtime_error& error) {
    EXPECT_STREQ(error.what(), "maintenance failure");
  }
  EXPECT_EQ(throwing.executions, 1u);
  EXPECT_EQ(later.executions, 0u);
  EXPECT_EQ(scheduler.flush(), maintenance::DrainResult::Drained);
  EXPECT_EQ(later.executions, 1u);
  EXPECT_EQ(scheduler.try_release(later_handle),
            maintenance::ReleaseResult::NotIdle);
  EXPECT_EQ(scheduler.flush(), maintenance::DrainResult::Idle);
  EXPECT_EQ(scheduler.try_release(throwing_handle),
            maintenance::ReleaseResult::Released);
  EXPECT_EQ(scheduler.try_release(later_handle),
            maintenance::ReleaseResult::Released);
}

TEST(TessMaintenanceContract,
     StructuralBackendExceptionConsumesOnlyFailingOffer) {
  ThrowingTask throwing;
  CountingTask later;
  Structural scheduler(2);
  const auto throwing_handle =
      require_handle(scheduler.register_task(throwing));
  const auto later_handle = require_handle(scheduler.register_task(later));
  scheduler.seal();
  ASSERT_EQ(scheduler.schedule(throwing_handle),
            maintenance::ScheduleResult::Accepted);
  ASSERT_EQ(scheduler.schedule(later_handle),
            maintenance::ScheduleResult::Accepted);

  EXPECT_THROW(static_cast<void>(scheduler.flush()), std::runtime_error);
  EXPECT_EQ(scheduler.metrics().executions, 1u);
  EXPECT_EQ(later.executions, 0u);
  EXPECT_EQ(scheduler.flush(), maintenance::DrainResult::Drained);
  EXPECT_EQ(later.executions, 1u);
}

TEST(TessMaintenanceContract, ImmediateTaskExceptionPropagatesVerbatim) {
  ThrowingTask task;
  Immediate scheduler(1);
  const auto handle = require_handle(scheduler.register_task(task));
  scheduler.seal();

  EXPECT_THROW(static_cast<void>(scheduler.schedule(handle)),
               std::runtime_error);
}

TEST(TessMaintenanceContract,
     ImmediateThrowConsumesItsCallLocalReentrantChain) {
  SelfSchedulingThrowingTask<Immediate> task;
  Immediate scheduler(1);
  task.scheduler = &scheduler;
  const auto handle = require_handle(scheduler.register_task(task));
  task.handle = handle;
  scheduler.seal();

  try {
    static_cast<void>(scheduler.schedule(handle));
    FAIL() << "expected the original callback exception";
  } catch (const std::runtime_error& error) {
    EXPECT_STREQ(error.what(), "maintenance failure after follow-up");
  }
  EXPECT_TRUE(task.dirty);
  EXPECT_EQ(task.executions, 1u);
  EXPECT_EQ(scheduler.metrics().schedule_calls, 2u);
  EXPECT_EQ(scheduler.metrics().executions, 1u);
  EXPECT_EQ(scheduler.flush(), maintenance::DrainResult::Idle);
  EXPECT_EQ(scheduler.schedule(handle), maintenance::ScheduleResult::Accepted);
  EXPECT_FALSE(task.dirty);
  EXPECT_EQ(task.executions, 2u);
  EXPECT_EQ(scheduler.metrics().schedule_calls, 3u);
  EXPECT_EQ(scheduler.metrics().executions, 2u);
  EXPECT_EQ(scheduler.flush(), maintenance::DrainResult::Idle);
}
#endif

TEST(TessMaintenanceContract, ReleaseRequiresASeparatePositiveIdleObservation) {
  CountingTask task;
  DirtyBit scheduler(1);
  const auto handle = require_handle(scheduler.register_task(task));
  scheduler.seal();

  EXPECT_EQ(scheduler.try_release(handle), maintenance::ReleaseResult::NotIdle);
  ASSERT_EQ(scheduler.schedule(handle), maintenance::ScheduleResult::Accepted);
  EXPECT_EQ(scheduler.flush(), maintenance::DrainResult::Drained);
  EXPECT_EQ(scheduler.try_release(handle), maintenance::ReleaseResult::NotIdle);
  EXPECT_EQ(scheduler.flush(), maintenance::DrainResult::Idle);
  EXPECT_EQ(scheduler.try_release(handle),
            maintenance::ReleaseResult::Released);
  EXPECT_EQ(scheduler.try_release(handle),
            maintenance::ReleaseResult::InvalidHandle);
}

TEST(TessMaintenanceContract, SuccessfulScheduleInvalidatesIdleObservation) {
  CountingTask task;
  Coalescing scheduler(1);
  const auto handle = require_handle(scheduler.register_task(task));
  scheduler.seal();
  ASSERT_EQ(scheduler.flush(), maintenance::DrainResult::Idle);
  ASSERT_EQ(scheduler.schedule(handle), maintenance::ScheduleResult::Accepted);

  EXPECT_EQ(scheduler.try_release(handle), maintenance::ReleaseResult::NotIdle);
}

TEST(TessMaintenanceContract, ConcurrentFacadeOperationsSerializeRelease) {
  BlockingTask task;
  Immediate scheduler(1);
  const auto handle = require_handle(scheduler.register_task(task));
  scheduler.seal();
  ASSERT_EQ(scheduler.flush(), maintenance::DrainResult::Idle);

  std::atomic<bool> release_returned = false;
  auto release_result = maintenance::ReleaseResult::InvalidHandle;
  std::thread producer([&] { static_cast<void>(scheduler.schedule(handle)); });
  while (!task.entered.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  std::thread releaser([&] {
    release_result = scheduler.try_release(handle);
    release_returned.store(true, std::memory_order_release);
  });
  for (int attempt = 0; attempt < 10'000; ++attempt) {
    if (release_returned.load(std::memory_order_acquire)) {
      break;
    }
    std::this_thread::yield();
  }
  EXPECT_FALSE(release_returned.load(std::memory_order_acquire));

  task.release.store(true, std::memory_order_release);
  producer.join();
  releaser.join();
  EXPECT_EQ(release_result, maintenance::ReleaseResult::NotIdle);
}

TEST(TessMaintenanceContract, ConcurrentImmediateScheduleCannotProduceIdle) {
  BlockingTask task;
  Immediate scheduler(1);
  const auto handle = require_handle(scheduler.register_task(task));
  scheduler.seal();

  auto schedule_result = maintenance::ScheduleResult::Stalled;
  std::thread producer([&] { schedule_result = scheduler.schedule(handle); });
  while (!task.entered.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  std::atomic<bool> drain_started = false;
  std::atomic<bool> drain_returned = false;
  auto drain_result = maintenance::DrainResult::Idle;
  std::thread drainer([&] {
    drain_started.store(true, std::memory_order_release);
    drain_result = scheduler.flush();
    drain_returned.store(true, std::memory_order_release);
  });
  while (!drain_started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  for (int attempt = 0; attempt < 10'000; ++attempt) {
    if (drain_returned.load(std::memory_order_acquire)) {
      break;
    }
    std::this_thread::yield();
  }
  EXPECT_FALSE(drain_returned.load(std::memory_order_acquire));

  task.release.store(true, std::memory_order_release);
  producer.join();
  drainer.join();
  EXPECT_EQ(schedule_result, maintenance::ScheduleResult::Accepted);
  EXPECT_EQ(drain_result, maintenance::DrainResult::Drained);
  EXPECT_EQ(scheduler.flush(), maintenance::DrainResult::Idle);
}

TEST(TessMaintenanceContract, ConcurrentProducersUseTheHandleFacade) {
  CountingTask task;
  Coalescing scheduler(1);
  const auto handle = require_handle(scheduler.register_task(task));
  scheduler.seal();

  std::atomic<bool> start = false;
  std::array<std::thread, 8> producers;
  for (auto& producer : producers) {
    producer = std::thread([&] {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      for (int call = 0; call < 10'000; ++call) {
        EXPECT_EQ(scheduler.schedule(handle),
                  maintenance::ScheduleResult::Accepted);
      }
    });
  }
  start.store(true, std::memory_order_release);
  for (auto& producer : producers) {
    producer.join();
  }

  EXPECT_EQ(scheduler.flush(), maintenance::DrainResult::Drained);
  EXPECT_EQ(task.executions, 1u);
  EXPECT_EQ(scheduler.metrics().schedule_calls, 80'000u);
}

TEST(TessMaintenanceContract,
     StructuralBackendLinearizesConcurrentProducerAndDrain) {
  constexpr auto offers = std::uint64_t{2'000};
  ConcurrentTask task;
  Structural scheduler(1, 32);
  const auto handle = require_handle(scheduler.register_task(task));
  scheduler.seal();

  std::atomic<bool> producer_done = false;
  std::atomic<bool> unexpected_result = false;
  std::thread producer([&] {
    for (std::uint64_t offer = 0; offer < offers; ++offer) {
      for (;;) {
        const auto result = scheduler.schedule(handle);
        if (result == maintenance::ScheduleResult::Accepted) {
          break;
        }
        if (result != maintenance::ScheduleResult::CapacityExhausted) {
          unexpected_result.store(true, std::memory_order_release);
          producer_done.store(true, std::memory_order_release);
          return;
        }
        std::this_thread::yield();
      }
    }
    producer_done.store(true, std::memory_order_release);
  });

  while (!producer_done.load(std::memory_order_acquire)) {
    if (scheduler.run_some(maintenance::MaintenanceBudget{1}) ==
        maintenance::DrainResult::Stalled) {
      unexpected_result.store(true, std::memory_order_release);
    }
  }
  producer.join();
  EXPECT_NE(scheduler.flush(), maintenance::DrainResult::Stalled);

  EXPECT_FALSE(unexpected_result.load(std::memory_order_acquire));
  EXPECT_FALSE(task.overlapped.load(std::memory_order_acquire));
  EXPECT_EQ(task.executions.load(std::memory_order_relaxed), offers);
  EXPECT_EQ(scheduler.flush(), maintenance::DrainResult::Idle);
}

TEST(TessMaintenanceContract, FacadeSerializesCustomBackendDrains) {
  ConcurrentTask task;
  Structural scheduler(1, 32);
  const auto handle = require_handle(scheduler.register_task(task));
  scheduler.seal();
  for (int offer = 0; offer < 32; ++offer) {
    ASSERT_EQ(scheduler.schedule(handle),
              maintenance::ScheduleResult::Accepted);
  }

  auto first = maintenance::DrainResult::Stalled;
  auto second = maintenance::DrainResult::Stalled;
  std::thread first_drain([&] { first = scheduler.flush(); });
  std::thread second_drain([&] { second = scheduler.flush(); });
  first_drain.join();
  second_drain.join();

  EXPECT_FALSE(task.overlapped.load(std::memory_order_acquire));
  EXPECT_EQ(task.executions.load(std::memory_order_relaxed), 32u);
  EXPECT_NE(first, maintenance::DrainResult::Stalled);
  EXPECT_NE(second, maintenance::DrainResult::Stalled);
  EXPECT_EQ(scheduler.flush(), maintenance::DrainResult::Idle);
}

template <typename Scheduler>
struct ReleaseFromRunTask final : maintenance::MaintenanceTask {
  Scheduler* scheduler = nullptr;
  maintenance::MaintenanceHandle handle{};

  void run(maintenance::MaintenanceBudget&) override {
    scheduler->release(handle);
  }
};

template <typename Scheduler>
struct ScheduleOtherOwnerTask final : maintenance::MaintenanceTask {
  Scheduler* scheduler = nullptr;
  maintenance::MaintenanceHandle handle{};

  void run(maintenance::MaintenanceBudget&) override {
    static_cast<void>(scheduler->schedule(handle));
  }
};

template <typename Scheduler>
struct DrainFromRunTask final : maintenance::MaintenanceTask {
  Scheduler* scheduler = nullptr;

  void run(maintenance::MaintenanceBudget&) override {
    static_cast<void>(scheduler->flush());
  }
};

TEST(TessMaintenanceContractDeathTest, LifecycleMutationFromTaskFailsFast) {
  ReleaseFromRunTask<Immediate> task;
  Immediate scheduler(1);
  task.scheduler = &scheduler;
  const auto handle = require_handle(scheduler.register_task(task));
  task.handle = handle;
  scheduler.seal();

  EXPECT_DEATH(static_cast<void>(scheduler.schedule(handle)),
               "lifecycle mutation called from a running task");
}

TEST(TessMaintenanceContractDeathTest,
     NestedSameBackendCrossOwnerScheduleFailsFast) {
  ScheduleOtherOwnerTask<Immediate> task;
  CountingTask target;
  Immediate first(1);
  Immediate second(1);
  const auto target_handle = require_handle(second.register_task(target));
  task.scheduler = &second;
  task.handle = target_handle;
  const auto trigger_handle = require_handle(first.register_task(task));
  first.seal();
  second.seal();

  EXPECT_DEATH(static_cast<void>(first.schedule(trigger_handle)),
               "nested cross-scheduler operation");
}

TEST(TessMaintenanceContract, NestedCrossOwnerMetricsObservationIsAllowed) {
  CountingTask counted;
  ObserveOtherOwnerMetricsTask<Immediate> observer;
  Immediate first(1);
  Immediate second(1);
  const auto counted_handle = require_handle(second.register_task(counted));
  const auto observer_handle = require_handle(first.register_task(observer));
  observer.scheduler = &second;
  first.seal();
  second.seal();

  ASSERT_EQ(second.schedule(counted_handle),
            maintenance::ScheduleResult::Accepted);
  ASSERT_EQ(first.schedule(observer_handle),
            maintenance::ScheduleResult::Accepted);
  EXPECT_EQ(observer.observed_schedule_calls, 1u);
}

TEST(TessMaintenanceContractDeathTest,
     NestedCrossBackendCrossOwnerScheduleFailsFast) {
  ScheduleOtherOwnerTask<Fifo> task;
  CountingTask target;
  Immediate first(1);
  Fifo second(1);
  const auto target_handle = require_handle(second.register_task(target));
  task.scheduler = &second;
  task.handle = target_handle;
  const auto trigger_handle = require_handle(first.register_task(task));
  first.seal();
  second.seal();

  EXPECT_DEATH(static_cast<void>(first.schedule(trigger_handle)),
               "nested cross-scheduler operation");
}

TEST(TessMaintenanceContractDeathTest, ReentrantDrainFailsFast) {
  DrainFromRunTask<Immediate> task;
  Immediate scheduler(1);
  task.scheduler = &scheduler;
  const auto handle = require_handle(scheduler.register_task(task));
  scheduler.seal();

  EXPECT_DEATH(static_cast<void>(scheduler.schedule(handle)),
               "drain called from a running task");
}

TEST(TessMaintenanceContractDeathTest, CrossSchedulerTaskOwnerFailsFast) {
  CountingTask task;
  Fifo first(1);
  Fifo second(1);
  ASSERT_TRUE(first.register_task(task).has_value());

  EXPECT_DEATH(static_cast<void>(second.register_task(task)),
               "task belongs to another scheduler");
}

TEST(TessMaintenanceContractDeathTest, RepeatedSealFailsFast) {
  Fifo scheduler(1);
  scheduler.seal();

  EXPECT_DEATH(scheduler.seal(), "seal called twice");
}

TEST(TessMaintenanceContractDeathTest, FixedBackendCapacityMismatchFailsFast) {
  CountingTask first;
  CountingTask second;
  FixedHook scheduler(2, 1);
  ASSERT_TRUE(scheduler.register_task(first).has_value());
  ASSERT_TRUE(scheduler.register_task(second).has_value());

  EXPECT_DEATH(scheduler.seal(), "fixed backend registry capacity mismatch");
}

TEST(TessMaintenanceContractDeathTest, SchedulingBeforeSealFailsFast) {
  CountingTask task;
  Fifo scheduler(1);
  const auto handle = require_handle(scheduler.register_task(task));

  EXPECT_DEATH(static_cast<void>(scheduler.schedule(handle)),
               "operation called before seal");
}

TEST(TessMaintenanceContract, SchedulerDestructionReleasesTaskOwnership) {
  CountingTask task;
  {
    Fifo scheduler(1);
    ASSERT_TRUE(scheduler.register_task(task).has_value());
    scheduler.seal();
  }
  Fifo next(1);
  EXPECT_TRUE(next.register_task(task).has_value());
}

TEST(TessMaintenanceContract, DirtyBitWarmSchedulingDoesNotAllocate) {
  CountingTask task;
  DirtyBit scheduler(1);
  const auto handle = require_handle(scheduler.register_task(task));
  scheduler.seal();
  ASSERT_EQ(scheduler.schedule(handle), maintenance::ScheduleResult::Accepted);
  ASSERT_EQ(scheduler.flush(), maintenance::DrainResult::Drained);

  {
    tess_test::ScopedAllocationCounter counter;
    for (int call = 0; call < 1'000; ++call) {
      EXPECT_EQ(scheduler.schedule(handle),
                maintenance::ScheduleResult::Accepted);
    }
    EXPECT_EQ(counter.count(), 0u);
  }
}

using TessMaintenanceContractDeathTest = ::testing::Test;

TEST(TessMaintenanceContractDeathTest, UncheckedForeignHandleFailsFast) {
  CountingTask task;
  Fifo first(1);
  Fifo second(1);
  const auto handle = require_handle(first.register_task(task));
  first.seal();
  second.seal();

  EXPECT_DEATH(static_cast<void>(second.schedule(handle)),
               "schedule.*wrong scheduler or registration epoch");
}

TEST(TessMaintenanceContractDeathTest, UncheckedRepeatedReleaseFailsFast) {
  CountingTask task;
  Fifo scheduler(1);
  const auto handle = require_handle(scheduler.register_task(task));
  scheduler.release(handle);

  EXPECT_DEATH(scheduler.release(handle), "release.*stale maintenance handle");
}

TEST(TessMaintenanceContractDeathTest, RegistrationAfterSealFailsFast) {
  CountingTask task;
  Fifo scheduler(1);
  scheduler.seal();

  EXPECT_DEATH(static_cast<void>(scheduler.register_task(task)),
               "register_task.*after seal");
}

TEST(TessMaintenanceContractDeathTest,
     TaskDestructionWhileRegisteredFailsFast) {
  Fifo scheduler(1);
  auto task = std::make_unique<CountingTask>();
  const auto handle = require_handle(scheduler.register_task(*task));

  EXPECT_DEATH(task.reset(), "MaintenanceTask destroyed while registered");

  scheduler.release(handle);
  task.reset();
}

TEST(TessMaintenanceContractDeathTest, NonIdleUncheckedReleaseFailsFast) {
  CountingTask task;
  Fifo scheduler(1);
  const auto handle = require_handle(scheduler.register_task(task));
  scheduler.seal();

  EXPECT_DEATH(scheduler.release(handle), "release.*positive Idle");
}

}  // namespace
