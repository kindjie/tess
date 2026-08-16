#pragma once

#include <tess/core/assert.h>
#include <tess/core/config.h>
#include <tess/diagnostics/diagnostics.h>

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <thread>
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
   * result reports bounded capacity exhaustion, incomplete backend setup, an
   * unknown task identity, or a backend that cannot make progress;
   * authoritative state must retain its dirty signal so a caller can retry.
   */
  [[nodiscard]] virtual auto schedule(MaintenanceTask& task) -> bool = 0;

  /**
   * Runs reachable work until the queue or supplied budget is exhausted.
   *
   * Do not call this or `flush()` from `MaintenanceTask::run()`: queued
   * backends serialize drains with a non-recursive lock. `schedule()` is the
   * only reentrant scheduler operation supported from a running task.
   */
  [[nodiscard]] virtual auto run_some(MaintenanceBudget budget) -> bool = 0;

  /// Completes all reachable work; see the non-reentrant drain contract above.
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

/// One queued maintenance entry with its flow-accounting admission stamp.
struct QueuedMaintenanceEntry {
  MaintenanceTask* task = nullptr;
  std::uint64_t admitted_tick = 0;
  std::size_t queue_slot = std::numeric_limits<std::size_t>::max();
};

class BoundedTaskQueue {
 public:
  explicit BoundedTaskQueue(std::size_t capacity) : entries_(capacity) {}

  [[nodiscard]] auto push(MaintenanceTask& task, std::uint64_t admitted_tick,
                          std::size_t* queue_slot = nullptr) noexcept -> bool {
    if (size_ == entries_.size()) {
      return false;
    }
    const auto tail = (head_ + size_) % entries_.size();
    entries_[tail] = QueuedMaintenanceEntry{&task, admitted_tick, tail};
    if (queue_slot != nullptr) {
      *queue_slot = tail;
    }
    ++size_;
    return true;
  }

  [[nodiscard]] auto pop() noexcept -> QueuedMaintenanceEntry {
    if (size_ == 0) {
      return {};
    }
    auto entry = entries_[head_];
    entries_[head_] = {};
    head_ = (head_ + 1) % entries_.size();
    --size_;
    return entry;
  }

  [[nodiscard]] auto empty() const noexcept -> bool { return size_ == 0; }

  [[nodiscard]] auto size() const noexcept -> std::size_t { return size_; }

  [[nodiscard]] auto oldest_admitted_tick() const noexcept -> std::uint64_t {
    return size_ == 0 ? 0 : entries_[head_].admitted_tick;
  }

 private:
  std::vector<QueuedMaintenanceEntry> entries_;
  std::size_t head_ = 0;
  std::size_t size_ = 0;
};

/// Preallocated chained membership index for pending queue slots.
class PendingTaskIndex {
 public:
  explicit PendingTaskIndex(std::size_t capacity)
      : buckets_(bucket_count(capacity), npos), nodes_(capacity) {}

