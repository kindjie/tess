#pragma once

#include <tess/core/assert.h>
#include <tess/diagnostics/diagnostics.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <source_location>
#include <type_traits>
#include <utility>
#include <vector>

namespace tess {

/// Generation-stamped handle for one cooperative asynchronous result.
struct AsyncTicket {
  std::uint32_t index = 0;
  std::uint64_t generation = 0;

  friend constexpr bool operator==(AsyncTicket lhs,
                                   AsyncTicket rhs) noexcept = default;
};

/// Caller-defined version stamp attached to requirements and results.
struct AsyncVersion {
  std::uint64_t value = 0;

  constexpr AsyncVersion() noexcept = default;
  constexpr AsyncVersion(std::uint64_t version) noexcept : value(version) {}

  friend constexpr bool operator==(AsyncVersion lhs,
                                   AsyncVersion rhs) noexcept = default;
};

/// Full caller-visible lifecycle of a resumable result ticket.
enum class AsyncResultState : std::uint8_t {
  Unbound,
  Immediate,
  Pending,
  Ready,
  Failed,
  Cancelled,
  Superseded,
  Stale,
};

/// Deterministic item allowance shared by one queue advance.
struct AsyncWorkBudget {
  std::uint32_t max_items = 0;
};

/// States a continuation may report after one bounded step.
enum class AsyncStepState : std::uint8_t {
  Pending,
  Ready,
  Failed,
  Stale,
};

/// Reports progress and the version produced by one continuation step.
struct AsyncWorkStep {
  AsyncStepState state = AsyncStepState::Pending;
  std::uint32_t items_done = 0;
  AsyncVersion result_version{};
};

/**
 * Summarizes one deterministic FIFO pass over resumable work.
 *
 * `invoked` and `items_done` describe work performed by this `advance()` call.
 * The state counts describe the whole queue after that pass.
 * `failed` is the aggregate terminal-negative count: `Failed`, `Cancelled`,
 * and `Superseded` tickets all contribute to it; inspect individual ticket
 * state when that distinction matters.
 */
struct AsyncAdvanceStats {
  std::uint32_t invoked = 0;
  std::uint32_t items_done = 0;
  std::uint32_t pending = 0;
  std::uint32_t ready = 0;
  std::uint32_t failed = 0;
  std::uint32_t stale = 0;
};

/// Owns versioned result slots and advances caller-owned continuations.
///
/// This queue is cooperative rather than internally threaded: `advance`
/// invokes pending work in submission order and bounds both reported items and
/// callback invocations by the supplied deterministic item budget. Queue-state
/// summaries still scan all retained tickets, so callers should `clear` old
/// batches instead of treating the queue as an unbounded history. A callback
/// and its context are non-owning and must outlive the ticket. Tickets remain
/// observable across calls until `clear`, which invalidates them by generation.
/// Submission order is strict: with a one-invocation budget, a front
/// continuation that repeatedly reports zero progress can starve later work.
/// Choose a larger invocation budget or terminalize the stalled ticket when
/// fairness is required.
/// Callbacks may inspect the queue but must not mutate it or call `advance`;
/// those reentrant operations are rejected. Instances are externally
/// synchronized. If a callback throws, the exception propagates and its slot
/// remains `Pending`; any mutations it already made to the result value remain,
/// and a later `advance` retries that callback. Earlier completed slots retain
/// their terminal states.
template <typename T>
class ResumableWorkQueue {
  static_assert(std::is_default_constructible_v<T>);

 public:
  using WorkFn = AsyncWorkStep (*)(void*, AsyncWorkBudget, T&);

  ResumableWorkQueue() = default;
  ResumableWorkQueue(const ResumableWorkQueue&) = delete;
  auto operator=(const ResumableWorkQueue&) -> ResumableWorkQueue& = delete;
  ResumableWorkQueue(ResumableWorkQueue&&) = delete;
  auto operator=(ResumableWorkQueue&&) -> ResumableWorkQueue& = delete;
  ~ResumableWorkQueue() = default;

  /**
   * Attaches caller-owned flow accounting; null detaches.
   *
   * Attach or detach only while the queue is empty so the retention
   * identity starts true; the accountant must outlive the attachment.
   */
  void set_flow_accounting(diagnostics::FlowAccounting* accounting) noexcept {
    TESS_ASSERT(slots_.empty());
    accounting_ = accounting;
  }

