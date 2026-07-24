#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <vector>

namespace tess::experimental::maintenance {

/// Shared unit budget passed through one maintenance drain.
class MaintenanceBudget {
 public:
  explicit constexpr MaintenanceBudget(
      std::uint64_t units = std::numeric_limits<std::uint64_t>::max()) noexcept
      : remaining_(units) {}

  [[nodiscard]] constexpr auto consume(std::uint64_t units = 1) noexcept
      -> bool {
    if (units > remaining_) {
      return false;
    }
    remaining_ -= units;
    return true;
  }

  [[nodiscard]] constexpr auto remaining() const noexcept -> std::uint64_t {
    return remaining_;
  }

 private:
  std::uint64_t remaining_;
};

/// Long-lived derived-state maintenance operation.
class MaintenanceTask {
 public:
  virtual ~MaintenanceTask() = default;
  virtual void run(MaintenanceBudget& budget) = 0;
};

/// Scheduler observations used by experiments and diagnostics.
struct MaintenanceMetrics {
  std::uint64_t schedule_calls = 0;
  std::uint64_t coalesced_calls = 0;
  std::uint64_t executions = 0;
  std::uint64_t capacity_failures = 0;
};

/// Backend-neutral experimental maintenance scheduler interface.
class MaintenanceScheduler {
 public:
  virtual ~MaintenanceScheduler() = default;

  /**
   * Schedules derived-state work.
   *
   * The task must outlive this scheduler or an explicit `flush()`. A false
   * result reports bounded queue exhaustion or a backend that cannot make
   * progress; authoritative state must retain its dirty signal so a caller can
   * retry.
   */
  [[nodiscard]] virtual auto schedule(MaintenanceTask& task) -> bool = 0;

  /// Runs reachable work until the queue or supplied budget is exhausted.
  [[nodiscard]] virtual auto run_some(MaintenanceBudget budget) -> bool = 0;

  /// Completes all reachable work using an unbounded unit budget.
  [[nodiscard]] virtual auto flush() -> bool = 0;

  [[nodiscard]] virtual auto metrics() const noexcept -> MaintenanceMetrics = 0;
};

namespace detail {

class MetricsStore {
 public:
  void record_schedule() noexcept {
    schedule_calls_.fetch_add(1, std::memory_order_relaxed);
  }
  void record_coalesced() noexcept {
    coalesced_calls_.fetch_add(1, std::memory_order_relaxed);
  }
  void record_execution() noexcept {
    executions_.fetch_add(1, std::memory_order_relaxed);
  }
  void record_capacity_failure() noexcept {
    capacity_failures_.fetch_add(1, std::memory_order_relaxed);
  }

  [[nodiscard]] auto snapshot() const noexcept -> MaintenanceMetrics {
    return MaintenanceMetrics{
        schedule_calls_.load(std::memory_order_relaxed),
        coalesced_calls_.load(std::memory_order_relaxed),
        executions_.load(std::memory_order_relaxed),
        capacity_failures_.load(std::memory_order_relaxed)};
  }

 private:
  std::atomic<std::uint64_t> schedule_calls_ = 0;
  std::atomic<std::uint64_t> coalesced_calls_ = 0;
  std::atomic<std::uint64_t> executions_ = 0;
  std::atomic<std::uint64_t> capacity_failures_ = 0;
};

class BoundedTaskQueue {
 public:
  explicit BoundedTaskQueue(std::size_t capacity) : tasks_(capacity) {}

  [[nodiscard]] auto push(MaintenanceTask& task) noexcept -> bool {
    if (size_ == tasks_.size()) {
      return false;
    }
    const auto tail = (head_ + size_) % tasks_.size();
    tasks_[tail] = &task;
    ++size_;
    return true;
  }

  [[nodiscard]] auto contains(const MaintenanceTask& task) const noexcept
      -> bool {
    for (std::size_t offset = 0; offset < size_; ++offset) {
      const auto index = (head_ + offset) % tasks_.size();
      if (tasks_[index] == &task) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] auto pop() noexcept -> MaintenanceTask* {
    if (size_ == 0) {
      return nullptr;
    }
    auto* task = tasks_[head_];
    tasks_[head_] = nullptr;
    head_ = (head_ + 1) % tasks_.size();
    --size_;
    return task;
  }

  [[nodiscard]] auto empty() const noexcept -> bool { return size_ == 0; }

 private:
  std::vector<MaintenanceTask*> tasks_;
  std::size_t head_ = 0;
  std::size_t size_ = 0;
};

template <bool Coalescing>
class QueuedScheduler : public MaintenanceScheduler {
 public:
  explicit QueuedScheduler(std::size_t capacity) : queue_(capacity) {}

  [[nodiscard]] auto schedule(MaintenanceTask& task) -> bool override {
    metrics_.record_schedule();
    const auto lock = std::scoped_lock{queue_mutex_};
    if (&task == running_task_) {
      running_task_rescheduled_ = true;
    }
    if constexpr (Coalescing) {
      if (queue_.contains(task)) {
        metrics_.record_coalesced();
        return true;
      }
    }
    if (!queue_.push(task)) {
      metrics_.record_capacity_failure();
      return false;
    }
    return true;
  }

