#pragma once

#include <tess/core/fail_fast.h>
#include <tess/experimental/maintenance.h>

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <type_traits>

namespace tess::experimental::maintenance {

/**
 * Outcome of offering one valid registered task to a backend.
 *
 * `Accepted` means the offer was synchronously executed, retained, or
 * coalesced without loss on normal return. If a callback later throws from the
 * same synchronous call-local invocation, the exception disposition described
 * by `MaintenanceBackend` applies. `CapacityExhausted` means no work state was
 * retained for that offer, so the caller may retry. `Stalled` means the offer
 * ran but could not make safe forward progress; authoritative dirty state
 * decides the retry.
 */
enum class ScheduleResult : std::uint8_t {
  Accepted,
  CapacityExhausted,
  Stalled,
};

/**
 * Backend-level drain outcome before the facade observes pending work.
 *
 * `Completed` means the backend applied the requested budget/flush policy;
 * pending work is inspected separately. `Stalled` means execution made no
 * safe forward progress and caller intervention is required.
 */
enum class BackendDrainResult : std::uint8_t {
  Completed,
  Stalled,
};

/** Outcome of one explicit run or flush operation. */
enum class DrainResult : std::uint8_t {
  Idle,
  Drained,
  BudgetExhausted,
  Stalled,
};

/** Outcome of checked task-registration retirement. */
enum class ReleaseResult : std::uint8_t {
  Released,
  InvalidHandle,
  NotIdle,
};

/**
 * Structural backend boundary used by the registered contract candidate.
 *
 * The facade serializes `run_some()` and `flush()` calls with each other, but
 * `schedule()` may run concurrently on many producer threads and concurrently
 * with a drain. `schedule()`, `has_pending()`, and `metrics()` must therefore
 * be thread-safe. Each accepted offer linearizes at synchronous execution or
 * at lossless retention/coalescing. `has_pending()` linearizes against
 * scheduling and draining and reports work still retained at its observation;
 * a concurrent drain may consume accepted work before `schedule()` returns.
 * A capacity result must not retain or execute that offer. Metrics counters
 * are monotonic, but their no-throw diagnostic snapshot need not be
 * transactional across fields while operations are in flight. Backends must
 * not invoke one task concurrently with itself and must permit a running
 * callback to schedule through the same facade.
 *
 * Callback exceptions propagate verbatim. The invocation that throws is
 * consumed rather than restored and its execution is counted. Offers
 * coalesced into that same synchronous call-local invocation are consumed with
 * it; every independently retained accepted offer remains reachable. The
 * caller's authoritative dirty-mask/content-version state decides whether
 * explicitly scheduling a retry is safe.
 */
template <typename Backend>
concept MaintenanceBackend =
    std::constructible_from<Backend, std::size_t> &&
    requires(Backend& backend, const Backend& const_backend,
             MaintenanceTask& task, MaintenanceBudget budget) {
      { backend.schedule(task) } -> std::same_as<ScheduleResult>;
      { backend.run_some(budget) } -> std::same_as<BackendDrainResult>;
      { backend.flush() } -> std::same_as<BackendDrainResult>;
      { const_backend.metrics() } noexcept -> std::same_as<MaintenanceMetrics>;
      { const_backend.has_pending() } noexcept -> std::same_as<bool>;
    };

/**
 * Optional paired setup hooks for a backend with its own fixed registry.
 *
 * At facade `seal()`, no-throw `register_task()` is called once for every live
 * slot in slot order, followed by exactly one no-throw backend `seal()`. A
 * false registration result is an unrecoverable facade/backend capacity
 * mismatch. After sealing, the backend retains those task identities until
 * facade destruction; release is permitted only after the facade's
 * positive-Idle gate.
 */
template <typename Backend>
concept FixedRegistrationBackend =
    MaintenanceBackend<Backend> &&
    requires(Backend& backend, MaintenanceTask& task) {
      { backend.register_task(task) } noexcept -> std::same_as<bool>;
      { backend.seal() } noexcept -> std::same_as<void>;
    };

/** Opaque identity for one fixed maintenance-task registration. */
class MaintenanceHandle {
 public:
  constexpr MaintenanceHandle() noexcept = default;

