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
