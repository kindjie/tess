#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "allocation_counter.h"

// S7.1: the Schedule core -- phase-major then registration-order execution,
// cadence exactness (EveryTick / EveryN under backlog and disablement /
// OnDirty with own-bit clearing / Background continuation / Manual),
// same-tick dirty propagation to later phases, deterministic trigger
// consumption, and allocation-free dispatch after seal.
namespace {

constexpr std::uint32_t DirtyTerrain = 1u << 0u;
constexpr std::uint32_t DirtyConstruction = 1u << 1u;
constexpr std::uint32_t EventArrived = 1u << 0u;
constexpr std::uint32_t EventBlocked = 1u << 1u;

// Records its runs into a shared log; returns a fixed result.
struct LogTask {
  std::string label;
  std::vector<std::string>* log = nullptr;
  tess::ScheduleTaskResult result{};
  std::uint32_t last_pending = 0;
  std::uint32_t last_budget = 0;
  std::uint32_t last_events = 0;

  auto operator()(const tess::ScheduleTaskContext& context)
      -> tess::ScheduleTaskResult {
    if (log != nullptr) {
      log->push_back(label);
    }
    last_pending = context.pending_dirty;
    last_budget = context.budget_items;
    last_events = context.pending_events;
    return result;
  }
};

TEST(TessSchedule, RunsPhasesInOrderThenRegistrationOrder) {
  std::vector<std::string> log;
  LogTask render{"render", &log};
  LogTask ops{"ops", &log};
  LogTask agents_a{"agents_a", &log};
  LogTask agents_b{"agents_b", &log};

  tess::Schedule schedule;
  schedule.reserve_tasks(4);
  // Registered out of phase order deliberately.
  (void)schedule.add_task(
      {"render", tess::SimPhase::RenderDelta, tess::Cadence::every_tick()},
      render);
  (void)schedule.add_task(
      {"agents_a", tess::SimPhase::Movement, tess::Cadence::every_tick()},
      agents_a);
  (void)schedule.add_task(
      {"ops", tess::SimPhase::PreUpdate, tess::Cadence::every_tick()}, ops);
  (void)schedule.add_task(
      {"agents_b", tess::SimPhase::Movement, tess::Cadence::every_tick()},
      agents_b);
  schedule.seal();

  tess::SimClock clock;
  const auto stats = schedule.run_tick(clock);
  EXPECT_EQ(stats.tick, 1u);
  EXPECT_EQ(stats.tasks_run, 4u);
  ASSERT_EQ(log.size(), 4u);
  EXPECT_EQ(log[0], "ops");
  EXPECT_EQ(log[1], "agents_a");
  EXPECT_EQ(log[2], "agents_b");
  EXPECT_EQ(log[3], "render");
}

TEST(TessSchedule, EveryNIsExactAndDisablementKeepsLockstep) {
  std::vector<std::string> log;
  LogTask task{"n", &log};
  tess::Schedule schedule;
  const auto id = schedule.add_task(
      {"n", tess::SimPhase::PreUpdate, tess::Cadence::every_ticks(4)}, task);
  schedule.seal();

  tess::SimClock clock;
  std::vector<std::uint64_t> run_ticks;
  for (int i = 0; i < 17; ++i) {
    // Disable across the tick-8 due point: the countdown still advances and
    // resets, the run is skipped, and the NEXT fire stays at tick 12.
    schedule.set_enabled(id, !(clock.tick >= 6 && clock.tick < 9));
    const auto stats = schedule.run_tick(clock);
    if (stats.tasks_run == 1) {
      run_ticks.push_back(stats.tick);
    }
  }
  ASSERT_EQ(run_ticks.size(), 3u);
  EXPECT_EQ(run_ticks[0], 4u);
  EXPECT_EQ(run_ticks[1], 12u);
  EXPECT_EQ(run_ticks[2], 16u);
  EXPECT_EQ(schedule.task_stats(id).skipped, 1u);  // the tick-8 due point
}

TEST(TessSchedule, OnDirtyFiresIffOwnMaskBitsArePending) {
  LogTask terrain{"terrain"};
  LogTask construction{"construction"};
  tess::Schedule schedule;
  (void)schedule.add_task({"terrain", tess::SimPhase::Topology,
                           tess::Cadence::on_dirty(DirtyTerrain)},
                          terrain);
  (void)schedule.add_task({"construction", tess::SimPhase::Topology,
                           tess::Cadence::on_dirty(DirtyConstruction)},
                          construction);
  schedule.seal();

  tess::SimClock clock;
  // No dirty: neither fires.
  EXPECT_EQ(schedule.run_tick(clock).tasks_run, 0u);

  // Both bits at once: each task consumes ONLY its own bit.
  schedule.notify_dirty(DirtyTerrain | DirtyConstruction);
  auto stats = schedule.run_tick(clock);
  EXPECT_EQ(stats.tasks_run, 2u);
  EXPECT_EQ(terrain.last_pending, DirtyTerrain);
  EXPECT_EQ(construction.last_pending, DirtyConstruction);

  // Consumed: nothing re-fires without a new mark.
  EXPECT_EQ(schedule.run_tick(clock).tasks_run, 0u);

  // A foreign bit alone never fires a task.
  schedule.notify_dirty(1u << 7u);
  EXPECT_EQ(schedule.run_tick(clock).tasks_run, 0u);
}

TEST(TessSchedule, ProducedDirtyReachesLaterPhasesSameTickEarlierNext) {
  std::vector<std::string> log;
  LogTask producer{"producer", &log,
                   tess::ScheduleTaskResult{DirtyTerrain, 0, false}};
  LogTask late{"late", &log};
  LogTask early{"early", &log};
  tess::Schedule schedule;
  // Producer runs once (Manual) in Movement; consumers on the same mask sit
  // in a later phase (Topology) and an earlier phase (PreUpdate).
  const auto producer_id = schedule.add_task(
      {"producer", tess::SimPhase::Movement, tess::Cadence::manual()},
      producer);
  (void)schedule.add_task({"early", tess::SimPhase::PreUpdate,
                           tess::Cadence::on_dirty(DirtyTerrain)},
                          early);
  (void)schedule.add_task(
      {"late", tess::SimPhase::Topology, tess::Cadence::on_dirty(DirtyTerrain)},
      late);
  schedule.seal();

  tess::SimClock clock;
  schedule.request_run(producer_id);
  auto stats = schedule.run_tick(clock);
  EXPECT_EQ(stats.dirty_mask_produced, DirtyTerrain);
  ASSERT_EQ(log.size(), 2u);
  EXPECT_EQ(log[0], "producer");
  EXPECT_EQ(log[1], "late");  // same tick, later phase

  log.clear();
  (void)schedule.run_tick(clock);
  ASSERT_EQ(log.size(), 1u);
  EXPECT_EQ(log[0], "early");  // next tick, earlier phase
}

TEST(TessSchedule, OnEventCoalescesAndConsumesOnlySubscribedBits) {
  LogTask arrived{"arrived"};
  LogTask blocked{"blocked"};
  tess::Schedule schedule;
  (void)schedule.add_task(
      {"arrived", tess::SimPhase::AI, tess::Cadence::on_event(EventArrived)},
      arrived);
  (void)schedule.add_task(
      {"blocked", tess::SimPhase::AI, tess::Cadence::on_event(EventBlocked)},
      blocked);
  schedule.seal();

  tess::SimClock clock;
  EXPECT_EQ(schedule.run_tick(clock).tasks_run, 0u);

  schedule.notify_events(EventArrived);
  schedule.notify_events(EventArrived | EventBlocked);
  const auto stats = schedule.run_tick(clock);
  EXPECT_EQ(stats.tasks_run, 2u);
  EXPECT_EQ(arrived.last_events, EventArrived);
  EXPECT_EQ(blocked.last_events, EventBlocked);
  EXPECT_EQ(schedule.run_tick(clock).tasks_run, 0u);

  schedule.notify_events(1u << 7u);
  EXPECT_EQ(schedule.run_tick(clock).tasks_run, 0u);
}

TEST(TessSchedule, ProducedEventsReachLaterPhasesSameTickEarlierNext) {
  std::vector<std::string> log;
  LogTask producer{"producer", &log};
  producer.result.event_mask = EventBlocked;
  LogTask late{"late", &log};
  LogTask early{"early", &log};
  tess::Schedule schedule;
  const auto producer_id = schedule.add_task(
      {"producer", tess::SimPhase::Movement, tess::Cadence::manual()},
      producer);
  (void)schedule.add_task({"early", tess::SimPhase::PreUpdate,
                           tess::Cadence::on_event(EventBlocked)},
                          early);
  (void)schedule.add_task(
      {"late", tess::SimPhase::Topology, tess::Cadence::on_event(EventBlocked)},
      late);
  schedule.seal();

  tess::SimClock clock;
  schedule.request_run(producer_id);
  const auto stats = schedule.run_tick(clock);
  EXPECT_EQ(stats.event_mask_produced, EventBlocked);
  ASSERT_EQ(log.size(), 2u);
  EXPECT_EQ(log[0], "producer");
  EXPECT_EQ(log[1], "late");

  log.clear();
  (void)schedule.run_tick(clock);
  ASSERT_EQ(log.size(), 1u);
  EXPECT_EQ(log[0], "early");
}

TEST(TessSchedule, EventStreamKeepsExactTickStampedOrderAndRejectsOverflow) {
  struct Arrival {
    std::uint32_t entity = 0;
  };
  tess::EventStream<Arrival> stream;
  stream.reserve_events(3);

  EXPECT_TRUE(stream.publish(4, Arrival{11}));
  EXPECT_TRUE(stream.publish(4, Arrival{12}));
  EXPECT_TRUE(stream.publish(7, Arrival{13}));
  EXPECT_FALSE(stream.publish(8, Arrival{14}));

  const auto events = stream.events();
  ASSERT_EQ(events.size(), 3u);
  EXPECT_EQ(events[0].tick, 4u);
  EXPECT_EQ(events[0].sequence, 0u);
  EXPECT_EQ(events[0].value.entity, 11u);
  EXPECT_EQ(events[1].tick, 4u);
  EXPECT_EQ(events[1].sequence, 1u);
  EXPECT_EQ(events[2].tick, 7u);
  EXPECT_EQ(events[2].sequence, 2u);
  EXPECT_EQ(stream.rejected_events(), 1u);

  stream.clear();
  EXPECT_TRUE(stream.empty());
  EXPECT_TRUE(stream.publish(9, Arrival{15}));
  ASSERT_EQ(stream.events().size(), 1u);
  EXPECT_EQ(stream.events()[0].sequence, 3u);
}

TEST(TessSchedule, WarmEventPublicationAndDispatchAreAllocationFree) {
  struct Event {
    std::uint32_t value = 0;
  };
  tess::EventStream<Event> stream;
  stream.reserve_events(1);
  LogTask task{"event"};
  tess::Schedule schedule;
  schedule.reserve_tasks(1);
  (void)schedule.add_task(
      {"event", tess::SimPhase::AI, tess::Cadence::on_event(EventArrived)},
      task);
  schedule.seal();
  tess::SimClock clock;

  {
    tess_test::ScopedAllocationCounter counter;
    for (std::uint32_t i = 0; i < 32; ++i) {
      stream.clear();
      EXPECT_TRUE(schedule.publish_event(EventArrived, stream, clock.tick + 1,
                                         Event{i}));
      (void)schedule.run_tick(clock);
    }
    EXPECT_EQ(counter.count(), 0u);
  }
}

TEST(TessSchedule, ResumableWorkTaskRetainsBackgroundWorkAcrossTicks) {
  struct Work {
    std::uint32_t remaining = 5;
    auto operator()(tess::AsyncWorkBudget budget, std::uint32_t& result)
        -> tess::AsyncWorkStep {
      const auto count = std::min(budget.max_items, remaining);
      remaining -= count;
      result += count;
      return {
          remaining == 0 ? tess::AsyncStepState::Ready
                         : tess::AsyncStepState::Pending,
          count,
          tess::AsyncVersion{9},
      };
    }
  };

  Work work;
  tess::ResumableWorkQueue<std::uint32_t> queue;
  queue.reserve_tickets(1);
  const auto ticket = queue.submit(work);
  tess::ResumableWorkTask<std::uint32_t> task{queue};
  tess::Schedule schedule;
  const auto task_id =
      schedule.add_task({"async", tess::SimPhase::Background,
                         tess::Cadence::background(tess::BackgroundBudget{2})},
                        task);
  schedule.seal();

  tess::SimClock clock;
  schedule.request_run(task_id);
  EXPECT_EQ(schedule.run_tick(clock).background_items, 2u);
  EXPECT_EQ(queue.state(ticket), tess::AsyncResultState::Pending);
  EXPECT_EQ(schedule.run_tick(clock).background_items, 2u);
  EXPECT_EQ(queue.state(ticket), tess::AsyncResultState::Pending);
  EXPECT_EQ(schedule.run_tick(clock).background_items, 1u);
  EXPECT_EQ(queue.state(ticket), tess::AsyncResultState::Ready);
  EXPECT_EQ(schedule.run_tick(clock).tasks_run, 0u);
  ASSERT_NE(queue.result(ticket), nullptr);
  EXPECT_EQ(*queue.result(ticket), 5u);
}

// Background: armed once, then continues on its own while more_work holds,
// consuming a deterministic item budget per tick.
struct DrainTask {
  std::uint32_t remaining = 0;
  std::vector<std::uint32_t> per_tick;