  [[nodiscard]] auto contains(const MaintenanceTask& task) const noexcept
      -> bool {
    if (buckets_.empty()) {
      return false;
    }
    for (auto index = buckets_[home(task)]; index != npos;
         index = nodes_[index].next) {
      if (nodes_[index].task == &task) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] auto insert(MaintenanceTask& task,
                            std::size_t queue_slot) noexcept -> bool {
    if (buckets_.empty() || queue_slot >= nodes_.size() ||
        nodes_[queue_slot].task != nullptr) {
      return false;
    }
    const auto bucket = home(task);
    nodes_[queue_slot] = Node{&task, buckets_[bucket]};
    buckets_[bucket] = queue_slot;
    return true;
  }

  [[nodiscard]] auto erase(const MaintenanceTask& task,
                           std::size_t queue_slot) noexcept -> bool {
    if (buckets_.empty() || queue_slot >= nodes_.size()) {
      return false;
    }
    auto* link = &buckets_[home(task)];
    while (*link != npos) {
      if (*link == queue_slot) {
        *link = nodes_[queue_slot].next;
        nodes_[queue_slot] = {};
        return true;
      }
      link = &nodes_[*link].next;
    }
    return false;
  }

 private:
  static constexpr auto npos = std::numeric_limits<std::size_t>::max();

  struct Node {
    MaintenanceTask* task = nullptr;
    std::size_t next = npos;
  };

  [[nodiscard]] static auto bucket_count(std::size_t capacity) noexcept
      -> std::size_t {
    if (capacity == 0) {
      return 0;
    }
    const auto maximum = std::numeric_limits<std::size_t>::max();
    return capacity > maximum - capacity ? capacity : capacity * 2;
  }

  [[nodiscard]] auto home(const MaintenanceTask& task) const noexcept
      -> std::size_t {
    auto value = reinterpret_cast<std::uintptr_t>(&task);
    value >>= 3u;
    value ^= value >> 17u;
    if constexpr (sizeof(value) >= sizeof(std::uint64_t)) {
      value *= std::uintptr_t{0x9e3779b97f4a7c15ULL};
    } else {
      value *= std::uintptr_t{0x9e3779b9U};
    }
    return static_cast<std::size_t>(value) % buckets_.size();
  }

  std::vector<std::size_t> buckets_;
  std::vector<Node> nodes_;
};

template <bool Coalescing>
class QueuedScheduler : public MaintenanceScheduler {
 public:
  explicit QueuedScheduler(std::size_t capacity)
      : queue_(capacity), pending_(Coalescing ? capacity : 0) {}

  [[nodiscard]] auto schedule(MaintenanceTask& task) -> bool override {
    metrics_.record_schedule();
    const auto lock = std::scoped_lock{queue_mutex_};
    // Only synchronous calls from task.run() establish a follow-up. A
    // concurrent producer must not make a completed task look stalled.
    const auto called_from_task = running_thread_ == std::this_thread::get_id();
    if constexpr (Coalescing) {
      if (pending_.contains(task)) {
        if (called_from_task) {
          running_task_scheduled_ = true;
        }
        metrics_.record_coalesced();
        if (accounting_ != nullptr) {
          ++accounting_->counters.offered;
          ++accounting_->counters.coalesced_into_pending;
        }
        return true;
      }
    }
    const auto admitted_tick =
        accounting_ != nullptr ? accounting_->last_observed_tick : 0;
    auto queue_slot = std::size_t{0};
    if (!queue_.push(task, admitted_tick, &queue_slot)) {
      metrics_.record_capacity_failure();
      if (accounting_ != nullptr) {
        ++accounting_->counters.offered;
        ++accounting_->counters.rejected;
      }
      return false;
    }
    if constexpr (Coalescing) {
      const auto inserted = pending_.insert(task, queue_slot);
      TESS_ASSERT(inserted);
      static_cast<void>(inserted);
    }
    if (called_from_task) {
      running_task_scheduled_ = true;
    }
    if (accounting_ != nullptr) {
      ++accounting_->counters.offered;
      accounting_->record_admitted();
    }
    return true;
  }

