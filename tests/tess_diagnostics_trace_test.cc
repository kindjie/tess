#include <gtest/gtest.h>
#include <tess/tess.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace {

using tess::diagnostics::BufferedWarningSink;
using tess::diagnostics::NullWarningSink;
using tess::diagnostics::ScopedTimer;
using tess::diagnostics::ScopedTrace;
using tess::diagnostics::TraceBuffer;
using tess::diagnostics::TraceCategory;
using tess::diagnostics::TraceRecord;
using tess::diagnostics::Warning;
using tess::diagnostics::WarningCategory;

// Static string storage satisfies the non-owning Warning::message contract.
constexpr std::string_view kBudgetMessage = "residency budget exceeded";
constexpr std::string_view kStaleMessage = "region graph stale";

TEST(TessWarningSink, NullSinkSatisfiesConceptAndDiscards) {
  static_assert(tess::diagnostics::WarningSink<NullWarningSink>);
  NullWarningSink sink;
  sink.warn(Warning{WarningCategory::Storage, kBudgetMessage, 42});
  SUCCEED();
}

TEST(TessWarningSink, BufferedSinkSatisfiesConcept) {
  static_assert(tess::diagnostics::WarningSink<BufferedWarningSink<4>>);
  SUCCEED();
}

TEST(TessWarningSink, RetainsWarningsOldestFirst) {
  BufferedWarningSink<4> sink;
  EXPECT_TRUE(sink.empty());
  EXPECT_EQ(sink.capacity(), 4u);

  sink.warn(Warning{WarningCategory::Storage, kBudgetMessage, 1});
  sink.warn(Warning{WarningCategory::Topology, kStaleMessage, 2});

  EXPECT_EQ(sink.size(), 2u);
  EXPECT_FALSE(sink.empty());
  EXPECT_FALSE(sink.full());
  EXPECT_EQ(sink.dropped(), 0u);

  // Oldest-first indexing.
  EXPECT_EQ(sink[0].category, WarningCategory::Storage);
  EXPECT_EQ(sink[0].message, kBudgetMessage);
  EXPECT_EQ(sink[0].detail, 1u);
  EXPECT_EQ(sink[1].category, WarningCategory::Topology);
  EXPECT_EQ(sink[1].message, kStaleMessage);
  EXPECT_EQ(sink[1].detail, 2u);
}

TEST(TessWarningSink, OverflowDropsOldestAndCounts) {
  BufferedWarningSink<3> sink;
  for (std::uint64_t i = 0; i < 5; ++i) {
    sink.warn(Warning{WarningCategory::Path, kBudgetMessage, i});
  }

  // Capacity 3, five warnings: two oldest (details 0 and 1) were dropped.
  EXPECT_TRUE(sink.full());
  EXPECT_EQ(sink.size(), 3u);
  EXPECT_EQ(sink.dropped(), 2u);

  // Window now holds details 2, 3, 4 oldest-first.
  EXPECT_EQ(sink[0].detail, 2u);
  EXPECT_EQ(sink[1].detail, 3u);
  EXPECT_EQ(sink[2].detail, 4u);
}

TEST(TessWarningSink, ClearResetsWindowAndDropped) {
  BufferedWarningSink<2> sink;
  sink.warn(Warning{WarningCategory::Queued, kBudgetMessage, 7});
  sink.warn(Warning{WarningCategory::Queued, kBudgetMessage, 8});
  sink.warn(Warning{WarningCategory::Queued, kBudgetMessage, 9});
  ASSERT_EQ(sink.dropped(), 1u);

  sink.clear();
  EXPECT_TRUE(sink.empty());
  EXPECT_EQ(sink.size(), 0u);
  EXPECT_EQ(sink.dropped(), 0u);

  sink.warn(Warning{WarningCategory::Queued, kStaleMessage, 10});
  EXPECT_EQ(sink.size(), 1u);
  EXPECT_EQ(sink[0].detail, 10u);
}

