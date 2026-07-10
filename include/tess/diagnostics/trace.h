#pragma once

#include <tess/diagnostics/diagnostics.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// Trace macros live here, next to the trace_event/TraceCategory they expand to,
// so a translation unit that includes this header gets a self-contained macro:
// TESS_DIAG_TRACE routes to the active trace buffer when diagnostics are on and
// compiles to an empty statement (never naming its arguments) when off.
#if TESS_DIAGNOSTICS_ENABLED
#define TESS_DIAG_TRACE(category, label)                      \
  do {                                                        \
    ::tess::diagnostics::trace_event((category), (label), 0); \
  } while (false)
#define TESS_DIAG_TRACE_VALUE(category, label, value)               \
  do {                                                              \
    ::tess::diagnostics::trace_event((category), (label), (value)); \
  } while (false)
#else
#define TESS_DIAG_TRACE(category, label) \
  do {                                   \
  } while (false)
#define TESS_DIAG_TRACE_VALUE(category, label, value) \
  do {                                                \
  } while (false)
#endif

namespace tess::diagnostics {

#if TESS_DIAGNOSTICS_ENABLED

// Coarse category for a trace record and its per-category timing bucket.
// `Count` is a sentinel used to size the per-category stats array; it is not a
// real category and must not be recorded against.
enum class TraceCategory : std::uint8_t {
  General,
  Path,
  Topology,
  Queued,
  Planner,
  Scheduler,
  Render,
  Count,
};

inline constexpr std::size_t trace_category_count =
    static_cast<std::size_t>(TraceCategory::Count);

// One structured trace point. `label` is a non-owning view that MUST reference
// storage outliving every reader of the buffer (string literals or other static
// storage), the same contract as Warning::message and PathView. `value` is a
// category/label-specific datum (an operation index, a phase number, a duration
// in nanoseconds). `sequence` is the global ordinal of this record() call on
// its buffer, so gaps reveal how many records were dropped by ring overflow.
struct TraceRecord {
  TraceCategory category = TraceCategory::General;
  std::string_view label;
  std::uint64_t value = 0;
  std::uint64_t sequence = 0;
};

// Accumulated timing for one category. Defaults are all-zero (no sentinel), so
// a category that has never been sampled reads cleanly in a snapshot.
struct TraceCategoryStats {
  std::uint64_t samples = 0;
  std::uint64_t total_ns = 0;
  std::uint64_t min_ns = 0;
  std::uint64_t max_ns = 0;

  void reset() noexcept { *this = TraceCategoryStats{}; }
};

// A caller-owned trace buffer: a fixed-capacity ring of recent TraceRecords
// over storage the caller provides, plus a per-category timing accumulator. The
// backing std::span must outlive the TraceBuffer (and any
// ScopedTrace/ScopedTimer that targets it). An empty span is valid: records are
// counted as dropped and nothing is written. Nothing here allocates.
class TraceBuffer {
 public:
  explicit TraceBuffer(std::span<TraceRecord> storage) noexcept
      : storage_{storage} {}

  // Caller-owned and referenced by address: ScopedTrace/ScopedTimer capture a
  // TraceBuffer* and the ring metadata plus timing accumulators live in the
  // object while the records live in the shared backing span. Copying or moving
  // would split that metadata from the storage, so a by-value copy would
  // collect records the caller's original never sees. The buffer is therefore
  // pinned to its storage -- construct it in place, pass it by reference.
  TraceBuffer(const TraceBuffer&) = delete;
  auto operator=(const TraceBuffer&) -> TraceBuffer& = delete;
  TraceBuffer(TraceBuffer&&) = delete;
  auto operator=(TraceBuffer&&) -> TraceBuffer& = delete;

  // Append a structured trace record. When the ring is full the oldest record
  // is overwritten and dropped() is bumped; sequence numbers keep advancing so
  // a reader can see the gap. A record against the Count sentinel (or any
  // out-of-range category) is rejected and counted as dropped, so the ring
  // never carries a non-category value.
  void record(TraceCategory category, std::string_view label,
              std::uint64_t value) noexcept {
    const auto seq = sequence_++;
    if (storage_.empty() ||
        static_cast<std::size_t>(category) >= trace_category_count) {
      ++dropped_;
      return;
    }
    if (count_ == storage_.size()) {
      head_ = (head_ + 1) % storage_.size();
      ++dropped_;
    } else {
      ++count_;
    }
    const auto slot = (head_ + count_ - 1) % storage_.size();
    storage_[slot] = TraceRecord{category, label, value, seq};
  }

  // Fold one timing sample (nanoseconds) into a category's accumulator. Records
  // against the Count sentinel or any out-of-range category are ignored.
  // total_ns is a running sum that wraps only after ~584 years of accumulated
  // time, so it is treated as unbounded in practice.
  void record_timing(TraceCategory category, std::uint64_t nanos) noexcept {
    const auto index = static_cast<std::size_t>(category);
    if (index >= stats_.size()) {
      return;
    }
    auto& stats = stats_[index];
    if (stats.samples == 0) {
      stats.min_ns = nanos;
      stats.max_ns = nanos;
    } else {
      if (nanos < stats.min_ns) {
        stats.min_ns = nanos;
      }
      if (nanos > stats.max_ns) {
        stats.max_ns = nanos;
      }
    }
    ++stats.samples;
    stats.total_ns += nanos;
  }

