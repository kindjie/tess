#include <array>
#include <cstdint>
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
      .flow_identities = true,
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
      &demo::ReadinessEvidence::flow_identities,
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

[[nodiscard]] auto flow_identities_hold(const demo::DiagnosticsModel& model)
    -> bool {
  const auto& flow = model.flow_snapshot();
  return flow.admission_identity_ok && flow.retention_identity_ok &&
         flow.counters.offered == flow.counters.admitted &&
         flow.counters.outstanding_high_water == demo::default_agent_count;
}

[[nodiscard]] auto wall_edit_produces_queued_work(demo::DiagnosticsModel& model)
    -> bool {
  const auto before_calls = model.queued_phase_calls();
  const auto before_dirty = model.queued_dirty_merged();
  return model.set_passable(65, 48, false) && model.tick() &&
         model.queued_phase_calls() > before_calls &&
         model.queued_dirty_merged() > before_dirty &&
         !model.selected_passable();
}

}  // namespace

auto main() -> int {
  demo::DiagnosticsModel model;
  if (!readiness_without_each_required_signal_is_false()) {
    std::cerr << "readiness contract failed\n";
    return 1;
  }
  if (!model.tick()) {
    std::cerr << "initial colony workload failed\n";
    return 1;
  }
  const auto first = model.snapshot();
  if (first.path.passability_checks == 0 ||
      first.path.reconstructed_nodes == 0 || first.queued.phase_calls == 0 ||
      first.queued.dirty_chunks_merged == 0 ||
      first.allocation.allocations == 0 ||
      first.allocation.allocations != first.allocation.deallocations ||
      first.allocation.allocation_bytes !=
          first.allocation.deallocation_bytes ||
      first.allocation.live_bytes != 0 || !flow_identities_hold(model)) {
    std::cerr << "initial diagnostics snapshot failed: passability="
              << first.path.passability_checks
              << " reconstructed=" << first.path.reconstructed_nodes
              << " queued=" << first.queued.phase_calls
              << " dirty=" << first.queued.dirty_chunks_merged
              << " alloc/free=" << first.allocation.allocations << '/'
              << first.allocation.deallocations
              << " bytes=" << first.allocation.allocation_bytes << '/'
              << first.allocation.deallocation_bytes
              << " live=" << first.allocation.live_bytes
              << " planning=" << model.planning_queries() << '/'
              << model.planning_expansions()
              << " flow=" << flow_identities_hold(model) << '\n';
    return 1;
  }

  if (!model.tick() || model.snapshot().allocation.allocations != 0 ||
      model.snapshot().allocation.deallocations != 0) {
    std::cerr << "reserved warm tick allocation contract failed\n";
    return 1;
  }
  model.set_paused(true);
  const auto paused_tick = model.fixed_ticks();
  if (!model.tick() || model.fixed_ticks() != paused_tick) {
    std::cerr << "pause contract failed\n";
    return 1;
  }
  model.set_paused(false);
  if (!model.select(65, 48) || !wall_edit_produces_queued_work(model)) {
    std::cerr << "wall edit did not produce queued work\n";
    return 1;
  }

  for (int tick = 0;
       tick < 1000 && model.flow_snapshot().counters.completed == 0; ++tick) {
    if (!model.tick()) {
      std::cerr << "colony lifecycle tick failed\n";
      return 1;
    }
  }
  if (model.flow_snapshot().counters.completed == 0 ||
      model.planning_expansions() < 0 || !flow_identities_hold(model)) {
    std::cerr << "terminal lifecycle accounting failed\n";
    return 1;
  }

  model.note_runtime_initialized();
  model.note_imgui_frame(true);
  if (!model.ready()) {
    std::cerr << "readiness evidence failed\n";
    return 1;
  }
  model.reset();
  if (model.fixed_ticks() != 0 || model.paused() ||
      model.flow_snapshot().counters.admitted != demo::default_agent_count ||
      !model.tick() || !model.ready()) {
    std::cerr << "reset contract failed\n";
    return 1;
  }
  std::cout << "web colony diagnostics model: ok\n";
  return 0;
}