  friend constexpr auto operator==(MaintenanceHandle,
                                   MaintenanceHandle) noexcept
      -> bool = default;

 private:
  template <typename Backend>
  friend class RegisteredScheduler;

  constexpr MaintenanceHandle(std::uint64_t owner_epoch, std::size_t slot,
                              std::uint64_t slot_generation) noexcept
      : owner_epoch_(owner_epoch),
        slot_(slot),
        slot_generation_(slot_generation) {}

  std::uint64_t owner_epoch_ = 0;
  std::size_t slot_ = std::numeric_limits<std::size_t>::max();
  std::uint64_t slot_generation_ = 0;
};

namespace detail {

inline std::atomic<std::uint64_t> next_owner_epoch = 1;
// Reuse the handle owner identity for reentrancy checks. Keeping only the
// nonzero epoch avoids retaining an object address beyond the lexical guard.
inline thread_local std::uint64_t active_registered_scheduler_epoch = 0;

[[nodiscard]] inline auto claim_owner_epoch() noexcept -> std::uint64_t {
  const auto epoch = next_owner_epoch.fetch_add(1, std::memory_order_relaxed);
  if (epoch == 0 || epoch == std::numeric_limits<std::uint64_t>::max()) {
    ::tess::detail::fail_fast(
        "RegisteredScheduler exhausted maintenance owner epochs");
  }
  return epoch;
}

template <typename Backend>
inline constexpr bool is_immediate_v =
    std::is_same_v<Backend, ImmediateScheduler>;

template <typename Backend>
inline constexpr bool is_dirty_bit_v =
    std::is_same_v<Backend, DirtyBitScheduler>;

template <typename Backend>
inline constexpr bool is_raw_backend_v =
    is_immediate_v<Backend> || is_dirty_bit_v<Backend> ||
    std::is_same_v<Backend, FifoScheduler> ||
    std::is_same_v<Backend, CoalescingScheduler>;

}  // namespace detail

/**
 * Fixed-registration handle and result contract over an experimental backend.
 *
 * Register caller-owned tasks during setup, then call `seal()` exactly once.
 * A handle belongs to one scheduler owner epoch and remains valid until
 * checked or unchecked release, or scheduler destruction. Tasks are
 * non-owning, non-movable, and must remain alive while registered.
 *
 * `try_schedule()` and `try_release()` are the checked operations for
 * expected stale-handle uncertainty. Their unchecked counterparts fail fast
 * on wrong-owner, stale, or lifecycle misuse in every build. Release never
 * cancels work. After sealing it requires a separate `Idle` observation made
 * after the last successful schedule; that observation means only that the
 * scheduler has no reachable execution. It does not prove that external dirty
 * state is clean or that world/residency mutation is quiescent.
 *
 * Callback exceptions propagate verbatim. The throwing invocation and its
 * synchronous call-local reentrant chain are consumed, while independently
 * retained accepted work remains reachable; callers retain authoritative
 * dirty state and explicitly decide whether retry is safe. A callback may
 * schedule through its own registered scheduler, but nested identity,
 * scheduling, drain, or lifecycle operations on a different registered
 * scheduler fail fast, independent of backend type. The no-throw `metrics()`
 * snapshot remains callable across owners. FIFO and coalescing remain
 * comparison machinery, and wrapping them grants no new execution authority.
 */
template <typename Backend>
class RegisteredScheduler final {
  static_assert(MaintenanceBackend<Backend> ||
                    detail::is_raw_backend_v<Backend>,
                "RegisteredScheduler requires a MaintenanceBackend or a "
                "built-in experimental raw backend");

