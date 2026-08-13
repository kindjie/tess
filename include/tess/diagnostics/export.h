#pragma once

#include <tess/diagnostics/diagnostics.h>
#include <tess/diagnostics/trace.h>

#include <array>
#include <cstddef>

namespace tess::diagnostics {

#if TESS_DIAGNOSTICS_ENABLED

/** Maximum recent records retained in a diagnostics value snapshot. */
inline constexpr std::size_t diagnostics_snapshot_trace_capacity = 64;

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
  [[nodiscard]] auto stats(TraceCategory value) const noexcept
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
 * Every counter and timing is a copy and outlives the corresponding sinks.
 * `TraceRecord::label` is the exception: it is a `std::string_view`, so a
 * snapshot only outlives its sink to the extent the labels do. The trace
 * API already requires a label to outlive every reader of the buffer (see
 * `diagnostics/trace.h`), which a string literal satisfies -- but a
 * dynamically built label handed to another thread through this snapshot
 * is a dangling read, and this comment previously promised it was not.
 */
struct DiagnosticsSnapshot {
  PathCounters path;
  AllocationCounters allocation;
  QueuedPhaseCounters queued;
  TimingSnapshot timing;
  std::array<TraceRecord, diagnostics_snapshot_trace_capacity> trace_records{};
  std::size_t trace_record_count = 0;
  std::uint64_t trace_records_dropped = 0;
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
  snapshot.per_category = buffer.all_stats();
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
  DiagnosticsSnapshot snapshot;
  snapshot.path = path;
  snapshot.allocation = allocation;
  snapshot.queued = queued;
  snapshot.timing = capture_timing(buffer);
  const auto omitted = buffer.size() > snapshot.trace_records.size()
                           ? buffer.size() - snapshot.trace_records.size()
                           : std::size_t{0};
  snapshot.trace_record_count = buffer.size() - omitted;
  snapshot.trace_records_dropped =
      buffer.dropped() + static_cast<std::uint64_t>(omitted);
  for (std::size_t index = 0; index < snapshot.trace_record_count; ++index) {
    snapshot.trace_records[index] = buffer[omitted + index];
  }
  return snapshot;
}

#endif  // TESS_DIAGNOSTICS_ENABLED

}  // namespace tess::diagnostics
