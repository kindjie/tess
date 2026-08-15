#pragma once

// Reference Dear ImGui debug panels over the diagnostics export snapshots.
//
// This header is opt-in and dependency-free for tess core: it is compiled only
// when the CONSUMER defines both TESS_ENABLE_IMGUI and TESS_ENABLE_DIAGNOSTICS
// on its own target, and it never fetches or links Dear ImGui itself. The
// consumer must include <imgui.h> BEFORE this header (the panels call into the
// ImGui namespace); when the gates are on but ImGui has not been included, the
// #error below fires instead of emitting confusing name-lookup failures. The
// timing panel requires the Dear ImGui tables API available since 1.80.
//
// tess.h does NOT include this header, so a diagnostics build that does not use
// ImGui never sees it. The panels use stable ImGui text and table APIs, and
// uint64 values are printed through unsigned long long casts for portable
// printf-style formatting.
//
// Threading: the panels only read the snapshot copies they are passed, so they
// are safe on a render thread -- but producing those copies is not. Capture
// them per export.h's threading contract (on the recording thread, or with
// capture externally synchronized against recording), then hand the snapshot
// to the thread that draws.

#include <tess/diagnostics/diagnostics.h>

#if defined(TESS_ENABLE_IMGUI) && TESS_DIAGNOSTICS_ENABLED

#ifndef IMGUI_VERSION
#error "tess/debug/imgui/panels.h requires <imgui.h> to be included first"
#endif

#ifndef IMGUI_HAS_TABLE
#error "tess/debug/imgui/panels.h requires Dear ImGui 1.80 or later"
#endif

#include <tess/diagnostics/export.h>
#include <tess/diagnostics/trace.h>

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace tess::debug::imgui {

namespace detail {
[[nodiscard]] inline auto to_ull(std::uint64_t value) noexcept
    -> unsigned long long {
  return static_cast<unsigned long long>(value);
}

inline void draw_right_aligned(std::uint64_t value) {
  std::array<char, std::numeric_limits<std::uint64_t>::digits10 + 2> text{};
  const auto result =
      std::to_chars(text.data(), text.data() + text.size() - 1, value);
  const auto text_width = ImGui::CalcTextSize(text.data(), result.ptr).x;
  const auto available_width = ImGui::GetContentRegionAvail().x;
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available_width - text_width);
  ImGui::TextUnformatted(text.data(), result.ptr);
}
}  // namespace detail

/** Returns the stable display label used for a trace category in panels. */
[[nodiscard]] inline auto category_name(
    diagnostics::TraceCategory category) noexcept -> const char* {
  switch (category) {
    case diagnostics::TraceCategory::General:
      return "General";
    case diagnostics::TraceCategory::Path:
      return "Path";
    case diagnostics::TraceCategory::Topology:
      return "Topology";
    case diagnostics::TraceCategory::Queued:
      return "Queued";
    case diagnostics::TraceCategory::Planner:
      return "Planner";
    case diagnostics::TraceCategory::Scheduler:
      return "Scheduler";
    case diagnostics::TraceCategory::Render:
      return "Render";
    case diagnostics::TraceCategory::Count:
      return "Count";
  }
  return "?";
}

/** Draws per-category timing statistics in stable, independently clipped cells.
 */
inline void draw_timing_panel(const diagnostics::TimingSnapshot& timing) {
  ImGui::TextUnformatted("Timing (ns)");
  ImGui::Separator();
  constexpr int column_count = 6;
  constexpr ImGuiTableFlags flags =
      ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings |
      ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
      ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX |
      ImGuiTableFlags_ScrollY;
  const auto unit = ImGui::GetTextLineHeightWithSpacing();
  const auto visible_rows =
      static_cast<float>(diagnostics::trace_category_count) + 2.0F;
  if (!ImGui::BeginTable("##tess_timing", column_count, flags,
                         ImVec2{0.0F, unit * visible_rows})) {
    return;
  }
  ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed,
                          unit * 4.5F);
  ImGui::TableSetupColumn("Samples", ImGuiTableColumnFlags_WidthFixed,
                          unit * 3.5F);
  ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthFixed,
                          unit * 5.5F);
  ImGui::TableSetupColumn("Average", ImGuiTableColumnFlags_WidthFixed,
                          unit * 5.5F);
  ImGui::TableSetupColumn("Minimum", ImGuiTableColumnFlags_WidthFixed,
                          unit * 5.5F);
  ImGui::TableSetupColumn("Maximum", ImGuiTableColumnFlags_WidthFixed,
                          unit * 5.5F);
  ImGui::TableSetupScrollFreeze(1, 1);
  ImGui::TableHeadersRow();
  for (std::size_t index = 0; index < diagnostics::trace_category_count;
       ++index) {
    const auto category = static_cast<diagnostics::TraceCategory>(index);
    const auto& stats = timing.stats(category);
    const auto average =
        stats.samples == 0 ? std::uint64_t{0} : stats.total_ns / stats.samples;
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(category_name(category));
    ImGui::TableSetColumnIndex(1);
    detail::draw_right_aligned(stats.samples);
    ImGui::TableSetColumnIndex(2);
    detail::draw_right_aligned(stats.total_ns);
    ImGui::TableSetColumnIndex(3);
    detail::draw_right_aligned(average);
    ImGui::TableSetColumnIndex(4);
    detail::draw_right_aligned(stats.min_ns);
    ImGui::TableSetColumnIndex(5);
    detail::draw_right_aligned(stats.max_ns);
  }
  ImGui::EndTable();
}