  [[nodiscard]] auto run_some(MaintenanceBudget budget) -> bool override {
    const auto run_lock = std::scoped_lock{run_mutex_};
    if (accounting_ != nullptr &&
        budget.remaining() != std::numeric_limits<std::uint64_t>::max()) {
      const auto lock = std::scoped_lock{queue_mutex_};
      accounting_->counters.offered_work_units += budget.remaining();
    }
    while (budget.remaining() != 0) {
      auto entry = detail::QueuedMaintenanceEntry{};
      {
        const auto queue_lock = std::scoped_lock{queue_mutex_};
        entry = pop_pending();
      }
      if (entry.task == nullptr) {
        return true;
      }
      if (!run_task(entry, budget)) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] auto flush() -> bool override {
    const auto run_lock = std::scoped_lock{run_mutex_};
    // The unbounded flush budget is deliberately not offered work: it
    // is not a meaningful workload measurement.
    auto budget = MaintenanceBudget{};
    for (;;) {
      auto entry = detail::QueuedMaintenanceEntry{};
      {
        const auto queue_lock = std::scoped_lock{queue_mutex_};
        entry = pop_pending();
      }
      if (entry.task == nullptr) {
        return true;
      }
      if (!run_task(entry, budget)) {
        return false;
      }
    }
  }

  [[nodiscard]] auto metrics() const noexcept -> MaintenanceMetrics override {
    return metrics_.snapshot();
  }

  /**
   * Attaches caller-owned flow accounting; null detaches.
   *
   * Attach or detach only while the queue is empty and no drain is
   * running; updates happen under the scheduler's own locks, and
   * snapshots are meaningful only at quiescent points (serial use).
   */
  void set_flow_accounting(diagnostics::FlowAccounting* accounting) {
    const auto run_lock = std::scoped_lock{run_mutex_};
    const auto lock = std::scoped_lock{queue_mutex_};
    TESS_ASSERT(queue_.empty());
    accounting_ = accounting;
  }

  /// Observes one monotonic tick: weights inventory and refreshes the
  /// oldest outstanding age across both queued and in-flight work.
  void observe_flow_tick(std::uint64_t tick) {
    const auto lock = std::scoped_lock{queue_mutex_};
    if (accounting_ == nullptr) {
      return;
    }
    accounting_->observe_tick(tick);
    const auto now = accounting_->last_observed_tick;
    auto any = false;
    auto oldest = now;
    if (!queue_.empty()) {
      any = true;
      oldest = queue_.oldest_admitted_tick();
    }
    if (running_active_ && running_admitted_tick_ < oldest) {
      any = true;
      oldest = running_admitted_tick_;
    }
    if (running_active_ && !any) {
      any = true;
    }
    accounting_->counters.oldest_outstanding_age_ticks = any ? now - oldest : 0;
  }

 private:
  [[nodiscard]] auto run_task(detail::QueuedMaintenanceEntry entry,
                              MaintenanceBudget& budget) -> bool {
    auto& task = *entry.task;
    metrics_.record_execution();
    const auto before = budget.remaining();
    {
      const auto lock = std::scoped_lock{queue_mutex_};
      running_thread_ = std::this_thread::get_id();
      running_task_scheduled_ = false;
      running_active_ = true;
      running_admitted_tick_ = entry.admitted_tick;
    }
#if TESS_HAS_EXCEPTIONS
    try {
      task.run(budget);
    } catch (...) {
      // The queue entry was consumed before invocation and is not restored.
      // Tasks own the authoritative dirty/version state, which must remain set
      // on failure; the caller decides whether explicitly scheduling a retry
      // is safe after observing the exception.
      const auto lock = std::scoped_lock{queue_mutex_};
      running_thread_ = {};
      running_task_scheduled_ = false;
      running_active_ = false;
      account_terminal(entry, before, budget.remaining(), false);
      throw;
    }
#else
    task.run(budget);
#endif
    auto scheduled_follow_up = false;
    {
      const auto lock = std::scoped_lock{queue_mutex_};
      scheduled_follow_up = running_task_scheduled_;
      running_thread_ = {};
      running_task_scheduled_ = false;
      running_active_ = false;
      account_terminal(entry, before, budget.remaining(), true);
    }
    // A no-op task may finish without consuming budget. A task that queues
    // follow-up work has not finished, however, so continuing could spin
    // through A -> B -> A forever. Stop this drain and leave the follow-up
    // queued for explicit caller intervention.
    return budget.remaining() != before || !scheduled_follow_up;
  }

  /// Pops one entry and removes its pending-membership stamp. Callers hold
  /// queue_mutex_ (or are destroying a quiescent scheduler).
  [[nodiscard]] auto pop_pending() noexcept -> QueuedMaintenanceEntry {
    auto entry = queue_.pop();
    if constexpr (Coalescing) {
      if (entry.task != nullptr) {
        const auto erased = pending_.erase(*entry.task, entry.queue_slot);
        TESS_ASSERT(erased);
        static_cast<void>(erased);
      }
    }
    return entry;
  }

  /// Buckets one popped entry after its run; callers hold queue_mutex_.
  /// A popped-but-running task stayed outstanding until here, so the
  /// retention identity holds at every quiescent point.
  void account_terminal(const detail::QueuedMaintenanceEntry& entry,
                        std::uint64_t budget_before, std::uint64_t budget_after,
                        bool completed) noexcept {
    if (accounting_ == nullptr) {
      return;
    }
    auto& counters = accounting_->counters;
    counters.consumed_work_units += budget_before - budget_after;
    ++(completed ? counters.completed : counters.failed);
    accounting_->record_left_outstanding();
    counters.residence_ticks_accumulated +=
        accounting_->last_observed_tick - entry.admitted_tick;
  }

  mutable std::mutex queue_mutex_;
  std::mutex run_mutex_;
  BoundedTaskQueue queue_;
  PendingTaskIndex pending_;

 public:
  /// Destroying a scheduler with queued work drops it after admission,
  /// so an attached accountant's retention identity stays truthful.
  ~QueuedScheduler() override {
    if (accounting_ == nullptr) {
      return;
    }
    for (;;) {
      const auto entry = pop_pending();
      if (entry.task == nullptr) {
        break;
      }
      ++accounting_->counters.dropped_after_admission;
      accounting_->record_left_outstanding();
      accounting_->counters.residence_ticks_accumulated +=
          accounting_->last_observed_tick - entry.admitted_tick;
    }
  }

 private:
  MetricsStore metrics_;
  std::thread::id running_thread_;
  bool running_task_scheduled_ = false;
  bool running_active_ = false;
  std::uint64_t running_admitted_tick_ = 0;
  diagnostics::FlowAccounting* accounting_ = nullptr;
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
        account_offer(Offer::Rejected);
        return false;
      }
      ++active->pending;
      account_offer(Offer::Coalesced);
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
    account_offer(Offer::Admitted);
    auto budget = MaintenanceBudget{};
    auto completed_ok = true;
    auto consumed_total = std::uint64_t{0};
    while (active.pending != 0) {
      --active.pending;
      const auto before = budget.remaining();
      metrics_.record_execution();
#if TESS_HAS_EXCEPTIONS
      try {
        task.run(budget);
      } catch (...) {
        consumed_total += before - budget.remaining();
        account_run_terminal(consumed_total, true);
        throw;
      }
#else
      task.run(budget);
#endif
      consumed_total += before - budget.remaining();
      if (active.pending != 0 && budget.remaining() == before) {
        completed_ok = false;
        break;
      }
    }
    // A zero-progress abandonment still ran the admitted request; its
    // residual repeats were coalesced offers, never admissions.
    account_run_terminal(consumed_total, false);
    return completed_ok;
  }

