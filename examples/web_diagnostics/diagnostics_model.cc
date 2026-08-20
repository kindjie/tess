#include "diagnostics_model.h"

#include <tess/diagnostics/trace.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace tess::examples::web_diagnostics {
namespace {

constexpr auto kTerrainDirty = tess::DirtyMask{1U << 0U};

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

[[nodiscard]] auto in_bounds(int x, int y) noexcept -> bool {
  return x >= 0 && x < width && y >= 0 && y < height;
}

[[nodiscard]] auto coord(int x, int y) noexcept -> Coord2 { return {x, y}; }

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
    if (source.min_ns < destination.min_ns) {
      destination.min_ns = source.min_ns;
    }
    if (source.max_ns > destination.max_ns) {
      destination.max_ns = source.max_ns;
    }
  }
}

}  // namespace

auto ReadinessEvidence::ready() const noexcept -> bool {
  return runtime_initialized && workload_tick && path_counters &&
         queued_counters && timing_samples && duration_spans &&
         allocation_counters && allocation_balanced && imgui_frame;
}

DiagnosticsModel::DiagnosticsModel() {
  initialize_world();
  path_scratch_.reserve_nodes(static_cast<std::size_t>(width) *
                              static_cast<std::size_t>(height));
}

void DiagnosticsModel::initialize_world() {
  world_.fill_field<PassableTag>(true);
  for (int y = 0; y < height; ++y) {
    if (y != 5 && y != 18) {
      world_.field<PassableTag>(coord(width / 3, y)) = false;
    }
    if (y != 9 && y != 21) {
      world_.field<PassableTag>(coord(2 * width / 3, y)) = false;
    }
  }
}

void DiagnosticsModel::set_intensity(int value) noexcept {
  intensity_ = std::clamp(value, 1, 32);
}

auto DiagnosticsModel::select(int x, int y) noexcept -> bool {
  if (!in_bounds(x, y)) {
    return false;
  }
  selected_ = coord(x, y);
  return true;
}

auto DiagnosticsModel::set_passable(int x, int y, bool value) noexcept -> bool {
  if (!in_bounds(x, y)) {
    return false;
  }
  const auto tile = coord(x, y);
  world_.field<PassableTag>(tile) = value;
  const auto resolved = world_.try_resolve(tile);
  if (resolved.has_value()) {
    world_.mark_dirty(resolved->chunk_key, kTerrainDirty,
                      Box3{tile, Extent3{1, 1, 1}});
  }
  return true;
}

auto DiagnosticsModel::run_path_workload() -> bool {
  diagnostics::ScopedTimer timer{diagnostics::TraceCategory::Path,
                                 "path_search"};
  const auto start = coord(0, 0);
  const auto goal = coord(width - 1, height - 1);
  for (int iteration = 0; iteration < intensity_; ++iteration) {
    const auto result = astar_path<World, PassableTag>(
        world_, PathRequest{start, goal}, path_scratch_);
    switch (result.status) {
      case PathStatus::Found:
      case PathStatus::InvalidStart:
      case PathStatus::InvalidGoal:
      case PathStatus::NoPath:
        break;
      case PathStatus::NotComputed:
      case PathStatus::NoCandidate:
      case PathStatus::Indeterminate:
      case PathStatus::CostOverflow:
        return false;
    }
  }
  return true;
}

