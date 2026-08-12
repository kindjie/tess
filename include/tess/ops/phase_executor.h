#pragma once

#include <tess/core/assert.h>
#include <tess/core/config.h>
#include <tess/core/fail_fast.h>
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
  [[nodiscard]] auto operator()(std::size_t /*operation_index*/) const
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
  [[nodiscard]] auto for_each_operation(ExecutorPhaseRange range, Fn&& fn) const
      -> PlannedExecutionResult {
    return for_each_operation(range.first_operation, range.operation_count,
                              std::forward<Fn>(fn));
  }

  template <typename Fn>
  [[nodiscard]] auto for_each_operation(std::size_t first, std::size_t count,
                                        Fn&& fn) const
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
 * Executor that spawns and joins worker threads for each phase.
 * Callback exceptions cancel unclaimed work and are rethrown after all
 * already-running callbacks have joined. If callbacks throw concurrently,
 * which exception is propagated is unspecified.
 */
template <bool CaptureExceptions>
class ScopedThreadPhaseExecutorImpl {
 public:
  static_assert(!CaptureExceptions || has_exceptions,
                "exception capture requires compiler exception support");
  static constexpr bool captures_callback_exceptions = CaptureExceptions;

  explicit ScopedThreadPhaseExecutorImpl(std::size_t worker_count) noexcept
      : worker_count_(worker_count == 0 ? 1 : worker_count) {}

  ScopedThreadPhaseExecutorImpl() noexcept
      : ScopedThreadPhaseExecutorImpl(std::thread::hardware_concurrency()) {}

  [[nodiscard]] auto worker_count() const noexcept -> std::size_t {
    return worker_count_;
  }

  template <typename Fn>
  [[nodiscard]] auto for_each_operation(std::size_t first, std::size_t count,
                                        Fn&& fn) const
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
    static_assert(
        !has_exceptions || CaptureExceptions ||
            std::is_nothrow_invocable_r_v<PlannedExecutionResult,
                                          decltype(callback)&, std::size_t>,
        "NoThrow executors require noexcept callbacks when exceptions are "
        "enabled");

    constexpr auto no_throw_callback =
        !CaptureExceptions ||
        std::is_nothrow_invocable_r_v<PlannedExecutionResult,
                                      decltype(callback)&, std::size_t>;

