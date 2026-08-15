// Conservation-identity and transition-point coverage for the flow
// accounting layer (redesign section 3.3, queue-flow accounting):
// FlowCounters identities, the delta-weighted tick observation
// protocol, and the ResumableWorkQueue's exhaustive transition mapping
// including the documented completed->stale reclassification.

#include <gtest/gtest.h>
#include <tess/diagnostics/diagnostics.h>
#include <tess/experimental/maintenance.h>
#include <tess/ops/async_work.h>
#include <tess/sim/event_stream.h>
#include <tess/sim/joint_movement.h>
#include <tess/tess.h>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {

namespace mv = tess::movement;

using tess::diagnostics::FlowAccounting;
using tess::diagnostics::FlowCounters;

auto step_ready(void*, tess::AsyncWorkBudget, int& value)
    -> tess::AsyncWorkStep {
  value = 7;
  return {tess::AsyncStepState::Ready, 1, tess::AsyncVersion{2}};
}

auto step_pending(void*, tess::AsyncWorkBudget, int&) -> tess::AsyncWorkStep {
  return {tess::AsyncStepState::Pending, 1, {}};
}

auto step_failed(void*, tess::AsyncWorkBudget, int&) -> tess::AsyncWorkStep {
  return {tess::AsyncStepState::Failed, 0, {}};
}

TEST(TessFlowCounters, IdentitiesHoldOnZeroAndSimpleFlows) {
  FlowCounters counters;
  EXPECT_TRUE(counters.admission_identity_holds());
  EXPECT_TRUE(counters.retention_identity_holds());

  counters.offered = 5;
  counters.admitted = 3;
  counters.rejected = 1;
  counters.coalesced_into_pending = 1;
  counters.completed = 2;
  counters.outstanding_current = 1;
  EXPECT_TRUE(counters.admission_identity_holds());
  EXPECT_TRUE(counters.retention_identity_holds());
  EXPECT_EQ(counters.terminal(), 2u);

  counters.failed = 1;  // one admission, two buckets: identity breaks
  EXPECT_FALSE(counters.retention_identity_holds());
}

TEST(TessFlowAccounting, ObserveTickWeightsInventoryByElapsedTicks) {
  FlowAccounting accounting;
  accounting.counters.outstanding_current = 3;
  accounting.observe_tick(4);
  EXPECT_EQ(accounting.counters.inventory_tick_weighted, 12u);
  accounting.observe_tick(6);
  EXPECT_EQ(accounting.counters.inventory_tick_weighted, 18u);
  accounting.observe_tick(6);  // non-advancing observation adds nothing
  EXPECT_EQ(accounting.counters.inventory_tick_weighted, 18u);
}

TEST(TessFlowAccounting, SnapshotReportsBothIdentities) {
  FlowAccounting accounting;
  accounting.counters.offered = 1;
  const auto health = tess::diagnostics::snapshot(accounting);
  EXPECT_FALSE(health.admission_identity_ok);
  EXPECT_TRUE(health.retention_identity_ok);
}

