#pragma once

#include <tess/core/shape.h>
#include <tess/experimental/registered_maintenance.h>
#include <tess/storage/chunk_meta.h>
#include <tess/storage/residency.h>
#include <tess/storage/sparse_world.h>
#include <tess/storage/world.h>

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace tess::experimental::maintenance {

/** Result of an authoritative dirty mark and its maintenance offer. */
enum class ChunkMarkResult : std::uint8_t {
  Accepted,
  CapacityExhausted,
  Stalled,
  InvalidMask,
  Missing,
  Released,
};

/** Freshness of one adapter-owned derived chunk product. */
enum class ChunkProductState : std::uint8_t {
  Unavailable,
  Stale,
  Current,
};

/** Version and residency identity of one completed derived product. */
struct ChunkProductToken {
  ChunkKey key{};
  std::uint32_t version = 0;
  std::uint64_t residency_generation = 0;

  friend constexpr auto operator==(ChunkProductToken,
                                   ChunkProductToken) noexcept
      -> bool = default;
};

/** Borrowed adapter product plus its explicit freshness classification. */
template <typename Product>
struct ChunkProductView {
  const Product* value = nullptr;
  ChunkProductToken token{};
  ChunkProductState state = ChunkProductState::Unavailable;
};

/**
 * Result of an adapter-owned sparse residency acquisition or reconciliation.
 */
enum class ChunkResidencyStatus : std::uint8_t {
  Ready,
  Missing,
  NotIdle,
  Released,
};

/** Sparse residency acquisition result and new residency identity. */
struct ChunkResidencyResult {
  ChunkResidencyStatus status = ChunkResidencyStatus::Missing;
  ResidencyHandle handle{};
};

/** Result of an adapter-owned sparse eviction. */
enum class ChunkEvictionResult : std::uint8_t {
  Evicted,
  Missing,
  NotIdle,
  Released,
};

/** Result of retiring every fixed task owned by an adapter. */
enum class ChunkAdapterReleaseResult : std::uint8_t {
  Released,
  NotIdle,
  AlreadyReleased,
};

/**
 * External fixed-slot adapter for one derived product per chunk.
 *
 * `World` is borrowed and must outlive this immovable adapter. `Product` is
 * adapter-owned, while `Rebuild` is invoked as
 * `rebuild(const World&, ChunkKey, DirtyObservation, Product&)`. A successful
 * callback publishes a version/residency token, then clears only the observed
 * bits. The observation may have no flags when `retry()` repairs shared
 * content-version drift caused by a disjoint owner. If the generation or
 * version changed, the product remains explicitly stale and the fixed task
 * schedules another pass. Callback exceptions propagate verbatim, leave
 * the owned dirty signal or version drift intact, and require the caller to
 * use `retry()` after inspecting partial product effects.
 *
 * `owned_dirty_mask` must be nonzero and disjoint from every other clearing
 * owner for the same world. `mark_dirty()` rejects zero and foreign bits. It
 * mutates authoritative metadata before offering maintenance, so a capacity
 * failure loses no dirty signal. Scheduling is coalescing; one call does not
 * imply one execution. A disjoint owner may advance the shared content
 * version and stale this product without setting an owned bit; `retry()` then
 * rebuilds against that version without clearing the other owner's bits.
 * Failed follow-up admission is retained as adapter-owned retry debt and
 * reoffered by unbounded `flush()` or explicit `retry()`, so it cannot produce
 * a false `Idle`.
 *
 * Dense slots equal chunk keys. Sparse slots equal the world's fixed resident
 * slots and are rebound only through this adapter at a quiescent boundary.
 * Before `ensure_resident()`, `evict()`, or `reconcile_residency()`, callers
 * close and join every producer, call `flush()` until it returns a fresh
 * positive `Idle`, and keep producers closed through the transition. Once a
 * sparse adapter exists, direct world residency mutation is unsupported;
 * `reconcile_residency()` is the explicit exception for a coordinated archive
 * load or other externally owned transition. A task rechecks `resident_ref()`
 * before every unchecked sparse access, but that check does not make
 * unsynchronized world mutation safe.
 *
 * World reads and mutations are not internally synchronized. Concurrent
 * offers are supported by the registered scheduler, but callers must provide
 * the world's documented external synchronization whenever a producer or
 * drain accesses it. Residency and release operations require exclusive
 * adapter access after every producer is closed and joined. Product borrows
 * remain valid until adapter destruction, but sparse slot rebinding may
 * replace their value; copy a needed product before a residency transition.
 */
template <typename World, typename Product, typename Rebuild,
          typename Backend = DirtyBitScheduler>
  requires std::default_initializable<Product> &&
           std::invocable<Rebuild&, const World&, ChunkKey, DirtyObservation,
                          Product&>
class ChunkMaintenanceAdapter final {
  static constexpr bool is_dense =
      std::same_as<typename World::residency_type, AlwaysResident>;
  static constexpr bool is_sparse =
      std::same_as<typename World::residency_type, SparseResident>;
  static_assert(is_dense || is_sparse,
                "ChunkMaintenanceAdapter supports tess dense and sparse "
                "worlds only");

  struct Task final : MaintenanceTask {
    ChunkMaintenanceAdapter* owner = nullptr;
    std::size_t slot = 0;

    void run(MaintenanceBudget& budget) override {
      owner->run_slot(slot, budget);
    }
  };

  struct Slot {
    Product product{};
    Task task{};
    ChunkProductToken token{};
    ChunkKey key{};
    std::uint64_t residency_generation = 0;
    bool bound = false;
    bool built = false;
    std::atomic<bool> retry_debt = false;
  };

 public:
  /**
   * Allocates fixed task, product, handle, and backend storage, then seals it.
   *
   * A zero `backend_capacity` selects the slot count. A smaller capacity is
   * useful only for queue-capacity comparison backends; fixed-registration
   * backends require enough capacity for every adapter slot.
   */
  ChunkMaintenanceAdapter(World& world, std::uint32_t owned_dirty_mask,
                          Rebuild rebuild, std::size_t backend_capacity = 0)
      : world_(&world),
        owned_dirty_mask_(owned_dirty_mask),
        rebuild_(std::move(rebuild)),
        slot_count_(world_slot_count(world)),
        slots_(std::make_unique<Slot[]>(slot_count_)),
        scheduler_(slot_count_,
                   backend_capacity == 0 ? slot_count_ : backend_capacity),
        handles_(std::make_unique<MaintenanceHandle[]>(slot_count_)) {
    if (owned_dirty_mask_ == 0) {
      ::tess::detail::fail_fast(
          "ChunkMaintenanceAdapter requires a nonzero owned dirty mask");
    }
    for (std::size_t slot = 0; slot < slot_count_; ++slot) {
      slots_[slot].task.owner = this;
      slots_[slot].task.slot = slot;
      const auto handle = scheduler_.register_task(slots_[slot].task);
      if (!handle.has_value()) {
        ::tess::detail::fail_fast(
            "ChunkMaintenanceAdapter fixed registry capacity mismatch");
      }
      handles_[slot] = handle.value();
      if constexpr (is_dense) {
        bind_slot(slot, ChunkKey{slot}, 0);
      }
    }
    if constexpr (is_sparse) {
      bind_current_sparse_residency();
    }
    scheduler_.seal();
  }

  ChunkMaintenanceAdapter(const ChunkMaintenanceAdapter&) = delete;
  auto operator=(const ChunkMaintenanceAdapter&)
      -> ChunkMaintenanceAdapter& = delete;
  ChunkMaintenanceAdapter(ChunkMaintenanceAdapter&&) = delete;
  auto operator=(ChunkMaintenanceAdapter&&)
      -> ChunkMaintenanceAdapter& = delete;

  ~ChunkMaintenanceAdapter() = default;

  /** Marks owned dirty bits and offers the corresponding fixed task. */
  [[nodiscard]] auto mark_dirty(ChunkKey key, std::uint32_t flags, Box3 bounds)
      -> ChunkMarkResult {
    if (released_) {
      return ChunkMarkResult::Released;
    }
    if (flags == 0 || (flags & ~owned_dirty_mask_) != 0) {
      return ChunkMarkResult::InvalidMask;
    }
    const auto slot = bound_slot(key);
    if (!slot.has_value()) {
      return ChunkMarkResult::Missing;
    }
    world_->mark_dirty(key, flags, bounds);
    return map_mark_result(schedule_slot(slot.value()));
  }

  /**
   * Reoffers dirty or version-stale work without changing world state.
   *
   * Empty means an out-of-bounds or currently non-resident key, or a released
   * adapter. Capacity and zero-progress outcomes remain explicit.
   */
  [[nodiscard]] auto retry(ChunkKey key) -> std::optional<ScheduleResult> {
    if (released_) {
      return std::nullopt;
    }
    const auto slot = bound_slot(key);
    if (!slot.has_value()) {
      return std::nullopt;
    }
    return schedule_slot(slot.value());
  }

  /**
   * Runs fixed tasks within `budget`; world access needs external locking.
   * Adapter retry debt is not reoffered here because a custom backend may
   * execute an accepted offer synchronously outside this budget. Retained debt
   * makes an otherwise completed drain report `BudgetExhausted`; use `retry()`
   * or unbounded `flush()` to reoffer it.
   */
  [[nodiscard]] auto run_some(MaintenanceBudget budget) -> DrainResult {
    transition_ready_.store(false, std::memory_order_release);
#if TESS_HAS_EXCEPTIONS
    try {
      const auto backend_result = scheduler_.run_some(budget);
      const auto result = finalize_drain(backend_result, {});
      transition_ready_.store(result == DrainResult::Idle,
                              std::memory_order_release);
      return result;
    } catch (...) {
      transition_ready_.store(false, std::memory_order_release);
      throw;
    }
#else
    const auto backend_result = scheduler_.run_some(budget);
    const auto result = finalize_drain(backend_result, {});
    transition_ready_.store(result == DrainResult::Idle,
                            std::memory_order_release);
    return result;
#endif
  }

  /**
   * Flushes reachable tasks; `Idle` opens the sparse transition boundary.
   * Adapter retry debt is reoffered around the drain and prevents `Idle`.
   */
  [[nodiscard]] auto flush() -> DrainResult {
    transition_ready_.store(false, std::memory_order_release);
#if TESS_HAS_EXCEPTIONS
    try {
      static_cast<void>(reoffer_retry_debt());
      const auto backend_result = scheduler_.flush();
      const auto follow_up = reoffer_retry_debt();
      const auto result = finalize_drain(backend_result, follow_up);
      transition_ready_.store(result == DrainResult::Idle,
                              std::memory_order_release);
      return result;
    } catch (...) {
      transition_ready_.store(false, std::memory_order_release);
      throw;
    }
#else
    static_cast<void>(reoffer_retry_debt());
    const auto backend_result = scheduler_.flush();
    const auto follow_up = reoffer_retry_debt();
    const auto result = finalize_drain(backend_result, follow_up);
    transition_ready_.store(result == DrainResult::Idle,
                            std::memory_order_release);
    return result;
#endif
  }

  /** Returns one product borrow and validates its version/residency token. */
  [[nodiscard]] auto product(ChunkKey key) const -> ChunkProductView<Product> {
    const auto slot_index = bound_slot(key);
    if (!slot_index.has_value()) {
      return {};
    }
    const auto& slot = slots_[slot_index.value()];
    if (!slot.built || slot.token.key != key) {
      return {};
    }
    const auto state = token_current(slot.token, slot_index.value())
                           ? ChunkProductState::Current
                           : ChunkProductState::Stale;
    return ChunkProductView<Product>{&slot.product, slot.token, state};
  }

  /** Returns whether `token` still names the current clean slot product. */
  [[nodiscard]] auto current(ChunkProductToken token) const noexcept -> bool {
    const auto slot = bound_slot(token.key);
    return slot.has_value() && slots_[slot.value()].built &&
           slots_[slot.value()].token == token &&
           token_current(token, slot.value());
  }

  /** Thread-safe monotonic scheduler diagnostics. */
  [[nodiscard]] auto metrics() const noexcept -> MaintenanceMetrics {
    return scheduler_.metrics();
  }

  /**
   * Makes a sparse key resident at an established quiescent boundary.
   *
   * The boundary stays open for a batch of adapter-owned residency changes;
   * any later maintenance offer closes it.
   */
  [[nodiscard]] auto ensure_resident(ChunkKey key) -> ChunkResidencyResult
    requires(is_sparse)
  {
    if (released_) {
      return ChunkResidencyResult{ChunkResidencyStatus::Released, {}};
    }
    if (!transition_ready_.load(std::memory_order_acquire)) {
      return ChunkResidencyResult{ChunkResidencyStatus::NotIdle, {}};
    }
    const auto handle = world_->ensure_resident(key);
    if (handle.generation == 0) {
      return ChunkResidencyResult{ChunkResidencyStatus::Missing, {}};
    }
    const auto ref = world_->resident_ref(key);
    if (ref.meta == nullptr || ref.slot >= slot_count_ ||
        ref.generation != handle.generation) {
      ::tess::detail::fail_fast(
          "ChunkMaintenanceAdapter sparse residency binding mismatch");
    }
    bind_slot(ref.slot, key, ref.generation);
    return ChunkResidencyResult{ChunkResidencyStatus::Ready, handle};
  }

  /** Evicts a sparse key at an established quiescent boundary. */
  [[nodiscard]] auto evict(ChunkKey key) -> ChunkEvictionResult
    requires(is_sparse)
  {
    if (released_) {
      return ChunkEvictionResult::Released;
    }
    if (!transition_ready_.load(std::memory_order_acquire)) {
      return ChunkEvictionResult::NotIdle;
    }
    const auto ref = world_->resident_ref(key);
    if (ref.meta == nullptr) {
      return ChunkEvictionResult::Missing;
    }
    if (!world_->evict(key)) {
      return ChunkEvictionResult::Missing;
    }
    unbind_slot(ref.slot);
    return ChunkEvictionResult::Evicted;
  }

  /**
   * Rebinds sparse slots after one coordinated external residency transition.
   *
   * This is intended for archive load. It is not permission for arbitrary
   * direct world residency mutation while producers or tasks are active.
   * Returns `Ready`, `NotIdle`, or `Released`; reconciliation cannot report
   * `Missing` because it scans the world's complete resident set.
   */
  [[nodiscard]] auto reconcile_residency() -> ChunkResidencyStatus
    requires(is_sparse)
  {
    if (released_) {
      return ChunkResidencyStatus::Released;
    }
    if (!transition_ready_.load(std::memory_order_acquire)) {
      return ChunkResidencyStatus::NotIdle;
    }
    for (std::size_t slot = 0; slot < slot_count_; ++slot) {
      unbind_slot(slot);
    }
    bind_current_sparse_residency();
    return ChunkResidencyStatus::Ready;
  }

  /**
   * Retires every task after the required fresh positive `Idle`.
   *
   * The caller first closes and joins every producer, then keeps exclusive
   * access to this adapter through the `Idle` observation and release.
   */
  [[nodiscard]] auto try_release() -> ChunkAdapterReleaseResult {
    if (released_) {
      return ChunkAdapterReleaseResult::AlreadyReleased;
    }
    if (!transition_ready_.load(std::memory_order_acquire)) {
      return ChunkAdapterReleaseResult::NotIdle;
    }
    for (std::size_t slot = 0; slot < slot_count_; ++slot) {
      const auto result = scheduler_.try_release(handles_[slot]);
      if (result != ReleaseResult::Released) {
        return ChunkAdapterReleaseResult::NotIdle;
      }
    }
    released_ = true;
    transition_ready_.store(false, std::memory_order_release);
    return ChunkAdapterReleaseResult::Released;
  }

 private:
  [[nodiscard]] static auto world_slot_count(const World& world)
      -> std::size_t {
    if constexpr (is_dense) {
      static_assert(
          World::chunk_count <=
          static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()));
      return static_cast<std::size_t>(World::chunk_count);
    } else {
      return world.capacity();
    }
  }

  [[nodiscard]] auto bound_slot(ChunkKey key) const noexcept
      -> std::optional<std::size_t> {
    if constexpr (is_dense) {
      if (key.value >= World::chunk_count) {
        return std::nullopt;
      }
      return static_cast<std::size_t>(key.value);
    } else {
      const auto ref = world_->resident_ref(key);
      if (ref.meta == nullptr || ref.slot >= slot_count_) {
        return std::nullopt;
      }
      const auto& slot = slots_[ref.slot];
      if (!slot.bound || slot.key != key ||
          slot.residency_generation != ref.generation) {
        return std::nullopt;
      }
      return ref.slot;
    }
  }

  void bind_slot(std::size_t slot_index, ChunkKey key,
                 std::uint64_t generation) noexcept {
    auto& slot = slots_[slot_index];
    if (!slot.bound || slot.key != key ||
        slot.residency_generation != generation) {
      slot.built = false;
      slot.retry_debt.store(false, std::memory_order_release);
    }
    slot.bound = true;
    slot.key = key;
    slot.residency_generation = generation;
  }

  void unbind_slot(std::size_t slot_index) noexcept {
    auto& slot = slots_[slot_index];
    slot.bound = false;
    slot.built = false;
    slot.residency_generation = 0;
    slot.retry_debt.store(false, std::memory_order_release);
  }

  void bind_current_sparse_residency()
    requires(is_sparse)
  {
    for (const auto key : world_->resident_chunk_keys()) {
      const auto ref = world_->resident_ref(key);
      if (ref.meta == nullptr || ref.slot >= slot_count_) {
        ::tess::detail::fail_fast(
            "ChunkMaintenanceAdapter sparse residency reconciliation "
            "mismatch");
      }
      bind_slot(ref.slot, key, ref.generation);
    }
  }

  [[nodiscard]] auto schedule_slot(std::size_t slot) -> ScheduleResult {
    transition_ready_.store(false, std::memory_order_release);
    const auto claimed_debt =
        slots_[slot].retry_debt.exchange(false, std::memory_order_acq_rel);
    const auto result = scheduler_.schedule(handles_[slot]);
    if (claimed_debt && result != ScheduleResult::Accepted) {
      slots_[slot].retry_debt.store(true, std::memory_order_release);
    }
    return result;
  }

  struct RetryOfferSummary {
    bool offered = false;
    bool stalled = false;
  };

  [[nodiscard]] auto reoffer_retry_debt() -> RetryOfferSummary {
    auto summary = RetryOfferSummary{};
    for (std::size_t slot = 0; slot < slot_count_; ++slot) {
      if (!slots_[slot].retry_debt.load(std::memory_order_acquire)) {
        continue;
      }
      summary.offered = true;
      const auto result = schedule_slot(slot);
      summary.stalled = summary.stalled || result == ScheduleResult::Stalled;
    }
    return summary;
  }

  [[nodiscard]] auto has_retry_debt() const noexcept -> bool {
    for (std::size_t slot = 0; slot < slot_count_; ++slot) {
      if (slots_[slot].retry_debt.load(std::memory_order_acquire)) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] auto finalize_drain(DrainResult result,
                                    RetryOfferSummary follow_up) const noexcept
      -> DrainResult {
    if (result == DrainResult::Stalled || follow_up.stalled) {
      return DrainResult::Stalled;
    }
    if (follow_up.offered || has_retry_debt()) {
      return DrainResult::BudgetExhausted;
    }
    return result;
  }

  [[nodiscard]] static auto map_mark_result(ScheduleResult result) noexcept
      -> ChunkMarkResult {
    switch (result) {
      case ScheduleResult::Accepted:
        return ChunkMarkResult::Accepted;
      case ScheduleResult::CapacityExhausted:
        return ChunkMarkResult::CapacityExhausted;
      case ScheduleResult::Stalled:
        return ChunkMarkResult::Stalled;
    }
    ::tess::detail::fail_fast(
        "ChunkMaintenanceAdapter received an invalid schedule result");
  }

  [[nodiscard]] auto sparse_binding_current(
      const Slot& slot, std::size_t slot_index) const noexcept -> bool {
    if constexpr (is_dense) {
      static_cast<void>(slot);
      static_cast<void>(slot_index);
      return true;
    } else {
      const auto ref = world_->resident_ref(slot.key);
      return ref.meta != nullptr && ref.slot == slot_index &&
             ref.generation == slot.residency_generation;
    }
  }

  [[nodiscard]] auto token_current(ChunkProductToken token,
                                   std::size_t slot_index) const noexcept
      -> bool {
    const auto& slot = slots_[slot_index];
    if (!sparse_binding_current(slot, slot_index) || slot.key != token.key ||
        slot.residency_generation != token.residency_generation) {
      return false;
    }
    return world_->meta(token.key).version == token.version &&
           (world_->dirty_flags(token.key) & owned_dirty_mask_) == 0;
  }

  void run_slot(std::size_t slot_index, MaintenanceBudget& budget) {
    auto& slot = slots_[slot_index];
    if (!slot.bound || !budget.consume()) {
      return;
    }
    if (!sparse_binding_current(slot, slot_index)) {
      return;
    }
    const auto observed = world_->observe_dirty(slot.key, owned_dirty_mask_);
    if (observed.flags == 0 &&
        (!slot.built || slot.token.version == observed.version)) {
      slot.retry_debt.store(false, std::memory_order_release);
      return;
    }
    if (!sparse_binding_current(slot, slot_index)) {
      return;
    }
    std::invoke(rebuild_, std::as_const(*world_), slot.key, observed,
                slot.product);
    if (!sparse_binding_current(slot, slot_index)) {
      return;
    }
    slot.token = ChunkProductToken{slot.key, observed.version,
                                   slot.residency_generation};
    slot.built = true;
    if (world_->clear_dirty_observed(slot.key, observed)) {
      slot.retry_debt.store(false, std::memory_order_release);
      return;
    }
    if (schedule_slot(slot_index) != ScheduleResult::Accepted) {
      slot.retry_debt.store(true, std::memory_order_release);
    }
  }

  World* world_;
  std::uint32_t owned_dirty_mask_;
  Rebuild rebuild_;
  std::size_t slot_count_;
  std::unique_ptr<Slot[]> slots_;
  RegisteredScheduler<Backend> scheduler_;
  std::unique_ptr<MaintenanceHandle[]> handles_;
  std::atomic<bool> transition_ready_ = false;
  bool released_ = false;
};

}  // namespace tess::experimental::maintenance
