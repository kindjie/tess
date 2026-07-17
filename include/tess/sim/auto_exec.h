#pragma once

#include <tess/ops/phase_executor.h>
#include <tess/ops/queued.h>
#include <tess/ops/result_channel.h>
#include <tess/sim/schedule.h>

#include <cstddef>
#include <cstdint>

// The M5 auto-exec task: one schedule task running the whole queued-ops
// pipeline -- plan -> parallel phase planning -> execute (serial or pool,
// chosen per phase) -> per-phase dirty apply -> ack drain -- over a
// caller-owned FrameOps queue. Enqueue whenever; the pipeline runs on the
// task's cadence and both the queue and its result channel are cleared
// together at the end of every run (the paired-clear discipline handles
// restart at zero).
namespace tess {

/// Outcome of the most recent automatic queued-operation execution pass.
enum class AutoExecStatus : std::uint8_t {
  // No operations were queued; the run was a no-op.
  Idle,
  // Every planned operation executed; rejected operations (if any) were
  // delivered through the drain with reasons.
  Executed,
  // At least one queued operation's write policy differs from the task's
  // Policy parameter: NOTHING executed (asserted in debug), and the queue
  // is DROPPED -- keeping it would wedge the task forever in release,
  // rescanning and refusing the same poisoned frame while new enqueues pile
  // on. Pre-validating keeps runtime aborts unreachable, so serial and pool
  // execution can never diverge on partially-applied plans.
  PolicyMismatch,
};

// Statistics of the most recent run, readable between ticks.
/// Counts planning, execution, dirty merging, draining, and phase dispatch.
struct AutoExecRunStats {
  AutoExecStatus status = AutoExecStatus::Idle;
  std::size_t planned_ops = 0;
  std::size_t rejected_ops = 0;
  std::size_t executed_chunks = 0;
  std::size_t merged_dirty_chunks = 0;
  std::size_t drained = 0;
  std::size_t phases = 0;
  std::size_t pool_phases = 0;
};

// Auto-exec over a dense (AlwaysResident) world. `Policy` must be ReadOnly
// or UniquePerChunk (the write policies the parallel phase planner
// supports), and every enqueued operation must carry exactly that policy.
// `ChunkFn` is the per-chunk kernel `fn(view, Ack&)` from the
// result-bearing execute wrappers; `Ack` accumulates op-exclusively on the
// executing thread, but the kernel object itself is SHARED across workers
// when a pool is attached -- it must be safe for concurrent invocation
// (stateless, or synchronizing any mutable state itself). When a worker pool is
// attached, phases with at least `parallel_threshold` operations run on it;
// smaller phases and everything else stay serial, and results are
// byte-identical either way because pre-validation makes runtime aborts
// unreachable.
//
// Planning reuses a task-owned ExecutionReport (its rows, planned ops,
// and chunk lists are recycled between runs), so steady-state ticks plan
// allocation-free once capacities warm up (audit 2026-07-11 M4).
/// Schedule task that plans, executes, merges, and drains a queued-op frame.
template <typename World, WritePolicy Policy, typename Ack, typename ChunkFn>
class AutoExecTask {
  static_assert(Policy == WritePolicy::ReadOnly ||
                    Policy == WritePolicy::UniquePerChunk,
                "auto-exec supports the parallel-phase write policies only");

 public:
  using ResultHook = void (*)(void* ctx, OpHandle handle,
                              const OpCompletion& completion,
                              const Ack* ack) noexcept;

  AutoExecTask(World& world, FrameOps& ops, ChunkFn fn)
      : world_(&world), ops_(&ops), fn_(static_cast<ChunkFn&&>(fn)) {}

  void reserve_operations(std::size_t count) {
    channel_.reserve_operations(count);
    scratch_.reserve_operations(count);
  }

  // Attaches the production pool: phases with at least `threshold`
  // operations run on it. The pool must outlive the task.
  void use_pool(WorkerPoolPhaseExecutor& pool,
                std::size_t threshold = 2) noexcept {
    pool_ = &pool;
    parallel_threshold_ = threshold == 0 ? 1 : threshold;
  }

  void set_result_hook(void* ctx, ResultHook hook) noexcept {
    hook_ctx_ = ctx;
    hook_ = hook;
  }

  [[nodiscard]] auto last_run() const noexcept -> const AutoExecRunStats& {
    return last_run_;
  }

  auto operator()(const ScheduleTaskContext&) -> ScheduleTaskResult {
    last_run_ = AutoExecRunStats{};
    if (ops_->empty()) {
      return ScheduleTaskResult{};
    }
    try {
      return run_nonempty();
    } catch (...) {
      // Planning/execution exceptions preserve the caller-owned queue for
      // inspection or replacement, but transient completion slots must never
      // leak into a later run. Partial world writes make blind retry unsafe.
      channel_.clear();
      throw;
    }
  }