TEST(TessAsyncFlow, SubmitAdvanceAndTerminalsKeepIdentities) {
  tess::ResumableWorkQueue<int> queue;
  FlowAccounting accounting;
  queue.set_flow_accounting(&accounting);
  queue.observe_flow_tick(1);

  auto ready_work = step_ready;
  auto pending_work = step_pending;
  auto failed_work = step_failed;
  const auto ready = queue.submit(nullptr, ready_work);
  const auto cancelled = queue.submit(nullptr, pending_work);
  const auto superseded = queue.submit(nullptr, pending_work);
  const auto failed = queue.submit(nullptr, failed_work);
  (void)ready;
  (void)failed;
  EXPECT_EQ(accounting.counters.offered, 4u);
  EXPECT_EQ(accounting.counters.admitted, 4u);
  EXPECT_EQ(accounting.counters.outstanding_current, 4u);
  EXPECT_EQ(accounting.counters.outstanding_high_water, 4u);

  EXPECT_TRUE(queue.cancel(cancelled));
  EXPECT_TRUE(queue.supersede(superseded));
  queue.observe_flow_tick(3);
  const auto stats = queue.advance(tess::AsyncWorkBudget{8});
  EXPECT_EQ(stats.invoked, 2u);

  const auto& counters = accounting.counters;
  EXPECT_EQ(counters.completed, 1u);
  EXPECT_EQ(counters.cancelled, 1u);
  EXPECT_EQ(counters.superseded, 1u);
  EXPECT_EQ(counters.failed, 1u);
  EXPECT_EQ(counters.outstanding_current, 0u);
  EXPECT_EQ(counters.offered_work_units, 8u);
  EXPECT_EQ(counters.consumed_work_units, 1u);
  // Cancel/supersede at tick 1 (residence 0 each); ready and failed
  // terminalize at tick 3 (residence 2 each, submitted at tick 1).
  EXPECT_EQ(counters.residence_ticks_accumulated, 4u);
  EXPECT_TRUE(counters.admission_identity_holds());
  EXPECT_TRUE(counters.retention_identity_holds());
}

TEST(TessAsyncFlow, ImmediateResultsCompleteAtSubmissionAndReclassifyStale) {
  tess::ResumableWorkQueue<int> queue;
  FlowAccounting accounting;
  queue.set_flow_accounting(&accounting);

  const auto ticket = queue.submit_immediate(9, tess::AsyncVersion{1});
  EXPECT_EQ(accounting.counters.completed, 1u);
  EXPECT_EQ(accounting.counters.outstanding_current, 0u);
  EXPECT_TRUE(accounting.counters.retention_identity_holds());

  EXPECT_TRUE(queue.mark_stale_if_version(ticket, tess::AsyncVersion{2}));
  EXPECT_EQ(accounting.counters.completed, 0u);
  EXPECT_EQ(accounting.counters.stale, 1u);
  EXPECT_TRUE(accounting.counters.admission_identity_holds());
  EXPECT_TRUE(accounting.counters.retention_identity_holds());
}

TEST(TessAsyncFlow, ClearDropsPendingWorkAfterAdmission) {
  tess::ResumableWorkQueue<int> queue;
  FlowAccounting accounting;
  queue.set_flow_accounting(&accounting);
  auto pending_work = step_pending;
  (void)queue.submit(nullptr, pending_work);
  (void)queue.submit_immediate(1);

  queue.clear();

  EXPECT_EQ(accounting.counters.dropped_after_admission, 1u);
  EXPECT_EQ(accounting.counters.completed, 1u);
  EXPECT_EQ(accounting.counters.outstanding_current, 0u);
  EXPECT_TRUE(accounting.counters.retention_identity_holds());
}

TEST(TessAsyncFlow, OldestAgeTracksTheEarliestPendingSubmission) {
  tess::ResumableWorkQueue<int> queue;
  FlowAccounting accounting;
  queue.set_flow_accounting(&accounting);
  auto pending_work = step_pending;
  queue.observe_flow_tick(2);
  (void)queue.submit(nullptr, pending_work);
  queue.observe_flow_tick(7);
  EXPECT_EQ(accounting.counters.oldest_outstanding_age_ticks, 5u);
  queue.clear();
  queue.observe_flow_tick(9);
  EXPECT_EQ(accounting.counters.oldest_outstanding_age_ticks, 0u);
}

TEST(TessAsyncFlow, QueueIdentityIsStable) {
  static_assert(!std::is_copy_constructible_v<tess::ResumableWorkQueue<int>>);
  static_assert(!std::is_copy_assignable_v<tess::ResumableWorkQueue<int>>);
  static_assert(!std::is_move_constructible_v<tess::ResumableWorkQueue<int>>);
  static_assert(!std::is_move_assignable_v<tess::ResumableWorkQueue<int>>);
}

