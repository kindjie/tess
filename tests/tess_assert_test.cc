#include <gtest/gtest.h>
#include <tess/core/assert.h>
#include <tess/tess.h>

#include <array>
#include <cstdint>
#include <span>
#include <utility>

namespace {

struct TerrainTag {};
struct PassableTag {};

using TopDown2D =
    tess::Shape<tess::Extent3{128, 64, 1}, tess::Extent3{32, 16, 1}>;

using TerrainField = tess::Field<TerrainTag, std::uint16_t>;
using Schema = tess::FieldSchema<TerrainField>;
using World = tess::AlwaysResidentWorld<TopDown2D, Schema>;

using PathSchema = tess::FieldSchema<tess::Field<PassableTag, bool>>;
using PathWorld = tess::AlwaysResidentWorld<TopDown2D, PathSchema>;

#if TESS_ENABLE_ASSERTS

constexpr auto kAssertDeathMessage = "tess assertion failed";

#endif  // TESS_ENABLE_ASSERTS

TEST(TessAssert, MacroIsCompiledOutExactlyWhenAssertsDisabled) {
#if TESS_ENABLE_ASSERTS
  EXPECT_TRUE(TESS_ENABLE_ASSERTS);
#else
  // The disabled forms must still swallow the condition expression.
  bool evaluated = false;
  TESS_ASSERT((evaluated = true));
  EXPECT_FALSE(evaluated);
  TESS_ASSERT_MSG((evaluated = true), "unused message");
  EXPECT_FALSE(evaluated);
#endif
}

TEST(TessAssert, AssertMsgPassesWithoutSideEffectsWhenConditionHolds) {
  int evaluations = 0;
  TESS_ASSERT_MSG(++evaluations > 0, "condition must be evaluated once");
#if TESS_ENABLE_ASSERTS
  EXPECT_EQ(evaluations, 1);
#else
  EXPECT_EQ(evaluations, 0);
#endif
}

TEST(TessAssert, UncheckedAccessorsStayNoexcept) {
  static_assert(noexcept(std::declval<World&>().resolve(tess::Coord3{})));
  static_assert(noexcept(std::declval<World&>().chunk(tess::ChunkKey{})));
  static_assert(noexcept(std::declval<World&>().meta(tess::ChunkKey{})));
  SUCCEED();
}

#if TESS_ENABLE_ASSERTS

using TessAssertDeathTest = ::testing::Test;

TEST(TessAssertDeathTest, AssertMsgAbortsWithTheCustomMessage) {
  // TESS_ASSERT_MSG replaces the stringified condition with the caller's
  // message in the abort diagnostic.
  bool condition = false;
  EXPECT_DEATH(TESS_ASSERT_MSG(condition, "custom precondition message"),
               "tess assertion failed: custom precondition message");
}

TEST(TessAssertDeathTest, ResolveRejectsNegativeCoordinate) {
  World world;
  EXPECT_DEATH(static_cast<void>(world.resolve(tess::Coord3{-1, 0, 0})),
               kAssertDeathMessage);
}

TEST(TessAssertDeathTest, ResolveRejectsCoordinateBeyondShape) {
  World world;
  EXPECT_DEATH(static_cast<void>(world.resolve(tess::Coord3{128, 0, 0})),
               kAssertDeathMessage);
}

TEST(TessAssertDeathTest, FieldRejectsNegativeCoordinate) {
  World world;
  EXPECT_DEATH(
      static_cast<void>(world.field<TerrainTag>(tess::Coord3{0, -1, 0})),
      kAssertDeathMessage);
}

TEST(TessAssertDeathTest, ChunkRejectsOutOfRangeKey) {
  World world;
  EXPECT_DEATH(
      static_cast<void>(world.chunk(tess::ChunkKey{World::chunk_count})),
      kAssertDeathMessage);
}

TEST(TessAssertDeathTest, MetaRejectsOutOfRangeKey) {
  World world;
  EXPECT_DEATH(
      static_cast<void>(world.meta(tess::ChunkKey{World::chunk_count})),
      kAssertDeathMessage);
}

TEST(TessAssertDeathTest, TileKeyRejectsCoordinateOutsideShape) {
  EXPECT_DEATH(
      static_cast<void>(tess::tile_key<TopDown2D>(tess::Coord3{-1, 0, 0})),
      kAssertDeathMessage);
}

#endif  // TESS_ENABLE_ASSERTS

// Deliberately outside the TESS_ENABLE_ASSERTS guard above. These three
// preconditions used to be TESS_ASSERT, so they were checked only in builds
// that opted in -- and in the builds that did not, one silently returned a
// wrong answer and one was undefined behaviour. They are unconditional now,
// and these tests run in every configuration, so an accidental regression
// back to an assert-gated check fails here rather than passing quietly.
using TessFailFastDeathTest = ::testing::Test;

TEST(TessFailFastDeathTest, ScheduleTaskStatsRejectsUnknownId) {
  tess::Schedule schedule;
  // Empty schedule: id 0 names no task. The old form returned a
  // default-constructed ScheduleTaskStats -- all zeroes, which is exactly
  // what a registered task that has never run reports.
  EXPECT_DEATH(static_cast<void>(schedule.task_stats(0)), "unknown TaskId");
}

TEST(TessFailFastDeathTest, ScheduleSetEnabledRejectsUnknownId) {
  tess::Schedule schedule;
  EXPECT_DEATH(schedule.set_enabled(0, false), "unknown TaskId");
}

TEST(TessFailFastDeathTest, RuntimeResultRejectsUnpublishedResult) {
  tess::PathRequestRuntime runtime;
  const auto ticket = runtime.submit(
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{1, 0, 0}});
  EXPECT_DEATH(static_cast<void>(runtime.result(ticket)),
               "no published result batch");
}

