#pragma once

#include <tess/diagnostics/diagnostics.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <source_location>
#include <string_view>

namespace tess::diagnostics {

#if TESS_DIAGNOSTICS_ENABLED

// Coarse origin tag for a warning. A small fixed enum so a sink can bucket
// warnings without parsing the message string.
enum class WarningCategory : std::uint8_t {
  General,
  Storage,
  Path,
  Topology,
  Queued,
  Scheduler,
  Render,
};

// A single non-owning warning record. `message` MUST reference storage that
// outlives every sink that retains the warning -- string literals or other
// static storage. A sink copies the Warning by value but never copies the
// pointed-to characters, so a message backed by a temporary std::string is a
// dangling read (the same non-owning contract as PathView). This precondition
// is not enforceable at compile time; callers must honor it. `detail` carries
// an optional numeric datum (an index, a byte count, a status code) whose
// meaning is specific to the category/message pair. `where` defaults to the
// call site that constructs the Warning.
struct Warning {
  WarningCategory category = WarningCategory::General;
  std::string_view message;
  std::uint64_t detail = 0;
  std::source_location where = std::source_location::current();
};

// A WarningSink is anything that can absorb a Warning without throwing. The
// noexcept requirement keeps sinks usable from noexcept instrumentation paths.
template <typename T>
concept WarningSink = requires(T sink, const Warning& warning) {
  { sink.warn(warning) } noexcept;
};

// Discards every warning. The zero-cost default when a consumer must satisfy a
// WarningSink parameter but has no interest in the warnings.
struct NullWarningSink {
  void warn(const Warning&) noexcept {}
};

static_assert(WarningSink<NullWarningSink>);

// Fixed-capacity ring buffer of the most recent warnings. The caller owns the
// whole object; storage is an inline std::array, so warn() never allocates.
// When the ring is full the oldest warning is overwritten and dropped() counts
// every such loss. Indexing is oldest-first: operator[](0) is the oldest
// retained warning and operator[](size() - 1) is the newest.
template <std::size_t Capacity>
class BufferedWarningSink {
  static_assert(Capacity > 0, "BufferedWarningSink needs a positive capacity");

 public:
  void warn(const Warning& warning) noexcept {
    if (count_ == Capacity) {
      // Full: advance the window, dropping the current oldest.
      head_ = (head_ + 1) % Capacity;
      ++dropped_;
    } else {
      ++count_;
    }
    const auto slot = (head_ + count_ - 1) % Capacity;
    entries_[slot] = warning;
  }

  [[nodiscard]] auto size() const noexcept -> std::size_t { return count_; }

  [[nodiscard]] static constexpr auto capacity() noexcept -> std::size_t {
    return Capacity;
  }

  [[nodiscard]] bool empty() const noexcept { return count_ == 0; }

  [[nodiscard]] bool full() const noexcept { return count_ == Capacity; }

  // Total warnings overwritten (lost) because the ring was full when they
  // arrived.
  [[nodiscard]] auto dropped() const noexcept -> std::uint64_t {
    return dropped_;
  }

  // Oldest-first access: index 0 is the oldest retained warning, size() - 1 is
  // the newest. Behavior is undefined for index >= size().
  [[nodiscard]] auto operator[](std::size_t index) const noexcept
      -> const Warning& {
    return entries_[(head_ + index) % Capacity];
  }

  void clear() noexcept {
    head_ = 0;
    count_ = 0;
    dropped_ = 0;
  }

 private:
  std::array<Warning, Capacity> entries_{};
  std::size_t head_ = 0;   // index of the oldest retained warning
  std::size_t count_ = 0;  // number of retained warnings (<= Capacity)
  std::uint64_t dropped_ = 0;
};

#endif  // TESS_DIAGNOSTICS_ENABLED

}  // namespace tess::diagnostics
