#pragma once

#include <tess/core/assert.h>
#include <tess/diagnostics/diagnostics.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace tess {

/// One exact event payload with deterministic simulation and stream stamps.
template <typename T>
struct TickStampedEvent {
  std::uint64_t tick = 0;
  std::uint64_t sequence = 0;
  T value{};
};

/// Caller-owned, bounded batch of exact tick-stamped events.
///
/// Call `reserve_events` during setup. Publication then performs no container
/// allocation and rejects overflow instead of silently dropping or
/// overwriting an event; payload copy/move operations must themselves be
/// allocation-free for the whole publication call to be allocation-free.
/// `clear` starts a new batch without resetting sequence numbers, allowing
/// consumers and diagnostics to detect gaps across ticks.
template <typename T>
class EventStream {
 public:
  using event_type = TickStampedEvent<T>;

  void reserve_events(std::size_t count) {
    TESS_ASSERT(events_.empty());
    max_events_ = count;
    events_.reserve(count);
  }

  [[nodiscard]] auto publish(std::uint64_t tick, const T& value) -> bool {
    if (events_.size() >= max_events_) {
      ++rejected_events_;
      account_publish(false);
      return false;
    }
    events_.push_back(event_type{tick, next_sequence_, value});
    ++next_sequence_;
    account_publish(true);
    return true;
  }

  [[nodiscard]] auto publish(std::uint64_t tick, T&& value) -> bool {
    if (events_.size() >= max_events_) {
      ++rejected_events_;
      account_publish(false);
      return false;
    }
    events_.push_back(event_type{tick, next_sequence_, std::move(value)});
    ++next_sequence_;
    account_publish(true);
    return true;
  }

  [[nodiscard]] auto events() const noexcept -> std::span<const event_type> {
    return {events_.data(), events_.size()};
  }

  [[nodiscard]] auto size() const noexcept -> std::size_t {
    return events_.size();
  }

  [[nodiscard]] bool empty() const noexcept { return events_.empty(); }

  [[nodiscard]] auto rejected_events() const noexcept -> std::uint64_t {
    return rejected_events_;
  }

  /**
   * Starts a new batch without judging the old one.
   *
   * With flow accounting attached this conservatively counts the
   * discarded batch as dropped after admission; consumers that read
   * the batch should prefer `consume_all`.
   */
  void clear() noexcept { retire_batch(false); }

  /// Retires the batch as read: every event counts completed.
  void consume_all() noexcept { retire_batch(true); }

  /// Retires the batch unread: every event counts dropped.
  void discard_all() noexcept { retire_batch(false); }

  /**
   * Attaches caller-owned flow accounting; null detaches.
   *
   * Attach or detach only on an empty batch; the accountant must
   * outlive the attachment. Events are retained inventory: publication
   * admits, and `consume_all`/`discard_all`/`clear` terminalize.
   */
  void set_flow_accounting(diagnostics::FlowAccounting* accounting) noexcept {
    TESS_ASSERT(events_.empty());
    accounting_ = accounting;
  }

  /**
   * Observes one monotonic simulation tick for time accounting.
   *
   * Ages derive from the accounting clock at publication, not from the
   * events' payload ticks, which callers may stamp into the future.
   */
  void observe_flow_tick(std::uint64_t tick) noexcept {
    if (accounting_ == nullptr) {
      return;
    }
    accounting_->observe_tick(tick);
    accounting_->counters.oldest_outstanding_age_ticks =
        events_.empty() ? 0 : tick - oldest_published_tick_;
  }

 private:
  void retire_batch(bool consumed) noexcept {
    if (accounting_ != nullptr) {
      auto& counters = accounting_->counters;
      const auto count = static_cast<std::uint64_t>(events_.size());
      (consumed ? counters.completed : counters.dropped_after_admission) +=
          count;
      counters.outstanding_current -= count;
      counters.residence_ticks_accumulated +=
          accounting_->last_observed_tick * count - published_tick_total_;
      published_tick_total_ = 0;
      counters.oldest_outstanding_age_ticks = 0;
    }
    events_.clear();
  }

  void account_publish(bool admitted) noexcept {
    if (accounting_ == nullptr) {
      return;
    }
    ++accounting_->counters.offered;
    if (!admitted) {
      ++accounting_->counters.rejected;
      return;
    }
    accounting_->record_admitted();
    if (events_.size() == 1) {
      oldest_published_tick_ = accounting_->last_observed_tick;
    }
    published_tick_total_ += accounting_->last_observed_tick;
  }

  std::vector<event_type> events_;
  std::size_t max_events_ = 0;
  std::uint64_t next_sequence_ = 0;
  std::uint64_t rejected_events_ = 0;
  std::uint64_t oldest_published_tick_ = 0;
  std::uint64_t published_tick_total_ = 0;
  diagnostics::FlowAccounting* accounting_ = nullptr;
};

}  // namespace tess