TEST(TessWarningSink, WarningDefaultsSourceLocationToCallSite) {
  const Warning warning{WarningCategory::Scheduler, kStaleMessage, 3};
  // The default source_location is captured at the aggregate-init site above,
  // so it names this test's translation unit rather than the header.
  EXPECT_NE(std::string_view{warning.where.file_name()}.find("trace_test"),
            std::string_view::npos);
}

// Buffered warn() writes into inline storage only; it must never allocate.
TEST(TessWarningSink, WarnIsAllocationFree) {
  BufferedWarningSink<8> sink;
  tess::diagnostics::AllocationCounters counters;
  {
    tess::diagnostics::ScopedAllocationCounters scope{counters};
    for (std::uint64_t i = 0; i < 32; ++i) {
      sink.warn(Warning{WarningCategory::General, kBudgetMessage, i});
    }
  }
  EXPECT_EQ(counters.allocations, 0u);
  EXPECT_EQ(sink.dropped(), 24u);
}

constexpr std::string_view kConflictLabel = "conflict";
constexpr std::string_view kPlanLabel = "planned";

TEST(TessTraceBuffer, RecordsOldestFirstWithSequences) {
  std::array<TraceRecord, 4> storage{};
  TraceBuffer buffer{storage};
  EXPECT_TRUE(buffer.empty());
  EXPECT_EQ(buffer.capacity(), 4u);

  buffer.record(TraceCategory::Planner, kPlanLabel, 10);
  buffer.record(TraceCategory::Planner, kConflictLabel, 11);
  buffer.record(TraceCategory::Queued, kPlanLabel, 12);

  EXPECT_EQ(buffer.size(), 3u);
  EXPECT_EQ(buffer.dropped(), 0u);
  EXPECT_EQ(buffer[0].sequence, 0u);
  EXPECT_EQ(buffer[0].value, 10u);
  EXPECT_EQ(buffer[0].label, kPlanLabel);
  EXPECT_EQ(buffer[1].category, TraceCategory::Planner);
  EXPECT_EQ(buffer[1].label, kConflictLabel);
  EXPECT_EQ(buffer[2].sequence, 2u);
  EXPECT_EQ(buffer[2].category, TraceCategory::Queued);
}

TEST(TessTraceBuffer, OverflowDropsOldestKeepsSequenceGaps) {
  std::array<TraceRecord, 3> storage{};
  TraceBuffer buffer{storage};
  for (std::uint64_t i = 0; i < 5; ++i) {
    buffer.record(TraceCategory::Planner, kPlanLabel, i);
  }

  EXPECT_TRUE(buffer.full());
  EXPECT_EQ(buffer.size(), 3u);
  EXPECT_EQ(buffer.dropped(), 2u);
  // Retained window is values 2,3,4 with their original sequences.
  EXPECT_EQ(buffer[0].value, 2u);
  EXPECT_EQ(buffer[0].sequence, 2u);
  EXPECT_EQ(buffer[2].value, 4u);
  EXPECT_EQ(buffer[2].sequence, 4u);
}

TEST(TessTraceBuffer, EmptySpanDropsEveryRecord) {
  TraceBuffer buffer{std::span<TraceRecord>{}};
  buffer.record(TraceCategory::Planner, kPlanLabel, 1);
  buffer.record(TraceCategory::Planner, kPlanLabel, 2);

  EXPECT_EQ(buffer.capacity(), 0u);
  EXPECT_TRUE(buffer.empty());
  EXPECT_EQ(buffer.size(), 0u);
  EXPECT_EQ(buffer.dropped(), 2u);
}