    if constexpr (no_throw_callback) {
      const auto start_worker = [&] {
        threads.emplace_back([&] {
          while (true) {
            const auto offset = next_offset.fetch_add(1);
            if (offset >= count) {
              return;
            }
            results[offset] = callback(first + offset);
          }
        });
      };
#if TESS_HAS_EXCEPTIONS
      try {
        for (std::size_t worker = 0; worker < thread_count; ++worker) {
          start_worker();
        }
      } catch (...) {
        for (auto& thread : threads) {
          thread.join();
        }
        throw;
      }
#else
      for (std::size_t worker = 0; worker < thread_count; ++worker) {
        start_worker();
      }
#endif
    } else {
#if TESS_HAS_EXCEPTIONS
      std::atomic<bool> cancelled = false;
      std::exception_ptr exception;
      std::mutex exception_mutex;

      for (std::size_t worker = 0; worker < thread_count; ++worker) {
        try {
          threads.emplace_back([&] {
            while (true) {
              const auto offset = next_offset.fetch_add(1);
              if (offset >= count ||
                  cancelled.load(std::memory_order_acquire)) {
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
#endif
    }

    if constexpr (no_throw_callback) {
      for (auto& thread : threads) {
        thread.join();
      }
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

/** Scoped-thread executor selected for the active exception configuration. */
using ScopedThreadPhaseExecutor = ScopedThreadPhaseExecutorImpl<has_exceptions>;

/** Scoped-thread executor for callbacks whose no-throw contract is explicit. */
using NoThrowScopedThreadPhaseExecutor = ScopedThreadPhaseExecutorImpl<false>;

// Stable persistent worker-pool backend behind the PhaseExecutor contract:
// workers are created once and reused across phases, so phase
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
// the nested call. Both violations fail fast under the pool mutex in every
// build before dispatch state can be changed. Distinct executors are
// independent and may dispatch in parallel.
// The analyzer's padding complaint is the point: the alignas(128) members
// below buy false-sharing isolation with those bytes (audit 2026-07-11 M8).
#if defined(_MSC_VER)
#pragma warning(push)
// C4324 reports padding introduced by alignment. The padding in this class is
// intentional: it isolates contended worker-pool state from false sharing.
#pragma warning(disable : 4324)
#endif
// NOLINTBEGIN(clang-analyzer-optin.performance.Padding)
namespace detail {

using PhaseJobInvoke = auto (*)(void*, std::size_t) -> PlannedExecutionResult;
using NoThrowPhaseJobInvoke = auto (*)(void*, std::size_t) noexcept
    -> PlannedExecutionResult;

template <bool CaptureExceptions>
struct WorkerPoolExceptionState {};

template <>
struct alignas(128) WorkerPoolExceptionState<true> {
  mutable std::atomic<bool> cancelled_ = false;
  mutable std::exception_ptr exception_;
  mutable PhaseJobInvoke invoke_ = nullptr;
  mutable bool no_throw_job_ = false;
};

}  // namespace detail

/**
 * Persistent worker pool for allocation-free repeated dispatch.
 * Callback exceptions are rethrown after join; callbacks must not re-enter
 * the same pool. If callbacks throw concurrently, which exception is
 * propagated is unspecified.
 */
template <bool CaptureExceptions>
class WorkerPoolPhaseExecutorImpl
    : private detail::WorkerPoolExceptionState<CaptureExceptions> {
 public:
  static_assert(!CaptureExceptions || has_exceptions,
                "exception capture requires compiler exception support");
  static constexpr bool captures_callback_exceptions = CaptureExceptions;

  explicit WorkerPoolPhaseExecutorImpl(std::size_t worker_count) {
    const auto count = worker_count == 0 ? std::size_t{1} : worker_count;
    workers_.reserve(count);
#if TESS_HAS_EXCEPTIONS
    try {
      for (std::size_t worker = 0; worker < count; ++worker) {
        workers_.emplace_back([this] { run_worker(); });
      }
    } catch (...) {
      // A std::thread constructor threw mid-pool-construction: stop and
      // join the workers that did start, then rethrow instead of letting
      // workers_ unwind over joinable threads, which would terminate.
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
#else
    for (std::size_t worker = 0; worker < count; ++worker) {
      workers_.emplace_back([this] { run_worker(); });
    }
#endif
  }

  WorkerPoolPhaseExecutorImpl()
      : WorkerPoolPhaseExecutorImpl(std::thread::hardware_concurrency()) {}

  WorkerPoolPhaseExecutorImpl(const WorkerPoolPhaseExecutorImpl&) = delete;
  auto operator=(const WorkerPoolPhaseExecutorImpl&)
      -> WorkerPoolPhaseExecutorImpl& = delete;
  WorkerPoolPhaseExecutorImpl(WorkerPoolPhaseExecutorImpl&&) = delete;
  auto operator=(WorkerPoolPhaseExecutorImpl&&)
      -> WorkerPoolPhaseExecutorImpl& = delete;

  ~WorkerPoolPhaseExecutorImpl() {
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
    if (dispatch_active_) {
      detail::fail_fast(
          "WorkerPoolPhaseExecutor::reserve_operations called during an "
          "active dispatch");
    }
    if (results_.size() < count) {
      results_.resize(count);
    }
  }

  template <typename Fn>
  [[nodiscard]] auto for_each_operation(std::size_t first, std::size_t count,
                                        Fn&& fn) const
      -> PlannedExecutionResult {
    if (count == 0) {
      return PlannedExecutionResult{};
    }
    TESS_DIAG_EVENT_VALUE(queued_worker_pool_dispatch,
                          std::min(workers_.size(), count));

    auto&& callback = fn;
    using Callback = std::remove_reference_t<decltype(callback)>;
    static_assert(
        !has_exceptions || CaptureExceptions ||
            std::is_nothrow_invocable_r_v<PlannedExecutionResult, Callback&,
                                          std::size_t>,
        "NoThrow executors require noexcept callbacks when exceptions are "
        "enabled");
    constexpr auto no_throw_callback =
        !CaptureExceptions ||
        std::is_nothrow_invocable_r_v<PlannedExecutionResult, Callback&,
                                      std::size_t>;
    std::size_t runs = 0;
    PlannedExecutionResult result{};
#if TESS_HAS_EXCEPTIONS
    std::exception_ptr exception;
#endif
    {
      const std::scoped_lock lock{mutex_};
      // Single-dispatch guard under the mutex already acquired for setup.
      if (dispatch_active_) {
        detail::fail_fast(
            "WorkerPoolPhaseExecutor::for_each_operation re-entered during "
            "an active dispatch");
      }
      if (results_.size() < count) {
        results_.resize(count);
      }
      // Set only after the potentially throwing resize so a bad_alloc
      // cannot leave the flag wedged; the whole block holds mutex_, so
      // a competing dispatch still observes the flag before touching
      // any job state.
      dispatch_active_ = true;
      job_context_ = &callback;
      if constexpr (no_throw_callback) {
        job_invoke_nothrow_ =
            [](void* context,
               std::size_t index) noexcept -> PlannedExecutionResult {
          return (*static_cast<Callback*>(context))(index);
        };
      }
#if TESS_HAS_EXCEPTIONS
      if constexpr (CaptureExceptions) {
        this->no_throw_job_ = no_throw_callback;
        if constexpr (!no_throw_callback) {
          this->invoke_ = [](void* context,
                             std::size_t index) -> PlannedExecutionResult {
            return (*static_cast<Callback*>(context))(index);
          };
        }
      }
#endif
      job_first_ = first;
      job_count_ = count;
      // Claim short runs instead of single operations: one contended RMW
      // per run instead of per op, while ~4 runs per worker keep the
      // tail balanced (audit 2026-07-11 M8).
      job_stride_ = std::max<std::size_t>(
          1, count / (std::max<std::size_t>(1, workers_.size()) * 4));
      next_offset_.store(0, std::memory_order_relaxed);
      finished_operations_.store(0, std::memory_order_relaxed);
#if TESS_HAS_EXCEPTIONS
      if constexpr (CaptureExceptions) {
        this->cancelled_.store(false, std::memory_order_relaxed);
        this->exception_ = nullptr;
      }
#endif
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
        if constexpr (CaptureExceptions) {
#if TESS_HAS_EXCEPTIONS
          return active_workers_ == 0 &&
                 (this->exception_ || finished_operations_.load(
                                          std::memory_order_acquire) == count);
#else
          return false;
#endif
        } else {
          return active_workers_ == 0 &&
                 finished_operations_.load(std::memory_order_acquire) == count;
        }
      });
#if TESS_HAS_EXCEPTIONS
      if constexpr (CaptureExceptions) {
        exception = this->exception_;
      }
#endif
      // Select and copy the result before releasing dispatch ownership. A
      // competing caller may resize or overwrite results_ as soon as the
      // flag clears.
      for (std::size_t offset = 0; offset < count; ++offset) {
        if (results_[offset].status != PlannedExecutionStatus::Executed) {
          result = results_[offset];
          break;
        }
      }
      job_active_ = false;
      dispatch_active_ = false;
    }

#if TESS_HAS_EXCEPTIONS
    if constexpr (CaptureExceptions) {
      if (exception) {
        std::rethrow_exception(exception);
      }
    }
#endif

    return result;
  }

 private:
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
      const auto invoke_nothrow = job_invoke_nothrow_;
#if TESS_HAS_EXCEPTIONS
      detail::PhaseJobInvoke invoke = nullptr;
      auto no_throw_job = true;
      if constexpr (CaptureExceptions) {
        invoke = this->invoke_;
        no_throw_job = this->no_throw_job_;
      }
#endif
      const auto first = job_first_;
      const auto count = job_count_;
      const auto stride = job_stride_;
      lock.unlock();

      if constexpr (!CaptureExceptions) {
        run_no_throw_job(context, invoke_nothrow, first, count, stride);
      }
#if TESS_HAS_EXCEPTIONS
      if constexpr (CaptureExceptions) {
        if (no_throw_job) {
          run_no_throw_job(context, invoke_nothrow, first, count, stride);
        } else {
          run_catching_job(context, invoke, first, count, stride);
        }
      }
#endif

      lock.lock();
      --active_workers_;
      if (active_workers_ == 0) {
        done_cv_.notify_one();
      }
    }
  }

  void run_no_throw_job(void* context, detail::NoThrowPhaseJobInvoke invoke,
                        std::size_t first, std::size_t count,
                        std::size_t stride) const noexcept {
    while (true) {
      const auto begin =
          next_offset_.fetch_add(stride, std::memory_order_relaxed);
      if (begin >= count) {
        break;
      }
      const auto end = std::min(begin + stride, count);
      for (auto offset = begin; offset < end; ++offset) {
        results_[offset] = invoke(context, first + offset);
      }
      finished_operations_.fetch_add(end - begin, std::memory_order_release);
    }
  }

#if TESS_HAS_EXCEPTIONS
  void run_catching_job(void* context, detail::PhaseJobInvoke invoke,
                        std::size_t first, std::size_t count,
                        std::size_t stride) const {
    auto cancelled = false;
    while (!this->cancelled_.load(std::memory_order_acquire)) {
      const auto begin =
          next_offset_.fetch_add(stride, std::memory_order_relaxed);
      if (begin >= count) {
        break;
      }
      const auto end = std::min(begin + stride, count);
      auto finished = std::size_t{0};
      for (auto offset = begin; offset < end; ++offset) {
        if (this->cancelled_.load(std::memory_order_acquire)) {
          cancelled = true;
          break;
        }
        try {
          results_[offset] = invoke(context, first + offset);
          ++finished;
        } catch (...) {
          this->cancelled_.store(true, std::memory_order_release);
          {
            const std::scoped_lock exception_lock{mutex_};
            if (!this->exception_) {
              this->exception_ = std::current_exception();
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
  }
#endif

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
  alignas(128) mutable void* job_context_ = nullptr;
  mutable detail::NoThrowPhaseJobInvoke job_invoke_nothrow_ = nullptr;
  mutable std::size_t job_first_ = 0;
  mutable std::size_t job_count_ = 0;
  mutable std::size_t job_stride_ = 1;
  mutable std::uint64_t job_epoch_ = 0;
  mutable std::size_t active_workers_ = 0;
  mutable bool job_active_ = false;
  mutable bool dispatch_active_ = false;
  bool stop_ = false;
  std::vector<std::thread> workers_;
};

/** Persistent pool selected for the active exception configuration. */
using WorkerPoolPhaseExecutor = WorkerPoolPhaseExecutorImpl<has_exceptions>;

/** Persistent pool with exception-only coordination removed. */
using NoThrowWorkerPoolPhaseExecutor = WorkerPoolPhaseExecutorImpl<false>;
// NOLINTEND(clang-analyzer-optin.performance.Padding)
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

/** Dispatches one explicit index range through a compatible executor. */
template <typename Executor, typename Fn>
[[nodiscard]] auto execute_operation_index_range(Executor&& executor,
                                                 ExecutorPhaseRange range,
                                                 Fn&& fn)
    -> PlannedExecutionResult {
  return executor.for_each_operation(
      range.first_operation, range.operation_count, std::forward<Fn>(fn));
}

}  // namespace tess
