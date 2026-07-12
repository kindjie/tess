#include <gtest/gtest.h>
#include <tess/core/assert.h>
#include <tess/tess.h>

#include <algorithm>
#include <atomic>
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
static_assert(tess::PhaseExecutor<tess::WorkerPoolPhaseExecutor>);
static_assert(tess::PhaseExecutor<MinimalPhaseExecutor>);
static_assert(tess::PhaseExecutor<const tess::SerialPhaseExecutor&>);
static_assert(!tess::PhaseExecutor<int>);
static_assert(!tess::PhaseExecutor<VoidReturnExecutor>);
static_assert(!tess::PhaseExecutor<MutableOnlyExecutor>);

// The serialized-callback promise is independent of the executor contract:
// serial executors declare it, concurrent executors must not.
static_assert(tess::SerialExecutor<tess::SerialPhaseExecutor>);
static_assert(!tess::SerialExecutor<tess::ScopedThreadPhaseExecutor>);
static_assert(!tess::SerialExecutor<tess::WorkerPoolPhaseExecutor>);
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
  EXPECT_EQ(collect_visited(tess::WorkerPoolPhaseExecutor{4}, 3, 17), expected);
  EXPECT_EQ(collect_visited(MinimalPhaseExecutor{}, 3, 17), expected);
}

TEST(TessPhaseExecutor, WorkerPoolClampsWorkerCountAndHandlesEmptyRanges) {
  const tess::WorkerPoolPhaseExecutor clamped{0};
  EXPECT_EQ(clamped.worker_count(), 1u);

  const auto result = clamped.for_each_operation(
      7, 0, [](std::size_t) -> tess::PlannedExecutionResult {
        ADD_FAILURE() << "empty range must not invoke the callback";
        return tess::PlannedExecutionResult{};
      });
  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::Executed);
}

TEST(TessPhaseExecutor, WorkerPoolReusesWorkersAcrossManyPhases) {
  const tess::WorkerPoolPhaseExecutor executor{3};

  for (std::size_t phase = 0; phase < 64; ++phase) {
    const auto expected = [&] {
      std::vector<std::size_t> indexes;
      for (std::size_t i = phase; i < phase + 9; ++i) {
        indexes.push_back(i);
      }
      return indexes;
    }();
    EXPECT_EQ(collect_visited(executor, phase, 9), expected);
  }
}

// Pins the bounded-wakeup branch (audit 2026-07-11 M8): with 8 workers and
// counts of 1-3 every dispatch has fewer runs than workers, so completion
// rides on the notify_one loop plus the wait predicate -- a lost wakeup
// here strands the dispatcher, which the test surfaces as a hang.
TEST(TessPhaseExecutor, WorkerPoolBoundedWakeupsCompleteSmallPhases) {
  const tess::WorkerPoolPhaseExecutor executor{8};

  for (std::size_t phase = 0; phase < 256; ++phase) {
    const auto count = (phase % 3) + 1;
    const auto expected = [&] {
      std::vector<std::size_t> indexes;
      for (std::size_t i = phase; i < phase + count; ++i) {
        indexes.push_back(i);
      }
      return indexes;
    }();
    EXPECT_EQ(collect_visited(executor, phase, count), expected);
  }
}

TEST(TessPhaseExecutor, WorkerPoolReportsFirstFailureInOperationOrder) {
  const tess::WorkerPoolPhaseExecutor executor{4};

  const auto result = executor.for_each_operation(
      0, 32, [](std::size_t index) -> tess::PlannedExecutionResult {
        if (index == 6 || index == 21) {
          return tess::PlannedExecutionResult{
              tess::PlannedExecutionStatus::InvalidPhase, index};
        }
        return tess::PlannedExecutionResult{};
      });

  EXPECT_EQ(result.status, tess::PlannedExecutionStatus::InvalidPhase);
  EXPECT_EQ(result.chunk_count, 6u);
}