TEST(TessTraceBuffer, RecordTimingAccumulatesPerCategory) {
  std::array<TraceRecord, 2> storage{};
  TraceBuffer buffer{storage};

  buffer.record_timing(TraceCategory::Path, 10);
  buffer.record_timing(TraceCategory::Path, 30);
  buffer.record_timing(TraceCategory::Path, 20);

  const auto& path = buffer.stats(TraceCategory::Path);
  EXPECT_EQ(path.samples, 3u);
  EXPECT_EQ(path.total_ns, 60u);
  EXPECT_EQ(path.min_ns, 10u);
  EXPECT_EQ(path.max_ns, 30u);

  // An untouched category reads clean zeros (no sentinel leakage).
  const auto& topo = buffer.stats(TraceCategory::Topology);
  EXPECT_EQ(topo.samples, 0u);
  EXPECT_EQ(topo.min_ns, 0u);
  EXPECT_EQ(topo.max_ns, 0u);
}

TEST(TessTraceBuffer, RecordTimingFirstSampleSetsMinAndMax) {
  std::array<TraceRecord, 1> storage{};
  TraceBuffer buffer{storage};
  buffer.record_timing(TraceCategory::Queued, 15);

  const auto& stats = buffer.stats(TraceCategory::Queued);
  EXPECT_EQ(stats.samples, 1u);
  EXPECT_EQ(stats.total_ns, 15u);
  EXPECT_EQ(stats.min_ns, 15u);
  EXPECT_EQ(stats.max_ns, 15u);
}

TEST(TessTraceBuffer, RecordTimingIgnoresCountSentinel) {
  std::array<TraceRecord, 1> storage{};
  TraceBuffer buffer{storage};
  buffer.record_timing(TraceCategory::Count, 99);  // must not index OOB

  const auto& sentinel = buffer.stats(TraceCategory::Count);
  EXPECT_EQ(sentinel.samples, 0u);
  EXPECT_EQ(buffer.stats(TraceCategory::Path).samples, 0u);
}

TEST(TessTraceBuffer, RecordRejectsCountSentinel) {
  std::array<TraceRecord, 4> storage{};
  TraceBuffer buffer{storage};
  buffer.record(TraceCategory::Count, kPlanLabel, 1);  // sentinel is not a
                                                       // recordable category
  buffer.record(TraceCategory::Planner, kPlanLabel, 2);

  // The sentinel record is dropped, never stored; only the real one survives.
  EXPECT_EQ(buffer.size(), 1u);
  EXPECT_EQ(buffer.dropped(), 1u);
  EXPECT_EQ(buffer[0].category, TraceCategory::Planner);
  EXPECT_EQ(buffer[0].value, 2u);
}

TEST(TessTraceBuffer, ClearResetsRecordsAndStats) {
  std::array<TraceRecord, 2> storage{};
  TraceBuffer buffer{storage};
  buffer.record(TraceCategory::Planner, kPlanLabel, 1);
  buffer.record_timing(TraceCategory::Path, 5);
  buffer.clear();

  EXPECT_TRUE(buffer.empty());
  EXPECT_EQ(buffer.dropped(), 0u);
  EXPECT_EQ(buffer.stats(TraceCategory::Path).samples, 0u);
  // Sequence numbering restarts from zero after a clear.
  buffer.record(TraceCategory::Planner, kPlanLabel, 7);
  EXPECT_EQ(buffer[0].sequence, 0u);
}

TEST(TessScopedTrace, InstallsActiveBufferAndNests) {
  std::array<TraceRecord, 4> outer_storage{};
  std::array<TraceRecord, 4> inner_storage{};
  TraceBuffer outer{outer_storage};
  TraceBuffer inner{inner_storage};

  // No active buffer: the event is discarded.
  tess::diagnostics::trace_event(TraceCategory::Planner, kPlanLabel, 0);

  {
    ScopedTrace outer_scope{outer};
    tess::diagnostics::trace_event(TraceCategory::Planner, kPlanLabel, 1);
    {
      ScopedTrace inner_scope{inner};
      tess::diagnostics::trace_event(TraceCategory::Planner, kConflictLabel, 2);
    }
    // Inner scope restored: events flow back to the outer buffer.
    tess::diagnostics::trace_event(TraceCategory::Planner, kPlanLabel, 3);
  }

  EXPECT_EQ(outer.size(), 2u);
  EXPECT_EQ(outer[0].value, 1u);
  EXPECT_EQ(outer[1].value, 3u);
  EXPECT_EQ(inner.size(), 1u);
  EXPECT_EQ(inner[0].value, 2u);
}