  /**
   * Observes one monotonic simulation tick for time accounting.
   *
   * Call once per tick before that tick's transitions: weights the
   * outstanding inventory by elapsed ticks and refreshes the oldest
   * outstanding age from per-slot submission stamps.
   */
  void observe_flow_tick(std::uint64_t tick) noexcept {
    if (accounting_ == nullptr) {
      return;
    }
    accounting_->observe_tick(tick);
    const auto now = accounting_->last_observed_tick;
    auto oldest = now;
    auto any_pending = false;
    for (const auto& slot : slots_) {
      if (slot.state == AsyncResultState::Pending) {
        any_pending = true;
        oldest = slot.submitted_tick < oldest ? slot.submitted_tick : oldest;
      }
    }
    accounting_->counters.oldest_outstanding_age_ticks =
        any_pending ? now - oldest : 0;
  }

  void reserve_tickets(std::size_t count) {
    if (reject_reentrant_mutation()) {
      return;
    }
    slots_.reserve(count);
  }

  template <typename Work>
  [[nodiscard]] auto submit(
      Work& work, AsyncVersion required_version = {},
      std::source_location source = std::source_location::current())
      -> AsyncTicket {
    static_assert(
        std::is_invocable_r_v<AsyncWorkStep, Work&, AsyncWorkBudget, T&>);
    return submit(
        static_cast<void*>(&work),
        [](void* context, AsyncWorkBudget budget, T& value) -> AsyncWorkStep {
          return (*static_cast<Work*>(context))(budget, value);
        },
        required_version, source);
  }

  [[nodiscard]] auto submit(
      void* context, WorkFn work, AsyncVersion required_version = {},
      std::source_location source = std::source_location::current())
      -> AsyncTicket {
    if (reject_reentrant_mutation()) {
      account_rejected_offer();
      return {};
    }
    TESS_ASSERT(work != nullptr);
    TESS_ASSERT(slots_.size() <= std::numeric_limits<std::uint32_t>::max());
    const auto index = static_cast<std::uint32_t>(slots_.size());
    slots_.push_back(Slot{});
    auto& slot = slots_.back();
    slot.context = context;
    slot.work = work;
    slot.required_version = required_version;
    slot.source = source;
    slot.state = AsyncResultState::Pending;
    if (accounting_ != nullptr) {
      ++accounting_->counters.offered;
      accounting_->record_admitted();
      slot.submitted_tick = accounting_->last_observed_tick;
    }
    return AsyncTicket{index, generation_};
  }

  [[nodiscard]] auto submit_immediate(
      T value, AsyncVersion result_version = {},
      std::source_location source = std::source_location::current())
      -> AsyncTicket {
    if (reject_reentrant_mutation()) {
      account_rejected_offer();
      return {};
    }
    TESS_ASSERT(slots_.size() <= std::numeric_limits<std::uint32_t>::max());
    const auto index = static_cast<std::uint32_t>(slots_.size());
    // Build the slot before storing it so a throwing value move admits
    // nothing and leaves no Unbound slot behind.
    auto slot = Slot{};
    slot.value = std::move(value);
    slot.result_version = result_version;
    slot.source = source;
    slot.state = AsyncResultState::Immediate;
    slots_.push_back(std::move(slot));
    if (accounting_ != nullptr) {
      auto& counters = accounting_->counters;
      ++counters.offered;
      ++counters.admitted;
      ++counters.completed;
    }
    return AsyncTicket{index, generation_};
  }

  [[nodiscard]] auto advance(AsyncWorkBudget budget) -> AsyncAdvanceStats {
    if (in_advance_) {
      TESS_ASSERT_MSG(!in_advance_, "reentrant queue advance");
      return {};
    }
    struct AdvanceGuard {
      bool& active;
      // Exception propagation is intentional. This guard restores the
      // reentrancy sentinel while leaving the throwing slot Pending so the
      // caller may inspect state and choose whether to retry or fail it.
      ~AdvanceGuard() { active = false; }
    };
    in_advance_ = true;
    const auto guard = AdvanceGuard{in_advance_};
    auto stats = AsyncAdvanceStats{};
    if (accounting_ != nullptr) {
      accounting_->counters.offered_work_units += budget.max_items;
    }
    auto remaining = budget.max_items;
    auto invocations_remaining = budget.max_items;
    for (auto& slot : slots_) {
      if (remaining == 0 || invocations_remaining == 0) {
        break;
      }
      if (slot.state != AsyncResultState::Pending) {
        continue;
      }
      TESS_ASSERT(slot.work != nullptr);
      const auto step =
          slot.work(slot.context, AsyncWorkBudget{remaining}, slot.value);
      ++stats.invoked;
      --invocations_remaining;
      TESS_ASSERT(step.items_done <= remaining);
      if (step.items_done > remaining) {
        slot.state = AsyncResultState::Failed;
        account_terminal(slot);
        continue;
      }
      remaining -= step.items_done;
      if (accounting_ != nullptr) {
        // Committed per step: a later callback's exception must not
        // discard work already consumed in this advance.
        accounting_->counters.consumed_work_units += step.items_done;
      }
      slot.result_version = step.result_version;
      switch (step.state) {
        case AsyncStepState::Pending:
          break;
        case AsyncStepState::Ready:
          slot.state = AsyncResultState::Ready;
          account_terminal(slot);
          break;
        case AsyncStepState::Failed:
          slot.state = AsyncResultState::Failed;
          account_terminal(slot);
          break;
        case AsyncStepState::Stale:
          slot.state = AsyncResultState::Stale;
          account_terminal(slot);
          break;
      }
      stats.items_done += step.items_done;
    }
    summarize_states(stats);
    return stats;
  }