  static constexpr bool has_backend_register = requires(
      Backend& backend, MaintenanceTask& task) { backend.register_task(task); };
  static constexpr bool has_backend_seal =
      requires(Backend& backend) { backend.seal(); };
  static_assert(
      detail::is_dirty_bit_v<Backend> ||
          ((!has_backend_register && !has_backend_seal) ||
           FixedRegistrationBackend<Backend>),
      "a custom maintenance backend must provide both no-throw "
      "register_task(MaintenanceTask&) -> bool and seal() -> void hooks, or "
      "neither");

  struct SlotTask final : MaintenanceTask {
    MaintenanceTask* target = nullptr;

    void run(MaintenanceBudget& budget) override {
      if (target == nullptr) {
        ::tess::detail::fail_fast(
            "RegisteredScheduler invoked a released maintenance slot");
      }
      target->run(budget);
    }
  };

  struct Slot {
    SlotTask task{};
    std::uint64_t generation = 1;
  };

  class OperationGuard {
   public:
    explicit OperationGuard(RegisteredScheduler& scheduler)
        : previous_(detail::active_registered_scheduler_epoch) {
      if (previous_ == scheduler.owner_epoch_) {
        return;
      }
      if (previous_ != 0) {
        ::tess::detail::fail_fast(
            "RegisteredScheduler nested cross-scheduler operation called "
            "from a running task");
      }
      lock_ = std::shared_lock<std::shared_mutex>{scheduler.lifecycle_mutex_};
      detail::active_registered_scheduler_epoch = scheduler.owner_epoch_;
      outer_ = true;
    }

    OperationGuard(const OperationGuard&) = delete;
    auto operator=(const OperationGuard&) -> OperationGuard& = delete;

    ~OperationGuard() {
      if (outer_) {
        detail::active_registered_scheduler_epoch = previous_;
      }
    }

   private:
    std::uint64_t previous_ = 0;
    std::shared_lock<std::shared_mutex> lock_{};
    bool outer_ = false;
  };

  class ScheduleActivityGuard {
   public:
    explicit ScheduleActivityGuard(std::atomic<std::uint64_t>& in_flight,
                                   std::mutex& observation_mutex)
        : in_flight_(in_flight), observation_mutex_(observation_mutex) {
      const auto lock = std::scoped_lock{observation_mutex_};
      const auto previous = in_flight_.fetch_add(1, std::memory_order_acq_rel);
      if (previous == std::numeric_limits<std::uint64_t>::max()) {
        ::tess::detail::fail_fast(
            "RegisteredScheduler exhausted in-flight schedule count");
      }
    }

    ScheduleActivityGuard(const ScheduleActivityGuard&) = delete;
    auto operator=(const ScheduleActivityGuard&)
        -> ScheduleActivityGuard& = delete;

    ~ScheduleActivityGuard() {
      const auto lock = std::scoped_lock{observation_mutex_};
      const auto previous = in_flight_.fetch_sub(1, std::memory_order_acq_rel);
      if (previous == 0) {
        ::tess::detail::fail_fast(
            "RegisteredScheduler in-flight schedule count underflowed");
      }
    }

   private:
    std::atomic<std::uint64_t>& in_flight_;
    std::mutex& observation_mutex_;
  };

 public:
  explicit RegisteredScheduler(std::size_t capacity)
      : RegisteredScheduler(capacity, capacity) {}

  RegisteredScheduler(std::size_t registry_capacity,
                      std::size_t backend_capacity)
      : owner_epoch_(detail::claim_owner_epoch()),
        capacity_(registry_capacity),
        slots_(std::make_unique<Slot[]>(registry_capacity)),
        backend_(backend_capacity) {}

  RegisteredScheduler(const RegisteredScheduler&) = delete;
  auto operator=(const RegisteredScheduler&) -> RegisteredScheduler& = delete;
  RegisteredScheduler(RegisteredScheduler&&) = delete;
  auto operator=(RegisteredScheduler&&) -> RegisteredScheduler& = delete;

  ~RegisteredScheduler() {
    for (std::size_t index = 0; index < capacity_; ++index) {
      auto* task = slots_[index].task.target;
      if (task == nullptr) {
        continue;
      }
      auto expected = owner_epoch_;
      if (!task->registration_epoch_.compare_exchange_strong(
              expected, 0, std::memory_order_relaxed)) {
        ::tess::detail::fail_fast(
            "RegisteredScheduler task ownership changed unexpectedly");
      }
      slots_[index].task.target = nullptr;
    }
  }