struct ThrowingMove {
  ThrowingMove() = default;
  ThrowingMove(const ThrowingMove&) = default;
  auto operator=(const ThrowingMove&) -> ThrowingMove& = default;
  ThrowingMove(ThrowingMove&&) = default;
  // This type exists to throw on move assignment.
  // NOLINTNEXTLINE(performance-noexcept-move-constructor,bugprone-exception-escape)
  auto operator=(ThrowingMove&&) -> ThrowingMove& {
    throw std::runtime_error{"move"};
  }
  ~ThrowingMove() = default;
};

TEST(TessAsyncFlow, ThrowingImmediateMoveAdmitsNothing) {
  tess::ResumableWorkQueue<ThrowingMove> queue;
  FlowAccounting accounting;
  queue.set_flow_accounting(&accounting);

  EXPECT_THROW((void)queue.submit_immediate(ThrowingMove{}),
               std::runtime_error);

  EXPECT_EQ(queue.size(), 0u);
  EXPECT_EQ(accounting.counters.offered, 0u);
  EXPECT_EQ(accounting.counters.admitted, 0u);
  EXPECT_TRUE(accounting.counters.retention_identity_holds());
}

TEST(TessEventFlow, CopiesStartUnattachedAndCannotDoubleRetire) {
  tess::EventStream<int> stream;
  stream.reserve_events(2);
  FlowAccounting accounting;
  stream.set_flow_accounting(&accounting);
  ASSERT_TRUE(stream.publish(3, 1));

  auto copy = stream;
  copy.consume_all();  // unattached: accounting untouched
  stream.consume_all();

  EXPECT_EQ(accounting.counters.completed, 1u);
  EXPECT_EQ(accounting.counters.outstanding_current, 0u);
  EXPECT_TRUE(accounting.counters.retention_identity_holds());
}

// Every other terminalization site routes through
// FlowAccounting::record_left_outstanding, which floors at zero.
// EventStream subtracted its batch size directly, so any state where the
// accountant reports fewer outstanding than the batch holds -- a shared
// accountant whose other flow terminalized first, or a reset between
// publish and retire -- wrapped the unsigned counter to about 2^64 and
// took inventory_tick_weighted and the retention identity with it.
TEST(TessEventFlow, RetireDoesNotWrapOutstandingWhenTheCounterWasReset) {
  tess::EventStream<int> stream;
  stream.reserve_events(4);
  FlowAccounting accounting;
  stream.set_flow_accounting(&accounting);
  ASSERT_TRUE(stream.publish(1, 1));
  ASSERT_TRUE(stream.publish(2, 1));
  ASSERT_EQ(accounting.counters.outstanding_current, 2u);

  // The accountant is reset while the stream still holds its batch.
  accounting.counters.reset();
  ASSERT_EQ(accounting.counters.outstanding_current, 0u);

  stream.consume_all();

  EXPECT_EQ(accounting.counters.outstanding_current, 0u);
  EXPECT_LT(accounting.counters.outstanding_current, 1000u);
}

TEST(TessEventFlow, PublishRejectConsumeAndDiscardKeepIdentities) {
  tess::EventStream<int> stream;
  stream.reserve_events(2);
  FlowAccounting accounting;
  stream.set_flow_accounting(&accounting);
  stream.observe_flow_tick(1);

  EXPECT_TRUE(stream.publish(10, 1));
  EXPECT_TRUE(stream.publish(10, 2));
  EXPECT_FALSE(stream.publish(10, 3));  // over the bound
  EXPECT_EQ(accounting.counters.offered, 3u);
  EXPECT_EQ(accounting.counters.admitted, 2u);
  EXPECT_EQ(accounting.counters.rejected, 1u);
  EXPECT_EQ(accounting.counters.outstanding_current, 2u);

  stream.observe_flow_tick(4);
  EXPECT_EQ(accounting.counters.oldest_outstanding_age_ticks, 3u);
  stream.consume_all();
  EXPECT_EQ(accounting.counters.completed, 2u);
  EXPECT_EQ(accounting.counters.outstanding_current, 0u);
  EXPECT_EQ(accounting.counters.residence_ticks_accumulated, 6u);
  EXPECT_TRUE(accounting.counters.admission_identity_holds());
  EXPECT_TRUE(accounting.counters.retention_identity_holds());

  EXPECT_TRUE(stream.publish(11, 4));
  stream.clear();  // conservative: unread batch counts dropped
  EXPECT_EQ(accounting.counters.dropped_after_admission, 1u);
  EXPECT_TRUE(accounting.counters.retention_identity_holds());
}