/** Draws headline path-search work counters from a stable snapshot. */
inline void draw_path_counters_panel(const diagnostics::PathCounters& path) {
  ImGui::TextUnformatted("Path counters");
  ImGui::Separator();
  ImGui::Text("heap push / pop: %llu / %llu", detail::to_ull(path.heap_pushes),
              detail::to_ull(path.heap_pops));
  ImGui::Text("relax ok / attempts: %llu / %llu",
              detail::to_ull(path.relax_successes),
              detail::to_ull(path.relax_attempts));
  ImGui::Text("touched nodes: %llu", detail::to_ull(path.touched_nodes));
  ImGui::Text("passability checks: %llu",
              detail::to_ull(path.passability_checks));
}

/** Draws queued-phase execution and dirty-merge counters. */
inline void draw_queued_counters_panel(
    const diagnostics::QueuedPhaseCounters& queued) {
  ImGui::TextUnformatted("Queued phase counters");
  ImGui::Separator();
  ImGui::Text("phase calls / ops: %llu / %llu",
              detail::to_ull(queued.phase_calls),
              detail::to_ull(queued.phase_operations));
  ImGui::Text("failures: %llu", detail::to_ull(queued.phase_failures));
  ImGui::Text("dirty merged: %llu", detail::to_ull(queued.dirty_chunks_merged));
}

/** Draws allocation counts and byte totals. */
inline void draw_allocation_counters_panel(
    const diagnostics::AllocationCounters& allocation) {
  ImGui::TextUnformatted("Allocation counters");
  ImGui::Separator();
  ImGui::Text("alloc: %llu (%llu bytes)",
              detail::to_ull(allocation.allocations),
              detail::to_ull(allocation.allocation_bytes));
  ImGui::Text("free: %llu (%llu bytes)",
              detail::to_ull(allocation.deallocations),
              detail::to_ull(allocation.deallocation_bytes));
  ImGui::Text("live / peak: %llu / %llu bytes",
              detail::to_ull(allocation.live_bytes),
              detail::to_ull(allocation.peak_live_bytes));
}

/** Draws the newest duration spans with inclusive allocation traffic. */
inline void draw_recent_timing_spans_panel(
    const diagnostics::DiagnosticsSnapshot& snapshot) {
  ImGui::TextUnformatted("Recent timed spans");
  ImGui::Separator();
  for (std::size_t index = 0; index < snapshot.trace_record_count; ++index) {
    const auto& record = snapshot.trace_records[index];
    if (record.kind != diagnostics::TraceRecordKind::Duration) {
      continue;
    }
    const auto label_size =
        record.label.size() >
                static_cast<std::size_t>(std::numeric_limits<int>::max())
            ? std::numeric_limits<int>::max()
            : static_cast<int>(record.label.size());
    const auto milliseconds = static_cast<double>(record.value) / 1'000'000.0;
    const auto* label = record.label.empty() ? "" : record.label.data();
    ImGui::Text("%-10s %.*s: %.3f ms; alloc/free %llu/%llu bytes",
                category_name(record.category), label_size, label, milliseconds,
                detail::to_ull(record.allocation_bytes),
                detail::to_ull(record.deallocation_bytes));
  }
  if (snapshot.trace_records_dropped != 0) {
    ImGui::Text("dropped: %llu",
                detail::to_ull(snapshot.trace_records_dropped));
  }
}

/** Draws all timing, path, queue, and allocation snapshot sections. */
inline void draw_diagnostics_panel(
    const diagnostics::DiagnosticsSnapshot& snapshot) {
  draw_timing_panel(snapshot.timing);
  ImGui::Separator();
  draw_recent_timing_spans_panel(snapshot);
  ImGui::Separator();
  draw_path_counters_panel(snapshot.path);
  ImGui::Separator();
  draw_queued_counters_panel(snapshot.queued);
  ImGui::Separator();
  draw_allocation_counters_panel(snapshot.allocation);
}

}  // namespace tess::debug::imgui

#endif  // defined(TESS_ENABLE_IMGUI) && TESS_DIAGNOSTICS_ENABLED