TEST(TessFailFastDeathTest, RuntimeResultRejectsOutOfRangeTicket) {
  PathWorld world;
  tess::PathRequestRuntime runtime;
  (void)runtime.process_unit_cached<PathWorld, PassableTag>(world);
  EXPECT_DEATH(static_cast<void>(runtime.result(tess::PathTicket{7, 0})),
               "out-of-range PathTicket");
}

TEST(TessFailFastDeathTest, RuntimeResultRejectsStaleTicketGeneration) {
  tess::PathRequestRuntime runtime;
  const auto stale = runtime.submit(
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{1, 0, 0}});
  runtime.clear_requests();
  EXPECT_DEATH(static_cast<void>(runtime.result(stale)), "stale PathTicket");
}

[[nodiscard]] auto idle_task(void*, const tess::ScheduleTaskContext&)
    -> tess::ScheduleTaskResult {
  return {};
}

TEST(TessFailFastDeathTest, ScheduleRequestRunRejectsUnknownId) {
  tess::Schedule schedule;
  EXPECT_DEATH(schedule.request_run(0), "request_run.*unknown TaskId");
}

TEST(TessFailFastDeathTest, ScheduleRejectsRegistrationAfterSeal) {
  tess::Schedule schedule;
  schedule.seal();
  const auto desc = tess::ScheduleTaskDesc{};
  EXPECT_DEATH(static_cast<void>(schedule.add_task(desc, nullptr, idle_task)),
               "add_task.*after seal");
}

TEST(TessFailFastDeathTest, ScheduleRejectsReservationAfterSeal) {
  tess::Schedule schedule;
  schedule.seal();
  EXPECT_DEATH(schedule.reserve_tasks(1), "reserve_tasks.*after seal");
}

TEST(TessFailFastDeathTest, ScheduleRejectsNullCallback) {
  tess::Schedule schedule;
  EXPECT_DEATH(static_cast<void>(schedule.add_task(tess::ScheduleTaskDesc{},
                                                   nullptr, nullptr)),
               "add_task.*null callback");
}

TEST(TessFailFastDeathTest, ScheduleRejectsInvalidPhaseValues) {
  EXPECT_DEATH(
      {
        tess::Schedule schedule;
        auto desc = tess::ScheduleTaskDesc{};
        // Deliberately exercise the runtime guard for an out-of-domain value.
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
        desc.phase = static_cast<tess::SimPhase>(255);
        static_cast<void>(schedule.add_task(desc, nullptr, idle_task));
      },
      "invalid SimPhase");
  EXPECT_DEATH(
      {
        tess::Schedule schedule;
        auto desc = tess::ScheduleTaskDesc{};
        desc.phase = tess::SimPhase::Count;
        static_cast<void>(schedule.add_task(desc, nullptr, idle_task));
      },
      "invalid SimPhase");
}