class CountingTask final
    : public tess::experimental::maintenance::MaintenanceTask {
 public:
  void run(
      tess::experimental::maintenance::MaintenanceBudget& budget) override {
    (void)budget.consume(2);
    ++runs;
  }
  int runs = 0;
};

class ThrowingTask final
    : public tess::experimental::maintenance::MaintenanceTask {
 public:
  void run(tess::experimental::maintenance::MaintenanceBudget&) override {
    throw std::runtime_error{"maintenance failure"};
  }
};

TEST(TessMaintenanceFlow, CoalescingScheduleDrainAndRejectKeepIdentities) {
  namespace mnt = tess::experimental::maintenance;
  mnt::CoalescingScheduler scheduler{1};
  FlowAccounting accounting;
  scheduler.set_flow_accounting(&accounting);
  scheduler.observe_flow_tick(2);

  CountingTask task_a;
  CountingTask task_b;
  EXPECT_TRUE(scheduler.schedule(task_a));   // admitted
  EXPECT_TRUE(scheduler.schedule(task_a));   // coalesced into pending
  EXPECT_FALSE(scheduler.schedule(task_b));  // capacity rejected
  EXPECT_EQ(accounting.counters.offered, 3u);
  EXPECT_EQ(accounting.counters.admitted, 1u);
  EXPECT_EQ(accounting.counters.coalesced_into_pending, 1u);
  EXPECT_EQ(accounting.counters.rejected, 1u);
  EXPECT_EQ(accounting.counters.outstanding_current, 1u);

  scheduler.observe_flow_tick(5);
  EXPECT_EQ(accounting.counters.oldest_outstanding_age_ticks, 3u);
  EXPECT_TRUE(scheduler.run_some(mnt::MaintenanceBudget{8}));
  EXPECT_EQ(task_a.runs, 1);
  EXPECT_EQ(accounting.counters.completed, 1u);
  EXPECT_EQ(accounting.counters.offered_work_units, 8u);
  EXPECT_EQ(accounting.counters.consumed_work_units, 2u);
  EXPECT_EQ(accounting.counters.residence_ticks_accumulated, 3u);
  EXPECT_TRUE(accounting.counters.admission_identity_holds());
  EXPECT_TRUE(accounting.counters.retention_identity_holds());
}

TEST(TessMaintenanceFlow, ThrowingTasksTerminalizeAsFailed) {
  namespace mnt = tess::experimental::maintenance;
  mnt::FifoScheduler scheduler{4};
  FlowAccounting accounting;
  scheduler.set_flow_accounting(&accounting);
  ThrowingTask task;
  EXPECT_TRUE(scheduler.schedule(task));

  EXPECT_THROW((void)scheduler.flush(), std::runtime_error);

  EXPECT_EQ(accounting.counters.failed, 1u);
  EXPECT_EQ(accounting.counters.outstanding_current, 0u);
  EXPECT_TRUE(accounting.counters.retention_identity_holds());
}

