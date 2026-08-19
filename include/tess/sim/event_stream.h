#pragma once

#include <tess/core/assert.h>
#include <tess/core/fail_fast.h>
#include <tess/diagnostics/diagnostics.h>

#include <algorithm>
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
    if (!events_.empty()) {
      detail::fail_fast("EventStream::reserve_events requires an empty stream");
    }
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
    if (!events_.empty()) {
      detail::fail_fast(
          "EventStream::set_flow_accounting requires an empty stream");
    }
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
    const auto now = accounting_->last_observed_tick;
    accounting_->counters.oldest_outstanding_age_ticks =
        events_.empty() ? 0 : now - oldest_published_tick_;
  }

  EventStream() = default;

  // An attached accountant tracks exactly one stream: copies start
  // unattached with cleared residence stamps, moves transfer the
  // attachment. Assigning over an instrumented, non-empty stream would
  // orphan outstanding inventory, so the destination must be empty or
  // unattached.
  EventStream(const EventStream& other)
      : events_{other.events_},
        max_events_{other.max_events_},
        next_sequence_{other.next_sequence_},
        rejected_events_{other.rejected_events_} {}
  auto operator=(const EventStream& other) -> EventStream& {
    if (this != &other) {
      if (accounting_ != nullptr && !events_.empty()) {
        detail::fail_fast(
            "EventStream copy assignment would orphan outstanding "
            "accounting");
      }
      events_ = other.events_;
      max_events_ = other.max_events_;
      next_sequence_ = other.next_sequence_;
      rejected_events_ = other.rejected_events_;
      oldest_published_tick_ = 0;
      published_tick_total_ = 0;
      accounting_ = nullptr;
    }
    return *this;
  }
  EventStream(EventStream&& other) noexcept
      : events_{std::move(other.events_)},
        max_events_{other.max_events_},
        next_sequence_{other.next_sequence_},
        rejected_events_{other.rejected_events_},
        oldest_published_tick_{other.oldest_published_tick_},
        published_tick_total_{other.published_tick_total_},
        accounting_{other.accounting_} {
    other.accounting_ = nullptr;
    other.oldest_published_tick_ = 0;
    other.published_tick_total_ = 0;
  }
  auto operator=(EventStream&& other) noexcept -> EventStream& {
    if (this != &other) {
      if (accounting_ != nullptr && !events_.empty()) {
        detail::fail_fast(
            "EventStream move assignment would orphan outstanding "
            "accounting");
      }
      events_ = std::move(other.events_);
      max_events_ = other.max_events_;
      next_sequence_ = other.next_sequence_;
      rejected_events_ = other.rejected_events_;
      oldest_published_tick_ = other.oldest_published_tick_;
      published_tick_total_ = other.published_tick_total_;
      accounting_ = other.accounting_;
      other.accounting_ = nullptr;
      other.oldest_published_tick_ = 0;
      other.published_tick_total_ = 0;
    }
    return *this;
  }
  ~EventStream() = default;

 private:
  void retire_batch(bool consumed) noexcept {
    if (accounting_ != nullptr) {
      auto& counters = accounting_->counters;
      const auto count = static_cast<std::uint64_t>(events_.size());
      (consumed ? counters.completed : counters.dropped_after_admission) +=
          count;
      // Clamped, matching `FlowAccounting::record_left_outstanding` and
      // every other terminalization site. A shared accountant whose other
      // flow terminalized first, or a `counters.reset()` between publish
      // and retire, would otherwise wrap this unsigned counter and take
      // `inventory_tick_weighted` and the retention identity with it.
      counters.outstanding_current -=
          std::min(counters.outstanding_current, count);
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