TEST(TessFailFastDeathTest, ScheduleRejectsInvalidCadenceValues) {
  EXPECT_DEATH(
      {
        tess::Schedule schedule;
        auto desc = tess::ScheduleTaskDesc{};
        // Deliberately exercise the runtime guard for an out-of-domain value.
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
        desc.cadence.kind = static_cast<tess::CadenceKind>(255);
        static_cast<void>(schedule.add_task(desc, nullptr, idle_task));
      },
      "invalid CadenceKind");
  EXPECT_DEATH(
      {
        tess::Schedule schedule;
        auto desc = tess::ScheduleTaskDesc{};
        desc.cadence.kind = tess::CadenceKind::EveryN;
        desc.cadence.every_n = 0;
        static_cast<void>(schedule.add_task(desc, nullptr, idle_task));
      },
      "EveryN cadence requires every_n");
  EXPECT_DEATH(
      {
        tess::Schedule schedule;
        auto desc = tess::ScheduleTaskDesc{};
        desc.cadence.kind = tess::CadenceKind::Background;
        desc.cadence.budget.max_items = 0;
        static_cast<void>(schedule.add_task(desc, nullptr, idle_task));
      },
      "Background cadence requires a nonzero budget");
}

TEST(TessFailFastDeathTest, ScheduleRejectsRunBeforeSeal) {
  tess::Schedule schedule;
  auto clock = tess::SimClock{};
  EXPECT_DEATH(static_cast<void>(schedule.run_tick(clock)),
               "run_tick.*before seal");
}

struct ReentrantScheduleTask {
  tess::Schedule* schedule = nullptr;
  tess::SimClock* clock = nullptr;

  [[nodiscard]] auto operator()(const tess::ScheduleTaskContext&) const
      -> tess::ScheduleTaskResult {
    static_cast<void>(schedule->run_tick(*clock));
    return {};
  }
};

TEST(TessFailFastDeathTest, ScheduleRejectsReentrantRun) {
  tess::Schedule schedule;
  auto clock = tess::SimClock{};
  auto task = ReentrantScheduleTask{&schedule, &clock};
  static_cast<void>(schedule.add_task(tess::ScheduleTaskDesc{}, task));
  schedule.seal();
  EXPECT_DEATH(static_cast<void>(schedule.run_tick(clock)),
               "reentrant run_tick");
}

[[nodiscard]] auto overreported_background(void*,
                                           const tess::ScheduleTaskContext&)
    -> tess::ScheduleTaskResult {
  return {.items_done = 2};
}

[[nodiscard]] auto nonbackground_items(void*, const tess::ScheduleTaskContext&)
    -> tess::ScheduleTaskResult {
  return {.items_done = 1};
}

TEST(TessFailFastDeathTest, ScheduleRejectsBackgroundOverreport) {
  tess::Schedule schedule;
  const auto desc = tess::ScheduleTaskDesc{
      .name = {},
      .cadence = tess::Cadence::background(tess::BackgroundBudget{1})};
  const auto id = schedule.add_task(desc, nullptr, overreported_background);
  schedule.request_run(id);
  schedule.seal();
  auto clock = tess::SimClock{};
  EXPECT_DEATH(static_cast<void>(schedule.run_tick(clock)),
               "reported more background items than offered");
}

TEST(TessFailFastDeathTest, ScheduleRejectsItemsFromNonbackgroundTask) {
  tess::Schedule schedule;
  static_cast<void>(schedule.add_task(tess::ScheduleTaskDesc{}, nullptr,
                                      nonbackground_items));
  schedule.seal();
  auto clock = tess::SimClock{};
  EXPECT_DEATH(static_cast<void>(schedule.run_tick(clock)),
               "non-background task reported background items");
}

[[nodiscard]] auto pending_work(void*, tess::AsyncWorkBudget, std::uint32_t&)
    -> tess::AsyncWorkStep {
  return {tess::AsyncStepState::Pending, 0, {}};
}

[[nodiscard]] auto overreporting_work(void*, tess::AsyncWorkBudget budget,
                                      std::uint32_t&) -> tess::AsyncWorkStep {
  return {tess::AsyncStepState::Ready, budget.max_items + 1, {}};
}

TEST(TessFailFastDeathTest, QueueRejectsNullRawCallback) {
  tess::ResumableWorkQueue<std::uint32_t> queue;
  EXPECT_DEATH(static_cast<void>(queue.submit(nullptr, nullptr)),
               "ResumableWorkQueue::submit received a null callback");
}

TEST(TessFailFastDeathTest, QueueRejectsAccountingRebindWithRetainedWork) {
  tess::ResumableWorkQueue<std::uint32_t> queue;
  tess::diagnostics::FlowAccounting accounting;
  static_cast<void>(queue.submit(nullptr, pending_work));
  EXPECT_DEATH(queue.set_flow_accounting(&accounting),
               "set_flow_accounting requires an empty queue");
}

enum class ReentrantQueueOperation : std::uint8_t {
  Advance,
  ObserveTick,
  Reserve,
  Submit,
  SubmitImmediate,
  Cancel,
  Supersede,
  Fail,
  MarkStale,
  MarkStaleIfVersion,
  Clear,
};

