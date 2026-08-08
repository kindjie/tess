// Arrival-rate machinery for the budgeted-progress benchmark harness
// (docs/planning/budgeted-progress-benchmarks.md, sections 6.2, 8-9).
//
// `RationalRate` is the deterministic integer (Bresenham-style)
// release accumulator the design mandates instead of random sampling:
// after T ticks exactly floor(T * num / (den * base_tps)) events have
// been released. `ArrivalTracker` is the single-class FIFO per-item
// tracker for open-loop arrival cells: because service follows
// admission order, the oldest outstanding item is always the next
// unserviced one, so per-tick bookkeeping is O(1) and safe inside the
// measured service timer (design section 11.1); cohort, lateness, and
// starvation derive from the per-item records at seal time, untimed.
// Flow-counter writes follow the same production `FlowAccounting`
// semantics as the rest of the harness.
//
// Harness support only, never a public header.

#pragma once

#include <tess/diagnostics/diagnostics.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace tess_test::budgeted {

// Releases events at a rational rate of `rate_num / rate_den` events
// per simulation second against a `base_tps` tick clock.
class RationalRate {
 public:
  RationalRate(std::uint64_t rate_num, std::uint64_t rate_den,
               std::uint32_t base_tps) noexcept
      : step_num_(rate_num), step_den_(rate_den * base_tps) {}

  // The number of events due at this tick; call exactly once per tick.
  [[nodiscard]] auto release_at_tick() noexcept -> std::uint64_t {
    accumulator_ += step_num_;
    const std::uint64_t events = accumulator_ / step_den_;
    accumulator_ -= events * step_den_;
    return events;
  }

 private:
  std::uint64_t step_num_ = 0;
  std::uint64_t step_den_ = 1;
  std::uint64_t accumulator_ = 0;
};

struct ArrivalSummary {
  std::uint64_t useful_completions = 0;  // Completion tick in window.
  std::uint64_t cohort_admitted = 0;     // Admission tick in window.
  std::uint64_t cohort_deadline_met = 0;
  std::uint64_t starved_items = 0;
  std::vector<std::uint64_t> lateness_ticks;  // Completed cohort only.
};

// Single-class open-loop tracker. Admissions and completions are both
// FIFO; deadlines are inclusive simulation ticks (admission +
// allowance). Only `observe_tick`, `admit`, `next`, and `complete`
// run inside timed frames, all O(1); everything else is seal-time.
class ArrivalTracker {
 public:
  static constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();

  ArrivalTracker(std::uint64_t allowance_ticks, std::uint32_t base_tps,
                 std::size_t expected_items)
      : allowance_ticks_(allowance_ticks), base_tps_(base_tps) {
    admitted_ticks_.reserve(expected_items);
    completion_ticks_.reserve(expected_items);
    oldest_age_samples_.reserve(4096);
  }

  [[nodiscard]] auto accounting() noexcept
      -> tess::diagnostics::FlowAccounting& {
    return accounting_;
  }

  [[nodiscard]] auto counters() const noexcept
      -> const tess::diagnostics::FlowCounters& {
    return accounting_.counters;
  }

  void observe_tick(std::uint64_t tick) {
    accounting_.observe_tick(tick);
    const bool outstanding = next_service_ < admitted_ticks_.size();
    accounting_.counters.oldest_outstanding_age_ticks =
        outstanding ? tick - admitted_ticks_[next_service_] : 0;
    if (in_window_) {
      oldest_age_samples_.push_back(
          accounting_.counters.oldest_outstanding_age_ticks);
    }
  }

  void admit(std::uint64_t tick) {
    ++accounting_.counters.offered;
    accounting_.record_admitted();
    admitted_ticks_.push_back(tick);
  }

  // The next unserviced admission, or npos when the queue is empty.
  [[nodiscard]] auto next() noexcept -> std::size_t {
    if (next_service_ >= admitted_ticks_.size()) {
      return npos;
    }
    return next_service_++;
  }

  void complete(std::size_t item, std::uint64_t tick,
                std::uint64_t work_units) {
    (void)item;  // FIFO: completions land in admission order.
    completion_ticks_.push_back(tick);
    auto& counters = accounting_.counters;
    ++counters.completed;
    counters.offered_work_units += work_units;
    counters.consumed_work_units += work_units;
    accounting_.record_left_outstanding();
    counters.residence_ticks_accumulated +=
        tick - admitted_ticks_[completion_ticks_.size() - 1];
  }

  void begin_window(std::uint64_t tick) {
    window_start_tick_ = tick;
    in_window_ = true;
  }

  void end_window(std::uint64_t tick) {
    window_end_tick_ = tick;
    in_window_ = false;
  }

  [[nodiscard]] auto oldest_age_samples() const
      -> const std::vector<std::uint64_t>& {
    return oldest_age_samples_;
  }

  [[nodiscard]] auto starvation_window_ticks() const noexcept -> std::uint64_t {
    const std::uint64_t four = 4 * allowance_ticks_;
    return four > base_tps_ ? four : base_tps_;
  }

  // Seal-time derivation over the per-item records; call after the
  // settlement observations are complete (never inside timed frames).
  [[nodiscard]] auto summarize(std::uint64_t final_tick) const
      -> ArrivalSummary {
    ArrivalSummary out;
    const std::uint64_t starvation = starvation_window_ticks();
    for (std::size_t i = 0; i < admitted_ticks_.size(); ++i) {
      const std::uint64_t admitted = admitted_ticks_[i];
      const bool completed = i < completion_ticks_.size();
      const std::uint64_t deadline = admitted + allowance_ticks_;
      if (completed && completion_ticks_[i] >= window_start_tick_ &&
          completion_ticks_[i] <= window_end_tick_) {
        ++out.useful_completions;
      }
      if (admitted < window_start_tick_ || admitted > window_end_tick_) {
        continue;
      }
      ++out.cohort_admitted;
      if (completed && completion_ticks_[i] <= deadline) {
        ++out.cohort_deadline_met;
      }
      if (completed && completion_ticks_[i] > deadline) {
        out.lateness_ticks.push_back(completion_ticks_[i] - deadline);
      }
      // Wait while dependency-ready (always ready in this open-loop
      // cell): service tick for completed items, the final observed
      // tick for items still outstanding at seal.
      const std::uint64_t served_or_now =
          completed ? completion_ticks_[i] : final_tick;
      if (served_or_now - admitted >= starvation) {
        ++out.starved_items;
      }
    }
    return out;
  }

 private:
  std::uint64_t allowance_ticks_ = 0;
  std::uint32_t base_tps_ = 0;
  tess::diagnostics::FlowAccounting accounting_;
  std::vector<std::uint64_t> admitted_ticks_;
  std::vector<std::uint64_t> completion_ticks_;
  std::vector<std::uint64_t> oldest_age_samples_;
  std::size_t next_service_ = 0;
  std::uint64_t window_start_tick_ = 0;
  std::uint64_t window_end_tick_ = std::numeric_limits<std::uint64_t>::max();
  bool in_window_ = false;
};

}  // namespace tess_test::budgeted