  auto operator()(const tess::ScheduleTaskContext& context)
      -> tess::ScheduleTaskResult {
    const auto batch = std::min(context.budget_items, remaining);
    remaining -= batch;
    per_tick.push_back(batch);
    return tess::ScheduleTaskResult{0, batch, remaining > 0};
  }
};

TEST(TessSchedule, BackgroundBudgetContinuesDeterministically) {
  DrainTask drain{10, {}};
  tess::Schedule schedule;
  const auto id =
      schedule.add_task({"drain", tess::SimPhase::Background,
                         tess::Cadence::background(tess::BackgroundBudget{3})},
                        drain);
  schedule.seal();

  tess::SimClock clock;
  schedule.request_run(id);
  std::uint32_t total = 0;
  for (int i = 0; i < 6; ++i) {
    total += schedule.run_tick(clock).background_items;
  }
  EXPECT_EQ(total, 10u);
  ASSERT_EQ(drain.per_tick.size(), 4u);  // 3 + 3 + 3 + 1, then dormant
  EXPECT_EQ(drain.per_tick[0], 3u);
  EXPECT_EQ(drain.per_tick[3], 1u);
  EXPECT_EQ(schedule.task_stats(id).background_items, 10u);
  EXPECT_EQ(schedule.task_stats(id).runs, 4u);
}

TEST(TessSchedule, ManualRunsExactlyOncePerRequest) {
  LogTask task{"manual"};
  std::vector<std::string> log;
  task.log = &log;
  tess::Schedule schedule;
  const auto id = schedule.add_task(
      {"manual", tess::SimPhase::PreUpdate, tess::Cadence::manual()}, task);
  schedule.seal();

  tess::SimClock clock;
  EXPECT_EQ(schedule.run_tick(clock).tasks_run, 0u);
  schedule.request_run(id);
  EXPECT_EQ(schedule.run_tick(clock).tasks_run, 1u);
  EXPECT_EQ(schedule.run_tick(clock).tasks_run, 0u);
}

TEST(TessSchedule, PersistentTriggersSurviveDisablement) {
  LogTask task{"gated"};
  tess::Schedule schedule;
  const auto id = schedule.add_task({"gated", tess::SimPhase::Topology,
                                     tess::Cadence::on_dirty(DirtyTerrain)},
                                    task);
  schedule.seal();

  tess::SimClock clock;
  schedule.set_enabled(id, false);
  schedule.notify_dirty(DirtyTerrain);
  EXPECT_EQ(schedule.run_tick(clock).tasks_skipped, 1u);
  EXPECT_EQ(schedule.run_tick(clock).tasks_skipped, 1u);  // still armed
  schedule.set_enabled(id, true);
  EXPECT_EQ(schedule.run_tick(clock).tasks_run, 1u);
  EXPECT_EQ(schedule.run_tick(clock).tasks_run, 0u);
}

TEST(TessSchedule, DispatchAfterSealIsAllocationFree) {
  LogTask every{"every"};
  LogTask dirty{"dirty"};
  DrainTask drain{1000, {}};
  drain.per_tick.reserve(64);
  tess::Schedule schedule;
  schedule.reserve_tasks(3);
  (void)schedule.add_task(
      {"every", tess::SimPhase::Movement, tess::Cadence::every_tick()}, every);
  (void)schedule.add_task({"dirty", tess::SimPhase::Topology,
                           tess::Cadence::on_dirty(DirtyTerrain)},
                          dirty);
  const auto drain_id =
      schedule.add_task({"drain", tess::SimPhase::Background,
                         tess::Cadence::background(tess::BackgroundBudget{4})},
                        drain);
  schedule.seal();

  tess::SimClock clock;
  schedule.request_run(drain_id);
  {
    tess_test::ScopedAllocationCounter counter;
    for (int i = 0; i < 32; ++i) {
      schedule.notify_dirty(DirtyTerrain);
      (void)schedule.run_tick(clock);
    }
    EXPECT_EQ(counter.count(), 0u);
  }
}

// S7.2: the frame driver -- cadences count fixed ticks, never frames, so
// EveryN stays exact across SimSpeed changes and backlogged frames.
TEST(TessSchedule, FrameDriverKeepsEveryNExactAcrossSpeedAndBacklog) {
  LogTask task{"n"};
  std::vector<std::string> log;
  task.log = &log;
  tess::Schedule schedule;
  const auto id = schedule.add_task(
      {"n", tess::SimPhase::PreUpdate, tess::Cadence::every_ticks(4)}, task);
  schedule.seal();

  // 20 ticks/second, up to 8 per frame.
  tess::FixedStepAccumulator accumulator{20, 8};
  tess::SimClock clock;

  // 1x: 0.05 s frames grant one tick each; the task fires on ticks 4 and 8.
  for (int frame = 0; frame < 8; ++frame) {
    (void)tess::run_schedule_frame(
        schedule, clock, accumulator, 0.05,
        tess::SimTimeControl{tess::SimSpeed::Speed1x});
  }
  EXPECT_EQ(clock.tick, 8u);
  EXPECT_EQ(schedule.task_stats(id).runs, 2u);

  // 4x: the same real frames grant four ticks each; two frames add eight
  // ticks and exactly two more fires (ticks 12 and 16).
  for (int frame = 0; frame < 2; ++frame) {
    const auto summary =
        tess::run_schedule_frame(schedule, clock, accumulator, 0.05,
                                 tess::SimTimeControl{tess::SimSpeed::Speed4x});
    EXPECT_EQ(summary.ticks, 4u);
  }
  EXPECT_EQ(clock.tick, 16u);
  EXPECT_EQ(schedule.task_stats(id).runs, 4u);

  // A backlogged frame (0.36 s at 1x banks 7 whole ticks; 0.35 sits at
  // 6.999... in the double domain and would grant 6) advances the cadence
  // through every granted tick.
  const auto backlog =
      tess::run_schedule_frame(schedule, clock, accumulator, 0.36,
                               tess::SimTimeControl{tess::SimSpeed::Speed1x});
  EXPECT_EQ(backlog.ticks, 7u);
  EXPECT_EQ(clock.tick, 23u);
  EXPECT_EQ(schedule.task_stats(id).runs, 5u);  // tick 20 fired in-frame

  // Paused frames grant nothing and change nothing.
  const auto paused =
      tess::run_schedule_frame(schedule, clock, accumulator, 10.0,
                               tess::SimTimeControl{tess::SimSpeed::Paused});
  EXPECT_EQ(paused.ticks, 0u);
  EXPECT_EQ(clock.tick, 23u);
}

// Review hardening: request_run on an OnDirty task delivers pending_dirty
// == 0 (a full-run poke, not a no-op) and consumes the request.
TEST(TessSchedule, RequestRunPokesOnDirtyWithZeroMask) {
  LogTask task{"gated"};
  tess::Schedule schedule;
  const auto id = schedule.add_task(
      {"gated", tess::SimPhase::Topology, tess::Cadence::on_dirty(1u)}, task);
  schedule.seal();

  tess::SimClock clock;
  schedule.request_run(id);
  task.last_pending = 0xffffffffu;
  EXPECT_EQ(schedule.run_tick(clock).tasks_run, 1u);
  EXPECT_EQ(task.last_pending, 0u);
  EXPECT_EQ(schedule.run_tick(clock).tasks_run, 0u);  // consumed
}

// A task callback that throws must not latch the schedule "in run" --
// the next tick would fail the reentrancy assert even though the
// schedule state is recoverable (audit 2026-07-11 C2).
TEST(TessSchedule, ThrowingTaskDoesNotLatchInRun) {
  struct ThrowOnceTask {
    bool armed = true;
    auto operator()(const tess::ScheduleTaskContext&)
        -> tess::ScheduleTaskResult {
      if (armed) {
        armed = false;
        throw std::runtime_error{"task failure"};
      }
      return {};
    }
  };
  ThrowOnceTask task;
  tess::Schedule schedule;
  schedule.add_task(
      {"throws", tess::SimPhase::PreUpdate, tess::Cadence::every_tick()}, task);
  schedule.seal();

  tess::SimClock clock;
  EXPECT_THROW(schedule.run_tick(clock), std::runtime_error);
  const auto stats = schedule.run_tick(clock);
  EXPECT_EQ(stats.tasks_run, 1u);
}

// Codex review (audit3 W3): a redundant seal() -- even from inside a task
// callback mid-tick -- must be a no-op, not a rebuild of the dispatch
// order run_tick is iterating.
TEST(TessSchedule, RedundantSealInsideTaskIsANoOp) {
  struct ResealTask {
    tess::Schedule* schedule = nullptr;
    int runs = 0;
    auto operator()(const tess::ScheduleTaskContext&)
        -> tess::ScheduleTaskResult {
      ++runs;
      schedule->seal();
      return {};
    }
  };
  ResealTask reseal;
  LogTask after{"after"};
  std::vector<std::string> log;
  after.log = &log;
  tess::Schedule schedule;
  reseal.schedule = &schedule;
  schedule.add_task(
      {"reseal", tess::SimPhase::PreUpdate, tess::Cadence::every_tick()},
      reseal);
  schedule.add_task(
      {"after", tess::SimPhase::Movement, tess::Cadence::every_tick()}, after);
  schedule.seal();

  tess::SimClock clock;
  EXPECT_EQ(schedule.run_tick(clock).tasks_run, 2u);
  EXPECT_EQ(reseal.runs, 1);
  ASSERT_EQ(log.size(), 1u);
  EXPECT_EQ(log[0], "after");
}

// Codex review: request_run must arm EveryN tasks too -- one extra run
// without shifting the countdown's lockstep phase.
TEST(TessSchedule, RequestRunAddsAnExtraEveryNRun) {
  LogTask task{"n"};
  tess::Schedule schedule;
  const auto id = schedule.add_task(
      {"n", tess::SimPhase::PreUpdate, tess::Cadence::every_ticks(4)}, task);
  schedule.seal();

  tess::SimClock clock;
  schedule.request_run(id);
  EXPECT_EQ(schedule.run_tick(clock).tasks_run, 1u);  // tick 1: the poke
  EXPECT_EQ(schedule.run_tick(clock).tasks_run, 0u);  // consumed
  (void)schedule.run_tick(clock);
  EXPECT_EQ(schedule.run_tick(clock).tasks_run, 1u);  // tick 4: lockstep held
}

}  // namespace
