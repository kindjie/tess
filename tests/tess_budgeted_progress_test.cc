// Deterministic fake-clock tests for the budgeted-progress harness
// (docs/planning/budgeted-progress-benchmarks.md, section 13).
//
// Each test names the section 13 case it implements. All tests drive
// the scripted integer-nanosecond clock: work "duration" is modeled by
// advancing the clock inside mandatory/quantum callbacks, never by
// sleeping.

#include <gtest/gtest.h>
#include <tess/diagnostics/diagnostics.h>
#include <tess/ops/async_work.h>
#include <tess/ops/queued.h>
#include <tess/sim/time.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "budgeted_progress_arrival.h"
#include "budgeted_progress_artifact.h"
#include "budgeted_progress_clock.h"
#include "budgeted_progress_controller.h"
#include "budgeted_progress_records.h"
#include "budgeted_progress_search.h"

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

// After untimed maintenance between frames (a drain), rebase_pacing
// re-anchors the schedule: the next frame waits for a fresh edge with
// zero lag instead of sprinting through the overdue edges.
TEST(BudgetedController, RebasePacingReanchorsAfterUntimedMaintenance) {
  ScriptedClock clock;
  FrameBudgetConfig config = one_tick_config(kMs);
  config.pacing = Pacing::Paced;
  FrameBudgetController controller{clock, config};

  ScriptedQuanta idle0{&clock, {}};
  (void)controller.run_frame(no_mandatory, idle0);
  // Untimed maintenance consumes several frame periods of wall time.
  clock.advance(100 * kMs);

  // Without re-anchoring this frame would start ~83 ms overdue.
  controller.rebase_pacing();
  ScriptedQuanta idle1{&clock, {}};
  const FrameRecord frame1 = controller.run_frame(no_mandatory, idle1);
  EXPECT_EQ(frame1.frame_start_lag_ns, 0u);
  // And the following edge is one full period after the re-anchor.
  ScriptedQuanta idle2{&clock, {}};
  const FrameRecord frame2 = controller.run_frame(no_mandatory, idle2);
  EXPECT_EQ(frame2.frame_start_lag_ns, 0u);
  EXPECT_EQ(frame2.scheduled_start_ns - frame1.scheduled_start_ns, 16'666'667u);
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

// --- Section 13 cases for per-item records and flow accounting ---

using budgeted::DemandClassConfig;
using budgeted::ItemId;
using budgeted::ItemTracker;
using budgeted::Outcome;
using budgeted::ServiceEntry;
using budgeted::ServiceQueue;

auto one_class_tracker(std::uint64_t allowance_ticks,
                       std::uint32_t base_tps = 20) -> ItemTracker {
  return ItemTracker{{DemandClassConfig{allowance_ticks}}, base_tps};
}

// Section 13 test 6: resumable items stop only between advance() calls
// and resume without duplicates. A real ResumableWorkQueue is the
// quantum source; one advance at AsyncWorkBudget{1} is one quantum.
TEST(BudgetedRecords, ResumableItemsResumeAcrossFramesWithoutDuplicates) {
  ScriptedClock clock;
  FrameBudgetController controller{clock, one_tick_config(kMs)};

  tess::ResumableWorkQueue<std::uint32_t> queue;
  tess::diagnostics::FlowAccounting accounting;
  queue.set_flow_accounting(&accounting);
  queue.reserve_tickets(1);

  struct MultiStepWork {
    ScriptedClock* clock = nullptr;
    std::uint32_t total_items = 10;
    auto operator()(tess::AsyncWorkBudget budget, std::uint32_t& done)
        -> tess::AsyncWorkStep {
      const auto step =
          std::min<std::uint32_t>({budget.max_items, 1, total_items - done});
      clock->advance(600'000);  // 0.6 ms per item.
      done += step;
      const auto state = done >= total_items ? tess::AsyncStepState::Ready
                                             : tess::AsyncStepState::Pending;
      return {state, step, tess::AsyncVersion{1}};
    }
  };
  MultiStepWork work{&clock};
  const tess::AsyncTicket ticket = queue.submit(work);
  (void)ticket;

  std::uint64_t advances = 0;
  std::uint64_t items_done = 0;
  auto quantum = [&]() -> bool {
    const tess::AsyncAdvanceStats stats =
        queue.advance(tess::AsyncWorkBudget{1});
    advances += stats.invoked;
    items_done += stats.items_done;
    return stats.invoked > 0;
  };

  std::uint64_t frames = 0;
  while (accounting.counters.completed == 0 && frames < 20) {
    (void)controller.run_frame(no_mandatory, quantum);
    ++frames;
  }

  // Ten 0.6 ms items against a 1 ms allowance: two advances per frame,
  // five frames, every item processed exactly once.
  EXPECT_EQ(items_done, 10u);
  EXPECT_EQ(advances, 10u);
  EXPECT_EQ(frames, 5u);
  EXPECT_EQ(accounting.counters.completed, 1u);
  EXPECT_TRUE(accounting.counters.retention_identity_holds());

  // After the terminal state no further quantum reports work.
  EXPECT_FALSE(quantum());
}

// Section 13 test 9: 0/1/2+-tick accumulator patterns release demand
// and deadlines correctly. At 20 TPS / 60 FPS ticks land on every
// third frame; trace events release at their simulation tick.
TEST(BudgetedRecords, AccumulatorPatternsReleaseDemandAtTheirTicks) {
  ScriptedClock clock;
  FrameBudgetConfig config;
  config.budget_ns = kMs;
  config.base_tps = 20;
  FrameBudgetController controller{clock, config};

  ItemTracker tracker = one_class_tracker(1);
  struct TraceEvent {
    std::uint64_t tick;
    bool admitted = false;
    std::uint64_t admitted_frame = 0;
  };
  std::vector<TraceEvent> trace{{1}, {2}};

  for (std::uint64_t frame = 0; frame < 6; ++frame) {
    auto mandatory = [&](std::uint64_t tick) {
      tracker.observe_tick(tick);
      for (auto& event : trace) {
        if (event.tick == tick && !event.admitted) {
          (void)tracker.admit(0, tick);
          event.admitted = true;
          event.admitted_frame = frame;
        }
      }
    };
    ScriptedQuanta idle{&clock, {}};
    (void)controller.run_frame(mandatory, idle);
  }

  // Frames 0,1 grant nothing; frame 2 grants tick 1; frame 5 tick 2.
  EXPECT_TRUE(trace[0].admitted);
  EXPECT_EQ(trace[0].admitted_frame, 2u);
  EXPECT_TRUE(trace[1].admitted);
  EXPECT_EQ(trace[1].admitted_frame, 5u);
  EXPECT_EQ(tracker.counters().admitted, 2u);
}

// Section 13 test 10: completion exactly at the simulation deadline
// succeeds; the next tick misses.
TEST(BudgetedRecords, InclusiveDeadlineBoundary) {
  ItemTracker tracker = one_class_tracker(3);
  tracker.begin_window(0);
  tracker.observe_tick(5);
  const ItemId on_time = tracker.admit(0, 5);  // Deadline tick 8.
  const ItemId late = tracker.admit(0, 5);     // Deadline tick 8.
  tracker.observe_tick(8);
  tracker.resolve(on_time, Outcome::Completed, 8);
  tracker.observe_tick(9);
  tracker.resolve(late, Outcome::Completed, 9);
  tracker.end_window(20);
  tracker.close_settlement();

  const auto summary = tracker.summary();
  EXPECT_EQ(summary.total.cohort_admitted, 2u);
  EXPECT_EQ(summary.total.cohort_deadline_met, 1u);
  ASSERT_EQ(summary.total.lateness_ticks.size(), 1u);
  EXPECT_EQ(summary.total.lateness_ticks[0], 1u);
  EXPECT_EQ(summary.total.useful_completions, 2u);
}

// Section 13 test 11: admission, coalesce, and reject transitions
// preserve the admission identity after each event.
TEST(BudgetedRecords, AdmissionIdentityAfterEachEvent) {
  ItemTracker tracker = one_class_tracker(1);
  tracker.observe_tick(1);
  (void)tracker.admit(0, 1);
  EXPECT_TRUE(tracker.counters().admission_identity_holds());
  tracker.offer_rejected();
  EXPECT_TRUE(tracker.counters().admission_identity_holds());
  tracker.offer_coalesced();
  EXPECT_TRUE(tracker.counters().admission_identity_holds());
  (void)tracker.admit(0, 1);
  EXPECT_TRUE(tracker.counters().admission_identity_holds());
  EXPECT_EQ(tracker.counters().offered, 4u);
}

// Section 13 test 12: every terminal transition preserves the
// retention identity.
TEST(BudgetedRecords, RetentionIdentityAfterEveryTerminalTransition) {
  ItemTracker tracker = one_class_tracker(1);
  tracker.observe_tick(1);
  const Outcome outcomes[] = {
      Outcome::Completed, Outcome::Cancelled, Outcome::Superseded,
      Outcome::Stale,     Outcome::Failed,    Outcome::DroppedAfterAdmission,
  };
  std::vector<ItemId> ids;
  ids.reserve(6);
  for (int i = 0; i < 6; ++i) {
    ids.push_back(tracker.admit(0, 1));
  }
  std::uint64_t tick = 1;
  for (int i = 0; i < 6; ++i) {
    tracker.observe_tick(++tick);
    tracker.resolve(ids[static_cast<std::size_t>(i)], outcomes[i], tick);
    EXPECT_TRUE(tracker.counters().retention_identity_holds())
        << "outcome " << i;
  }
  EXPECT_EQ(tracker.counters().terminal(), 6u);
  EXPECT_EQ(tracker.counters().outstanding_current, 0u);
}

// Section 13 test 13: oldest outstanding age tracks the earliest
// pending admission.
TEST(BudgetedRecords, OldestAgeTracksEarliestPendingAdmission) {
  ItemTracker tracker = one_class_tracker(1);
  tracker.observe_tick(1);
  const ItemId first = tracker.admit(0, 1);
  tracker.observe_tick(5);
  (void)tracker.admit(0, 5);
  tracker.observe_tick(10);
  EXPECT_EQ(tracker.counters().oldest_outstanding_age_ticks, 9u);
  tracker.resolve(first, Outcome::Completed, 10);
  tracker.observe_tick(11);
  EXPECT_EQ(tracker.counters().oldest_outstanding_age_ticks, 6u);
}

// Section 13 test 14: starvation time counts only while
// dependency-ready.
TEST(BudgetedRecords, StarvationCountsOnlyWhileDependencyReady) {
  // Allowance 1 at 20 TPS: starvation window = max(4, 20) = 20 ticks.
  ItemTracker tracker = one_class_tracker(1);
  tracker.begin_window(0);
  tracker.observe_tick(0);
  const ItemId gated = tracker.admit(0, 0);
  const ItemId starved = tracker.admit(0, 0);

  for (std::uint64_t tick = 1; tick <= 10; ++tick) {
    tracker.observe_tick(tick);
  }
  // `gated` loses dependency-readiness for the middle stretch; its
  // streak resets and never reaches the 20-tick window.
  tracker.set_ready(gated, false);
  for (std::uint64_t tick = 11; tick <= 30; ++tick) {
    tracker.observe_tick(tick);
  }
  tracker.set_ready(gated, true);
  for (std::uint64_t tick = 31; tick <= 40; ++tick) {
    tracker.observe_tick(tick);
  }
  tracker.end_window(40);
  tracker.close_settlement();

  const auto summary = tracker.summary();
  EXPECT_EQ(summary.total.starved_items, 1u);
  (void)starved;
}

// Section 13 test 15: a quiescent drain cannot retroactively change
// the measured stability verdict.
TEST(BudgetedRecords, DrainAfterSealCannotChangeVerdict) {
  ItemTracker tracker = one_class_tracker(2);
  tracker.begin_window(0);
  tracker.observe_tick(1);
  const ItemId item = tracker.admit(0, 1);
  tracker.end_window(10);
  tracker.close_settlement();

  const auto sealed = tracker.summary();
  EXPECT_EQ(sealed.total.cohort_deadline_met, 0u);
  EXPECT_EQ(sealed.total.useful_completions, 0u);
  EXPECT_TRUE(sealed.retention_identity_ok);

  // Draining the item after settlement close changes live counters but
  // never the sealed verdict.
  tracker.observe_tick(11);
  tracker.resolve(item, Outcome::Completed, 11);
  const auto after_drain = tracker.summary();
  EXPECT_EQ(after_drain.total.cohort_deadline_met, 0u);
  EXPECT_EQ(after_drain.total.useful_completions, 0u);
}

// Section 13 test 17: a completion reclassified stale during the
// window or settlement is removed from useful completions and deadline
// success, and the negative completed delta corrupts nothing.
TEST(BudgetedRecords, ReclassificationDuringSettlementAttributesBack) {
  ItemTracker tracker = one_class_tracker(4);
  tracker.begin_window(0);
  tracker.observe_tick(1);
  const ItemId item = tracker.admit(0, 1);
  tracker.observe_tick(2);
  tracker.resolve(item, Outcome::Completed, 2);
  EXPECT_EQ(tracker.counters().completed, 1u);
  tracker.end_window(10);

  // Settlement: churn invalidates the produced result.
  tracker.reclassify_stale(item);
  EXPECT_EQ(tracker.counters().completed, 0u);
  EXPECT_EQ(tracker.counters().stale, 1u);
  EXPECT_TRUE(tracker.counters().retention_identity_holds());
  tracker.close_settlement();

  const auto summary = tracker.summary();
  EXPECT_EQ(summary.total.useful_completions, 0u);
  EXPECT_EQ(summary.total.cohort_deadline_met, 0u);
  EXPECT_TRUE(summary.retention_identity_ok);
}

// Section 13 test 18: a reclassification after settlement close does
// not alter the sealed verdict.
TEST(BudgetedRecords, ReclassificationAfterSealDoesNotAlterVerdict) {
  ItemTracker tracker = one_class_tracker(4);
  tracker.begin_window(0);
  tracker.observe_tick(1);
  const ItemId item = tracker.admit(0, 1);
  tracker.observe_tick(2);
  tracker.resolve(item, Outcome::Completed, 2);
  tracker.end_window(10);
  tracker.close_settlement();

  tracker.reclassify_stale(item);

  const auto summary = tracker.summary();
  EXPECT_EQ(summary.total.useful_completions, 1u);
  EXPECT_EQ(summary.total.cohort_deadline_met, 1u);
  // The live counters still swapped buckets under the guarded
  // decrement; only the sealed verdict is immutable.
  EXPECT_EQ(tracker.counters().completed, 0u);
  EXPECT_EQ(tracker.counters().stale, 1u);
}

// Section 13 test 21: per-class summaries aggregate exactly to the
// cell totals on both counting bases.
TEST(BudgetedRecords, PerClassSummariesAggregateToTotals) {
  ItemTracker tracker{{DemandClassConfig{1}, DemandClassConfig{20}}, 20};
  tracker.begin_window(0);
  tracker.observe_tick(1);
  const ItemId a0 = tracker.admit(0, 1);
  const ItemId a1 = tracker.admit(0, 1);
  const ItemId b0 = tracker.admit(1, 1);
  // Admitted in-window, dependency-ready, never serviced: crosses the
  // 20-tick class-0 starvation window during settlement observations.
  (void)tracker.admit(0, 1);
  tracker.observe_tick(2);
  tracker.resolve(a0, Outcome::Completed, 2);  // Met (deadline 2).
  tracker.observe_tick(4);
  tracker.resolve(a1, Outcome::Completed, 4);  // Late by 2.
  tracker.observe_tick(6);
  tracker.resolve(b0, Outcome::Completed, 6);  // Met (deadline 21).
  for (std::uint64_t tick = 7; tick <= 25; ++tick) {
    tracker.observe_tick(tick);
  }
  tracker.end_window(10);
  tracker.close_settlement();

  const auto summary = tracker.summary();
  ASSERT_EQ(summary.classes.size(), 2u);
  const auto& c0 = summary.classes[0];
  const auto& c1 = summary.classes[1];
  EXPECT_EQ(c0.useful_completions + c1.useful_completions,
            summary.total.useful_completions);
  EXPECT_EQ(c0.cohort_admitted + c1.cohort_admitted,
            summary.total.cohort_admitted);
  EXPECT_EQ(c0.cohort_deadline_met + c1.cohort_deadline_met,
            summary.total.cohort_deadline_met);
  EXPECT_EQ(c0.lateness_ticks.size() + c1.lateness_ticks.size(),
            summary.total.lateness_ticks.size());
  EXPECT_EQ(c0.starved_items + c1.starved_items, summary.total.starved_items);
  EXPECT_EQ(summary.total.useful_completions, 3u);
  EXPECT_EQ(summary.total.cohort_deadline_met, 2u);
  EXPECT_EQ(summary.total.starved_items, 1u);
  EXPECT_EQ(c0.starved_items, 1u);
}

// Section 13 test 23: the service order exercises the full tie-break
// chain — dependency-readiness gates, then Priority, then earliest
// inclusive deadline, then admission sequence.
TEST(BudgetedRecords, ServiceOrderTieBreakChain) {
  ServiceQueue queue;
  const auto gated =
      queue.push({tess::Priority::Immediate, 1, 0, /*ready=*/false});
  const auto maintenance = queue.push({tess::Priority::Maintenance, 1, 1});
  const auto late_deadline = queue.push({tess::Priority::Immediate, 9, 2});
  const auto tied_second = queue.push({tess::Priority::Immediate, 5, 4});
  const auto tied_first = queue.push({tess::Priority::Immediate, 5, 3});

  // Priority beats deadline; deadline beats admission sequence;
  // admission sequence breaks exact ties; not-ready never selected.
  EXPECT_EQ(queue.pop_next(), tied_first);
  EXPECT_EQ(queue.pop_next(), tied_second);
  EXPECT_EQ(queue.pop_next(), late_deadline);
  EXPECT_EQ(queue.pop_next(), maintenance);
  EXPECT_EQ(queue.pop_next(), ServiceQueue::npos);

  // Readiness restored: the gated Immediate item is now first.
  queue.entry(gated).ready = true;
  EXPECT_EQ(queue.pop_next(), gated);
}

// --- Arrival-rate machinery (section 6.2) ---

using budgeted::ArrivalTracker;
using budgeted::RationalRate;

// The Bresenham release accumulator is exact: after T ticks exactly
// floor(T * num / (den * base_tps)) events have been released, with
// no drift and no randomness.
TEST(BudgetedArrival, RationalRateReleasesExactIntegerPattern) {
  // 90 events per sim second at 60 TPS: 1.5 per tick -> 1,2,1,2,...
  RationalRate rate{90, 1, 60};
  std::uint64_t total = 0;
  for (std::uint64_t tick = 1; tick <= 120; ++tick) {
    const std::uint64_t events = rate.release_at_tick();
    EXPECT_EQ(events, tick % 2 == 1 ? 1u : 2u) << "tick " << tick;
    total += events;
    EXPECT_EQ(total, (tick * 90) / 60) << "tick " << tick;
  }

  // One event per sim second at 60 TPS: exactly at every 60th tick.
  RationalRate slow{1, 1, 60};
  std::uint64_t slow_total = 0;
  for (std::uint64_t tick = 1; tick <= 180; ++tick) {
    const std::uint64_t events = slow.release_at_tick();
    EXPECT_EQ(events, tick % 60 == 0 ? 1u : 0u) << "tick " << tick;
    slow_total += events;
  }
  EXPECT_EQ(slow_total, 3u);
}

// FIFO arrival tracking: oldest age follows the next unserviced
// admission, cohort/deadline/lateness/starvation derive from the
// per-item records at seal, and both identities hold throughout.
TEST(BudgetedArrival, ArrivalTrackerFifoWindowAndSeal) {
  ArrivalTracker tracker{/*allowance_ticks=*/2, /*base_tps=*/20,
                         /*expected_items=*/16};
  tracker.begin_window(1);
  tracker.observe_tick(1);
  tracker.admit(1);  // Deadline 3.
  tracker.admit(1);  // Deadline 3.
  tracker.observe_tick(2);
  EXPECT_EQ(tracker.counters().oldest_outstanding_age_ticks, 1u);

  const std::size_t first = tracker.next();
  ASSERT_NE(first, ArrivalTracker::npos);
  tracker.complete(first, 2, 10);  // Met (2 <= 3).
  tracker.observe_tick(5);
  EXPECT_EQ(tracker.counters().oldest_outstanding_age_ticks, 4u);

  const std::size_t second = tracker.next();
  ASSERT_NE(second, ArrivalTracker::npos);
  tracker.complete(second, 5, 10);  // Late by 2 (deadline 3).
  EXPECT_EQ(tracker.next(), ArrivalTracker::npos);

  // Third admission never serviced: starved once its wait crosses
  // max(4 * 2, 20) = 20 ticks.
  tracker.observe_tick(6);
  tracker.admit(6);
  tracker.end_window(10);
  for (std::uint64_t tick = 7; tick <= 30; ++tick) {
    tracker.observe_tick(tick);
  }

  const budgeted::ArrivalSummary summary = tracker.summarize(30);
  EXPECT_EQ(summary.useful_completions, 2u);
  EXPECT_EQ(summary.cohort_admitted, 3u);
  EXPECT_EQ(summary.cohort_deadline_met, 1u);
  ASSERT_EQ(summary.lateness_ticks.size(), 1u);
  EXPECT_EQ(summary.lateness_ticks[0], 2u);
  EXPECT_EQ(summary.starved_items, 1u);
  EXPECT_TRUE(tracker.counters().admission_identity_holds());
  EXPECT_TRUE(tracker.counters().retention_identity_holds());
  EXPECT_EQ(tracker.counters().outstanding_current, 1u);
  // Window-scoped oldest-age samples were collected each observation.
  EXPECT_FALSE(tracker.oldest_age_samples().empty());
}

// --- Capacity boundary search (section 9.3) ---

using budgeted::CapacityBand;
using budgeted::PointKind;
using budgeted::SearchPolicy;
using budgeted::SearchResult;

// A clean monotone boundary converges to within the terminal
// resolution, confirms, and reports a non-inverted band with every
// tested point retained and zero flapping.
TEST(BudgetedSearch, MonotoneBoundaryConvergesWithinResolution) {
  const std::uint64_t capacity = 1000;
  auto probe = [&](std::uint64_t rate) { return rate <= capacity; };
  auto confirm = probe;
  const SearchResult result =
      budgeted::search_capacity(SearchPolicy{60, 2, 24}, probe, confirm);

  ASSERT_TRUE(result.band.confirmed_stable.has_value());
  ASSERT_TRUE(result.band.lowest_unstable.has_value());
  const std::uint64_t confirmed = result.band.confirmed_stable.value_or(0);
  EXPECT_LE(confirmed, capacity);
  EXPECT_GE(confirmed, capacity - std::max<std::uint64_t>(1, capacity / 50));
  EXPECT_GT(result.band.lowest_unstable.value_or(0), confirmed);
  EXPECT_EQ(result.flapping, 0u);
  EXPECT_FALSE(result.points.empty());
  for (const auto& point : result.points) {
    EXPECT_EQ(point.stable, point.rate <= capacity);
  }
}

// A confirmation that fails at the search's candidate records the
// point as unstable and steps down one resolution unit at a time; the
// failed points become band-edge evidence, never a re-roll.
TEST(BudgetedSearch, ConfirmationFailureStepsDown) {
  auto probe = [](std::uint64_t rate) { return rate <= 1000; };
  auto confirm = [](std::uint64_t rate) { return rate <= 950; };
  const SearchResult result =
      budgeted::search_capacity(SearchPolicy{60, 2, 24}, probe, confirm);

  ASSERT_TRUE(result.band.confirmed_stable.has_value());
  EXPECT_LE(result.band.confirmed_stable.value_or(0), 950u);
  ASSERT_TRUE(result.band.lowest_unstable.has_value());
  EXPECT_GT(result.band.lowest_unstable.value_or(0),
            result.band.confirmed_stable.value_or(0));
  std::uint64_t failed_confirmations = 0;
  for (const auto& point : result.points) {
    if (point.kind == PointKind::Confirmation && !point.stable) {
      ++failed_confirmations;
    }
  }
  EXPECT_GE(failed_confirmations, 1u);
}

// A noisy boundary (deterministically flapping verdicts inside a
// band) is reported as flapping and the band still cannot invert.
TEST(BudgetedSearch, FlappingBoundaryNeverInvertsTheBand) {
  auto probe = [](std::uint64_t rate) {
    if (rate <= 950) {
      return true;
    }
    if (rate >= 1050) {
      return false;
    }
    return rate % 2 == 0;  // Deterministic flapping zone.
  };
  auto confirm = [](std::uint64_t rate) { return rate <= 980; };
  const SearchResult result =
      budgeted::search_capacity(SearchPolicy{60, 2, 24}, probe, confirm);

  ASSERT_TRUE(result.band.confirmed_stable.has_value());
  if (result.band.lowest_unstable.has_value()) {
    EXPECT_GT(result.band.lowest_unstable.value_or(0),
              result.band.confirmed_stable.value_or(0));
  }
}

// A seed above capacity halves down before bracketing; a workload
// that sustains nothing confirms nothing and reports only the lowest
// unstable observation.
TEST(BudgetedSearch, UnstableSeedAndHopelessWorkload) {
  auto low_probe = [](std::uint64_t rate) { return rate <= 10; };
  const SearchResult low =
      budgeted::search_capacity(SearchPolicy{60, 2, 24}, low_probe, low_probe);
  ASSERT_TRUE(low.band.confirmed_stable.has_value());
  EXPECT_LE(low.band.confirmed_stable.value_or(0), 10u);
  EXPECT_GE(low.band.confirmed_stable.value_or(0), 9u);

  auto never = [](std::uint64_t) { return false; };
  const SearchResult hopeless =
      budgeted::search_capacity(SearchPolicy{60, 2, 24}, never, never);
  EXPECT_FALSE(hopeless.band.confirmed_stable.has_value());
  ASSERT_TRUE(hopeless.band.lowest_unstable.has_value());
  EXPECT_EQ(hopeless.band.lowest_unstable.value_or(0), 1u);
}

// --- Summary derivation and artifact emission (sections 11-12) ---

using budgeted::Artifact;
using budgeted::emit_artifact_json;
using budgeted::PercentileFamily;
using budgeted::Sha256;
using budgeted::summarize_family;

// Percentiles publish only at their section 11.4 sample minimums and
// name their sample base; nearest-rank is exact.
TEST(BudgetedArtifact, PercentileMinimumsAndNearestRank) {
  std::vector<std::uint64_t> small(25);
  for (std::uint64_t i = 0; i < 25; ++i) {
    small[static_cast<std::size_t>(i)] = i + 1;
  }
  const PercentileFamily few = summarize_family("all_measured_frames", small);
  EXPECT_EQ(few.sample_base, "all_measured_frames");
  EXPECT_TRUE(few.p50.sufficient);
  EXPECT_EQ(few.p50.value, 13u);  // ceil(0.5 * 25) = 13th smallest.
  EXPECT_FALSE(few.p95.sufficient);
  EXPECT_FALSE(few.p99.sufficient);
  EXPECT_FALSE(few.p999.sufficient);
  EXPECT_TRUE(few.max.sufficient);
  EXPECT_EQ(few.max.value, 25u);

  const PercentileFamily none = summarize_family("empty", {});
  EXPECT_FALSE(none.p50.sufficient);
  EXPECT_FALSE(none.max.sufficient);

  std::vector<std::uint64_t> large(2'000);
  for (std::uint64_t i = 0; i < 2'000; ++i) {
    large[static_cast<std::size_t>(i)] = i;
  }
  const PercentileFamily many = summarize_family("frames", large);
  EXPECT_TRUE(many.p99.sufficient);
  EXPECT_EQ(many.p99.value, 1979u);  // ceil(0.99 * 2000) = 1980th = 1979.
  EXPECT_FALSE(many.p999.sufficient);
}

// The embedded SHA-256 matches the FIPS 180-4 test vectors.
TEST(BudgetedArtifact, Sha256KnownVectors) {
  Sha256 empty;
  EXPECT_EQ(empty.hex_digest(),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

  Sha256 abc;
  abc.update("abc", 3);
  EXPECT_EQ(abc.hex_digest(),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  // Finalization is idempotent: a second call returns the same digest.
  EXPECT_EQ(abc.hex_digest(), abc.hex_digest());
}

// Saturated artifacts omit the deadline group and paced-only fields
// entirely; suppressed percentiles emit null; strings are escaped.
TEST(BudgetedArtifact, SaturatedArtifactOmitsInapplicableGroups) {
  Artifact artifact;
  artifact.run.commit = "abc123";
  artifact.run.compiler = "clang \"21\"";
  artifact.experiment.kind = "isolated_saturated";
  artifact.experiment.scenario_id = "astar-unit-roomcorridor-512";
  artifact.experiment.workload_refs = {"path/astar_unit"};
  artifact.experiment.budget_ns = 500'000;
  artifact.trace.sha256 = "deadbeef";
  artifact.summary.measured_frames = 600;
  artifact.summary.frame_elapsed_ns =
      summarize_family("all_measured_frames", {1, 2, 3});
  artifact.calibration.clock_identity = "scripted";

  const std::string json = emit_artifact_json(artifact);

  EXPECT_NE(json.find("\"schema\": \"tess.budgeted_progress.v1\""),
            std::string::npos);
  EXPECT_NE(json.find("\"settlement_ticks\": 0"), std::string::npos);
  EXPECT_EQ(json.find("deadline_success_rate"), std::string::npos);
  EXPECT_EQ(json.find("frame_start_lag_ns"), std::string::npos);
  EXPECT_EQ(json.find("flow_stable"), std::string::npos);
  EXPECT_NE(json.find("\"capacity_band\": null"), std::string::npos);
  EXPECT_NE(json.find("\"correctness_hash\": null"), std::string::npos);
  EXPECT_NE(json.find("\"p50\": null"), std::string::npos);
  EXPECT_NE(json.find("\"max\": 3"), std::string::npos);
  EXPECT_EQ(json.find("useful_per_wall_second"), std::string::npos);
  EXPECT_EQ(json.find("measured_wall_ns"), std::string::npos);
  EXPECT_NE(json.find("clang \\\"21\\\""), std::string::npos);
}

// Demand-limited paced artifacts carry the deadline group, the lag
// family, and the flow-stability verdict.
TEST(BudgetedArtifact, DemandLimitedArtifactCarriesDeadlineGroup) {
  Artifact artifact;
  artifact.experiment.kind = "isolated_arrival_rate";
  artifact.experiment.pacing = "paced";
  artifact.summary.frame_start_lag_ns = summarize_family("paced_frames", {});
  budgeted::DeadlineGroup deadlines;
  deadlines.deadline_success_rate = 0.995;
  deadlines.lateness_ticks = summarize_family("completed_cohort_items", {});
  deadlines.oldest_age_ticks = summarize_family("per_tick_observations", {});
  deadlines.starved_items = 0;
  artifact.summary.deadlines = deadlines;
  artifact.summary.flow_stable_applicable = true;
  artifact.summary.flow_stable = true;
  artifact.summary.measured_wall_ns = 1'000'000'000;
  artifact.summary.useful_per_wall_second = 599.5;
  artifact.summary.correctness_hash = "cafef00d";
  budgeted::ClassArtifact interactive;
  interactive.class_id = "interactive_path";
  interactive.deadline_allowance_ticks = 1;
  interactive.useful_completions = 9;
  interactive.cohort_admitted = 9;
  interactive.deadline_success_rate = 0.995;
  interactive.lateness_ticks = summarize_family("completed_cohort_items", {});
  artifact.classes.push_back(interactive);

  const std::string json = emit_artifact_json(artifact);

  EXPECT_NE(json.find("\"deadline_success_rate\": 0.995"), std::string::npos);
  EXPECT_NE(json.find("\"frame_start_lag_ns\""), std::string::npos);
  EXPECT_NE(json.find("\"flow_stable\": true"), std::string::npos);
  EXPECT_NE(json.find("\"correctness_hash\": \"cafef00d\""), std::string::npos);
  EXPECT_NE(json.find("\"sample_base\": \"completed_cohort_items\""),
            std::string::npos);
  EXPECT_NE(json.find("\"classes\": [{\"class_id\": \"interactive_path\""),
            std::string::npos);
  EXPECT_NE(json.find("\"measured_wall_ns\": 1000000000"), std::string::npos);
  EXPECT_NE(json.find("\"useful_per_wall_second\": 599.5"), std::string::npos);
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