  [[nodiscard]] auto run_some(MaintenanceBudget) -> bool override {
    return true;
  }
  [[nodiscard]] auto flush() -> bool override { return true; }
  [[nodiscard]] auto metrics() const noexcept -> MaintenanceMetrics override {
    return metrics_.snapshot();
  }

  /**
   * Attaches caller-owned flow accounting; null detaches.
   *
   * Attach or detach only while no schedule call is running; updates
   * happen under the scheduler's lock (serial snapshots).
   */
  void set_flow_accounting(diagnostics::FlowAccounting* accounting) {
    const auto run_lock = std::scoped_lock{run_mutex_};
    TESS_ASSERT(active_run_ == nullptr);
    accounting_ = accounting;
  }

  /// Observes one monotonic tick; the immediate backend never retains
  /// work across calls, so only inventory weighting applies.
  void observe_flow_tick(std::uint64_t tick) {
    const auto run_lock = std::scoped_lock{run_mutex_};
    if (accounting_ != nullptr) {
      accounting_->observe_tick(tick);
      accounting_->counters.oldest_outstanding_age_ticks = 0;
    }
  }

 private:
  struct ActiveRun {
    MaintenanceTask* task = nullptr;
    std::uint64_t pending = 0;
    ActiveRun* parent = nullptr;
  };

  enum class Offer : std::uint8_t { Admitted, Rejected, Coalesced };

  void account_offer(Offer offer) noexcept {
    if (accounting_ == nullptr) {
      return;
    }
    ++accounting_->counters.offered;
    switch (offer) {
      case Offer::Admitted:
        accounting_->record_admitted();
        break;
      case Offer::Rejected:
        ++accounting_->counters.rejected;
        break;
      case Offer::Coalesced:
        ++accounting_->counters.coalesced_into_pending;
        break;
    }
  }

