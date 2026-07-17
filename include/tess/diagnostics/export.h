#pragma once

#include <tess/diagnostics/diagnostics.h>
#include <tess/diagnostics/trace.h>

#include <array>
#include <cstddef>

namespace tess::diagnostics {

#if TESS_DIAGNOSTICS_ENABLED

/**
 * Per-category timing totals copied independently of a live trace buffer.
 *
 * A consumer may retain and render the snapshot without retaining or reading
 * the source buffer.
 */
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

/**
 * Value snapshot of the counters and timings commonly rendered together.
 *
 * All members are copies and therefore outlive the corresponding sinks.
 */
struct DiagnosticsSnapshot {
  PathCounters path;
  AllocationCounters allocation;
  QueuedPhaseCounters queued;
  TimingSnapshot timing;
};

/**
 * Copies every category accumulator from `buffer` without allocating.
 *
 * @pre Capture occurs on the recording thread, or access to `buffer` is
 * externally synchronized against recording.
 */
[[nodiscard]] inline auto capture_timing(const TraceBuffer& buffer) noexcept
    -> TimingSnapshot {
  TimingSnapshot snapshot;
  snapshot.per_category = buffer.category_stats();
  return snapshot;
}

/**
 * Copies caller-owned counters and trace timing into an independent snapshot.
 *
 * @pre Capture occurs on the recording thread, or every source is externally
 * synchronized against recording.
 */
[[nodiscard]] inline auto capture_diagnostics(
    const PathCounters& path, const AllocationCounters& allocation,
    const QueuedPhaseCounters& queued, const TraceBuffer& buffer) noexcept
    -> DiagnosticsSnapshot {
  return DiagnosticsSnapshot{path, allocation, queued, capture_timing(buffer)};
}

#endif  // TESS_DIAGNOSTICS_ENABLED

}  // namespace tess::diagnostics
