#pragma once

#include <tess/core/assert.h>

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

/// Summarizes one deterministic FIFO pass over resumable work.
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
/// invokes pending work in submission order and never gives all callbacks
/// together more than the supplied deterministic item budget. A callback and
/// its context are non-owning and must outlive the ticket. Tickets remain
/// observable across calls until `clear`, which invalidates them by generation.
/// Instances are externally synchronized.
template <typename T>
class ResumableWorkQueue {
  static_assert(std::is_default_constructible_v<T>);

 public:
  using WorkFn = AsyncWorkStep (*)(void*, AsyncWorkBudget, T&);

  void reserve_tickets(std::size_t count) { slots_.reserve(count); }

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
    return AsyncTicket{index, generation_};
  }

  [[nodiscard]] auto submit_immediate(
      T value, AsyncVersion result_version = {},
      std::source_location source = std::source_location::current())
      -> AsyncTicket {
    TESS_ASSERT(slots_.size() <= std::numeric_limits<std::uint32_t>::max());
    const auto index = static_cast<std::uint32_t>(slots_.size());
    slots_.push_back(Slot{});
    auto& slot = slots_.back();
    slot.value = std::move(value);
    slot.result_version = result_version;
    slot.source = source;
    slot.state = AsyncResultState::Immediate;
    return AsyncTicket{index, generation_};
  }

  [[nodiscard]] auto advance(AsyncWorkBudget budget) -> AsyncAdvanceStats {
    auto stats = AsyncAdvanceStats{};
    auto remaining = budget.max_items;
    for (auto& slot : slots_) {
      if (remaining == 0) {
        break;
      }
      if (slot.state != AsyncResultState::Pending) {
        continue;
      }
      TESS_ASSERT(slot.work != nullptr);
      const auto step =
          slot.work(slot.context, AsyncWorkBudget{remaining}, slot.value);
      TESS_ASSERT(step.items_done <= remaining);
      if (step.items_done > remaining) {
        slot.state = AsyncResultState::Failed;
        continue;
      }
      remaining -= step.items_done;
      slot.result_version = step.result_version;
      switch (step.state) {
        case AsyncStepState::Pending:
          break;
        case AsyncStepState::Ready:
          slot.state = AsyncResultState::Ready;
          break;
        case AsyncStepState::Failed:
          slot.state = AsyncResultState::Failed;
          break;
        case AsyncStepState::Stale:
          slot.state = AsyncResultState::Stale;
          break;
      }
      ++stats.invoked;
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

  [[nodiscard]] bool cancel(AsyncTicket ticket) noexcept {
    return set_terminal(ticket, AsyncResultState::Cancelled);
  }

  [[nodiscard]] bool supersede(AsyncTicket ticket) noexcept {
    return set_terminal(ticket, AsyncResultState::Superseded);
  }

  [[nodiscard]] bool fail(AsyncTicket ticket) noexcept {
    return set_terminal(ticket, AsyncResultState::Failed);
  }

  [[nodiscard]] bool mark_stale(AsyncTicket ticket) noexcept {
    return set_terminal(ticket, AsyncResultState::Stale);
  }

  [[nodiscard]] bool mark_stale_if_version(AsyncTicket ticket,
                                           AsyncVersion current) noexcept {
    auto* slot = find(ticket);
    if (slot == nullptr ||
        (slot->state != AsyncResultState::Immediate &&
         slot->state != AsyncResultState::Ready) ||
        slot->result_version == current) {
      return false;
    }
    slot->state = AsyncResultState::Stale;
    return true;
  }

  [[nodiscard]] auto size() const noexcept -> std::size_t {
    return slots_.size();
  }

  [[nodiscard]] auto generation() const noexcept -> std::uint64_t {
    return generation_;
  }

  void clear() noexcept {
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
  };

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
    auto* slot = find(ticket);
    if (slot == nullptr || slot->state != AsyncResultState::Pending) {
      return false;
    }
    slot->state = state;
    return true;
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
};

}  // namespace tess