  /** Registers a task during setup; empty reports fixed capacity exhaustion. */
  [[nodiscard]] auto register_task(MaintenanceTask& task)
      -> std::optional<MaintenanceHandle> {
    reject_reentrant_lifecycle("register_task");
    const auto lock = std::unique_lock<std::shared_mutex>{lifecycle_mutex_};
    if (sealed_) {
      ::tess::detail::fail_fast(
          "RegisteredScheduler::register_task called after seal");
    }
    for (std::size_t index = 0; index < capacity_; ++index) {
      if (slots_[index].task.target == &task) {
        return make_handle(index);
      }
    }
    auto index = capacity_;
    for (std::size_t candidate = 0; candidate < capacity_; ++candidate) {
      if (slots_[candidate].task.target == nullptr) {
        index = candidate;
        break;
      }
    }
    if (index == capacity_) {
      return std::nullopt;
    }
    auto expected = std::uint64_t{0};
    if (!task.registration_epoch_.compare_exchange_strong(
            expected, owner_epoch_, std::memory_order_relaxed)) {
      ::tess::detail::fail_fast(
          "RegisteredScheduler::register_task task belongs to another "
          "scheduler or registration epoch");
    }
    slots_[index].task.target = &task;
    return make_handle(index);
  }

  /** Publishes the fixed registry and ends setup. */
  void seal() {
    reject_reentrant_lifecycle("seal");
    const auto lock = std::unique_lock<std::shared_mutex>{lifecycle_mutex_};
    if (sealed_) {
      ::tess::detail::fail_fast("RegisteredScheduler::seal called twice");
    }
    if constexpr (FixedRegistrationBackend<Backend> ||
                  detail::is_dirty_bit_v<Backend>) {
      for (std::size_t index = 0; index < capacity_; ++index) {
        if (slots_[index].task.target != nullptr &&
            !backend_.register_task(slots_[index].task)) {
          ::tess::detail::fail_fast(
              "RegisteredScheduler fixed backend registry capacity mismatch");
        }
      }
      backend_.seal();
    }
    sealed_ = true;
  }

  /** Returns whether a handle currently names a live local registration. */
  [[nodiscard]] auto valid(MaintenanceHandle handle) const -> bool {
    auto& self = const_cast<RegisteredScheduler&>(*this);
    const auto operation = OperationGuard{self};
    return self.resolve(handle) != nullptr;
  }

  /** Checked scheduling; empty means stale, foreign, or otherwise invalid. */
  [[nodiscard]] auto try_schedule(MaintenanceHandle handle)
      -> std::optional<ScheduleResult> {
    const auto operation = OperationGuard{*this};
    auto* slot = resolve(handle);
    if (slot == nullptr) {
      return std::nullopt;
    }
    require_sealed("try_schedule");
    const auto schedule_activity =
        ScheduleActivityGuard{in_flight_schedules_, observation_mutex_};
#if TESS_HAS_EXCEPTIONS
    try {
      const auto result = schedule_slot(*slot);
      if (result != ScheduleResult::CapacityExhausted) {
        invalidate_idle_observation();
      }
      return result;
    } catch (...) {
      invalidate_idle_observation();
      throw;
    }
#else
    const auto result = schedule_slot(*slot);
    if (result != ScheduleResult::CapacityExhausted) {
      invalidate_idle_observation();
    }
    return result;
#endif
  }

  /** Schedules a known-valid handle or fails fast on identity misuse. */
  [[nodiscard]] auto schedule(MaintenanceHandle handle) -> ScheduleResult {
    const auto result = try_schedule(handle);
    if (!result.has_value()) {
      ::tess::detail::fail_fast(
          "RegisteredScheduler::schedule received a stale handle from the "
          "wrong scheduler or registration epoch; use try_schedule for "
          "expected uncertainty");
    }
    return *result;
  }

