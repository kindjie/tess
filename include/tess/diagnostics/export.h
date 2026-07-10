#pragma once

#include <tess/diagnostics/diagnostics.h>
#include <tess/diagnostics/trace.h>

#include <array>
#include <cstddef>

namespace tess::diagnostics {

#if TESS_DIAGNOSTICS_ENABLED

// Per-category timing snapshot, decoupled from TraceBuffer's ring internals so
// a panel or consumer can hold and display it without touching the live buffer.
struct TimingSnapshot {
  std::array<TraceCategoryStats, trace_category_count> per_category{};

  // Timing accumulator for a category, guarding the Count sentinel and any
  // out-of-range value.
  [[nodiscard]] auto category(TraceCategory value) const noexcept
      -> const TraceCategoryStats& {
    static constexpr TraceCategoryStats kZero{};
    const auto index = static_cast<std::size_t>(value);
    if (index >= per_category.size()) {
      return kZero;
    }
    return per_category[index];
  }
};

// Aggregate snapshot of the diagnostic sinks a consumer typically renders
// together. The counter members are plain copies of caller-owned counter
// structs; timing is a copy of a TraceBuffer's per-category accumulators.
struct DiagnosticsSnapshot {
  PathCounters path;
  AllocationCounters allocation;
  QueuedPhaseCounters queued;
  TimingSnapshot timing;
};

// Copy every category's timing accumulator out of a TraceBuffer.
//
// Threading contract: capture performs plain unsynchronized reads of the
// buffer (and, for capture_diagnostics, of the counter structs). Call it on
// the thread that records into them -- the same thread_local routing contract
// documented in trace.h/diagnostics.h -- or externally synchronize capture
// against all recording. Calling from a UI/render thread while a sim thread
// is still tracing is a data race.
[[nodiscard]] inline auto capture_timing(const TraceBuffer& buffer) noexcept
    -> TimingSnapshot {
  TimingSnapshot snapshot;
  snapshot.per_category = buffer.category_stats();
  return snapshot;
}

// Assemble a full snapshot from caller-owned counter structs and a trace
// buffer. Every field is a plain copy, so the snapshot outlives the sources
// unchanged. Same threading contract as capture_timing above: capture on the
// recording thread or under external synchronization; only the returned
// snapshot is safe to hand to another thread.
[[nodiscard]] inline auto capture_diagnostics(
    const PathCounters& path, const AllocationCounters& allocation,
    const QueuedPhaseCounters& queued, const TraceBuffer& buffer) noexcept
    -> DiagnosticsSnapshot {
  return DiagnosticsSnapshot{path, allocation, queued, capture_timing(buffer)};
}

#endif  // TESS_DIAGNOSTICS_ENABLED

}  // namespace tess::diagnostics