  [[nodiscard]] auto state(AsyncTicket ticket) const noexcept
      -> AsyncResultState {
    const auto* slot = find(ticket);
    return slot == nullptr ? AsyncResultState::Unbound : slot->state;
  }

  /**
   * Borrows a completed value until queue storage may move.
   *
   * A later `submit` or `submit_immediate` can grow the slot vector and
   * invalidate the pointer. `clear` also invalidates it. Copy or consume the
   * value before mutating the queue.
   */
  [[nodiscard]] auto result(AsyncTicket ticket) const noexcept -> const T* {
    const auto* slot = find(ticket);
    if (slot == nullptr || (slot->state != AsyncResultState::Immediate &&
                            slot->state != AsyncResultState::Ready)) {
      return nullptr;
    }
    return &slot->value;
  }

  [[nodiscard]] auto required_version(AsyncTicket ticket) const noexcept
      -> AsyncVersion {
    const auto* slot = find(ticket);
    return slot == nullptr ? AsyncVersion{} : slot->required_version;
  }

  [[nodiscard]] auto result_version(AsyncTicket ticket) const noexcept
      -> AsyncVersion {
    const auto* slot = find(ticket);
    return slot == nullptr ? AsyncVersion{} : slot->result_version;
  }

  [[nodiscard]] auto source(AsyncTicket ticket) const noexcept
      -> std::source_location {
    const auto* slot = find(ticket);
    return slot == nullptr ? std::source_location{} : slot->source;
  }

  /**
   * Moves a `Pending` ticket to `Cancelled`.
   *
   * The four terminal setters below all answer one question: **did this
   * call change the state?** `false` therefore covers three unrelated
   * situations, and the queue already exposes which one applies —
   * `state(ticket)` returns `Unbound` for a ticket this queue never
   * issued or has since retired, and the slot's own state for one that
   * was already terminal. A caller that needs to tell them apart asks
   * `state(ticket)` rather than reading it out of the `bool`:
   *
   * - the ticket is unknown or retired (`state()` is `Unbound`);
   * - the slot is no longer `Pending`, so some earlier call already
   *   settled it (`state()` names that outcome);
   * - the call arrived during `advance()`, which is a contract
   *   violation, not an outcome. Assertions abort on it; a release build
   *   returns `false` and changes nothing.
   */
  [[nodiscard]] bool cancel(AsyncTicket ticket) noexcept {
    return set_terminal(ticket, AsyncResultState::Cancelled);
  }

  /// Moves a `Pending` ticket to `Superseded`; see `cancel` for `false`.
  [[nodiscard]] bool supersede(AsyncTicket ticket) noexcept {
    return set_terminal(ticket, AsyncResultState::Superseded);
  }

  /// Moves a `Pending` ticket to `Failed`; see `cancel` for `false`.
  [[nodiscard]] bool fail(AsyncTicket ticket) noexcept {
    return set_terminal(ticket, AsyncResultState::Failed);
  }

  /// Moves a `Pending` ticket to `Stale`; see `cancel` for `false`.
  [[nodiscard]] bool mark_stale(AsyncTicket ticket) noexcept {
    return set_terminal(ticket, AsyncResultState::Stale);
  }

  /**
   * Reclassifies a settled result as `Stale` when its version does not
   * match `current`.
   *
   * The comparison is equality, not ordering: a result stamped *ahead* of
   * `current` is marked stale too.
   *
   * The state rule is the opposite of the setters above: this one acts on
   * `Immediate` or `Ready` slots and refuses `Pending` ones. `false` adds
   * a fourth case to their three, and it is the ordinary one — the
   * result's version already equals `current`, so there is nothing to
   * reclassify. That is a no-change outcome rather than a failure, and a
   * caller that retries on it will retry forever; compare
   * `result_version(ticket)` if the distinction matters.
   */
  [[nodiscard]] bool mark_stale_if_version(AsyncTicket ticket,
                                           AsyncVersion current) noexcept {
    if (reject_reentrant_mutation()) {
      return false;
    }
    auto* slot = find(ticket);
    if (slot == nullptr ||
        (slot->state != AsyncResultState::Immediate &&
         slot->state != AsyncResultState::Ready) ||
        slot->result_version == current) {
      return false;
    }
    slot->state = AsyncResultState::Stale;
    if (accounting_ != nullptr) {
      // Reclassification, not a second terminal outcome: the result
      // completed earlier and its residence was already recorded, so
      // only the buckets swap. `completed` is non-monotonic here by
      // documented design.
      auto& counters = accounting_->counters;
      if (counters.completed > 0) {
        --counters.completed;
      }
      ++counters.stale;
    }
    return true;
  }