  /** Runs reachable work within a shared unit budget. */
  [[nodiscard]] auto run_some(MaintenanceBudget budget) -> DrainResult {
    reject_reentrant_drain("run_some");
    const auto operation = OperationGuard{*this};
    require_sealed("run_some");
    const auto drain_lock = std::scoped_lock{drain_mutex_};
    return observe_drain([&] { return run_backend_some(budget); });
  }

  /** Runs reachable work with the backend's unbounded flush policy. */
  [[nodiscard]] auto flush() -> DrainResult {
    reject_reentrant_drain("flush");
    const auto operation = OperationGuard{*this};
    require_sealed("flush");
    const auto drain_lock = std::scoped_lock{drain_mutex_};
    return observe_drain([&] { return flush_backend(); });
  }

  /**
   * Checked release. Post-seal release requires a fresh positive Idle result.
   * It never cancels pending work and fixed capacity is not reused post-seal.
   */
  [[nodiscard]] auto try_release(MaintenanceHandle handle) -> ReleaseResult {
    reject_reentrant_lifecycle("try_release");
    const auto lock = std::unique_lock<std::shared_mutex>{lifecycle_mutex_};
    auto* slot = resolve(handle);
    if (slot == nullptr) {
      return ReleaseResult::InvalidHandle;
    }
    if (sealed_ && (idle_epoch_.load(std::memory_order_acquire) !=
                        activity_epoch_.load(std::memory_order_acquire) ||
                    backend_.has_pending())) {
      return ReleaseResult::NotIdle;
    }
    auto* task = slot->task.target;
    auto expected = owner_epoch_;
    if (!task->registration_epoch_.compare_exchange_strong(
            expected, 0, std::memory_order_relaxed)) {
      ::tess::detail::fail_fast(
          "RegisteredScheduler::try_release task ownership changed");
    }
    slot->task.target = nullptr;
    ++slot->generation;
    if (slot->generation == 0) {
      ++slot->generation;
    }
    return ReleaseResult::Released;
  }

  /** Releases a known-live registration or fails fast on lifecycle misuse. */
  void release(MaintenanceHandle handle) {
    switch (try_release(handle)) {
      case ReleaseResult::Released:
        return;
      case ReleaseResult::InvalidHandle:
        ::tess::detail::fail_fast(
            "RegisteredScheduler::release received a stale maintenance "
            "handle; use try_release for expected uncertainty");
      case ReleaseResult::NotIdle:
        ::tess::detail::fail_fast(
            "RegisteredScheduler::release requires a positive Idle result "
            "after the last successful schedule; release never cancels");
    }
    ::tess::detail::fail_fast("RegisteredScheduler::release invalid result");
  }

  /** Thread-safe read-only snapshot; permitted from any callback owner. */
  [[nodiscard]] auto metrics() const noexcept -> MaintenanceMetrics {
    return backend_.metrics();
  }

 private:
  [[nodiscard]] auto make_handle(std::size_t index) const noexcept
      -> MaintenanceHandle {
    return MaintenanceHandle{owner_epoch_, index, slots_[index].generation};
  }

  [[nodiscard]] auto resolve(MaintenanceHandle handle) noexcept -> Slot* {
    if (handle.owner_epoch_ != owner_epoch_ || handle.slot_ >= capacity_) {
      return nullptr;
    }
    auto& slot = slots_[handle.slot_];
    if (slot.task.target == nullptr ||
        slot.generation != handle.slot_generation_) {
      return nullptr;
    }
    return &slot;
  }

  void require_sealed(const char* operation) const {
    if (sealed_) {
      return;
    }
    if (operation == nullptr) {
      ::tess::detail::fail_fast("RegisteredScheduler used before seal");
    }
    ::tess::detail::fail_fast(
        "RegisteredScheduler operation called before seal");
  }

  void reject_reentrant_lifecycle(const char* operation) const {
    if (detail::active_registered_scheduler_epoch == 0) {
      return;
    }
    static_cast<void>(operation);
    ::tess::detail::fail_fast(
        "RegisteredScheduler lifecycle mutation called from a running task");
  }