TEST(TessScopedTimer, RecordsTimingAndRecordWhenTraceActive) {
  std::array<TraceRecord, 4> storage{};
  TraceBuffer buffer{storage};
  constexpr std::string_view kTickLabel = "agent_tick";
  {
    ScopedTrace scope{buffer};
    ScopedTimer timer{TraceCategory::Path, kTickLabel};
  }

  EXPECT_EQ(buffer.stats(TraceCategory::Path).samples, 1u);
  ASSERT_EQ(buffer.size(), 1u);
  EXPECT_EQ(buffer[0].category, TraceCategory::Path);
  EXPECT_EQ(buffer[0].label, kTickLabel);
}

TEST(TessScopedTimer, BindsToBufferActiveAtConstruction) {
  std::array<TraceRecord, 4> a_storage{};
  std::array<TraceRecord, 4> b_storage{};
  TraceBuffer buffer_a{a_storage};
  TraceBuffer buffer_b{b_storage};

  {
    ScopedTrace a_scope{buffer_a};
    // Timer is constructed while buffer A is active, so it binds to A.
    std::optional<ScopedTimer> timer{std::in_place, TraceCategory::Path, "x"};
    {
      ScopedTrace b_scope{buffer_b};
      // Destroy the timer while buffer B is active. Capture-at-construction
      // sends the sample to A; a destruction-time binding would send it to B.
      timer.reset();
    }
  }

  EXPECT_EQ(buffer_a.stats(TraceCategory::Path).samples, 1u);
  EXPECT_EQ(buffer_a.size(), 1u);
  EXPECT_EQ(buffer_b.stats(TraceCategory::Path).samples, 0u);
  EXPECT_EQ(buffer_b.size(), 0u);
}

TEST(TessTraceBuffer, TraceIsAllocationFree) {
  std::array<TraceRecord, 8> storage{};
  TraceBuffer buffer{storage};
  tess::diagnostics::AllocationCounters counters;
  {
    tess::diagnostics::ScopedAllocationCounters alloc_scope{counters};
    ScopedTrace scope{buffer};
    for (std::uint64_t i = 0; i < 32; ++i) {
      tess::diagnostics::trace_event(TraceCategory::Planner, kPlanLabel, i);
      buffer.record_timing(TraceCategory::Planner, i);
    }
    ScopedTimer timer{TraceCategory::Queued, kPlanLabel};
  }
  EXPECT_EQ(counters.allocations, 0u);
}

// --- Planner trace instrumentation (queued.h) -----------------------------

struct PlannerTerrainTag {};

constexpr std::uint32_t kPlannerDirtyTerrain = 1u << 0u;

using PlannerShape =
    tess::Shape<tess::Extent3{64, 32, 1}, tess::Extent3{32, 16, 1}>;
using PlannerSchema =
    tess::FieldSchema<tess::Field<PlannerTerrainTag, std::uint16_t>>;
using PlannerWorld = tess::AlwaysResidentWorld<PlannerShape, PlannerSchema>;

// Returns true if the buffer holds a Planner record with the given label and
// value.
[[nodiscard]] bool has_planner_record(const TraceBuffer& buffer,
                                      std::string_view label,
                                      std::uint64_t value) {
  for (std::size_t i = 0; i < buffer.size(); ++i) {
    const auto& record = buffer[i];
    if (record.category == TraceCategory::Planner && record.label == label &&
        record.value == value) {
      return true;
    }
  }
  return false;
}