  void account_run_terminal(std::uint64_t consumed, bool failed) noexcept {
    if (accounting_ == nullptr) {
      return;
    }
    accounting_->counters.consumed_work_units += consumed;
    ++(failed ? accounting_->counters.failed : accounting_->counters.completed);
    accounting_->record_left_outstanding();
  }

  detail::MetricsStore metrics_;
  std::recursive_mutex run_mutex_;
  ActiveRun* active_run_ = nullptr;
  diagnostics::FlowAccounting* accounting_ = nullptr;
};

/**
 * Coalescing scheduler backed by registered task bits.
 *
 * Register every task during setup, before scheduling or draining begins.
 * Registration is idempotent for the same task and bounded by the constructor
 * capacity. Call `seal()` after registration; it publishes the immutable
 * registry to concurrent producers. Tasks remain non-owning and must outlive
 * this scheduler or a completed `flush()`. Registration order defines
 * deterministic drain order. Destroying the scheduler drops pending work
 * without executing it. A thread's first successful post-seal `schedule()` or
 * first task execution may initialize platform thread-local runtime state;
 * later scheduling on that thread is allocation-free.
 */
class DirtyBitScheduler final : public MaintenanceScheduler {
 public:
  explicit DirtyBitScheduler(std::size_t capacity)
      : tasks_(capacity, nullptr),
        registrations_(index_capacity(capacity)),
        pending_(word_count(capacity)) {
    for (auto& word : pending_) {
      word.store(0, std::memory_order_relaxed);
    }
  }

  /** Registers one task during setup; false reports exhausted capacity. */
  [[nodiscard]] auto register_task(MaintenanceTask& task) -> bool {
    const auto setup_lock = std::scoped_lock{setup_mutex_};
    if (sealed_.load(std::memory_order_relaxed)) {
      return false;
    }
    if (find_index(task) != npos) {
      return true;
    }
    if (registered_ == tasks_.size()) {
      metrics_.record_capacity_failure();
      return false;
    }
    const auto index = registered_++;
    tasks_[index] = &task;
    auto slot = home(task);
    while (registrations_[slot].task != nullptr) {
      slot = next_registration(slot);
    }
    registrations_[slot] = Registration{&task, index};
    return true;
  }

  /** Publishes the completed registry and ends the setup phase. */
  void seal() {
    const auto setup_lock = std::scoped_lock{setup_mutex_};
    sealed_.store(true, std::memory_order_release);
  }

  [[nodiscard]] auto schedule(MaintenanceTask& task) -> bool override {
    metrics_.record_schedule();
    if (!sealed_.load(std::memory_order_acquire)) {
      return false;
    }
    const auto index = find_index(task);
    if (index == npos) {
      return false;
    }
    const auto mask = std::uint64_t{1} << (index % 64u);
    const auto previous =
        pending_[index / 64u].fetch_or(mask, std::memory_order_release);
    if ((previous & mask) != 0) {
      metrics_.record_coalesced();
    }
    if (active_scheduler_ == this) {
      running_task_scheduled_ = true;
    }
    return true;
  }

  [[nodiscard]] auto run_some(MaintenanceBudget budget) -> bool override {
    if (!sealed_.load(std::memory_order_acquire)) {
      return false;
    }
    const auto run_lock = std::scoped_lock{run_mutex_};
    return drain(budget);
  }

  [[nodiscard]] auto flush() -> bool override {
    if (!sealed_.load(std::memory_order_acquire)) {
      return false;
    }
    const auto run_lock = std::scoped_lock{run_mutex_};
    auto budget = MaintenanceBudget{};
    return drain(budget);
  }

  [[nodiscard]] auto metrics() const noexcept -> MaintenanceMetrics override {
    return metrics_.snapshot();
  }

 private:
  struct Registration {
    MaintenanceTask* task = nullptr;
    std::size_t index = 0;
  };

  static constexpr auto npos = std::numeric_limits<std::size_t>::max();

  [[nodiscard]] static auto index_capacity(std::size_t capacity) noexcept
      -> std::size_t {
    if (capacity == 0) {
      return 0;
    }
    const auto maximum = std::numeric_limits<std::size_t>::max();
    return capacity > maximum - capacity ? capacity : capacity * 2;
  }

