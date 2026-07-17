#pragma once

#include <tess/core/assert.h>
#include <tess/diagnostics/diagnostics.h>

#include <algorithm>
#include <atomic>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// Phase executor contract.
//
// Queued-operation planning groups already-validated operations into phases
// whose members may execute together. A phase executor receives one
// contiguous planned-operation index range and invokes the per-operation
// callback for every index in it on normal return, completing or joining all
// callbacks (and making their writes visible) before returning. A callback
// exception may suppress work that has not started, but the executor still
// joins callbacks already in flight before rethrowing. Executors do not plan,
// do not reorder result reduction, and do not own dirty metadata: callers
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

/** Describes whether planned execution completed or why it was rejected. */
enum class PlannedExecutionStatus : std::uint8_t {
  Executed,
  PolicyMismatch,
  InvalidShape,
  InvalidChunk,
  InvalidPhase,
};
static_assert(sizeof(PlannedExecutionStatus) == sizeof(std::uint8_t));

/** Identifies a contiguous half-open range of planned operation indexes. */
struct ExecutorPhaseRange {
  std::size_t first_operation = 0;
  std::size_t operation_count = 0;
};

/**
 * Reports execution status and the successful plan-order chunk prefix.
 *
 * Concurrent work after the first failing operation may complete, but those
 * chunks are intentionally excluded from `chunk_count`.
 */
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

/**
 * Accepts executors that visit a contiguous operation range and return its
 * first failure after making every completed callback's writes visible.
 */
template <typename Executor>
concept PhaseExecutor =
    requires(const std::remove_cvref_t<Executor>& executor, std::size_t first,
             std::size_t count, detail::PhaseExecutorProbeCallback callback) {
      {
        executor.for_each_operation(first, count, callback)
      } -> std::same_as<PlannedExecutionResult>;
    };

/** Executes operation callbacks serially in increasing index order. */
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

/**
 * Accepts phase executors that promise callbacks never overlap. Custom serial
 * executors opt in with `using serial_execution_tag = void`.
 */
template <typename Executor>
concept SerialExecutor =
    requires { typename std::remove_cvref_t<Executor>::serial_execution_tag; };

