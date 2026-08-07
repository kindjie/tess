// Deterministic fake-clock tests for the budgeted-progress harness
// (docs/planning/budgeted-progress-benchmarks.md, section 13).
//
// Each test names the section 13 case it implements. All tests drive
// the scripted integer-nanosecond clock: work "duration" is modeled by
// advancing the clock inside mandatory/quantum callbacks, never by
// sleeping.

#include <gtest/gtest.h>
#include <tess/sim/time.h>

#include <cstdint>
#include <vector>

#include "budgeted_progress_clock.h"
#include "budgeted_progress_controller.h"

namespace {

namespace budgeted = tess_test::budgeted;
using budgeted::BudgetScope;
using budgeted::FrameBudgetConfig;
using budgeted::FrameBudgetController;
using budgeted::FrameRecord;
using budgeted::Nanos;
using budgeted::Pacing;
using budgeted::ScriptedClock;

constexpr Nanos kMs = 1'000'000;

// One tick per frame keeps grant arithmetic out of the way where a
// test is about the budget loop, not the accumulator.
auto one_tick_config(Nanos budget_ns) -> FrameBudgetConfig {
  FrameBudgetConfig config;
  config.budget_ns = budget_ns;
  config.base_tps = 60;
  return config;
}

// A quantum source that pops scripted durations; empty means no
// eligible work. Advancing the clock models the quantum running to its
// normal return.
struct ScriptedQuanta {
  ScriptedClock* clock = nullptr;
  std::vector<Nanos> durations;
  std::size_t next = 0;