  [[nodiscard]] static auto word_count(std::size_t capacity) noexcept
      -> std::size_t {
    return capacity / 64u + (capacity % 64u == 0 ? 0u : 1u);
  }

  [[nodiscard]] auto home(const MaintenanceTask& task) const noexcept
      -> std::size_t {
    auto value = reinterpret_cast<std::uintptr_t>(&task);
    value >>= 3u;
    value ^= value >> 17u;
    if constexpr (sizeof(value) >= sizeof(std::uint64_t)) {
      value *= std::uintptr_t{0x9e3779b97f4a7c15ULL};
    } else {
      value *= std::uintptr_t{0x9e3779b9U};
    }
    return static_cast<std::size_t>(value) % registrations_.size();
  }

  [[nodiscard]] auto next_registration(std::size_t slot) const noexcept
      -> std::size_t {
    return slot + 1 == registrations_.size() ? 0 : slot + 1;
  }

  [[nodiscard]] auto find_index(const MaintenanceTask& task) const noexcept
      -> std::size_t {
    if (registrations_.empty()) {
      return npos;
    }
    auto slot = home(task);
    for (std::size_t probed = 0; probed < registrations_.size(); ++probed) {
      if (registrations_[slot].task == nullptr) {
        return npos;
      }
      if (registrations_[slot].task == &task) {
        return registrations_[slot].index;
      }
      slot = next_registration(slot);
    }
    return npos;
  }

  [[nodiscard]] auto drain(MaintenanceBudget& budget) -> bool {
    for (;;) {
      auto executed = false;
      for (std::size_t word_index = 0; word_index < pending_.size();
           ++word_index) {
        auto word = pending_[word_index].exchange(0, std::memory_order_acquire);
        while (word != 0) {
          if (budget.remaining() == 0) {
            pending_[word_index].fetch_or(word, std::memory_order_release);
            return true;
          }
          const auto bit = static_cast<std::size_t>(std::countr_zero(word));
          const auto index = word_index * 64u + bit;
          const auto mask = std::uint64_t{1} << bit;
          word &= ~mask;
          executed = true;
#if TESS_HAS_EXCEPTIONS
          try {
            if (!run_task(*tasks_[index], budget)) {
              if (word != 0) {
                pending_[word_index].fetch_or(word, std::memory_order_release);
              }
              return false;
            }
          } catch (...) {
            if (word != 0) {
              pending_[word_index].fetch_or(word, std::memory_order_release);
            }
            throw;
          }
#else
          if (!run_task(*tasks_[index], budget)) {
            if (word != 0) {
              pending_[word_index].fetch_or(word, std::memory_order_release);
            }
            return false;
          }
#endif
        }
      }
      if (!executed) {
        return true;
      }
    }
  }

  [[nodiscard]] auto run_task(MaintenanceTask& task, MaintenanceBudget& budget)
      -> bool {
    metrics_.record_execution();
    const auto before = budget.remaining();
    struct ActiveRunGuard {
      DirtyBitScheduler*& active;
      DirtyBitScheduler* previous;
      ~ActiveRunGuard() { active = previous; }
    };
    const auto previous = active_scheduler_;
    active_scheduler_ = this;
    const auto guard = ActiveRunGuard{active_scheduler_, previous};
    running_task_scheduled_ = false;
    task.run(budget);
    return budget.remaining() != before || !running_task_scheduled_;
  }

  detail::MetricsStore metrics_;
  std::mutex setup_mutex_;
  std::mutex run_mutex_;
  std::vector<MaintenanceTask*> tasks_;
  std::vector<Registration> registrations_;
  std::vector<std::atomic<std::uint64_t>> pending_;
  std::size_t registered_ = 0;
  std::atomic<bool> sealed_ = false;
  bool running_task_scheduled_ = false;
  inline static thread_local DirtyBitScheduler* active_scheduler_ = nullptr;
};

/// Bounded non-deduplicating queue used as the amplification baseline.
using FifoScheduler = detail::QueuedScheduler<false>;

/// Bounded queue that retains at most one pending entry per task.
using CoalescingScheduler = detail::QueuedScheduler<true>;

}  // namespace tess::experimental::maintenance