auto DiagnosticsModel::run_queued_workload() -> bool {
  diagnostics::ScopedTimer timer{diagnostics::TraceCategory::Queued,
                                 "queued_phase"};
  OperationBatch ops;
  using KeyVector = std::vector<ChunkKey, CountingAllocator<ChunkKey>>;
  KeyVector keys;
  keys.reserve(4);
  keys.push_back(ChunkKey{0});
  keys.push_back(ChunkKey{1});
  keys.push_back(ChunkKey{4});
  keys.push_back(ChunkKey{5});
  const auto domain = DomainDesc::explicit_chunks(
      std::span<const ChunkKey>{keys.data(), keys.size()});
  (void)ops.update_field(domain,
                         FieldAccessDesc{0, kTerrainDirty.value, kTerrainDirty},
                         WritePolicy::UniquePerChunk);
  const auto report = plan_operations(world_, ops);
  if (!report.ok()) {
    return false;
  }
  const auto phases = plan_parallel_execution_phases(report.plan());
  if (!phases.ok() || phases.phases().empty()) {
    return false;
  }
  const auto result =
      execute_phase_partitioned_dirty_with<WritePolicy::UniquePerChunk>(
          SerialPhaseExecutor{}, world_, report.plan(), phases.phases()[0],
          phase_scratch_, [this](auto view) {
            auto terrain = view.template field_span<TerrainTag>();
            terrain[0] = static_cast<std::uint16_t>(
                (tick_number_ + view.key().value) & 0xffffU);
          });
  if (result.status != PlannedExecutionStatus::Executed) {
    return false;
  }
  return merge_planned_dirty(world_, phase_scratch_).status ==
         PlannedDirtyMergeStatus::Merged;
}

auto DiagnosticsModel::tick() -> bool {
  if (paused_) {
    return true;
  }
  path_counters_.reset();
  queued_counters_.reset();
  allocation_counters_.reset();
  trace_buffer_.clear();

  auto succeeded = false;
  {
    diagnostics::ScopedTrace trace{trace_buffer_};
    diagnostics::ScopedPathCounters path{path_counters_};
    diagnostics::ScopedQueuedPhaseCounters queued{queued_counters_};
    diagnostics::ScopedAllocationCounters allocation{allocation_counters_};
    diagnostics::ScopedTimer tick_timer{diagnostics::TraceCategory::General,
                                        "diagnostics_tick"};
    succeeded = run_path_workload() && run_queued_workload();
  }
  ++tick_number_;
  snapshot_ = diagnostics::capture_diagnostics(
      path_counters_, allocation_counters_, queued_counters_, trace_buffer_);
  accumulate_timing(timing_history_, snapshot_.timing);
  snapshot_.timing = timing_history_;
  evidence_.workload_tick = evidence_.workload_tick || succeeded;
  update_evidence();
  return succeeded;
}

void DiagnosticsModel::update_evidence() noexcept {
  evidence_.path_counters =
      evidence_.path_counters ||
      (snapshot_.path.initializations > 0 && snapshot_.path.heap_pushes > 0);
  evidence_.queued_counters =
      evidence_.queued_counters || (snapshot_.queued.phase_calls > 0 &&
                                    snapshot_.queued.phase_operations > 0 &&
                                    snapshot_.queued.dirty_chunks_merged > 0);
  evidence_.timing_samples =
      evidence_.timing_samples ||
      (snapshot_.timing.stats(diagnostics::TraceCategory::General).samples >
           0 &&
       snapshot_.timing.stats(diagnostics::TraceCategory::Path).samples > 0 &&
       snapshot_.timing.stats(diagnostics::TraceCategory::Queued).samples > 0);
  evidence_.duration_spans =
      evidence_.duration_spans || (has_duration(snapshot_, "path_search") &&
                                   has_duration(snapshot_, "queued_phase"));
  evidence_.allocation_counters = evidence_.allocation_counters ||
                                  (snapshot_.allocation.allocations > 0 &&
                                   snapshot_.allocation.allocation_bytes > 0 &&
                                   snapshot_.allocation.peak_live_bytes > 0);
  evidence_.allocation_balanced =
      evidence_.allocation_balanced ||
      (snapshot_.allocation.allocations == snapshot_.allocation.deallocations &&
       snapshot_.allocation.allocation_bytes ==
           snapshot_.allocation.deallocation_bytes &&
       snapshot_.allocation.live_bytes == 0);
}

}  // namespace tess::examples::web_diagnostics
