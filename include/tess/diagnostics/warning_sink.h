#pragma once

#include <tess/diagnostics/diagnostics.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <source_location>
#include <string_view>

namespace tess::diagnostics {

#if TESS_DIAGNOSTICS_ENABLED

/** Origin category used to bucket warnings without parsing their messages. */
enum class WarningCategory : std::uint8_t {
  General,
  Storage,
  Path,
  Topology,
  Queued,
  Scheduler,
  Render,
};

/**
 * Non-owning warning record carrying an origin, message, and numeric detail.
 *
 * `message` must outlive every sink retaining the warning. Its `detail`
 * meaning is defined by the category/message pair, and `where` defaults to the
 * construction site.
 */
struct Warning {
  WarningCategory category = WarningCategory::General;
  std::string_view message;
  std::uint64_t detail = 0;
  std::source_location where = std::source_location::current();
};

/** Sink capable of accepting a warning without throwing. */
template <typename T>
concept WarningSink = requires(T sink, const Warning& warning) {
  { sink.warn(warning) } noexcept;
};

/** No-op warning sink for consumers that do not retain diagnostics. */
struct NullWarningSink {
  void warn(const Warning&) noexcept {}
};

static_assert(WarningSink<NullWarningSink>);

/**
 * Fixed-capacity, allocation-free ring retaining the most recent warnings.
 *
 * A full sink overwrites its oldest entry and counts the loss. Indexing is
 * oldest-first and requires an index smaller than `size()`.
 */
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