/**
 * Prototype executor that spawns and joins worker threads for each phase.
 * Callback exceptions cancel unclaimed work and are rethrown after all
 * already-running callbacks have joined. If callbacks throw concurrently,
 * which exception is propagated is unspecified.
 */
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
    std::atomic<bool> cancelled = false;
    std::exception_ptr exception;
    std::mutex exception_mutex;
    std::vector<PlannedExecutionResult> results(count);
    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    auto&& callback = fn;

    for (std::size_t worker = 0; worker < thread_count; ++worker) {
      try {
        threads.emplace_back([&] {
          while (true) {
            const auto offset = next_offset.fetch_add(1);
            if (offset >= count || cancelled.load(std::memory_order_acquire)) {
              return;
            }
            try {
              results[offset] = callback(first + offset);
            } catch (...) {
              {
                const std::scoped_lock lock{exception_mutex};
                if (!exception) {
                  exception = std::current_exception();
                }
              }
              cancelled.store(true, std::memory_order_release);
              return;
            }
          }
        });
      } catch (...) {
        // A std::thread constructor threw mid-spawn: join the workers
        // that did start (they drain the remaining operations, so the
        // join is bounded) and rethrow instead of letting the vector
        // unwind over joinable threads, which would std::terminate.
        for (auto& thread : threads) {
          thread.join();
        }
        throw;
      }
    }

    for (auto& thread : threads) {
      thread.join();
    }

    if (exception) {
      std::rethrow_exception(exception);
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

// Prototype persistent worker-pool backend behind the PhaseExecutor
// contract: workers are created once and reused across phases, so phase
// dispatch does not create threads. It invokes callbacks concurrently, so
// like ScopedThreadPhaseExecutor it does not declare serial_execution_tag
// and pairs only with execute_phase_partitioned_dirty_with. AutoExec uses it
// as its synchronous parallel backend when a pool is attached; it is not a
// general asynchronous scheduler. Callback exceptions cancel unclaimed work
// and are rethrown on the dispatching thread after already-running callbacks
// finish.
// After reserve_operations, successful warm for_each_operation calls perform
// no dynamic allocation.
//
// Dispatch contract: at most one for_each_operation may be in flight per
// executor, and callbacks must not re-enter for_each_operation or call
// reserve_operations on the same executor. All dispatch state
// (job_context_ through results_) is shared per executor, so a nested or
// concurrent dispatch clobbers the active job and deadlocks the outer
// caller, which waits on done_cv_ while its own worker is parked inside
// the nested call. Debug builds (TESS_ENABLE_ASSERTS) fail fast on both
// violations; in release builds the assert compiles out (zero cost, per
// core/assert.h) and the misuse deadlocks or races exactly as documented
// here. Distinct executors are independent and may dispatch in parallel.
// The analyzer's padding complaint is the point: the alignas(128) members
// below buy false-sharing isolation with those bytes (audit 2026-07-11 M8).
#if defined(_MSC_VER)
#pragma warning(push)
// C4324 reports padding introduced by alignment. The padding in this class is
// intentional: it isolates contended worker-pool state from false sharing.
#pragma warning(disable : 4324)
#endif
// NOLINTBEGIN(clang-analyzer-optin.performance.Padding)
/**
 * Persistent prototype worker pool for allocation-free repeated dispatch.
 * Callback exceptions are rethrown after join; callbacks must not re-enter
 * the same pool. If callbacks throw concurrently, which exception is
 * propagated is unspecified.
 */
class WorkerPoolPhaseExecutor {
 public:
  explicit WorkerPoolPhaseExecutor(std::size_t worker_count) {
    const auto count = worker_count == 0 ? std::size_t{1} : worker_count;
    workers_.reserve(count);
    try {
      for (std::size_t worker = 0; worker < count; ++worker) {
        workers_.emplace_back([this] { run_worker(); });
      }
    } catch (...) {
      // A std::thread constructor threw mid-pool-construction: stop and
      // join the workers that did start, then rethrow instead of letting
      // workers_ unwind over joinable threads, which would
      // std::terminate.
      {
        const std::scoped_lock lock{mutex_};
        stop_ = true;
      }
      work_cv_.notify_all();
      for (auto& worker : workers_) {
        worker.join();
      }
      throw;
    }
  }

  WorkerPoolPhaseExecutor()
      : WorkerPoolPhaseExecutor(std::thread::hardware_concurrency()) {}

  WorkerPoolPhaseExecutor(const WorkerPoolPhaseExecutor&) = delete;
  auto operator=(const WorkerPoolPhaseExecutor&)
      -> WorkerPoolPhaseExecutor& = delete;
  WorkerPoolPhaseExecutor(WorkerPoolPhaseExecutor&&) = delete;
  auto operator=(WorkerPoolPhaseExecutor&&)
      -> WorkerPoolPhaseExecutor& = delete;

  ~WorkerPoolPhaseExecutor() {
    {
      const std::scoped_lock lock{mutex_};
      stop_ = true;
    }
    work_cv_.notify_all();
    for (auto& worker : workers_) {
      worker.join();
    }
  }

  [[nodiscard]] auto worker_count() const noexcept -> std::size_t {
    return workers_.size();
  }

  // Pre-sizes the per-operation result buffer so warm phases of up to
  // `count` operations do not allocate. A larger phase grows the buffer on
  // that dispatch. Only legal between dispatches: resizing results_ while
  // workers write into it would relocate their slots (use-after-realloc).
  void reserve_operations(std::size_t count) const {
    const std::scoped_lock lock{mutex_};
    TESS_ASSERT_MSG(!dispatch_active_,
                    "WorkerPoolPhaseExecutor::reserve_operations called "
                    "during an active dispatch");
    if (results_.size() < count) {
      results_.resize(count);
    }
  }

  template <typename Fn>
  auto for_each_operation(std::size_t first, std::size_t count, Fn&& fn) const
      -> PlannedExecutionResult {
    if (count == 0) {
      return PlannedExecutionResult{};
    }
    TESS_DIAG_EVENT_VALUE(queued_worker_pool_dispatch,
                          std::min(workers_.size(), count));

    auto&& callback = fn;
    using Callback = std::remove_reference_t<decltype(callback)>;
    std::size_t runs = 0;
    std::exception_ptr exception;
    {
      const std::scoped_lock lock{mutex_};
      // Single-dispatch guard; see the class comment. The flag is
      // maintained in release builds too (two stores under an
      // already-held lock), but only debug builds check it.
      TESS_ASSERT_MSG(!dispatch_active_,
                      "WorkerPoolPhaseExecutor::for_each_operation "
                      "re-entered during an active dispatch");
      if (results_.size() < count) {
        results_.resize(count);
      }
      // Set only after the potentially throwing resize so a bad_alloc
      // cannot leave the flag wedged; the whole block holds mutex_, so
      // a competing dispatch still observes the flag before touching
      // any job state.
      dispatch_active_ = true;
      job_context_ = &callback;
      job_invoke_ = [](void* context,
                       std::size_t index) -> PlannedExecutionResult {
        return (*static_cast<Callback*>(context))(index);
      };
      job_first_ = first;
      job_count_ = count;
      // Claim short runs instead of single operations: one contended RMW
      // per run instead of per op, while ~4 runs per worker keep the
      // tail balanced (audit 2026-07-11 M8).
      job_stride_ = std::max<std::size_t>(
          1, count / (std::max<std::size_t>(1, workers_.size()) * 4));
      next_offset_.store(0, std::memory_order_relaxed);
      finished_operations_.store(0, std::memory_order_relaxed);
      job_cancelled_.store(false, std::memory_order_relaxed);
      job_exception_ = std::exception_ptr{};
      ++job_epoch_;
      job_active_ = true;
      // Derived under the lock so the notify count below never reads
      // job_stride_ across the unlock (safe today with one dispatcher,
      // but locally obvious beats derivable).
      runs = (count + job_stride_ - 1) / job_stride_;
    }
    // Wake only as many workers as there are runs to claim; a small phase
    // on a wide pool otherwise storms every thread awake to find nothing
    // (audit 2026-07-11 M8). A worker that reaches wait() on its own sees
    // the live job through the predicate, so under-notification cannot
    // strand work.
    if (runs >= workers_.size()) {
      work_cv_.notify_all();
    } else {
      for (std::size_t i = 0; i < runs; ++i) {
        work_cv_.notify_one();
      }
    }

    {
      std::unique_lock lock{mutex_};
      done_cv_.wait(lock, [&] {
        return active_workers_ == 0 &&
               (job_exception_ ||
                finished_operations_.load(std::memory_order_acquire) == count);
      });
      exception = job_exception_;
      job_active_ = false;
      dispatch_active_ = false;
    }

    if (exception) {
      std::rethrow_exception(exception);
    }

    for (std::size_t offset = 0; offset < count; ++offset) {
      if (results_[offset].status != PlannedExecutionStatus::Executed) {
        return results_[offset];
      }
    }
    return PlannedExecutionResult{};
  }

 private:
  using JobInvoke = auto (*)(void*, std::size_t) -> PlannedExecutionResult;

  void run_worker() {
    std::uint64_t seen_epoch = 0;
    while (true) {
      std::unique_lock lock{mutex_};
      work_cv_.wait(lock, [&] {
        return stop_ || (job_active_ && job_epoch_ != seen_epoch);
      });
      if (stop_) {
        return;
      }
      seen_epoch = job_epoch_;
      ++active_workers_;
      auto* const context = job_context_;
      const auto invoke = job_invoke_;
      const auto first = job_first_;
      const auto count = job_count_;
      const auto stride = job_stride_;
      lock.unlock();

      auto cancelled = false;
      while (!job_cancelled_.load(std::memory_order_acquire)) {
        const auto begin =
            next_offset_.fetch_add(stride, std::memory_order_relaxed);
        if (begin >= count) {
          break;
        }
        const auto end = std::min(begin + stride, count);
        auto finished = std::size_t{0};
        for (auto offset = begin; offset < end; ++offset) {
          if (job_cancelled_.load(std::memory_order_acquire)) {
            cancelled = true;
            break;
          }
          try {
            results_[offset] = invoke(context, first + offset);
            ++finished;
          } catch (...) {
            job_cancelled_.store(true, std::memory_order_release);
            {
              const std::scoped_lock exception_lock{mutex_};
              if (!job_exception_) {
                job_exception_ = std::current_exception();
              }
            }
            cancelled = true;
            break;
          }
        }
        // One release-add per run publishes the whole run's results to
        // the dispatcher's acquire wait.
        finished_operations_.fetch_add(finished, std::memory_order_release);
        if (cancelled) {
          break;
        }
      }

      lock.lock();
      --active_workers_;
      // Only the last worker out can satisfy the dispatcher's predicate
      // (finished == count AND active_workers_ == 0), so intermediate
      // notifies were pure wakeup churn. notify_one: the single-dispatch
      // contract means done_cv_ has at most one waiter.
      if (active_workers_ == 0) {
        done_cv_.notify_one();
      }
    }
  }

  mutable std::mutex mutex_;
  mutable std::condition_variable work_cv_;
  mutable std::condition_variable done_cv_;
  mutable std::vector<PlannedExecutionResult> results_;
  // Own cache lines: every worker RMWs both counters per claimed run;
  // adjacent they ping-pong one line between cores (audit 2026-07-11 M8).
  // 128, not 64: Apple Silicon (where the A/B numbers were measured) has
  // 128-byte lines and x86 prefetches the adjacent line, while alignas
  // only fixes spacing relative to the object base -- at 64 the pair
  // could still share one 128-byte line depending on allocation address.
  alignas(128) mutable std::atomic<std::size_t> next_offset_ = 0;
  alignas(128) mutable std::atomic<std::size_t> finished_operations_ = 0;
  mutable std::atomic<bool> job_cancelled_ = false;
  alignas(128) mutable void* job_context_ = nullptr;
  mutable JobInvoke job_invoke_ = nullptr;
  mutable std::size_t job_first_ = 0;
  mutable std::size_t job_count_ = 0;
  mutable std::size_t job_stride_ = 1;
  mutable std::uint64_t job_epoch_ = 0;
  mutable std::size_t active_workers_ = 0;
  mutable std::exception_ptr job_exception_;
  mutable bool job_active_ = false;
  mutable bool dispatch_active_ = false;
  bool stop_ = false;
  std::vector<std::thread> workers_;
};
// NOLINTEND(clang-analyzer-optin.performance.Padding)
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

/** Dispatches one explicit index range through a compatible executor. */
template <typename Executor, typename Fn>
auto execute_operation_index_range(Executor&& executor,
                                   ExecutorPhaseRange range, Fn&& fn)
    -> PlannedExecutionResult {
  return executor.for_each_operation(
      range.first_operation, range.operation_count, std::forward<Fn>(fn));
}

}  // namespace tess
