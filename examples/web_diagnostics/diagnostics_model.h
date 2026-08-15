#pragma once

#include <tess/diagnostics/export.h>
#include <tess/tess.h>

#include <array>
#include <cstdint>

namespace tess::examples::web_diagnostics {

inline constexpr int width = 32;
inline constexpr int height = 24;

struct PassableTag {};
struct TerrainTag {};

using Shape = tess::Shape<Extent3{width, height, 1}, Extent3{8, 8, 1}>;
using Schema =
    FieldSchema<Field<PassableTag, bool>, Field<TerrainTag, std::uint16_t>>;
using World = AlwaysResidentWorld<Shape, Schema>;

struct ReadinessEvidence {
  bool runtime_initialized = false;
  bool workload_tick = false;
  bool path_counters = false;
  bool queued_counters = false;
  bool timing_samples = false;
  bool duration_spans = false;
  bool allocation_counters = false;
  bool allocation_balanced = false;
  bool imgui_frame = false;

  [[nodiscard]] auto ready() const noexcept -> bool;
};

class DiagnosticsModel {
 public:
  DiagnosticsModel();

  [[nodiscard]] auto tick() -> bool;
  void set_paused(bool value) noexcept { paused_ = value; }
  [[nodiscard]] auto paused() const noexcept -> bool { return paused_; }
  void set_intensity(int value) noexcept;
  [[nodiscard]] auto intensity() const noexcept -> int { return intensity_; }
  [[nodiscard]] auto select(int x, int y) noexcept -> bool;
  [[nodiscard]] auto set_passable(int x, int y, bool value) noexcept -> bool;
  [[nodiscard]] auto selected() const noexcept -> Coord3 { return selected_; }
  [[nodiscard]] auto selected_passable() const noexcept -> bool {
    return world_.field<PassableTag>(selected_);
  }
  [[nodiscard]] auto world() noexcept -> World& { return world_; }
  [[nodiscard]] auto world() const noexcept -> const World& { return world_; }
  [[nodiscard]] auto snapshot() const noexcept
      -> const diagnostics::DiagnosticsSnapshot& {
    return snapshot_;
  }
  [[nodiscard]] auto evidence() const noexcept -> ReadinessEvidence {
    return evidence_;
  }
  void note_runtime_initialized() noexcept {
    evidence_.runtime_initialized = true;
  }
  void note_imgui_frame(bool submitted) noexcept {
    evidence_.imgui_frame = submitted;
  }
  [[nodiscard]] auto ready() const noexcept -> bool {
    return evidence_.ready();
  }

 private:
  void initialize_world();
  [[nodiscard]] auto run_path_workload() -> bool;
  [[nodiscard]] auto run_queued_workload() -> bool;
  void update_evidence() noexcept;

  World world_;
  PathScratch path_scratch_;
  PlannedPhaseExecutionScratch phase_scratch_;
  std::array<diagnostics::TraceRecord, 128> trace_storage_{};
  diagnostics::TraceBuffer trace_buffer_{trace_storage_};
  diagnostics::PathCounters path_counters_{};
  diagnostics::QueuedPhaseCounters queued_counters_{};
  diagnostics::AllocationCounters allocation_counters_{};
  diagnostics::DiagnosticsSnapshot snapshot_{};
  Coord3 selected_{4, 4, 0};
  int intensity_ = 4;
  bool paused_ = false;
  std::uint64_t tick_number_ = 0;
  ReadinessEvidence evidence_{};
};

}  // namespace tess::examples::web_diagnostics
