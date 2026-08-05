#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace tess {

/** Result of deterministic capacity validation before allocation. */
enum class ReserveStatus : std::uint8_t {
  Reserved,
  CapacityExceeded,
};

namespace detail {

#if defined(TESS_INTERNAL_CAPACITY_TESTING)
inline thread_local std::size_t capacity_limit_for_testing =
    std::numeric_limits<std::size_t>::max();

class ScopedCapacityLimitForTesting {
 public:
  explicit ScopedCapacityLimitForTesting(std::size_t limit) noexcept
      : previous_(std::exchange(capacity_limit_for_testing, limit)) {}

  ScopedCapacityLimitForTesting(const ScopedCapacityLimitForTesting&) = delete;
  auto operator=(const ScopedCapacityLimitForTesting&)
      -> ScopedCapacityLimitForTesting& = delete;

  ~ScopedCapacityLimitForTesting() { capacity_limit_for_testing = previous_; }

 private:
  std::size_t previous_;
};
#endif

[[nodiscard]] inline auto effective_capacity_limit(
    std::size_t container_limit) noexcept -> std::size_t {
#if defined(TESS_INTERNAL_CAPACITY_TESTING)
  return container_limit < capacity_limit_for_testing
             ? container_limit
             : capacity_limit_for_testing;
#else
  return container_limit;
#endif
}

}  // namespace detail

}  // namespace tess