  [[nodiscard]] auto size() const noexcept -> std::size_t { return count_; }

  [[nodiscard]] auto capacity() const noexcept -> std::size_t {
    return storage_.size();
  }

  [[nodiscard]] bool empty() const noexcept { return count_ == 0; }

  [[nodiscard]] bool full() const noexcept { return count_ == storage_.size(); }

  // Total records lost to ring overflow (or, for an empty span, every record).
  [[nodiscard]] auto dropped() const noexcept -> std::uint64_t {
    return dropped_;
  }

  // Oldest-first access: index 0 is the oldest retained record, size() - 1 the
  // newest. Behavior is undefined for index >= size().
  [[nodiscard]] auto operator[](std::size_t index) const noexcept
      -> const TraceRecord& {
    return storage_[(head_ + index) % storage_.size()];
  }

  // Timing accumulator for a category. Returns a zeroed reference for the Count
  // sentinel or any out-of-range category.
  [[nodiscard]] auto stats(TraceCategory category) const noexcept
      -> const TraceCategoryStats& {
    static constexpr TraceCategoryStats kZero{};
    const auto index = static_cast<std::size_t>(category);
    if (index >= stats_.size()) {
      return kZero;
    }
    return stats_[index];
  }

  // Snapshot of every category's timing accumulator, indexed by
  // static_cast<std::size_t>(TraceCategory).
  [[nodiscard]] auto category_stats() const noexcept
      -> const std::array<TraceCategoryStats, trace_category_count>& {
    return stats_;
  }

  void clear() noexcept {
    head_ = 0;
    count_ = 0;
    sequence_ = 0;
    dropped_ = 0;
    for (auto& stats : stats_) {
      stats.reset();
    }
  }

 private:
  std::span<TraceRecord> storage_;
  std::array<TraceCategoryStats, trace_category_count> stats_{};
  std::size_t head_ = 0;        // index of the oldest retained record
  std::size_t count_ = 0;       // retained records (<= storage_.size())
  std::uint64_t sequence_ = 0;  // next record ordinal
  std::uint64_t dropped_ = 0;
};

// Thread-local active buffer, mirroring the counter sinks in diagnostics.h. The
// TESS_DIAG_TRACE macros and trace_event route to whichever buffer is installed
// on the current thread; worker threads do not feed the installer's buffer (the
// same deliberate thread_local limit documented for the counters). Reading a
// buffer -- including export.h's capture_timing/capture_diagnostics -- is
// likewise unsynchronized: read on the recording thread, or externally
// synchronize the read against all recording into that buffer.
inline thread_local TraceBuffer* active_trace_buffer = nullptr;

// RAII installer for the active trace buffer. Non-copyable and nestable: the
// innermost scope receives records, and destruction restores the outer buffer.
class ScopedTrace {
 public:
  explicit ScopedTrace(TraceBuffer& buffer) noexcept
      : previous_{active_trace_buffer} {
    active_trace_buffer = &buffer;
  }

  ScopedTrace(const ScopedTrace&) = delete;
  auto operator=(const ScopedTrace&) -> ScopedTrace& = delete;

  ~ScopedTrace() { active_trace_buffer = previous_; }

 private:
  TraceBuffer* previous_;
};

// Record a structured trace point into the active buffer, if any. A no-op when
// no ScopedTrace is installed on the current thread.
inline void trace_event(TraceCategory category, std::string_view label,
                        std::uint64_t value) noexcept {
  if (active_trace_buffer != nullptr) {
    active_trace_buffer->record(category, label, value);
  }
}

// RAII wall-clock timer. It binds to the buffer active AT CONSTRUCTION (not at
// destruction), so a timer started outside any ScopedTrace records nothing even
// if a buffer is later installed, and nested scopes attribute timing to the
// buffer that was active when the span began. On destruction it folds the
// elapsed nanoseconds into the category's timing accumulator and appends a
// trace record whose value is that duration.
class ScopedTimer {
 public:
  ScopedTimer(TraceCategory category, std::string_view label) noexcept
      : target_{active_trace_buffer},
        category_{category},
        label_{label},
        start_{std::chrono::steady_clock::now()} {}

  ScopedTimer(const ScopedTimer&) = delete;
  auto operator=(const ScopedTimer&) -> ScopedTimer& = delete;

  ~ScopedTimer() {
    if (target_ == nullptr) {
      return;
    }
    const auto elapsed = std::chrono::steady_clock::now() - start_;
    const auto ticks =
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    const auto nanos =
        ticks < 0 ? std::uint64_t{0} : static_cast<std::uint64_t>(ticks);
    target_->record_timing(category_, nanos);
    target_->record(category_, label_, nanos);
  }

 private:
  TraceBuffer* target_;
  TraceCategory category_;
  std::string_view label_;
  std::chrono::steady_clock::time_point start_;
};

#endif  // TESS_DIAGNOSTICS_ENABLED

}  // namespace tess::diagnostics