TEST(TessMaintenanceFlow, ImmediateSchedulerAccountsSelfSchedulesAsCoalesced) {
  namespace mnt = tess::experimental::maintenance;

  class SelfScheduling final : public mnt::MaintenanceTask {
   public:
    explicit SelfScheduling(mnt::MaintenanceScheduler& scheduler)
        : scheduler_{&scheduler} {}
    void run(mnt::MaintenanceBudget& budget) override {
      (void)budget.consume(1);
      if (++runs == 1) {
        EXPECT_TRUE(scheduler_->schedule(*this));
      }
    }
    int runs = 0;

   private:
    mnt::MaintenanceScheduler* scheduler_;
  };

  mnt::ImmediateScheduler scheduler;
  FlowAccounting accounting;
  scheduler.set_flow_accounting(&accounting);
  SelfScheduling task{scheduler};

  EXPECT_TRUE(scheduler.schedule(task));

  EXPECT_EQ(task.runs, 2);
  EXPECT_EQ(accounting.counters.offered, 2u);
  EXPECT_EQ(accounting.counters.admitted, 1u);
  EXPECT_EQ(accounting.counters.coalesced_into_pending, 1u);
  EXPECT_EQ(accounting.counters.completed, 1u);
  EXPECT_EQ(accounting.counters.consumed_work_units, 2u);
  EXPECT_TRUE(accounting.counters.admission_identity_holds());
  EXPECT_TRUE(accounting.counters.retention_identity_holds());
}

struct AgentPassableTag {};
struct AgentCostTag {};
struct AgentOccupancyTag {};
struct AgentReservationTag {};
using AgentSchema = tess::FieldSchema<tess::Field<AgentPassableTag, bool>,
                                      tess::Field<AgentCostTag, std::uint32_t>,
                                      tess::Field<AgentOccupancyTag, bool>,
                                      tess::Field<AgentReservationTag, bool>>;
using AgentWalker =
    mv::MovementClass<mv::Field<AgentPassableTag>, mv::FieldCost<AgentCostTag>>;
using AgentShape = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{4, 4, 1}>;
using AgentWorld = tess::AlwaysResidentWorld<AgentShape, AgentSchema>;

TEST(TessAgentFlow, GoalLifecyclesAccountAtTransitions) {
  AgentWorld world;
  for (std::int64_t y = 0; y < 8; ++y) {
    for (std::int64_t x = 0; x < 8; ++x) {
      world.field<AgentPassableTag>(tess::Coord3{x, y, 0}) = true;
    }
  }
  tess::PathAgentTickState tick_state;
  FlowAccounting accounting;
  tick_state.flow_accounting = &accounting;
  std::array<tess::PathAgentState, 2> agents{};
  agents[0].position = tess::Coord3{0, 0, 0};
  agents[1].position = tess::Coord3{7, 0, 0};
  tess::PathRequestRuntime runtime;

  tess::observe_path_agent_flow_tick(tick_state, agents, 1);
  tess::set_path_agent_goal(tick_state, agents[0], tess::Coord3{2, 0, 0});
  tess::set_path_agent_goal(tick_state, agents[1], tess::Coord3{7, 3, 0});
  EXPECT_EQ(accounting.counters.admitted, 2u);
  EXPECT_EQ(accounting.counters.outstanding_current, 2u);

  // Replacing an outstanding goal supersedes it; clearing cancels.
  tess::set_path_agent_goal(tick_state, agents[1], tess::Coord3{7, 5, 0});
  EXPECT_EQ(accounting.counters.superseded, 1u);
  tess::clear_path_agent_goal(tick_state, agents[1]);
  EXPECT_EQ(accounting.counters.cancelled, 1u);
  EXPECT_EQ(accounting.counters.outstanding_current, 1u);

  // Drive agent 0 to arrival through the tick driver.
  auto options = tess::PathAgentTickOptions{};
  options.max_steps = 8;
  for (std::uint64_t tick = 2; tick < 8; ++tick) {
    tess::observe_path_agent_flow_tick(tick_state, agents, tick);
    const auto stats =
        tess::tick_unit_path_agents<AgentWorld, AgentPassableTag>(
            tick_state, world, agents, runtime, options);
    if (stats.movement.arrived > 0 || stats.pathing.arrived > 0) {
      break;
    }
  }
  EXPECT_EQ(accounting.counters.completed, 1u);
  EXPECT_EQ(accounting.counters.outstanding_current, 0u);
  EXPECT_TRUE(accounting.counters.admission_identity_holds());
  EXPECT_TRUE(accounting.counters.retention_identity_holds());
}

