#include "diagnostics_model.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace tess::examples::web_diagnostics {
namespace {

template <typename T>
class CountingAllocator {
 public:
  using value_type = T;

  CountingAllocator() noexcept = default;

  template <typename U>
  CountingAllocator(const CountingAllocator<U>&) noexcept {}

  [[nodiscard]] auto allocate(std::size_t count) -> T* {
    const auto bytes = count * sizeof(T);
    auto* value = std::allocator<T>{}.allocate(count);
    diagnostics::record_allocation(bytes);
    return value;
  }

  void deallocate(T* value, std::size_t count) noexcept {
    diagnostics::record_deallocation(count * sizeof(T));
    std::allocator<T>{}.deallocate(value, count);
  }

  template <typename U>
  [[nodiscard]] auto operator==(const CountingAllocator<U>&) const noexcept
      -> bool {
    return true;
  }
};

[[nodiscard]] auto has_duration(
    const diagnostics::DiagnosticsSnapshot& snapshot,
    std::string_view label) noexcept -> bool {
  for (std::size_t index = 0; index < snapshot.trace_record_count; ++index) {
    const auto& record = snapshot.trace_records[index];
    if (record.kind == diagnostics::TraceRecordKind::Duration &&
        record.label == label) {
      return true;
    }
  }
  return false;
}

void accumulate_timing(diagnostics::TimingSnapshot& history,
                       const diagnostics::TimingSnapshot& frame) noexcept {
  for (std::size_t index = 0; index < diagnostics::trace_category_count;
       ++index) {
    const auto& source = frame.per_category[index];
    if (source.samples == 0) {
      continue;
    }
    auto& destination = history.per_category[index];
    if (destination.samples == 0) {
      destination = source;
      continue;
    }
    destination.samples += source.samples;
    destination.total_ns += source.total_ns;
    destination.min_ns = std::min(destination.min_ns, source.min_ns);
    destination.max_ns = std::max(destination.max_ns, source.max_ns);
  }
}

[[nodiscard]] auto in_bounds(int x, int y) noexcept -> bool {
  return x >= 0 && x < web_colony::width && y >= 0 && y < web_colony::height;
}

}  // namespace

auto ReadinessEvidence::ready() const noexcept -> bool {
  return runtime_initialized && workload_tick && path_counters &&
         queued_counters && timing_samples && duration_spans &&
         allocation_counters && allocation_balanced && flow_identities &&
         imgui_frame;
}

DiagnosticsModel::DiagnosticsModel() { reset(); }

void DiagnosticsModel::reset() {
  const auto runtime_initialized = evidence_.runtime_initialized;
  const auto imgui_frame = evidence_.imgui_frame;
  colony_.reset();
  flow_accounting_ = {};
  colony_ = std::make_unique<web_colony::ColonyModel>(default_agent_count,
                                                      &flow_accounting_);
  selected_x_ = 64;
  selected_y_ = 48;
  paused_ = false;
  allocation_probe_pending_ = true;
  fixed_ticks_ = 0;
  path_passability_checks_total_ = 0;
  queued_phase_calls_total_ = 0;
  queued_dirty_merged_total_ = 0;
  presentation_checksum_ = 0;
  timing_history_ = {};
  snapshot_ = {};
  flow_snapshot_ = diagnostics::snapshot(flow_accounting_);
  evidence_ = {
      .runtime_initialized = runtime_initialized,
      .imgui_frame = imgui_frame,
  };
  // The first real colony tick applies one queued edit. This gives readiness
  // an end-to-end queued-work proof without retaining the retired synthetic
  // operation batch.
  (void)set_passable(selected_x_, selected_y_, false);
}

auto DiagnosticsModel::select(int x, int y) noexcept -> bool {
  if (!in_bounds(x, y)) {
    return false;
  }
  selected_x_ = x;
  selected_y_ = y;
  return true;
}

auto DiagnosticsModel::set_passable(int x, int y, bool value) -> bool {
  if (!in_bounds(x, y)) {
    return false;
  }
  return colony_->set_wall(x, y, !value);
}

