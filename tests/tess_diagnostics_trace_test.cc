#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstdint>
#include <string_view>

namespace {

using tess::diagnostics::BufferedWarningSink;
using tess::diagnostics::NullWarningSink;
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

}  // namespace