TEST(TessAgentFlow, ExhaustedRetriesTerminalizeAsFailed) {
  AgentWorld world;
  for (std::int64_t y = 0; y < 8; ++y) {
    for (std::int64_t x = 0; x < 8; ++x) {
      world.field<AgentPassableTag>(tess::Coord3{x, y, 0}) = true;
    }
  }
  // Wall the goal off completely: planning fails every tick.
  world.field<AgentPassableTag>(tess::Coord3{6, 6, 0}) = false;
  world.field<AgentPassableTag>(tess::Coord3{7, 5, 0}) = false;
  world.field<AgentPassableTag>(tess::Coord3{6, 7, 0}) = false;

  tess::PathAgentTickState tick_state;
  FlowAccounting accounting;
  tick_state.flow_accounting = &accounting;
  std::array<tess::PathAgentState, 1> agents{};
  agents[0].position = tess::Coord3{0, 0, 0};
  tess::PathRequestRuntime runtime;
  auto options = tess::PathAgentTickOptions{};
  options.max_blocked_retries = 2;
  options.blocked_exhaustion_policy =
      tess::BlockedAgentExhaustionPolicy::MarkUnreachable;

  tess::set_path_agent_goal(tick_state, agents[0], tess::Coord3{7, 7, 0});
  for (std::uint64_t tick = 1; tick <= 8; ++tick) {
    tess::observe_path_agent_flow_tick(tick_state, agents, tick);
    (void)tess::tick_unit_path_agents<AgentWorld, AgentPassableTag>(
        tick_state, world, agents, runtime, options);
    if (agents[0].phase == tess::PathAgentPhase::Unreachable) {
      break;
    }
  }

  EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::Unreachable);
  EXPECT_EQ(accounting.counters.failed, 1u);
  EXPECT_EQ(accounting.counters.outstanding_current, 0u);
  EXPECT_TRUE(accounting.counters.retention_identity_holds());

  // Re-arming a terminal goal is a fresh admission, not a supersede.
  tess::set_path_agent_goal(tick_state, agents[0], tess::Coord3{1, 0, 0});
  EXPECT_EQ(accounting.counters.superseded, 0u);
  EXPECT_EQ(accounting.counters.admitted, 2u);
  EXPECT_TRUE(accounting.counters.admission_identity_holds());
  EXPECT_TRUE(accounting.counters.retention_identity_holds());
}

TEST(TessAgentFlow, StructuralMovementFailureTerminalizesAsFailed) {
  AgentWorld world;
  for (std::int64_t y = 0; y < 8; ++y) {
    for (std::int64_t x = 0; x < 8; ++x) {
      world.field<AgentPassableTag>(tess::Coord3{x, y, 0}) = true;
    }
  }
  FlowAccounting accounting;
  tess::PathAgentTickState tick_state;
  tick_state.flow_accounting = &accounting;
  std::array<tess::PathAgentState, 1> agents{};
  agents[0].position = tess::Coord3{0, 0, 0};
  tess::observe_path_agent_flow_tick(tick_state, agents, 1);
  tess::set_path_agent_goal(tick_state, agents[0], tess::Coord3{5, 5, 0});
  agents[0].phase = tess::PathAgentPhase::Following;
  agents[0].status = tess::PathStatus::Found;

  // A retained route whose next step is not adjacent is a caller bug
  // the movement-validated advance treats as a terminal structural
  // failure.
  tess::PathAgentRoutes routes;
  routes.ensure_size(1);
  routes.routes[0] = {tess::Coord3{0, 0, 0}, tess::Coord3{4, 4, 0}};
  const auto stats = tess::advance_path_agents_with_movement<
      AgentWorld, AgentPassableTag, AgentOccupancyTag, AgentReservationTag>(
      world, std::span<tess::PathAgentState>{agents}, routes, {.max_steps = 1},
      tick_state.flow_accounting);
  (void)stats;

  EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::Unreachable);
  EXPECT_EQ(accounting.counters.failed, 1u);
  EXPECT_EQ(accounting.counters.outstanding_current, 0u);
  EXPECT_TRUE(accounting.counters.retention_identity_holds());
}

