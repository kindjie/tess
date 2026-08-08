// Clock abstraction for the budgeted-progress benchmark harness
// (docs/planning/budgeted-progress-benchmarks.md, sections 4, 11.1, 13).
//
// The frame-budget controller is templated on a clock so that real
// campaigns run against the monotonic clock while the section 13
// deterministic tests drive a scripted integer-nanosecond clock with
// no sleeps. Pacing waits go through the clock (`wait_until`), never
// through std::this_thread directly, so paced-mode behavior is
// testable under the scripted clock.
//
// Harness support only, never a public header.

#pragma once

#include <chrono>
#include <concepts>
#include <cstdint>
#include <thread>

namespace tess_test::budgeted {

// Absolute times and durations are unsigned integer nanoseconds.
// Subtraction goes through `sub_clamped` so elapsed/overshoot
// arithmetic is explicitly non-negative and cannot wrap (design
// section 13, test 16).
using Nanos = std::uint64_t;

[[nodiscard]] constexpr auto sub_clamped(Nanos minuend,
                                         Nanos subtrahend) noexcept -> Nanos {
  return minuend > subtrahend ? minuend - subtrahend : 0;
}

template <typename Clock>
concept BudgetClock = requires(Clock clock, Nanos instant) {
  { clock.now() } noexcept -> std::same_as<Nanos>;
  { clock.wait_until(instant) } -> std::same_as<void>;
};

// Deterministic scripted clock: `now` moves only via `advance` (called
// from scripted work callbacks to model work duration) and
// `wait_until` (called by the controller's paced mode). No sleeps.
class ScriptedClock {
 public:
  [[nodiscard]] auto now() const noexcept -> Nanos { return now_ns_; }

  void advance(Nanos duration) noexcept { now_ns_ += duration; }

  void wait_until(Nanos instant) noexcept {
    if (instant > now_ns_) {
      now_ns_ = instant;
    }
  }

 private:
  Nanos now_ns_ = 0;
};

// Monotonic wall clock for real campaigns. `wait_until` sleeps while
// far from the target and spins across the final stretch so paced
// frame edges land tightly without burning a whole frame period.
class SteadyClock {
 public:
  [[nodiscard]] static auto now() noexcept -> Nanos {
    const auto since_epoch =
        std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<Nanos>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(since_epoch)
            .count());
  }

  void wait_until(Nanos instant) const {
    static constexpr Nanos kSpinWindowNs = 200'000;
    while (true) {
      const Nanos current = now();
      if (current >= instant) {
        return;
      }
      const Nanos remaining = instant - current;
      if (remaining > kSpinWindowNs) {
        std::this_thread::sleep_for(
            std::chrono::nanoseconds{remaining - kSpinWindowNs});
      }
      // Final stretch busy-spins to the edge.
    }
  }
};

static_assert(BudgetClock<ScriptedClock>);
static_assert(BudgetClock<SteadyClock>);

}  // namespace tess_test::budgeted
