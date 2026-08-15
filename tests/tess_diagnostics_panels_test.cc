// Compile-and-run check for the opt-in ImGui reference panels. The stub
// <imgui.h> (tests/imgui_stub) must be included before panels.h, exactly as a
// real consumer includes the real <imgui.h> first; the include order is pinned
// with clang-format off so the sorter cannot move panels.h ahead of it.
// clang-format off
#include <imgui.h>
// clang-format on

#include <gtest/gtest.h>
#include <tess/debug/imgui/panels.h>
#include <tess/debug/imgui/tools.h>
#include <tess/tess.h>

#include <cstring>

namespace {

// A populated snapshot exercises every branch (non-zero samples -> the average
// division path in the timing panel).
[[nodiscard]] tess::diagnostics::DiagnosticsSnapshot make_snapshot() {
  tess::diagnostics::DiagnosticsSnapshot snapshot;
  snapshot.path.heap_pushes = 12;
  snapshot.queued.phase_calls = 3;
  snapshot.allocation.allocations = 1;
  snapshot.allocation.live_bytes = 64;
  snapshot.allocation.peak_live_bytes = 128;
  snapshot.timing.per_category[static_cast<std::size_t>(
      tess::diagnostics::TraceCategory::Path)] = {2, 100, 40, 60};
  snapshot.trace_records[0] = {
      tess::diagnostics::TraceCategory::Scheduler,
      "schedule_tick",
      2'000'000,
      0,
      tess::diagnostics::TraceRecordKind::Duration,
      256,
      64,
  };
  snapshot.trace_record_count = 1;
  return snapshot;
}

TEST(TessDiagnosticsPanels, DrawFunctionsCompileAndRun) {
  const auto snapshot = make_snapshot();
  tess::debug::imgui::draw_diagnostics_panel(snapshot);
  tess::debug::imgui::draw_timing_panel(snapshot.timing);
  tess::debug::imgui::draw_path_counters_panel(snapshot.path);
  tess::debug::imgui::draw_queued_counters_panel(snapshot.queued);
  tess::debug::imgui::draw_allocation_counters_panel(snapshot.allocation);
  tess::debug::imgui::draw_recent_timing_spans_panel(snapshot);
  SUCCEED();
}

TEST(TessDiagnosticsPanels, EmptyTimingLabelUsesValidStringPointer) {
  tess_imgui_stub::reset();
  tess::diagnostics::DiagnosticsSnapshot snapshot;
  snapshot.trace_records[0].kind = tess::diagnostics::TraceRecordKind::Duration;
  snapshot.trace_record_count = 1;

  tess::debug::imgui::draw_recent_timing_spans_panel(snapshot);

  ASSERT_NE(tess_imgui_stub::last_precision_string, nullptr);
  EXPECT_STREQ(tess_imgui_stub::last_precision_string, "");
}

TEST(TessDiagnosticsPanels, TimingRowsUseStableColumnsAcrossDigitBoundaries) {
  tess_imgui_stub::reset();
  tess::diagnostics::TimingSnapshot timing;
  timing.per_category[0] = {9, 99, 9, 99};
  timing.per_category[1] = {10, 100, 10, 100};

  tess::debug::imgui::draw_timing_panel(timing);

  EXPECT_EQ(tess_imgui_stub::table_begin_count, 1);
  EXPECT_EQ(tess_imgui_stub::table_end_count, 1);
  EXPECT_EQ(tess_imgui_stub::table_column_count, 6);
  EXPECT_EQ(tess_imgui_stub::table_setup_count, 6);
  EXPECT_EQ(tess_imgui_stub::table_header_row_count, 1);
  EXPECT_EQ(tess_imgui_stub::table_row_count,
            static_cast<int>(tess::diagnostics::trace_category_count));
  EXPECT_EQ(tess_imgui_stub::table_frozen_columns, 1);
  EXPECT_EQ(tess_imgui_stub::table_frozen_rows, 1);
  EXPECT_NE(tess_imgui_stub::table_flags & ImGuiTableFlags_ScrollX, 0);
  EXPECT_NE(tess_imgui_stub::table_flags & ImGuiTableFlags_ScrollY, 0);
  EXPECT_GT(tess_imgui_stub::table_outer_size.y, 0.0F);
  for (const int visits : tess_imgui_stub::table_column_visits) {
    EXPECT_EQ(visits,
              static_cast<int>(tess::diagnostics::trace_category_count));
  }
}

TEST(TessDiagnosticsPanels, TimingValuesAreRightAligned) {
  tess_imgui_stub::reset();
  tess::diagnostics::TimingSnapshot timing;
  for (auto& stats : timing.per_category) {
    stats = {UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX};
  }

  tess::debug::imgui::draw_timing_panel(timing);

  constexpr auto numeric_columns = 5;
  EXPECT_EQ(tess_imgui_stub::cursor_pos_x_set_count,
            static_cast<int>(tess::diagnostics::trace_category_count) *
                numeric_columns);
  // The stub exposes 100 px after a 5 px cursor. A 20-digit uint64 is 160 px
  // wide and therefore starts at -55 px so its right edge stays at 105 px;
  // the one-digit average starts at 97 px and reaches the same edge.
  EXPECT_FLOAT_EQ(tess_imgui_stub::cursor_pos_x_values[0], -55.0F);
  EXPECT_FLOAT_EQ(tess_imgui_stub::cursor_pos_x_values[2], 97.0F);
}

TEST(TessDiagnosticsPanels, CategoryNamesCoverEveryCategory) {
  using tess::diagnostics::TraceCategory;
  EXPECT_STREQ(tess::debug::imgui::category_name(TraceCategory::General),
               "General");
  EXPECT_STREQ(tess::debug::imgui::category_name(TraceCategory::Planner),
               "Planner");
  // Every real category resolves to a non-empty, non-fallback name.
  for (std::size_t index = 0; index < tess::diagnostics::trace_category_count;
       ++index) {
    const char* name =
        tess::debug::imgui::category_name(static_cast<TraceCategory>(index));
    EXPECT_GT(std::strlen(name), 0u);
    EXPECT_STRNE(name, "?");
  }
}

}  // namespace