TEST(TessPlannerTrace, RecordsPlanDecisionsForConflictingPlan) {
  PlannerWorld world;
  tess::FrameOps ops;
  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      kPlannerDirtyTerrain,
      kPlannerDirtyTerrain,
  };
  // Two operations writing the same chunk's terrain: the second is a
  // write-write hazard against the first.
  const std::vector<tess::ChunkKey> keys{tess::ChunkKey{0}};
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);

  std::array<TraceRecord, 8> storage{};
  TraceBuffer buffer{storage};
  {
    ScopedTrace scope{buffer};
    const auto report = tess::plan_operations(world, ops);
    ASSERT_EQ(report.planned_count(), 1u);
    ASSERT_EQ(report.failed_count(), 1u);
  }

  EXPECT_TRUE(has_planner_record(buffer, "planned", 0));
  EXPECT_TRUE(has_planner_record(buffer, "conflict", 1));
}

TEST(TessPlannerTrace, RecordsPhaseAssignmentDecisions) {
  PlannerWorld world;
  tess::FrameOps ops;
  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      kPlannerDirtyTerrain,
      kPlannerDirtyTerrain,
  };
  // Two operations on different chunks: both plan, and the second merges into
  // the first parallel phase.
  const std::vector<tess::ChunkKey> first_keys{tess::ChunkKey{0}};
  const std::vector<tess::ChunkKey> second_keys{tess::ChunkKey{1}};
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(first_keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(second_keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);
  ASSERT_EQ(report.planned_count(), 2u);

  std::array<TraceRecord, 8> storage{};
  TraceBuffer buffer{storage};
  {
    ScopedTrace scope{buffer};
    const auto phases = tess::plan_parallel_execution_phases(report.plan());
    ASSERT_TRUE(phases.ok());
    ASSERT_EQ(phases.phases().size(), 1u);
  }

  EXPECT_TRUE(has_planner_record(buffer, "new_phase", 0));
  EXPECT_TRUE(has_planner_record(buffer, "merged", 1));
}

// --- Snapshot export (export.h) -------------------------------------------

TEST(TessDiagnosticsExport, CapturesCountersAndTiming) {
  tess::diagnostics::PathCounters path;
  {
    tess::diagnostics::ScopedPathCounters scope{path};
    TESS_DIAG_EVENT(path_heap_push);
    TESS_DIAG_EVENT(path_heap_push);
    TESS_DIAG_EVENT(path_heap_pop);
  }

  std::array<TraceRecord, 4> storage{};
  TraceBuffer buffer{storage};
  buffer.record_timing(TraceCategory::Topology, 40);
  buffer.record_timing(TraceCategory::Topology, 60);

  const tess::diagnostics::AllocationCounters allocation;
  const tess::diagnostics::QueuedPhaseCounters queued;
  const auto snapshot =
      tess::diagnostics::capture_diagnostics(path, allocation, queued, buffer);

  EXPECT_EQ(snapshot.path.heap_pushes, 2u);
  EXPECT_EQ(snapshot.path.heap_pops, 1u);
  const auto& topo = snapshot.timing.category(TraceCategory::Topology);
  EXPECT_EQ(topo.samples, 2u);
  EXPECT_EQ(topo.total_ns, 100u);
  EXPECT_EQ(topo.min_ns, 40u);
  EXPECT_EQ(topo.max_ns, 60u);

  // Every category is copied, including untouched ones, and the Count sentinel
  // reads clean zeros.
  EXPECT_EQ(snapshot.timing.category(TraceCategory::Path).samples, 0u);
  EXPECT_EQ(snapshot.timing.category(TraceCategory::Count).samples, 0u);

  const auto timing_only = tess::diagnostics::capture_timing(buffer);
  EXPECT_EQ(timing_only.category(TraceCategory::Topology).total_ns, 100u);
}

}  // namespace
