#pragma once

#include <tess/diagnostics/diagnostics.h>

#include <algorithm>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// Phase executor contract.
//
// Queued-operation planning groups already-validated operations into phases
// whose members may execute together. A phase executor receives one
// contiguous planned-operation index range and invokes the per-operation
// callback for every index in it, completing or joining all callbacks (and
// making their writes visible) before returning. Executors do not plan, do
// not reorder result reduction, and do not own dirty metadata: callers
// reduce operation results in plan order and merge caller-owned dirty
// partitions after the executor returns.
//
// Thread contract: `World` fields and `ChunkMeta` are not atomic. Concurrent
// callbacks are safe only because planning proves disjoint mutable chunk
// ownership per phase; callbacks write dirty records into per-operation
// partitions instead of shared metadata. Executors that invoke callbacks
// concurrently must not declare `serial_execution_tag` (see the
// SerialExecutor concept below).

namespace tess {

enum class PlannedExecutionStatus : std::uint8_t {
  Executed,
  PolicyMismatch,
  InvalidPhase,
};
static_assert(sizeof(PlannedExecutionStatus) == sizeof(std::uint8_t));

struct ExecutorPhaseRange {
  std::size_t first_operation = 0;
  std::size_t operation_count = 0;
};

struct PlannedExecutionResult {
  PlannedExecutionStatus status = PlannedExecutionStatus::Executed;
  std::size_t chunk_count = 0;
};

namespace detail {

// Probe callback used only to state the PhaseExecutor concept without
// evaluating a lambda in an unevaluated context.
struct PhaseExecutorProbeCallback {
  auto operator()(std::size_t /*operation_index*/) const
      -> PlannedExecutionResult {
    return PlannedExecutionResult{};
  }
};

}  // namespace detail

// The executor contract: given a contiguous planned-operation index range,
// invoke the callback once per index and return the first non-Executed
// result (or success). Implementations must complete or join every callback
// before returning so all callback writes are visible to the caller.
template <typename Executor>
concept PhaseExecutor =
    requires(const std::remove_cvref_t<Executor>& executor, std::size_t first,
             std::size_t count, detail::PhaseExecutorProbeCallback callback) {
      {
        executor.for_each_operation(first, count, callback)
      } -> std::same_as<PlannedExecutionResult>;
    };

struct SerialPhaseExecutor {
  // Serialized-callback promise; see the SerialExecutor concept below.
  using serial_execution_tag = void;

  template <typename Fn>
  auto for_each_operation(ExecutorPhaseRange range, Fn&& fn) const
      -> PlannedExecutionResult {
    return for_each_operation(range.first_operation, range.operation_count,
                              std::forward<Fn>(fn));
  }

  template <typename Fn>
  auto for_each_operation(std::size_t first, std::size_t count, Fn&& fn) const
      -> PlannedExecutionResult {
    auto&& callback = fn;
    const auto end = first + count;
    for (std::size_t i = first; i < end; ++i) {
      auto result = callback(i);
      if (result.status != PlannedExecutionStatus::Executed) {
        return result;
      }
    }
    return PlannedExecutionResult{};
  }
};

// Custom executors declare `using serial_execution_tag = void;` to promise
// that for_each_operation invokes the per-operation callback strictly one
// at a time (never concurrently). execute_phase_deferred_dirty_with
// requires this promise because it shares one PlannedDirtyAccumulator and
// a non-atomic chunk counter across all callbacks. Concurrent executors
// must not declare the tag and must use
// execute_phase_partitioned_dirty_with, which gives every operation its
// own dirty partition and result slot.
template <typename Executor>
concept SerialExecutor =
    requires { typename std::remove_cvref_t<Executor>::serial_execution_tag; };

// Documented prototype, not the production backend: spawns and joins raw
// std::thread workers per phase call to prove the phase handoff and
// visibility rules. It invokes callbacks concurrently, so it deliberately
// does not declare serial_execution_tag and pairs only with
// execute_phase_partitioned_dirty_with; the shared-accumulator
// execute_phase_deferred_dirty_with rejects it at compile time.
class ScopedThreadPhaseExecutor {
 public:
  explicit ScopedThreadPhaseExecutor(std::size_t worker_count) noexcept
      : worker_count_(worker_count == 0 ? 1 : worker_count) {}

  ScopedThreadPhaseExecutor() noexcept
      : ScopedThreadPhaseExecutor(std::thread::hardware_concurrency()) {}

  [[nodiscard]] auto worker_count() const noexcept -> std::size_t {
    return worker_count_;
  }

  template <typename Fn>
  auto for_each_operation(std::size_t first, std::size_t count, Fn&& fn) const
      -> PlannedExecutionResult {
    if (count == 0) {
      return PlannedExecutionResult{};
    }

    const auto thread_count = std::min(worker_count_, count);
    TESS_DIAG_EVENT_VALUE(queued_scoped_thread_dispatch, thread_count);
    std::atomic<std::size_t> next_offset = 0;
    std::vector<PlannedExecutionResult> results(count);
    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    auto&& callback = fn;

    for (std::size_t worker = 0; worker < thread_count; ++worker) {
      threads.emplace_back([&] {
        while (true) {
          const auto offset = next_offset.fetch_add(1);
          if (offset >= count) {
            return;
          }
          results[offset] = callback(first + offset);
        }
      });
    }

    for (auto& thread : threads) {
      thread.join();
    }

    for (const auto result : results) {
      if (result.status != PlannedExecutionStatus::Executed) {
        return result;
      }
    }
    return PlannedExecutionResult{};
  }

 private:
  std::size_t worker_count_ = 1;
};

template <typename Executor, typename Fn>
auto execute_operation_index_range(Executor&& executor,
                                   ExecutorPhaseRange range, Fn&& fn)
    -> PlannedExecutionResult {
  return executor.for_each_operation(
      range.first_operation, range.operation_count, std::forward<Fn>(fn));
}

}  // namespace tess