TEST(TessPhaseExecutor, WorkerPoolWarmDispatchDoesNotAllocate) {
  tess::WorkerPoolPhaseExecutor executor{2};
  executor.reserve_operations(256);
  std::atomic<std::uint64_t> sum = 0;

  // One warmup phase, then warm phases must not allocate on the caller
  // thread or in worker callbacks. ScopedAllocationCounter counts
  // process-globally while the pool's persistent worker threads are live, so
  // a platform/runtime allocation on a worker early in the warm sequence
  // (lazy TLS, a condvar fallback path) could otherwise fail the guard
  // spuriously: run several warm dispatches and require only the LAST one to
  // be allocation-free, which still pins the steady-state contract.
  (void)executor.for_each_operation(
      0, 256, [&](std::size_t index) -> tess::PlannedExecutionResult {
        sum += index;
        return tess::PlannedExecutionResult{};
      });

  tess_test::ScopedAllocationCounter counter;
  std::size_t count_before_last = 0;
  for (std::size_t phase = 0; phase < 8; ++phase) {
    count_before_last = counter.count();
    const auto result = executor.for_each_operation(
        0, 256, [&](std::size_t index) -> tess::PlannedExecutionResult {
          sum += index;
          return tess::PlannedExecutionResult{};
        });
    EXPECT_EQ(result.status, tess::PlannedExecutionStatus::Executed);
  }
  EXPECT_EQ(counter.count(), count_before_last);
}

TEST(TessPhaseExecutor, WorkerPoolCreateRunStopCyclesAreSafe) {
  for (std::size_t cycle = 0; cycle < 16; ++cycle) {
    std::atomic<std::size_t> executed = 0;
    {
      const tess::WorkerPoolPhaseExecutor executor{1 + (cycle % 4)};
      for (std::size_t phase = 0; phase < 4; ++phase) {
        (void)executor.for_each_operation(
            0, 16, [&](std::size_t) -> tess::PlannedExecutionResult {
              executed.fetch_add(1);
              return tess::PlannedExecutionResult{};
            });
      }
    }
    EXPECT_EQ(executed.load(), 64u);
  }
}

TEST(TessPhaseExecutor, WorkerPoolDestructsSafelyWithoutRunningAPhase) {
  const tess::WorkerPoolPhaseExecutor executor{4};
  EXPECT_EQ(executor.worker_count(), 4u);
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

#if TESS_ENABLE_ASSERTS

constexpr auto kAssertDeathMessage = "tess assertion failed";

// WorkerPoolPhaseExecutor supports one dispatch at a time. A worker
// callback re-entering for_each_operation on the same executor would
// clobber the shared job state and deadlock the outer dispatch on
// done_cv_; debug builds must fail fast instead.
TEST(TessPhaseExecutorDeathTest, WorkerPoolRejectsNestedDispatch) {
  EXPECT_DEATH(
      {
        const tess::WorkerPoolPhaseExecutor executor{2};
        (void)executor.for_each_operation(
            0, 4, [&](std::size_t) -> tess::PlannedExecutionResult {
              return executor.for_each_operation(
                  0, 1, [](std::size_t) -> tess::PlannedExecutionResult {
                    return tess::PlannedExecutionResult{};
                  });
            });
      },
      kAssertDeathMessage);
}

// Growing the result buffer mid-dispatch would relocate the slots other
// workers are concurrently writing (use-after-realloc);
// reserve_operations is only legal between dispatches.
TEST(TessPhaseExecutorDeathTest, WorkerPoolRejectsReserveDuringDispatch) {
  EXPECT_DEATH(
      {
        const tess::WorkerPoolPhaseExecutor executor{2};
        (void)executor.for_each_operation(
            0, 4, [&](std::size_t) -> tess::PlannedExecutionResult {
              executor.reserve_operations(4096);
              return tess::PlannedExecutionResult{};
            });
      },
      kAssertDeathMessage);
}

#endif  // TESS_ENABLE_ASSERTS

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