  auto operator()() -> bool {
    if (next >= durations.size()) {
      return false;
    }
    clock->advance(durations[next]);
    ++next;
    return true;
  }
};

auto no_mandatory(std::uint64_t) -> void {}

// Section 13 test 1: zero budget starts no defer-capable work.
TEST(BudgetedController, ZeroBudgetStartsNoDeferCapableWork) {
  ScriptedClock clock;
  FrameBudgetController controller{clock, one_tick_config(0)};
  ScriptedQuanta quanta{&clock, {kMs}};

  const FrameRecord record = controller.run_frame(no_mandatory, quanta);

  EXPECT_EQ(record.quanta_started, 0u);
  EXPECT_EQ(quanta.next, 0u);
  EXPECT_EQ(record.overshoot_quantum_tail_ns, 0u);
}

// Section 13 test 2: positive budget starts the first eligible quantum.
TEST(BudgetedController, PositiveBudgetStartsFirstQuantum) {
  ScriptedClock clock;
  FrameBudgetController controller{clock, one_tick_config(kMs)};
  ScriptedQuanta quanta{&clock, {kMs / 2}};

  const FrameRecord record = controller.run_frame(no_mandatory, quanta);

  EXPECT_EQ(record.quanta_started, 1u);
  EXPECT_EQ(record.overshoot_quantum_tail_ns, 0u);
  EXPECT_EQ(record.overshoot_mandatory_ns, 0u);
}

// Section 13 test 3: exact-deadline completion has zero overshoot and
// starts no next quantum.
TEST(BudgetedController, ExactDeadlineCompletionZeroOvershootNoNextQuantum) {
  ScriptedClock clock;
  FrameBudgetController controller{clock, one_tick_config(kMs)};
  ScriptedQuanta quanta{&clock, {kMs, kMs}};

  const FrameRecord record = controller.run_frame(no_mandatory, quanta);

  EXPECT_EQ(record.quanta_started, 1u);
  EXPECT_EQ(record.overshoot_quantum_tail_ns, 0u);
  EXPECT_EQ(record.overshoot_mandatory_ns, 0u);
  EXPECT_EQ(record.elapsed_ns, kMs);
}

// Section 13 test 4: one-nanosecond overrun records exactly one
// nanosecond.
TEST(BudgetedController, OneNanosecondOverrunRecordsExactlyOne) {
  ScriptedClock clock;
  FrameBudgetController controller{clock, one_tick_config(kMs)};
  ScriptedQuanta quanta{&clock, {kMs + 1}};

  const FrameRecord record = controller.run_frame(no_mandatory, quanta);

  EXPECT_EQ(record.overshoot_quantum_tail_ns, 1u);
  EXPECT_EQ(record.overshoot_mandatory_ns, 0u);
}

// Section 13 test 5: a long indivisible first quantum completes and
// counts useful work plus overshoot.
TEST(BudgetedController, LongIndivisibleFirstQuantumCompletesWithOvershoot) {
  ScriptedClock clock;
  FrameBudgetController controller{clock, one_tick_config(kMs)};
  ScriptedQuanta quanta{&clock, {50 * kMs}};

  const FrameRecord record = controller.run_frame(no_mandatory, quanta);

  EXPECT_EQ(record.quanta_started, 1u);
  EXPECT_EQ(quanta.next, 1u);
  EXPECT_EQ(record.overshoot_quantum_tail_ns, 49 * kMs);
  EXPECT_EQ(record.elapsed_ns, 50 * kMs);
}

// Section 13 test 7: multiple ticks in one rendered frame share one
// frame allowance.
TEST(BudgetedController, MultipleTicksShareOneFrameAllowance) {
  ScriptedClock clock;
  FrameBudgetConfig config;
  config.budget_ns = kMs;
  config.base_tps = 120;  // Two ticks per 60 FPS frame.
  FrameBudgetController controller{clock, config};

  auto mandatory = [&clock](std::uint64_t) { clock.advance(400'000); };
  ScriptedQuanta quanta{&clock, {150'000, 150'000, 150'000, 150'000}};

  const FrameRecord record = controller.run_frame(mandatory, quanta);

  ASSERT_EQ(record.granted_ticks, 2u);
  // Mandatory consumed 800 us of the shared 1 ms allowance; only 200 us
  // remained, admitting the quantum that straddles the deadline plus
  // one more start check, never a second tick's worth.
  EXPECT_EQ(record.quanta_started, 2u);
  EXPECT_EQ(record.overshoot_quantum_tail_ns, 100'000u);
  EXPECT_EQ(record.elapsed_ns, 1'100'000u);
}

// Section 13 test 8: per-tick mode intentionally resets the allowance
// each tick.
TEST(BudgetedController, TickScopeResetsAllowancePerTick) {
  ScriptedClock clock;
  FrameBudgetConfig config;
  config.budget_ns = kMs;
  config.scope = BudgetScope::Tick;
  config.base_tps = 120;
  FrameBudgetController controller{clock, config};

  auto mandatory = [&clock](std::uint64_t) { clock.advance(400'000); };
  ScriptedQuanta quanta{&clock, {300'000, 300'000, 300'000, 300'000}};

  const FrameRecord record = controller.run_frame(mandatory, quanta);

  ASSERT_EQ(record.granted_ticks, 2u);
  // Each tick ran its 400 us mandatory phase and then two 300 us quanta
  // inside its own fresh 1 ms allowance: roughly N * B_tick consumed.
  EXPECT_EQ(record.quanta_started, 4u);
  EXPECT_EQ(record.overshoot_quantum_tail_ns, 0u);
  EXPECT_EQ(record.elapsed_ns, 2 * kMs);
}

// Section 13 test 16: large nanosecond values do not underflow
// elapsed/overshoot arithmetic, and completion at or before the
// deadline yields exactly zero overshoot.
TEST(BudgetedController, LargeClockValuesNeverUnderflow) {
  ScriptedClock clock;
  clock.advance(Nanos{1} << 62);
  FrameBudgetController controller{clock, one_tick_config(8 * kMs)};
  ScriptedQuanta quanta{&clock, {kMs}};

  const FrameRecord record = controller.run_frame(no_mandatory, quanta);

  EXPECT_EQ(record.overshoot_quantum_tail_ns, 0u);
  EXPECT_EQ(record.overshoot_mandatory_ns, 0u);
  EXPECT_EQ(record.elapsed_ns, kMs);
  EXPECT_EQ(budgeted::sub_clamped(0, 5), 0u);
}

// Section 13 test 19 (controller half): paced mode records the frame
// start lag when a frame overruns its edge, and the next frame's
// allowance is unreduced.
TEST(BudgetedController, PacedModeRecordsLagAndKeepsAllowanceFresh) {
  ScriptedClock clock;
  FrameBudgetConfig config = one_tick_config(kMs);
  config.pacing = Pacing::Paced;
  FrameBudgetController controller{clock, config};

  // Frame 0 blows far past its edge.
  ScriptedQuanta long_quanta{&clock, {20 * kMs}};
  const FrameRecord frame0 = controller.run_frame(no_mandatory, long_quanta);
  EXPECT_EQ(frame0.frame_start_lag_ns, 0u);

  // Frame 1's edge (16.666 ms) is already past; it starts immediately
  // with the full allowance.
  ScriptedQuanta short_quanta{&clock, {500'000}};
  const FrameRecord frame1 = controller.run_frame(no_mandatory, short_quanta);
  EXPECT_EQ(frame1.frame_start_lag_ns, 20 * kMs - 16'666'666u);
  EXPECT_EQ(frame1.quanta_started, 1u);
  EXPECT_EQ(frame1.overshoot_quantum_tail_ns, 0u);

  // Frame 2 waits for its (future) edge and starts with zero lag.
  ScriptedQuanta idle{&clock, {}};
  const FrameRecord frame2 = controller.run_frame(no_mandatory, idle);
  EXPECT_EQ(frame2.frame_start_lag_ns, 0u);
  EXPECT_EQ(frame2.scheduled_start_ns, 33'333'333u);
}

// Unpaced frames run back to back and never report a start lag.
TEST(BudgetedController, UnpacedModeReportsNoLag) {
  ScriptedClock clock;
  FrameBudgetController controller{clock, one_tick_config(kMs)};
  ScriptedQuanta quanta{&clock, {20 * kMs}};

  const FrameRecord frame0 = controller.run_frame(no_mandatory, quanta);
  ScriptedQuanta idle{&clock, {}};
  const FrameRecord frame1 = controller.run_frame(no_mandatory, idle);

  EXPECT_EQ(frame0.frame_start_lag_ns, 0u);
  EXPECT_EQ(frame1.frame_start_lag_ns, 0u);
  EXPECT_EQ(frame1.frame_start_ns, frame0.frame_start_ns + 20 * kMs);
}

// Section 13 test 20: with multiple granted ticks, all ticks' mandatory
// work runs before any defer-capable quantum, and overshoot lands in
// the correct attribution bucket.
TEST(BudgetedController, MandatoryFirstOrderingAndBucketAttribution) {
  ScriptedClock clock;
  FrameBudgetConfig config;
  config.budget_ns = 500'000;
  config.base_tps = 120;
  FrameBudgetController controller{clock, config};

  std::vector<int> order;
  auto mandatory = [&](std::uint64_t) {
    order.push_back(0);
    clock.advance(400'000);
  };
  auto quantum = [&order]() -> bool {
    order.push_back(1);
    return false;
  };

  const FrameRecord record = controller.run_frame(mandatory, quantum);

  ASSERT_EQ(record.granted_ticks, 2u);
  // Both mandatory phases ran (800 us against a 500 us allowance); the
  // 300 us excess is mandatory overshoot and no quantum ever started.
  EXPECT_EQ(order, (std::vector<int>{0, 0}));
  EXPECT_EQ(record.overshoot_mandatory_ns, 300'000u);
  EXPECT_EQ(record.overshoot_quantum_tail_ns, 0u);
  EXPECT_EQ(record.quanta_started, 0u);
}

// The mandatory phase finishing inside the allowance and a quantum
// crossing the deadline produce a quantum-tail bucket, never a
// mandatory bucket: per frame at most one bucket is nonzero.
TEST(BudgetedController, QuantumTailBucketExcludesMandatoryBucket) {
  ScriptedClock clock;
  FrameBudgetConfig config;
  config.budget_ns = kMs;
  config.base_tps = 120;
  FrameBudgetController controller{clock, config};

  auto mandatory = [&clock](std::uint64_t) { clock.advance(100'000); };
  ScriptedQuanta quanta{&clock, {2 * kMs}};

  const FrameRecord record = controller.run_frame(mandatory, quanta);

  EXPECT_EQ(record.overshoot_mandatory_ns, 0u);
  EXPECT_EQ(record.overshoot_quantum_tail_ns, 1'200'000u);
}

// Section 13 test 22 (controller half): a frame granting zero ticks
// still receives the full frame allowance and reports the last-granted
// simulation tick for attribution.
TEST(BudgetedController, ZeroTickFrameReceivesFullAllowance) {
  ScriptedClock clock;
  FrameBudgetConfig config;
  config.budget_ns = kMs;
  config.base_tps = 20;  // One tick every third 60 FPS frame.
  FrameBudgetController controller{clock, config};

  ScriptedQuanta quanta{&clock, {500'000}};
  const FrameRecord frame0 = controller.run_frame(no_mandatory, quanta);
  EXPECT_EQ(frame0.granted_ticks, 0u);
  EXPECT_EQ(frame0.quanta_started, 1u);
  EXPECT_EQ(frame0.sim_tick, 0u);

  ScriptedQuanta idle1{&clock, {}};
  const FrameRecord frame1 = controller.run_frame(no_mandatory, idle1);
  EXPECT_EQ(frame1.granted_ticks, 0u);
  EXPECT_EQ(frame1.sim_tick, 0u);

  ScriptedQuanta idle2{&clock, {}};
  const FrameRecord frame2 = controller.run_frame(no_mandatory, idle2);
  EXPECT_EQ(frame2.granted_ticks, 1u);
  EXPECT_EQ(frame2.sim_tick, 1u);
}

// The accumulator grant pattern is deterministic for the canonical TPS
// ladder at 60 FPS and never drops simulation time (design section
// 3.2: dropped_seconds stays zero by construction).
TEST(BudgetedController, CanonicalTpsGrantPatternsNeverDropTime) {
  for (const std::uint32_t tps : {20u, 30u, 60u, 120u}) {
    ScriptedClock clock;
    FrameBudgetConfig config;
    config.budget_ns = kMs;
    config.base_tps = tps;
    FrameBudgetController controller{clock, config};

    std::size_t granted = 0;
    for (int frame = 0; frame < 600; ++frame) {
      ScriptedQuanta idle{&clock, {}};
      const FrameRecord record = controller.run_frame(no_mandatory, idle);
      granted += record.granted_ticks;
      ASSERT_EQ(record.dropped_seconds, 0.0) << "tps " << tps;
    }
    // 600 frames at 60 FPS is 10 seconds: exactly 10 * tps ticks.
    EXPECT_EQ(granted, static_cast<std::size_t>(tps) * 10) << "tps " << tps;
  }
}

}  // namespace