struct ReentrantQueueWork {
  tess::ResumableWorkQueue<std::uint32_t>* queue = nullptr;
  tess::AsyncTicket ticket{};
  ReentrantQueueOperation operation = ReentrantQueueOperation::Advance;

  [[nodiscard]] auto operator()(tess::AsyncWorkBudget, std::uint32_t&) const
      -> tess::AsyncWorkStep {
    switch (operation) {
      case ReentrantQueueOperation::Advance:
        static_cast<void>(queue->advance(tess::AsyncWorkBudget{1}));
        break;
      case ReentrantQueueOperation::ObserveTick:
        queue->observe_flow_tick(2);
        break;
      case ReentrantQueueOperation::Reserve:
        queue->reserve_tickets(2);
        break;
      case ReentrantQueueOperation::Submit:
        static_cast<void>(queue->submit(nullptr, pending_work));
        break;
      case ReentrantQueueOperation::SubmitImmediate:
        static_cast<void>(queue->submit_immediate(1));
        break;
      case ReentrantQueueOperation::Cancel:
        static_cast<void>(queue->cancel(ticket));
        break;
      case ReentrantQueueOperation::Supersede:
        static_cast<void>(queue->supersede(ticket));
        break;
      case ReentrantQueueOperation::Fail:
        static_cast<void>(queue->fail(ticket));
        break;
      case ReentrantQueueOperation::MarkStale:
        static_cast<void>(queue->mark_stale(ticket));
        break;
      case ReentrantQueueOperation::MarkStaleIfVersion:
        static_cast<void>(
            queue->mark_stale_if_version(ticket, tess::AsyncVersion{1}));
        break;
      case ReentrantQueueOperation::Clear:
        queue->clear();
        break;
    }
    return {tess::AsyncStepState::Ready, 1, {}};
  }
};

class QueueReentrancyDeathTest
    : public ::testing::TestWithParam<ReentrantQueueOperation> {};

[[nodiscard]] auto queue_operation_name(
    const ::testing::TestParamInfo<ReentrantQueueOperation>& info) -> const
    char* {
  switch (info.param) {
    case ReentrantQueueOperation::Advance:
      return "Advance";
    case ReentrantQueueOperation::ObserveTick:
      return "ObserveTick";
    case ReentrantQueueOperation::Reserve:
      return "Reserve";
    case ReentrantQueueOperation::Submit:
      return "Submit";
    case ReentrantQueueOperation::SubmitImmediate:
      return "SubmitImmediate";
    case ReentrantQueueOperation::Cancel:
      return "Cancel";
    case ReentrantQueueOperation::Supersede:
      return "Supersede";
    case ReentrantQueueOperation::Fail:
      return "Fail";
    case ReentrantQueueOperation::MarkStale:
      return "MarkStale";
    case ReentrantQueueOperation::MarkStaleIfVersion:
      return "MarkStaleIfVersion";
    case ReentrantQueueOperation::Clear:
      return "Clear";
  }
  return "Unknown";
}

TEST_P(QueueReentrancyDeathTest, RejectsMutationDuringAdvance) {
  tess::ResumableWorkQueue<std::uint32_t> queue;
  tess::diagnostics::FlowAccounting accounting;
  queue.set_flow_accounting(&accounting);
  auto work = ReentrantQueueWork{&queue, {}, GetParam()};
  work.ticket = queue.submit(work);
  EXPECT_DEATH(static_cast<void>(queue.advance(tess::AsyncWorkBudget{1})),
               "ResumableWorkQueue.*during advance");
}

INSTANTIATE_TEST_SUITE_P(
    AllMutations, QueueReentrancyDeathTest,
    ::testing::Values(
        ReentrantQueueOperation::Advance, ReentrantQueueOperation::ObserveTick,
        ReentrantQueueOperation::Reserve, ReentrantQueueOperation::Submit,
        ReentrantQueueOperation::SubmitImmediate,
        ReentrantQueueOperation::Cancel, ReentrantQueueOperation::Supersede,
        ReentrantQueueOperation::Fail, ReentrantQueueOperation::MarkStale,
        ReentrantQueueOperation::MarkStaleIfVersion,
        ReentrantQueueOperation::Clear),
    queue_operation_name);

TEST(TessAsyncWork, OverreportedProgressSettlesFailedInEveryBuild) {
  tess::ResumableWorkQueue<std::uint32_t> queue;
  const auto ticket = queue.submit(nullptr, overreporting_work);
  const auto stats = queue.advance(tess::AsyncWorkBudget{1});
  EXPECT_EQ(queue.state(ticket), tess::AsyncResultState::Failed);
  EXPECT_EQ(stats.failed, 1u);
  EXPECT_EQ(stats.items_done, 0u);
}