  void reject_reentrant_drain(const char* operation) const {
    if (detail::active_registered_scheduler_epoch != owner_epoch_) {
      return;
    }
    static_cast<void>(operation);
    ::tess::detail::fail_fast(
        "RegisteredScheduler drain called from a running task");
  }

  void invalidate_idle_observation() noexcept {
    const auto previous =
        activity_epoch_.fetch_add(1, std::memory_order_acq_rel);
    if (previous == std::numeric_limits<std::uint64_t>::max()) {
      ::tess::detail::fail_fast(
          "RegisteredScheduler exhausted activity epochs");
    }
    idle_epoch_.store(std::numeric_limits<std::uint64_t>::max(),
                      std::memory_order_release);
  }

  [[nodiscard]] auto schedule_slot(Slot& slot) -> ScheduleResult {
    if constexpr (MaintenanceBackend<Backend>) {
      return backend_.schedule(slot.task);
    } else {
      const auto accepted = backend_.schedule(slot.task);
      if (accepted) {
        return ScheduleResult::Accepted;
      }
      if constexpr (detail::is_immediate_v<Backend>) {
        return ScheduleResult::Stalled;
      }
      if constexpr (detail::is_dirty_bit_v<Backend>) {
        ::tess::detail::fail_fast(
            "RegisteredScheduler dirty-bit backend rejected a live sealed "
            "registration");
      }
      return ScheduleResult::CapacityExhausted;
    }
  }

  [[nodiscard]] auto run_backend_some(MaintenanceBudget budget)
      -> BackendDrainResult {
    if constexpr (MaintenanceBackend<Backend>) {
      return backend_.run_some(budget);
    } else {
      return backend_.run_some(budget) ? BackendDrainResult::Completed
                                       : BackendDrainResult::Stalled;
    }
  }

  [[nodiscard]] auto flush_backend() -> BackendDrainResult {
    if constexpr (MaintenanceBackend<Backend>) {
      return backend_.flush();
    } else {
      return backend_.flush() ? BackendDrainResult::Completed
                              : BackendDrainResult::Stalled;
    }
  }

  template <typename Drain>
  [[nodiscard]] auto observe_drain(Drain&& drain) -> DrainResult {
    idle_epoch_.store(std::numeric_limits<std::uint64_t>::max(),
                      std::memory_order_release);
    const auto activity_before =
        activity_epoch_.load(std::memory_order_acquire);
    const auto in_flight_before =
        in_flight_schedules_.load(std::memory_order_acquire);
    const auto pending_before = backend_.has_pending();
    const auto completed = drain();
    const auto pending_after = backend_.has_pending();
    const auto observation_lock = std::scoped_lock{observation_mutex_};
    const auto in_flight_after =
        in_flight_schedules_.load(std::memory_order_acquire);
    const auto activity_after = activity_epoch_.load(std::memory_order_acquire);
    if (completed == BackendDrainResult::Stalled) {
      return DrainResult::Stalled;
    }
    if (pending_after) {
      return DrainResult::BudgetExhausted;
    }
    if (!pending_before && in_flight_before == 0 && in_flight_after == 0 &&
        activity_before == activity_after) {
      idle_epoch_.store(activity_after, std::memory_order_release);
      return DrainResult::Idle;
    }
    return DrainResult::Drained;
  }

  const std::uint64_t owner_epoch_;
  const std::size_t capacity_;
  std::unique_ptr<Slot[]> slots_;
  Backend backend_;
  mutable std::shared_mutex lifecycle_mutex_;
  std::mutex observation_mutex_;
  std::mutex drain_mutex_;
  std::atomic<std::uint64_t> activity_epoch_ = 0;
  std::atomic<std::uint64_t> in_flight_schedules_ = 0;
  std::atomic<std::uint64_t> idle_epoch_ =
      std::numeric_limits<std::uint64_t>::max();
  bool sealed_ = false;
};

}  // namespace tess::experimental::maintenance