  [[nodiscard]] auto size() const noexcept -> std::size_t {
    return slots_.size();
  }

  [[nodiscard]] auto generation() const noexcept -> std::uint64_t {
    return generation_;
  }

  void clear() noexcept {
    if (reject_reentrant_mutation()) {
      return;
    }
    if (accounting_ != nullptr) {
      for (auto& slot : slots_) {
        if (slot.state == AsyncResultState::Pending) {
          ++accounting_->counters.dropped_after_admission;
          close_outstanding(slot);
        }
      }
    }
    slots_.clear();
    ++generation_;
    if (generation_ == 0) {
      ++generation_;
    }
  }

 private:
  struct Slot {
    T value{};
    void* context = nullptr;
    WorkFn work = nullptr;
    AsyncVersion required_version{};
    AsyncVersion result_version{};
    std::source_location source = std::source_location::current();
    AsyncResultState state = AsyncResultState::Unbound;
    std::uint64_t submitted_tick = 0;
  };

  [[nodiscard]] auto reject_reentrant_mutation() const noexcept -> bool {
    if (!in_advance_) {
      return false;
    }
    TESS_ASSERT_MSG(!in_advance_, "queue mutation during advance");
    return true;
  }

  void account_rejected_offer() noexcept {
    if (accounting_ != nullptr) {
      ++accounting_->counters.offered;
      ++accounting_->counters.rejected;
    }
  }

  [[nodiscard]] auto find(AsyncTicket ticket) noexcept -> Slot* {
    if (ticket.generation != generation_ || ticket.index >= slots_.size()) {
      return nullptr;
    }
    return &slots_[ticket.index];
  }

  [[nodiscard]] auto find(AsyncTicket ticket) const noexcept -> const Slot* {
    if (ticket.generation != generation_ || ticket.index >= slots_.size()) {
      return nullptr;
    }
    return &slots_[ticket.index];
  }

  [[nodiscard]] bool set_terminal(AsyncTicket ticket,
                                  AsyncResultState state) noexcept {
    if (reject_reentrant_mutation()) {
      return false;
    }
    auto* slot = find(ticket);
    if (slot == nullptr || slot->state != AsyncResultState::Pending) {
      return false;
    }
    slot->state = state;
    account_terminal(*slot);
    return true;
  }

  /// Buckets one just-terminalized Pending slot and closes its
  /// outstanding entry. Immediate results are accounted at submission.
  void account_terminal(Slot& slot) noexcept {
    if (accounting_ == nullptr) {
      return;
    }
    auto& counters = accounting_->counters;
    switch (slot.state) {
      case AsyncResultState::Ready:
        ++counters.completed;
        break;
      case AsyncResultState::Failed:
        ++counters.failed;
        break;
      case AsyncResultState::Cancelled:
        ++counters.cancelled;
        break;
      case AsyncResultState::Superseded:
        ++counters.superseded;
        break;
      case AsyncResultState::Stale:
        ++counters.stale;
        break;
      case AsyncResultState::Unbound:
      case AsyncResultState::Immediate:
      case AsyncResultState::Pending:
        return;
    }
    close_outstanding(slot);
  }

  void close_outstanding(const Slot& slot) noexcept {
    accounting_->record_left_outstanding();
    accounting_->counters.residence_ticks_accumulated +=
        accounting_->last_observed_tick - slot.submitted_tick;
  }

  void summarize_states(AsyncAdvanceStats& stats) const noexcept {
    for (const auto& slot : slots_) {
      switch (slot.state) {
        case AsyncResultState::Pending:
          ++stats.pending;
          break;
        case AsyncResultState::Immediate:
        case AsyncResultState::Ready:
          ++stats.ready;
          break;
        case AsyncResultState::Failed:
        case AsyncResultState::Cancelled:
        case AsyncResultState::Superseded:
          ++stats.failed;
          break;
        case AsyncResultState::Stale:
          ++stats.stale;
          break;
        case AsyncResultState::Unbound:
          break;
      }
    }
  }

  std::vector<Slot> slots_;
  std::uint64_t generation_ = 1;
  bool in_advance_ = false;
  diagnostics::FlowAccounting* accounting_ = nullptr;
};

}  // namespace tess
