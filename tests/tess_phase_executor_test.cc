#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "allocation_counter.h"

namespace {

// Minimal conforming executor: proves the PhaseExecutor concept describes
// exactly the for_each_operation(first, count, fn) contract and nothing
// about construction, tags, or extra members.
struct MinimalPhaseExecutor {
  template <typename Fn>
  auto for_each_operation(std::size_t first, std::size_t count, Fn&& fn) const
      -> tess::PlannedExecutionResult {
    auto&& callback = fn;
    for (std::size_t i = first; i < first + count; ++i) {
      auto result = callback(i);
      if (result.status != tess::PlannedExecutionStatus::Executed) {
        return result;
      }
    }
    return tess::PlannedExecutionResult{};
  }
};

// Wrong return type: must not satisfy the concept.
struct VoidReturnExecutor {
  template <typename Fn>
  void for_each_operation(std::size_t first, std::size_t count, Fn&& fn) const {
    auto&& callback = fn;
    for (std::size_t i = first; i < first + count; ++i) {
      static_cast<void>(callback(i));
    }
  }
};

// Non-const for_each_operation: must not satisfy the concept, because
// executors are passed by const reference through phase execution helpers.
struct MutableOnlyExecutor {
  template <typename Fn>
  auto for_each_operation(std::size_t first, std::size_t count, Fn&& fn)
      -> tess::PlannedExecutionResult {
    auto&& callback = fn;
    for (std::size_t i = first; i < first + count; ++i) {
      static_cast<void>(callback(i));
    }
    return tess::PlannedExecutionResult{};
  }
};

static_assert(tess::PhaseExecutor<tess::SerialPhaseExecutor>);
static_assert(tess::PhaseExecutor<tess::ScopedThreadPhaseExecutor>);
static_assert(tess::PhaseExecutor<MinimalPhaseExecutor>);
static_assert(tess::PhaseExecutor<const tess::SerialPhaseExecutor&>);
static_assert(!tess::PhaseExecutor<int>);
static_assert(!tess::PhaseExecutor<VoidReturnExecutor>);
static_assert(!tess::PhaseExecutor<MutableOnlyExecutor>);

// The serialized-callback promise is independent of the executor contract:
// serial executors declare it, concurrent executors must not.
static_assert(tess::SerialExecutor<tess::SerialPhaseExecutor>);
static_assert(!tess::SerialExecutor<tess::ScopedThreadPhaseExecutor>);
static_assert(!tess::SerialExecutor<MinimalPhaseExecutor>);

template <tess::PhaseExecutor Executor>
auto collect_visited(const Executor& executor, std::size_t first,
                     std::size_t count) -> std::vector<std::size_t> {
  std::vector<std::size_t> visited;
  visited.reserve(count);
  std::mutex mutex;
  const auto result = executor.for_each_operation(
      first, count, [&](std::size_t index) -> tess::PlannedExecutionResult {
        const std::scoped_lock lock{mutex};
        visited.push_back(index);
        return tess::PlannedExecutionResult{};
      });
  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::Executed);
  std::sort(visited.begin(), visited.end());
  return visited;
}

TEST(TessPhaseExecutor, ConformingExecutorsVisitExactRangeOnce) {
  const auto expected = [] {
    std::vector<std::size_t> indexes;
    for (std::size_t i = 3; i < 3 + 17; ++i) {
      indexes.push_back(i);
    }
    return indexes;
  }();

  EXPECT_EQ(collect_visited(tess::SerialPhaseExecutor{}, 3, 17), expected);
  EXPECT_EQ(collect_visited(tess::ScopedThreadPhaseExecutor{4}, 3, 17),
            expected);
  EXPECT_EQ(collect_visited(MinimalPhaseExecutor{}, 3, 17), expected);
}

TEST(TessPhaseExecutor, SerialExecutorStopsAtFirstNonExecutedResult) {
  const tess::SerialPhaseExecutor executor;
  std::vector<std::size_t> visited;
  visited.reserve(4);

  const auto result = executor.for_each_operation(
      0, 8, [&](std::size_t index) -> tess::PlannedExecutionResult {
        visited.push_back(index);
        if (index == 3) {
          return tess::PlannedExecutionResult{
              tess::PlannedExecutionStatus::PolicyMismatch, index};
        }
        return tess::PlannedExecutionResult{};
      });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::PolicyMismatch);
  EXPECT_EQ(result.chunk_count, 3u);
  EXPECT_EQ(visited, (std::vector<std::size_t>{0, 1, 2, 3}));
}

TEST(TessPhaseExecutor, ThreadedExecutorReportsFailureAfterJoin) {
  const tess::ScopedThreadPhaseExecutor executor{4};

  const auto result = executor.for_each_operation(
      0, 32, [&](std::size_t index) -> tess::PlannedExecutionResult {
        if (index == 9) {
          return tess::PlannedExecutionResult{
              tess::PlannedExecutionStatus::InvalidPhase, index};
        }
        return tess::PlannedExecutionResult{};
      });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::InvalidPhase);
  EXPECT_EQ(result.chunk_count, 9u);
}

TEST(TessPhaseExecutor, ExecuteOperationIndexRangeAcceptsAnyPhaseExecutor) {
  const auto range = tess::ExecutorPhaseRange{5, 3};
  std::vector<std::size_t> visited;
  visited.reserve(range.operation_count);

  const auto result = tess::execute_operation_index_range(
      MinimalPhaseExecutor{}, range,
      [&](std::size_t index) -> tess::PlannedExecutionResult {
        visited.push_back(index);
        return tess::PlannedExecutionResult{};
      });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::Executed);
  EXPECT_EQ(visited, (std::vector<std::size_t>{5, 6, 7}));
}

TEST(TessPhaseExecutor, SerialDispatchDoesNotAllocate) {
  const tess::SerialPhaseExecutor executor;
  std::uint64_t sum = 0;

  tess_test::ScopedAllocationCounter counter;
  const auto result = executor.for_each_operation(
      0, 4096, [&](std::size_t index) -> tess::PlannedExecutionResult {
        sum += index;
        return tess::PlannedExecutionResult{};
      });

  EXPECT_EQ(counter.count(), 0u);
  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::Executed);
  EXPECT_EQ(sum, 4096u * 4095u / 2u);
}

}  // namespace
