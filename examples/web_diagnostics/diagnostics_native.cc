#include <array>
#include <iostream>

#include "diagnostics_model.h"

namespace demo = tess::examples::web_diagnostics;

namespace {

[[nodiscard]] auto readiness_without_each_required_signal_is_false() -> bool {
  auto all = demo::ReadinessEvidence{
      .runtime_initialized = true,
      .workload_tick = true,
      .path_counters = true,
      .queued_counters = true,
      .timing_samples = true,
      .duration_spans = true,
      .allocation_counters = true,
      .allocation_balanced = true,
      .imgui_frame = true,
  };
  if (!all.ready()) {
    return false;
  }
  const std::array members{
      &demo::ReadinessEvidence::runtime_initialized,
      &demo::ReadinessEvidence::workload_tick,
      &demo::ReadinessEvidence::path_counters,
      &demo::ReadinessEvidence::queued_counters,
      &demo::ReadinessEvidence::timing_samples,
      &demo::ReadinessEvidence::duration_spans,
      &demo::ReadinessEvidence::allocation_counters,
      &demo::ReadinessEvidence::allocation_balanced,
      &demo::ReadinessEvidence::imgui_frame,
  };
  for (const auto member : members) {
    auto missing = all;
    missing.*member = false;
    if (missing.ready()) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] auto expected_path_outcomes_remain_operational(
    demo::DiagnosticsModel& model) -> bool {
  if (!model.set_passable(0, 0, false) || !model.tick()) {
    return false;
  }
  if (!model.set_passable(0, 0, true)) {
    return false;
  }
  return model.tick();
}

}  // namespace

auto main() -> int {
  demo::DiagnosticsModel model;
  if (!readiness_without_each_required_signal_is_false()) {
    std::cerr << "readiness contract failed\n";
    return 1;
  }
  if (!model.tick()) {
    std::cerr << "initial workload failed\n";
    return 1;
  }
  const auto first = model.snapshot();
  if (first.path.initializations != 1 || first.path.heap_pops == 0 ||
      first.queued.phase_calls == 0 || first.allocation.live_bytes != 0) {
    std::cerr << "initial snapshot failed: path=" << first.path.initializations
              << " queue=" << first.queued.phase_calls
              << " live=" << first.allocation.live_bytes << '\n';
    return 1;
  }
  model.set_paused(true);
  if (!model.tick() ||
      model.snapshot().path.heap_pops != first.path.heap_pops) {
    std::cerr << "pause contract failed\n";
    return 1;
  }
  model.set_paused(false);
  model.set_intensity(7);
  if (!model.tick() ||
      model.snapshot().path.heap_pops <= first.path.heap_pops) {
    std::cerr << "intensity contract failed\n";
    return 1;
  }
  const auto& accumulated_path =
      model.snapshot().timing.stats(tess::diagnostics::TraceCategory::Path);
  if (accumulated_path.samples <=
      first.timing.stats(tess::diagnostics::TraceCategory::Path).samples) {
    std::cerr << "timing aggregation contract failed\n";
    return 1;
  }
  if (!expected_path_outcomes_remain_operational(model)) {
    std::cerr << "expected path outcome handling failed\n";
    return 1;
  }
  if (model.select(-1, 0) || !model.select(3, 4) ||
      model.set_passable(demo::width, 0, false) ||
      !model.set_passable(3, 4, false)) {
    std::cerr << "control validation failed\n";
    return 1;
  }
  model.note_runtime_initialized();
  model.note_imgui_frame(true);
  if (!model.ready()) {
    const auto evidence = model.evidence();
    std::cerr << "readiness evidence failed: " << evidence.runtime_initialized
              << evidence.workload_tick << evidence.path_counters
              << evidence.queued_counters << evidence.timing_samples
              << evidence.duration_spans << evidence.allocation_counters
              << evidence.allocation_balanced << evidence.imgui_frame << '\n';
    return 1;
  }
  std::cout << "web diagnostics model: ok\n";
  return 0;
}