TEST(TessFailFastDeathTest, EventStreamRejectsReserveWithRetainedEvents) {
  tess::EventStream<int> stream;
  stream.reserve_events(1);
  ASSERT_TRUE(stream.publish(0, 1));
  EXPECT_DEATH(stream.reserve_events(2),
               "reserve_events requires an empty stream");
}

TEST(TessFailFastDeathTest, EventStreamRejectsAccountingRebindWithEvents) {
  tess::EventStream<int> stream;
  tess::diagnostics::FlowAccounting accounting;
  stream.reserve_events(1);
  ASSERT_TRUE(stream.publish(0, 1));
  EXPECT_DEATH(stream.set_flow_accounting(&accounting),
               "set_flow_accounting requires an empty stream");
}

TEST(TessFailFastDeathTest, EventStreamRejectsCopyOverOutstandingAccounting) {
  tess::EventStream<int> source;
  source.reserve_events(1);
  ASSERT_TRUE(source.publish(0, 1));

  tess::EventStream<int> destination;
  tess::diagnostics::FlowAccounting accounting;
  destination.reserve_events(1);
  destination.set_flow_accounting(&accounting);
  ASSERT_TRUE(destination.publish(0, 2));
  EXPECT_DEATH(destination = source,
               "copy assignment would orphan outstanding accounting");
}

TEST(TessFailFastDeathTest, EventStreamRejectsMoveOverOutstandingAccounting) {
  tess::EventStream<int> source;
  source.reserve_events(1);
  ASSERT_TRUE(source.publish(0, 1));

  tess::EventStream<int> destination;
  tess::diagnostics::FlowAccounting accounting;
  destination.reserve_events(1);
  destination.set_flow_accounting(&accounting);
  ASSERT_TRUE(destination.publish(0, 2));
  EXPECT_DEATH(destination = std::move(source),
               "move assignment would orphan outstanding accounting");
}

// `IntentPayloadView::as<T>` is the one place the typed-intent surface
// checks a payload's type, and it used to answer a wrong-type query with
// an empty span -- the same answer a correctly-typed empty batch gives.
// Both configurations matter and disagree, which is why these live here
// rather than beside the rest of the payload-view coverage: the debug
// build must abort, and the release build must fall back to the empty span
// rather than reinterpreting the bytes as the wrong type.
namespace payload_probe {

struct Request {
  int value = 0;
};
struct OtherRequest {
  int value = 0;
};

[[nodiscard]] auto filled_view(std::span<Request> storage)
    -> tess::IntentPayloadView {
  return tess::IntentPayloadView::from(storage);
}

}  // namespace payload_probe

#if TESS_ENABLE_ASSERTS

using TessPayloadViewDeathTest = ::testing::Test;

TEST(TessPayloadViewDeathTest, AsRejectsAPayloadHoldingAnotherType) {
  std::array<payload_probe::Request, 2> storage{};
  const auto view = payload_probe::filled_view(storage);
  EXPECT_DEATH(static_cast<void>(view.as<payload_probe::OtherRequest>().size()),
               "does not hold T");
}

TEST(TessPayloadViewDeathTest, AsRejectsAPayloadThatHoldsNothing) {
  const tess::IntentPayloadView unbound{};
  EXPECT_DEATH(static_cast<void>(unbound.as<payload_probe::Request>().size()),
               "does not hold T");
}

#else

// The release contract: no abort, and no reinterpretation either. A
// consumer that ships with assertions off keeps the old silent-empty
// behaviour rather than reading one type's bytes as another's.
TEST(TessPayloadView, AsFallsBackToAnEmptySpanWithAssertsCompiledOut) {
  std::array<payload_probe::Request, 2> storage{};
  const auto view = payload_probe::filled_view(storage);
  EXPECT_TRUE(view.as<payload_probe::OtherRequest>().empty());

  const tess::IntentPayloadView unbound{};
  EXPECT_TRUE(unbound.as<payload_probe::Request>().empty());

  // The correctly-typed read still works in this configuration.
  EXPECT_EQ(view.as<payload_probe::Request>().size(), 2u);
}

#endif

// ResultChannel::value_for is hardened the same way but has no test here:
// it is a private producer hook reachable only from the friended execute
// wrappers, so no test can call it without becoming a friend itself, and a
// friendship declared for a test would be a larger change to the contract
// than the hardening it verifies.

}  // namespace