  [[nodiscard]] auto run_some(MaintenanceBudget budget) -> bool override {
    const auto run_lock = std::scoped_lock{run_mutex_};
    while (budget.remaining() != 0) {
      MaintenanceTask* task = nullptr;
      {
        const auto queue_lock = std::scoped_lock{queue_mutex_};
        task = queue_.pop();
      }
      if (task == nullptr) {
        return true;
      }
      if (!run_task(*task, budget)) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] auto flush() -> bool override {
    const auto run_lock = std::scoped_lock{run_mutex_};
    auto budget = MaintenanceBudget{};
    for (;;) {
      MaintenanceTask* task = nullptr;
      {
        const auto queue_lock = std::scoped_lock{queue_mutex_};
        task = queue_.pop();
      }
      if (task == nullptr) {
        return true;
      }
      if (!run_task(*task, budget)) {
        return false;
      }
    }
  }

  [[nodiscard]] auto metrics() const noexcept -> MaintenanceMetrics override {
    return metrics_.snapshot();
  }

 private:
  [[nodiscard]] auto run_task(MaintenanceTask& task, MaintenanceBudget& budget)
      -> bool {
    metrics_.record_execution();
    const auto before = budget.remaining();
    {
      const auto lock = std::scoped_lock{queue_mutex_};
      running_task_ = &task;
      running_task_rescheduled_ = false;
    }
    try {
      task.run(budget);
    } catch (...) {
      const auto lock = std::scoped_lock{queue_mutex_};
      running_task_ = nullptr;
      running_task_rescheduled_ = false;
      throw;
    }
    auto rescheduled = false;
    {
      const auto lock = std::scoped_lock{queue_mutex_};
      rescheduled = running_task_rescheduled_;
      running_task_ = nullptr;
      running_task_rescheduled_ = false;
    }
    return budget.remaining() != before || !rescheduled;
  }

  mutable std::mutex queue_mutex_;
  std::mutex run_mutex_;
  BoundedTaskQueue queue_;
  MetricsStore metrics_;
  MaintenanceTask* running_task_ = nullptr;
  bool running_task_rescheduled_ = false;
};

}  // namespace detail

/// Synchronous correctness baseline; each schedule call executes immediately.
class ImmediateScheduler final : public MaintenanceScheduler {
 public:
  explicit ImmediateScheduler(std::size_t = 0) {}

  [[nodiscard]] auto schedule(MaintenanceTask& task) -> bool override {
    // A task may call schedule() while it runs, so this must be recursive.
    // The same lock also preserves synchronous return semantics for concurrent
    // callers and prevents active_run_ from borrowing another thread's frame.
    const auto run_lock = std::scoped_lock{run_mutex_};
    metrics_.record_schedule();
    for (auto* active = active_run_; active != nullptr;
         active = active->parent) {
      if (active->task != &task) {
        continue;
      }
      if (active->pending == std::numeric_limits<std::uint64_t>::max()) {
        metrics_.record_capacity_failure();
        return false;
      }
      ++active->pending;
      return true;
    }

    // An intrusive stack of call-local frames makes A -> B -> A and direct
    // self-scheduling iterative without allocating. A count, rather than a
    // bool, preserves ImmediateScheduler's one-execution-per-request baseline.
    auto active = ActiveRun{&task, 1, active_run_};
    struct ActiveRunGuard {
      ActiveRun*& current;
      ActiveRun* previous;
      ~ActiveRunGuard() { current = previous; }
    };
    active_run_ = &active;
    // active_run_ borrows this frame only until guard restores its parent
    // during the same schedule() call; task.run() cannot retain the frame.
    // cppcheck-suppress danglingLifetime
    const auto guard = ActiveRunGuard{active_run_, active.parent};
    auto budget = MaintenanceBudget{};
    while (active.pending != 0) {
      --active.pending;
      const auto before = budget.remaining();
      metrics_.record_execution();
      task.run(budget);
      if (active.pending != 0 && budget.remaining() == before) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] auto run_some(MaintenanceBudget) -> bool override {
    return true;
  }
  [[nodiscard]] auto flush() -> bool override { return true; }
  [[nodiscard]] auto metrics() const noexcept -> MaintenanceMetrics override {
    return metrics_.snapshot();
  }

 private:
  struct ActiveRun {
    MaintenanceTask* task = nullptr;
    std::uint64_t pending = 0;
    ActiveRun* parent = nullptr;
  };

  detail::MetricsStore metrics_;
  std::recursive_mutex run_mutex_;
  ActiveRun* active_run_ = nullptr;
};

/// Bounded non-deduplicating queue used as the amplification baseline.
using FifoScheduler = detail::QueuedScheduler<false>;

/// Bounded queue that retains at most one pending entry per task.
using CoalescingScheduler = detail::QueuedScheduler<true>;

}  // namespace tess::experimental::maintenance
