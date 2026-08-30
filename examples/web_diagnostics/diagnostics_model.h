#pragma once

#include <tess/diagnostics/diagnostics.h>
#include <tess/diagnostics/export.h>
#include <tess/diagnostics/trace.h>

#include <array>
#include <cstdint>
#include <memory>

#include "../web_colony/colony_model.h"

namespace tess::examples::web_diagnostics {

inline constexpr int default_agent_count = 128;

struct ReadinessEvidence {
  bool runtime_initialized = false;
  bool workload_tick = false;
  bool path_counters = false;
  bool queued_counters = false;
  bool timing_samples = false;
  bool duration_spans = false;
  bool allocation_counters = false;
  bool allocation_balanced = false;
  bool flow_identities = false;
  bool imgui_frame = false;

  [[nodiscard]] auto ready() const noexcept -> bool;
};

/** Diagnostics-enabled host over the shared colony tutorial model. */
class DiagnosticsModel {
 public:
  DiagnosticsModel();

  [[nodiscard]] auto tick() -> bool;
  void reset();
  void set_paused(bool value) noexcept { paused_ = value; }
  [[nodiscard]] auto paused() const noexcept -> bool { return paused_; }
  [[nodiscard]] auto select(int x, int y) noexcept -> bool;
  [[nodiscard]] auto set_passable(int x, int y, bool value) -> bool;
  [[nodiscard]] auto selected_x() const noexcept -> int { return selected_x_; }
  [[nodiscard]] auto selected_y() const noexcept -> int { return selected_y_; }
  [[nodiscard]] auto selected_passable() const noexcept -> bool;

  [[nodiscard]] auto snapshot() const noexcept
      -> const diagnostics::DiagnosticsSnapshot& {
    return snapshot_;
  }
  [[nodiscard]] auto flow_snapshot() const noexcept
      -> const diagnostics::FlowHealthSnapshot& {
    return flow_snapshot_;
  }
  [[nodiscard]] auto fixed_ticks() const noexcept -> std::uint64_t {
    return fixed_ticks_;
  }
  [[nodiscard]] auto path_passability_checks() const noexcept -> std::uint64_t {
    return path_passability_checks_total_;
  }
  [[nodiscard]] auto queued_phase_calls() const noexcept -> std::uint64_t {
    return queued_phase_calls_total_;
  }
  [[nodiscard]] auto queued_dirty_merged() const noexcept -> std::uint64_t {
    return queued_dirty_merged_total_;
  }
  [[nodiscard]] auto planning_queries() const noexcept -> int;
  [[nodiscard]] auto planning_expansions() const noexcept -> int;
  [[nodiscard]] auto evidence() const noexcept -> ReadinessEvidence {
    return evidence_;
  }
  void note_runtime_initialized() noexcept {
    evidence_.runtime_initialized = true;
  }
  void note_imgui_frame(bool submitted) noexcept {
    evidence_.imgui_frame = evidence_.imgui_frame || submitted;
  }
  [[nodiscard]] auto ready() const noexcept -> bool {
    return evidence_.ready();
  }

 private:
  void update_evidence() noexcept;
  void run_consumer_allocation_probe();

  diagnostics::FlowAccounting flow_accounting_{};
  std::unique_ptr<web_colony::ColonyModel> colony_;
  std::array<diagnostics::TraceRecord, 128> trace_storage_{};
  diagnostics::TraceBuffer trace_buffer_{trace_storage_};
  diagnostics::PathCounters path_counters_{};
  diagnostics::QueuedPhaseCounters queued_counters_{};
  diagnostics::AllocationCounters allocation_counters_{};
  diagnostics::DiagnosticsSnapshot snapshot_{};
  diagnostics::TimingSnapshot timing_history_{};
  diagnostics::FlowHealthSnapshot flow_snapshot_{};
  std::uint64_t fixed_ticks_ = 0;
  std::uint64_t path_passability_checks_total_ = 0;
  std::uint64_t queued_phase_calls_total_ = 0;
  std::uint64_t queued_dirty_merged_total_ = 0;
  std::uint64_t presentation_checksum_ = 0;
  int selected_x_ = 64;
  int selected_y_ = 48;
  bool paused_ = false;
  bool allocation_probe_pending_ = true;
  ReadinessEvidence evidence_{};
};

}  // namespace tess::examples::web_diagnostics
