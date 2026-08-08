// Frame-budget controller for the budgeted-progress benchmark harness
// (docs/planning/budgeted-progress-benchmarks.md, sections 3-4, 13).
//
// The controller owns the honest cooperative enforcement loop: one
// rendered frame receives one wall-clock allowance, all granted ticks'
// mandatory work runs first, defer-capable quanta then run to normal
// return with the clock checked between quanta, and overshoot is
// recorded in two attributed buckets (quantum-tail versus mandatory).
// No quantum is ever interrupted mid-call. The wall-time budget lives
// here, in the benchmark driver, never in production Schedule.
//
// Harness support only, never a public header.

#pragma once

#include <tess/sim/time.h>

#include <cstddef>
#include <cstdint>

#include "budgeted_progress_clock.h"

namespace tess_test::budgeted {

// Design section 3.2: the two frame-pacing modes. Unpaced runs frames
// back to back; paced waits for each conceptual frame edge and records
// the start lag. Measured per-wall-second rates come from paced cells
// only.
enum class Pacing : std::uint8_t {
  Unpaced,
  Paced,
};

// Design section 3.3: the headline mode shares one allowance per
// rendered frame; the secondary mode grants each simulation tick its
// own allowance and answers a different question.
enum class BudgetScope : std::uint8_t {
  Frame,
  Tick,
};

struct FrameBudgetConfig {
  Nanos budget_ns = 0;
  BudgetScope scope = BudgetScope::Frame;
  Pacing pacing = Pacing::Unpaced;
  std::uint32_t frame_hz_num = 60;
  std::uint32_t frame_hz_den = 1;
  std::uint32_t base_tps = 20;
  tess::SimSpeed speed = tess::SimSpeed::Speed1x;
  std::size_t max_ticks_per_frame = 8;
};

// Everything the controller observed about one rendered frame. Under
// frame scope at most one overshoot bucket is nonzero per frame:
// mandatory overshoot means no defer-capable quantum started, and a
// quantum tail requires the mandatory phase to have finished inside
// the allowance. Tick scope can accumulate both buckets in one frame
// (tick 1 mandatory overshoot, tick 2 quantum tail).
struct FrameRecord {
  std::uint64_t frame_index = 0;
  std::size_t granted_ticks = 0;
  // The simulation tick after this frame's grants; completions in a
  // zero-tick frame are attributed to this (last-granted) tick.
  std::uint64_t sim_tick = 0;
  Nanos scheduled_start_ns = 0;
  Nanos frame_start_ns = 0;
  // Paced mode only: how late the frame started past its edge.
  Nanos frame_start_lag_ns = 0;
  Nanos elapsed_ns = 0;
  Nanos overshoot_mandatory_ns = 0;
  Nanos overshoot_quantum_tail_ns = 0;
  std::uint64_t quanta_started = 0;
  double dropped_seconds = 0.0;
};

// Drives frames against a BudgetClock. `MandatoryFn` is invoked once
// per granted tick (in tick order) and models tick-coupled work that
// cannot defer; `QuantumFn` executes one defer-capable quantum to its
// normal return and reports whether eligible work existed. The
// controller never subdivides either callback.
template <BudgetClock Clock>
class FrameBudgetController {
 public:
  FrameBudgetController(Clock& clock, FrameBudgetConfig config)
      : clock_(clock),
        config_(config),
        accumulator_(config.base_tps, config.max_ticks_per_frame) {}

  [[nodiscard]] auto config() const noexcept -> const FrameBudgetConfig& {
    return config_;
  }

  [[nodiscard]] auto sim_tick() const noexcept -> std::uint64_t {
    return sim_clock_.tick;
  }

  [[nodiscard]] auto frames_run() const noexcept -> std::uint64_t {
    return frame_index_;
  }

  // Re-anchors the paced schedule so the next frame's edge is now.
  // For benchmark drivers that run untimed maintenance (for example a
  // quiescing drain) between frames: without re-anchoring, every edge
  // that passed during the maintenance would be overdue and the
  // following frames would run back to back, silently unpaced, with
  // the lost time booked as frame-start lag. No-op when unpaced.
  void rebase_pacing() noexcept {
    if (config_.pacing == Pacing::Paced && frame_index_ > 0) {
      epoch_ns_ = clock_.now() - frame_edge_offset(frame_index_);
    }
  }