TEST(TessAgentFlow, TickStateCopiesStartUnattached) {
  tess::PathAgentTickState original;
  FlowAccounting accounting;
  original.flow_accounting = &accounting;

  auto copy = original;
  EXPECT_EQ(copy.flow_accounting, nullptr);

  auto moved = std::move(original);
  EXPECT_EQ(moved.flow_accounting, &accounting);
}

TEST(TessMaintenanceFlow, DestroyingASchedulerDropsQueuedWork) {
  namespace mnt = tess::experimental::maintenance;
  FlowAccounting accounting;
  CountingTask task;
  {
    mnt::FifoScheduler scheduler{4};
    scheduler.set_flow_accounting(&accounting);
    ASSERT_TRUE(scheduler.schedule(task));
  }

  EXPECT_EQ(accounting.counters.dropped_after_admission, 1u);
  EXPECT_EQ(accounting.counters.outstanding_current, 0u);
  EXPECT_TRUE(accounting.counters.retention_identity_holds());
}

TEST(TessAsyncFlow, ConsumedUnitsSurviveALaterThrowingCallback) {
  tess::ResumableWorkQueue<int> queue;
  FlowAccounting accounting;
  queue.set_flow_accounting(&accounting);
  auto ready_work = step_ready;
  auto throwing_work =
      +[](void*, tess::AsyncWorkBudget, int&) -> tess::AsyncWorkStep {
    throw std::runtime_error{"step"};
  };
  (void)queue.submit(nullptr, ready_work);
  (void)queue.submit(nullptr, throwing_work);

  EXPECT_THROW((void)queue.advance(tess::AsyncWorkBudget{4}),
               std::runtime_error);

  EXPECT_EQ(accounting.counters.consumed_work_units, 1u);
  EXPECT_EQ(accounting.counters.completed, 1u);
  EXPECT_TRUE(accounting.counters.retention_identity_holds());
}

TEST(TessAgentFlow, JointMovementTierAccountsArrivals) {
  AgentWorld world;
  for (std::int64_t y = 0; y < 8; ++y) {
    for (std::int64_t x = 0; x < 8; ++x) {
      world.field<AgentPassableTag>(tess::Coord3{x, y, 0}) = true;
      world.field<AgentCostTag>(tess::Coord3{x, y, 0}) = 1;
    }
  }
  tess::PathAgentTickState tick_state;
  FlowAccounting accounting;
  tick_state.flow_accounting = &accounting;
  std::array<tess::PathAgentState, 1> agents{};
  agents[0].position = tess::Coord3{0, 0, 0};
  tess::PathRequestRuntime runtime;
  tess::JointMoveScratch scratch;
  scratch.reserve(1);

  tess::observe_path_agent_flow_tick(tick_state, agents, 1);
  tess::set_path_agent_goal(tick_state, agents[0], tess::Coord3{0, 2, 0});
  for (std::uint64_t tick = 2; tick <= 8; ++tick) {
    tess::observe_path_agent_flow_tick(tick_state, agents, tick);
    (void)tess::tick_weighted_path_agents_with_joint_movement<
        AgentWorld, AgentWalker, 4096u, AgentOccupancyTag, AgentReservationTag>(
        tick_state, world, agents, runtime, scratch);
    if (!agents[0].has_goal) {
      break;
    }
  }

  EXPECT_FALSE(agents[0].has_goal);
  EXPECT_EQ(accounting.counters.completed, 1u);
  EXPECT_EQ(accounting.counters.outstanding_current, 0u);
  EXPECT_TRUE(accounting.counters.admission_identity_holds());
  EXPECT_TRUE(accounting.counters.retention_identity_holds());
}

}  // namespace