 private:
  auto run_nonempty() -> ScheduleTaskResult {
    // Pre-validate policy uniformity BEFORE planning so a mismatch executes
    // nothing at all (deterministic under any executor).
    for (const auto& operation : ops_->operations()) {
      if (operation.write_policy != Policy) {
        TESS_ASSERT_MSG(false,
                        "auto-exec queue contains a mismatched write policy");
        last_run_.status = AutoExecStatus::PolicyMismatch;
        ops_->clear();
        channel_.clear();
        return ScheduleTaskResult{};
      }
    }

    const auto& report = plan_operations(*world_, *ops_, plan_report_);
    (void)record_plan_completions(report, channel_);
    last_run_.planned_ops = report.planned_count();
    last_run_.rejected_ops = report.failed_count();

    auto produced_dirty = std::uint32_t{0};
    if (!report.plan().empty()) {
      const auto phases = plan_parallel_execution_phases(report.plan());
      // Policy uniformity was pre-validated against the planner-supported
      // set, so phase planning cannot fail.
      TESS_ASSERT(phases.ok());
      last_run_.phases = phases.phases().size();
      for (const auto& phase : phases.phases()) {
        const auto use_pool =
            pool_ != nullptr && phase.operation_count() >= parallel_threshold_;
        auto result = PlannedExecutionResult{};
        try {
          if (use_pool) {
            ++last_run_.pool_phases;
            result = execute_phase_partitioned_dirty_with_results<Policy>(
                *pool_, *world_, report.plan(), phase, scratch_, channel_,
                [this](auto view, Ack& ack) { fn_(view, ack); });
          } else {
            const SerialPhaseExecutor serial;
            result = execute_phase_partitioned_dirty_with_results<Policy>(
                serial, *world_, report.plan(), phase, scratch_, channel_,
                [this](auto view, Ack& ack) { fn_(view, ack); });
          }
        } catch (...) {
          // Dirty records are written before each callback. Both concurrent
          // executors join before rethrowing, and this allocation-free merge
          // is noexcept, so every started callback is conservatively visible
          // without replacing the original kernel exception.
          const auto merged =
              detail::merge_planned_dirty_after_exception(*world_, scratch_);
          TESS_ASSERT(merged.status == PlannedDirtyMergeStatus::Merged);
          last_run_.merged_dirty_chunks += merged.merged_chunk_count;
          throw;
        }
        TESS_ASSERT(result.status == PlannedExecutionStatus::Executed);
        last_run_.executed_chunks += result.chunk_count;
        // Merge after EACH phase: the partitioned scratch is re-prepared
        // per phase, so a single post-loop merge would drop every phase's
        // dirty records but the last.
        auto merged = PlannedDirtyMergeResult{};
        try {
          merged = merge_planned_dirty(*world_, scratch_);
        } catch (...) {
          // Normal coalescing reserves before consuming partitions. If that
          // reserve fails, the no-allocation cold path can still publish every
          // started callback's dirty metadata before preserving the exception.
          const auto fallback =
              detail::merge_planned_dirty_after_exception(*world_, scratch_);
          TESS_ASSERT(fallback.status == PlannedDirtyMergeStatus::Merged);
          last_run_.merged_dirty_chunks += fallback.merged_chunk_count;
          throw;
        }
        TESS_ASSERT(merged.status == PlannedDirtyMergeStatus::Merged);
        last_run_.merged_dirty_chunks += merged.merged_chunk_count;
      }
      for (const auto& operation : report.plan().operations()) {
        produced_dirty |= operation.field_access.dirty_mask;
      }
      last_run_.status = AutoExecStatus::Executed;
    } else {
      last_run_.status = report.operations().empty() ? AutoExecStatus::Idle
                                                     : AutoExecStatus::Executed;
    }

    // Clear the queue BEFORE draining: the plan already copied everything
    // execution needed, and a result hook may enqueue follow-up work -- it
    // lands in the fresh queue for the next run instead of being discarded.
    ops_->clear();
    if (hook_ != nullptr) {
      last_run_.drained += channel_.drain_results(
          [this](OpHandle handle, const OpCompletion& completion,
                 const Ack* ack) noexcept {
            hook_(hook_ctx_, handle, completion, ack);
          });
    }
    channel_.clear();
    return ScheduleTaskResult{produced_dirty, 0, false};
  }

  World* world_;
  FrameOps* ops_;
  ChunkFn fn_;
  WorkerPoolPhaseExecutor* pool_ = nullptr;
  std::size_t parallel_threshold_ = 2;
  ResultChannel<Ack> channel_;
  PlannedPhaseExecutionScratch scratch_;
  ResultHook hook_ = nullptr;
  void* hook_ctx_ = nullptr;
  AutoExecRunStats last_run_{};
  // Reused across runs; see the planning note above.
  ExecutionReport plan_report_;
};

}  // namespace tess