  template <typename MandatoryFn, typename QuantumFn>
  auto run_frame(MandatoryFn&& mandatory, QuantumFn&& quantum) -> FrameRecord {
    FrameRecord record;
    record.frame_index = frame_index_;

    if (config_.pacing == Pacing::Paced) {
      if (frame_index_ == 0) {
        epoch_ns_ = clock_.now();
      }
      record.scheduled_start_ns = epoch_ns_ + frame_edge_offset(frame_index_);
      clock_.wait_until(record.scheduled_start_ns);
      record.frame_start_ns = clock_.now();
      record.frame_start_lag_ns =
          sub_clamped(record.frame_start_ns, record.scheduled_start_ns);
    } else {
      record.frame_start_ns = clock_.now();
      record.scheduled_start_ns = record.frame_start_ns;
    }

    // Design section 3.2: v1 always feeds the conceptual constant frame
    // delta, keeping tick grants deterministic and identical across
    // budgets and machines; feedback dynamics are out of scope.
    const double frame_delta_seconds =
        static_cast<double>(config_.frame_hz_den) /
        static_cast<double>(config_.frame_hz_num);
    const tess::FixedStepFrame grant = accumulator_.consume(
        frame_delta_seconds, tess::SimTimeControl{config_.speed});
    record.granted_ticks = grant.ticks;
    record.dropped_seconds = grant.dropped_seconds;

    if (config_.scope == BudgetScope::Frame) {
      run_frame_scope(record, mandatory, quantum);
    } else {
      run_tick_scope(record, mandatory, quantum);
    }

    record.sim_tick = sim_clock_.tick;
    ++frame_index_;
    return record;
  }

 private:
  // Integer frame-edge math so paced schedules cannot drift: edge k is
  // epoch + floor(k * 1e9 * den / num) nanoseconds, computed without
  // 128-bit arithmetic (GCC -pedantic and MSVC reject __int128): with
  // K = 1e9 * den = q * num + r, floor(k * K / num) = k * q +
  // floor(k * r / num), and k * r stays far inside 64 bits for any
  // realistic frame count and rate.
  [[nodiscard]] auto frame_edge_offset(std::uint64_t frame_index) const noexcept
      -> Nanos {
    const std::uint64_t scaled_period = 1'000'000'000ULL * config_.frame_hz_den;
    const std::uint64_t quotient = scaled_period / config_.frame_hz_num;
    const std::uint64_t remainder = scaled_period % config_.frame_hz_num;
    return frame_index * quotient +
           (frame_index * remainder) / config_.frame_hz_num;
  }

  // Design section 3.4: all granted ticks' mandatory work first, then
  // defer-capable quanta against the one shared allowance. Every frame
  // receives the full allowance, including frames granting zero ticks.
  template <typename MandatoryFn, typename QuantumFn>
  void run_frame_scope(FrameRecord& record, MandatoryFn& mandatory,
                       QuantumFn& quantum) {
    const Nanos deadline = record.frame_start_ns + config_.budget_ns;
    for (std::size_t i = 0; i < record.granted_ticks; ++i) {
      mandatory(tess::advance_sim_tick(sim_clock_));
    }
    const Nanos mandatory_end = clock_.now();
    record.overshoot_mandatory_ns = sub_clamped(mandatory_end, deadline);

    while (clock_.now() < deadline) {
      if (!quantum()) {
        break;
      }
      ++record.quanta_started;
    }
    const Nanos frame_end = clock_.now();
    const Nanos quantum_anchor =
        mandatory_end > deadline ? mandatory_end : deadline;
    record.overshoot_quantum_tail_ns = sub_clamped(frame_end, quantum_anchor);
    record.elapsed_ns = sub_clamped(frame_end, record.frame_start_ns);
  }

  // Design section 3.3 secondary mode: each granted tick gets a fresh
  // allowance covering its mandatory work and defer-capable quanta;
  // zero-tick frames carry no allowance in this scope.
  template <typename MandatoryFn, typename QuantumFn>
  void run_tick_scope(FrameRecord& record, MandatoryFn& mandatory,
                      QuantumFn& quantum) {
    for (std::size_t i = 0; i < record.granted_ticks; ++i) {
      const Nanos tick_anchor = clock_.now();
      const Nanos deadline = tick_anchor + config_.budget_ns;
      mandatory(tess::advance_sim_tick(sim_clock_));
      const Nanos mandatory_end = clock_.now();
      record.overshoot_mandatory_ns += sub_clamped(mandatory_end, deadline);

      while (clock_.now() < deadline) {
        if (!quantum()) {
          break;
        }
        ++record.quanta_started;
      }
      const Nanos tick_end = clock_.now();
      const Nanos quantum_anchor =
          mandatory_end > deadline ? mandatory_end : deadline;
      record.overshoot_quantum_tail_ns += sub_clamped(tick_end, quantum_anchor);
    }
    record.elapsed_ns = sub_clamped(clock_.now(), record.frame_start_ns);
  }

  Clock& clock_;
  FrameBudgetConfig config_;
  tess::FixedStepAccumulator accumulator_;
  tess::SimClock sim_clock_;
  std::uint64_t frame_index_ = 0;
  Nanos epoch_ns_ = 0;
};

}  // namespace tess_test::budgeted
