#pragma once

#include <tess/core/assert.h>

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
/// Call `reserve_events` during setup. Publication then allocates nothing and
/// rejects overflow instead of silently dropping or overwriting an event.
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
      return false;
    }
    events_.push_back(event_type{tick, next_sequence_, value});
    ++next_sequence_;
    return true;
  }

  [[nodiscard]] auto publish(std::uint64_t tick, T&& value) -> bool {
    if (events_.size() >= max_events_) {
      ++rejected_events_;
      return false;
    }
    events_.push_back(event_type{tick, next_sequence_, std::move(value)});
    ++next_sequence_;
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

  void clear() noexcept { events_.clear(); }

 private:
  std::vector<event_type> events_;
  std::size_t max_events_ = 0;
  std::uint64_t next_sequence_ = 0;
  std::uint64_t rejected_events_ = 0;
};

}  // namespace tess
