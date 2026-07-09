#pragma once

// Reference Dear ImGui debug panels over the diagnostics export snapshots.
//
// This header is opt-in and dependency-free for tess core: it is compiled only
// when the CONSUMER defines both TESS_ENABLE_IMGUI and TESS_ENABLE_DIAGNOSTICS
// on its own target, and it never fetches or links Dear ImGui itself. The
// consumer must include <imgui.h> BEFORE this header (the panels call into the
// ImGui namespace); when the gates are on but ImGui has not been included, the
// #error below fires instead of emitting confusing name-lookup failures.
//
// tess.h does NOT include this header, so a diagnostics build that does not use
// ImGui never sees it. Only the three most stable ImGui text primitives are
// used (Text, TextUnformatted, Separator) so the panels compile across ImGui
// versions and uint64 values are printed through unsigned long long casts for
// portable printf-style formatting.

#include <tess/diagnostics/diagnostics.h>

#if defined(TESS_ENABLE_IMGUI) && TESS_DIAGNOSTICS_ENABLED

#ifndef IMGUI_VERSION
#error "tess/debug/imgui/panels.h requires <imgui.h> to be included first"
#endif

#include <tess/diagnostics/export.h>
#include <tess/diagnostics/trace.h>

#include <cstddef>
#include <cstdint>

namespace tess::debug::imgui {

namespace detail {
[[nodiscard]] inline auto to_ull(std::uint64_t value) noexcept
    -> unsigned long long {
  return static_cast<unsigned long long>(value);
}
}  // namespace detail

// Human-readable name for a trace category, for panel labels.
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

// Per-category timing table (nanoseconds): samples, total, average, min, max.
inline void draw_timing_panel(const diagnostics::TimingSnapshot& timing) {
  ImGui::TextUnformatted("Timing (ns)");
  ImGui::Separator();
  for (std::size_t index = 0; index < diagnostics::trace_category_count;
       ++index) {
    const auto category = static_cast<diagnostics::TraceCategory>(index);
    const auto& stats = timing.category(category);
    const auto average =
        stats.samples == 0 ? std::uint64_t{0} : stats.total_ns / stats.samples;
    ImGui::Text("%-10s n=%llu total=%llu avg=%llu min=%llu max=%llu",
                category_name(category), detail::to_ull(stats.samples),
                detail::to_ull(stats.total_ns), detail::to_ull(average),
                detail::to_ull(stats.min_ns), detail::to_ull(stats.max_ns));
  }
}

// Path-search counters (the headline A* internals).
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

// Queued-phase execution counters.
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

// Allocation counters.
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
}

// Composite panel drawing every section of a DiagnosticsSnapshot in order.
inline void draw_diagnostics_panel(
    const diagnostics::DiagnosticsSnapshot& snapshot) {
  draw_timing_panel(snapshot.timing);
  ImGui::Separator();
  draw_path_counters_panel(snapshot.path);
  ImGui::Separator();
  draw_queued_counters_panel(snapshot.queued);
  ImGui::Separator();
  draw_allocation_counters_panel(snapshot.allocation);
}

}  // namespace tess::debug::imgui

#endif  // defined(TESS_ENABLE_IMGUI) && TESS_DIAGNOSTICS_ENABLED
