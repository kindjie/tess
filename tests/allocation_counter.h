#pragma once

#include <cstddef>

namespace tess_test {

// Read-only snapshots of the process-global allocation counters. These do
// not enable counting; only ScopedAllocationCounter can do that.
[[nodiscard]] auto allocation_count() noexcept -> std::size_t;
[[nodiscard]] auto allocation_bytes() noexcept -> std::size_t;

// True when this build can safely reject allocations before they reach the
// backing allocator. Sanitizer-owned allocators expose observation hooks only,
// and MSVC checked iterators allocate inside noexcept STL operations.
[[nodiscard]] auto allocation_failure_injection_supported() noexcept -> bool;

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

// Rejects the zero-based `failure_index` allocation attempt made while this
// guard is active. The previous injection state is restored on destruction so
// nested scopes and exception unwinding cannot poison later tests. The guard is
// inert when allocation_failure_injection_supported() is false.
class [[nodiscard]] ScopedAllocationFailure {
 public:
  explicit ScopedAllocationFailure(std::size_t failure_index) noexcept;
  ~ScopedAllocationFailure();

  ScopedAllocationFailure(const ScopedAllocationFailure&) = delete;
  auto operator=(const ScopedAllocationFailure&)
      -> ScopedAllocationFailure& = delete;
  ScopedAllocationFailure(ScopedAllocationFailure&&) = delete;
  auto operator=(ScopedAllocationFailure&&)
      -> ScopedAllocationFailure& = delete;

  [[nodiscard]] auto attempts() const noexcept -> std::size_t;

 private:
  bool active_ = false;
  bool previous_enabled_ = false;
  std::size_t previous_failure_index_ = 0;
  std::size_t previous_attempts_ = 0;
};

}  // namespace tess_test
