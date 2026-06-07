#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

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
      return FixedStepFrame{0, alpha()};
    }

    accumulated_seconds_ +=
        std::max(0.0, real_delta_seconds) * speed_multiplier(control.speed);

    const auto step_seconds = 1.0 / static_cast<double>(base_tps_);
    auto available_ticks =
        static_cast<std::size_t>(accumulated_seconds_ / step_seconds);
    const auto ticks = std::min(available_ticks, max_ticks_per_frame_);
    accumulated_seconds_ -= static_cast<double>(ticks) * step_seconds;

    return FixedStepFrame{ticks, alpha()};
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
  return base_tps * sim_speed_multiplier(speed);
}

}  // namespace tess