auto DiagnosticsModel::selected_passable() const noexcept -> bool {
  const auto index = static_cast<std::size_t>(selected_y_) * web_colony::width +
                     static_cast<std::size_t>(selected_x_);
  return colony_->tiles()[index] == 0;
}

void DiagnosticsModel::run_consumer_allocation_probe() {
  if (!allocation_probe_pending_) {
    return;
  }
  using Snapshot = std::vector<std::int16_t, CountingAllocator<std::int16_t>>;
  Snapshot positions(colony_->current_agents(),
                     colony_->current_agents() + colony_->agent_count() * 2);
  presentation_checksum_ = 0;
  for (const auto value : positions) {
    presentation_checksum_ += static_cast<std::uint64_t>(value);
  }
  allocation_probe_pending_ = false;
}

auto DiagnosticsModel::tick() -> bool {
  if (paused_) {
    return true;
  }
  if (colony_->turnaround_ready()) {
    (void)colony_->relaunch();
  }
  path_counters_.reset();
  queued_counters_.reset();
  allocation_counters_.reset();
  trace_buffer_.clear();

  auto succeeded = false;
  {
    succeeded =
        colony_->tick_with_diagnostics(0.05, path_counters_, queued_counters_,
                                       trace_buffer_) >= 0.0;
    diagnostics::ScopedAllocationCounters allocation{allocation_counters_};
    run_consumer_allocation_probe();
  }
  ++fixed_ticks_;
  snapshot_ = diagnostics::capture_diagnostics(
      path_counters_, allocation_counters_, queued_counters_, trace_buffer_);
  accumulate_timing(timing_history_, snapshot_.timing);
  snapshot_.timing = timing_history_;
  flow_snapshot_ = diagnostics::snapshot(flow_accounting_);
  path_passability_checks_total_ += snapshot_.path.passability_checks;
  queued_phase_calls_total_ += snapshot_.queued.phase_calls;
  queued_dirty_merged_total_ += snapshot_.queued.dirty_chunks_merged;
  evidence_.workload_tick = evidence_.workload_tick || succeeded;
  update_evidence();
  return succeeded;
}

auto DiagnosticsModel::planning_queries() const noexcept -> int {
  return colony_->planning_queries_last_tick();
}

auto DiagnosticsModel::planning_expansions() const noexcept -> int {
  return colony_->planning_expansions_last_tick();
}

void DiagnosticsModel::update_evidence() noexcept {
  evidence_.path_counters =
      evidence_.path_counters || (snapshot_.path.passability_checks > 0 &&
                                  snapshot_.path.reconstructed_nodes > 0);
  evidence_.queued_counters =
      evidence_.queued_counters || (snapshot_.queued.phase_calls > 0 &&
                                    snapshot_.queued.phase_operations > 0 &&
                                    snapshot_.queued.dirty_chunks_merged > 0);
  evidence_.timing_samples =
      evidence_.timing_samples ||
      (snapshot_.timing.stats(diagnostics::TraceCategory::General).samples >
           0 &&
       snapshot_.timing.stats(diagnostics::TraceCategory::Scheduler).samples >
           0);
  evidence_.duration_spans =
      evidence_.duration_spans || (has_duration(snapshot_, "schedule_tick") &&
                                   has_duration(snapshot_, "agents"));
  evidence_.allocation_counters = evidence_.allocation_counters ||
                                  (snapshot_.allocation.allocations > 0 &&
                                   snapshot_.allocation.allocation_bytes > 0 &&
                                   snapshot_.allocation.peak_live_bytes > 0);
  evidence_.allocation_balanced =
      evidence_.allocation_balanced ||
      (snapshot_.allocation.allocations > 0 &&
       snapshot_.allocation.allocations == snapshot_.allocation.deallocations &&
       snapshot_.allocation.allocation_bytes ==
           snapshot_.allocation.deallocation_bytes &&
       snapshot_.allocation.live_bytes == 0);
  evidence_.flow_identities =
      evidence_.flow_identities || (flow_snapshot_.counters.offered > 0 &&
                                    flow_snapshot_.admission_identity_ok &&
                                    flow_snapshot_.retention_identity_ok);
}

}  // namespace tess::examples::web_diagnostics
