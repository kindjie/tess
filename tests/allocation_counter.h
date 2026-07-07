#pragma once

#include <cstddef>

namespace tess_test {

// Read-only snapshots of the process-global allocation counters. These do
// not enable counting; only ScopedAllocationCounter can do that.
[[nodiscard]] auto allocation_count() noexcept -> std::size_t;
[[nodiscard]] auto allocation_bytes() noexcept -> std::size_t;

// RAII-only interface for allocation counting. Construction resets the
// global counters and enables counting; destruction disables counting.
// There is deliberately no free-function enable/disable API: a failed
// gtest ASSERT_* returns early, and a raw flag would stay enabled and
// poison every later allocation assertion in the binary. Scope exit
// (including early returns and stack unwinding) always disables counting.
class [[nodiscard]] ScopedAllocationCounter {
 public:
  ScopedAllocationCounter() noexcept;
  ~ScopedAllocationCounter();

  ScopedAllocationCounter(const ScopedAllocationCounter&) = delete;
  auto operator=(const ScopedAllocationCounter&)
      -> ScopedAllocationCounter& = delete;
  ScopedAllocationCounter(ScopedAllocationCounter&&) = delete;
  auto operator=(ScopedAllocationCounter&&)
      -> ScopedAllocationCounter& = delete;

  // Number of counted allocations since construction.
  [[nodiscard]] auto count() const noexcept -> std::size_t;
  // Total requested bytes across counted allocations since construction.
  [[nodiscard]] auto bytes() const noexcept -> std::size_t;
};

}  // namespace tess_test
