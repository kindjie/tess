#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace tess {

enum class SimSpeed : std::uint8_t {
  Paused,
  Speed1x,
  Speed2x,
  Speed4x,
};
static_assert(sizeof(SimSpeed) == sizeof(std::uint8_t));

struct SimTimeControl {
  SimSpeed speed = SimSpeed::Speed1x;
};

struct FixedStepFrame {
  std::size_t ticks = 0;
  double alpha = 0.0;
  // Sim-time seconds discarded because the frame hit max_ticks_per_frame
  // with more than one step of backlog remaining. Nonzero means the
  // simulation is running behind real time.
  double dropped_seconds = 0.0;
};

class FixedStepAccumulator {
 public:
  constexpr FixedStepAccumulator(std::uint32_t base_tps,
                                 std::size_t max_ticks_per_frame) noexcept
      : base_tps_(base_tps), max_ticks_per_frame_(max_ticks_per_frame) {}

  [[nodiscard]] constexpr auto base_tps() const noexcept -> std::uint32_t {
    return base_tps_;
  }

  [[nodiscard]] constexpr auto max_ticks_per_frame() const noexcept
      -> std::size_t {
    return max_ticks_per_frame_;
  }

  [[nodiscard]] constexpr auto accumulated_seconds() const noexcept -> double {
    return accumulated_seconds_;
  }

  constexpr void reset() noexcept { accumulated_seconds_ = 0.0; }

  constexpr auto consume(double real_delta_seconds,
                         SimTimeControl control) noexcept -> FixedStepFrame {
    if (control.speed == SimSpeed::Paused || base_tps_ == 0 ||
        max_ticks_per_frame_ == 0) {
      return FixedStepFrame{0, alpha(), 0.0};
    }

    // NaN and negative deltas contribute nothing (NaN fails the comparison).
    if (real_delta_seconds > 0.0) {
      accumulated_seconds_ +=
          real_delta_seconds * speed_multiplier(control.speed);
    }

    const auto step_seconds = 1.0 / static_cast<double>(base_tps_);
    // Compare availability in the double domain so the size_t cast below is
    // always in range, even for absurd frame deltas.
    const auto available = accumulated_seconds_ / step_seconds;
    std::size_t ticks = 0;
    if (available >= static_cast<double>(max_ticks_per_frame_)) {
      ticks = max_ticks_per_frame_;
    } else if (available >= 1.0) {
      ticks = static_cast<std::size_t>(available);
    }
    accumulated_seconds_ -= static_cast<double>(ticks) * step_seconds;
    // The rounded division above can round `available` up across an
    // integer boundary, granting one tick that is not quite fully banked
    // (a bounded one-tick borrow); the subtraction then leaves the bank
    // ~1 ulp negative. Clamp so consumers never observe a negative bank.
    accumulated_seconds_ = std::max(accumulated_seconds_, 0.0);

    // When the tick cap was hit, drop backlog beyond one step instead of
    // banking it: retained debt would force max-tick catch-up frames (or an
    // unrecoverable spiral), while one step of carry preserves alpha
    // continuity. Sim time slows instead; the drop is reported.
    double dropped_seconds = 0.0;
    if (ticks == max_ticks_per_frame_ && accumulated_seconds_ > step_seconds) {
      dropped_seconds = accumulated_seconds_ - step_seconds;
      accumulated_seconds_ = step_seconds;
    }

    return FixedStepFrame{ticks, alpha(), dropped_seconds};
  }

 private:
  [[nodiscard]] static constexpr auto speed_multiplier(SimSpeed speed) noexcept
      -> double {
    switch (speed) {
      case SimSpeed::Paused:
        return 0.0;
      case SimSpeed::Speed1x:
        return 1.0;
      case SimSpeed::Speed2x:
        return 2.0;
      case SimSpeed::Speed4x:
        return 4.0;
    }
    return 1.0;
  }

  [[nodiscard]] constexpr auto alpha() const noexcept -> double {
    if (base_tps_ == 0) {
      return 0.0;
    }
    return std::clamp(accumulated_seconds_ * static_cast<double>(base_tps_),
                      0.0, 1.0);
  }

  std::uint32_t base_tps_ = 0;
  std::size_t max_ticks_per_frame_ = 0;
  double accumulated_seconds_ = 0.0;
};

[[nodiscard]] constexpr auto sim_speed_multiplier(SimSpeed speed) noexcept
    -> std::uint32_t {
  switch (speed) {
    case SimSpeed::Paused:
      return 0;
    case SimSpeed::Speed1x:
      return 1;
    case SimSpeed::Speed2x:
      return 2;
    case SimSpeed::Speed4x:
      return 4;
  }
  return 1;
}

[[nodiscard]] constexpr auto effective_tps(std::uint32_t base_tps,
                                           SimSpeed speed) noexcept
    -> std::uint32_t {
  const auto product = static_cast<std::uint64_t>(base_tps) *
                       static_cast<std::uint64_t>(sim_speed_multiplier(speed));
  constexpr auto max_tps = std::numeric_limits<std::uint32_t>::max();
  return product > max_tps ? max_tps : static_cast<std::uint32_t>(product);
}

}  // namespace tess
