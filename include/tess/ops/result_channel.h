#pragma once

#include <tess/core/assert.h>
#include <tess/ops/queued.h>

#include <cstddef>
#include <cstdint>
#include <source_location>
#include <type_traits>
#include <vector>

// Queued-operation result channels (M4). A ResultChannel<T> is caller-owned
// scratch delivering one typed payload plus an OpCompletion per queued
// operation, keyed by OpHandle. Publication is synchronous and
// executor-agnostic: each op's executing thread writes only its own dense
// slot (the same per-operation-slot discipline the partitioned dirty scratch
// uses), and every read -- state, completion, drain -- happens on the frame
// owner's thread after the execute call returns, so the S1 executor join
// barrier supplies visibility and the channel holds no atomics.
//
// The current synchronous scope is deliberately drain-only: results use
// drain_results(visitor) in handle (== enqueue) order. There is no future
// type -- the pipeline has no asynchronous execution path yet, so a future
// could never be pending across a caller-visible boundary; one returns with
// budget-deferred execution. This is a recorded divergence from the TDD's
// full handle vocabulary, exactly like the deferred cancelled/superseded
// states.
namespace tess {

/// Dense per-operation completion and payload channel.
template <typename T>
class ResultChannel;

// Copies every plan-time rejection out of `report` into failed channel
// slots, so validation failures deliver their reasons through the same
// drain as executed results (never values). Returns the number of slots
// stamped. Call once per plan, before executing.
/// Copies plan-time failures into their result-channel slots.
template <typename T>
auto record_plan_completions(const ExecutionReport& report,
                             ResultChannel<T>& channel) -> std::size_t;

// Completion record for one queued operation, carrying both failure
// domains: plan-time verdicts (status/failure, from the OperationReport)
// and run-time verdicts (execution). `completed` distinguishes a stamped
// record from a default-constructed one, so a never-completed lookup can
// never read as success. `execution` is meaningful only for operations
// that reached execution; plan-time rejections keep its default.
/// Completion metadata spanning planning and execution failure domains.
struct OpCompletion {
  OperationStatus status = OperationStatus::Planned;
  OperationFailure failure = OperationFailure::None;
  PlannedExecutionStatus execution = PlannedExecutionStatus::Executed;
  std::size_t chunk_count = 0;
  std::source_location source = std::source_location::current();
  bool completed = false;

  [[nodiscard]] constexpr bool ok() const noexcept {
    return completed && status == OperationStatus::Planned &&
           failure == OperationFailure::None &&
           execution == PlannedExecutionStatus::Executed;
  }
};

// Lifecycle of one channel slot. `Pending` covers both "prepared, execution
// has not reached it" and "plan aborted before it ran" -- a drain after a
// partial execution visits only completed slots, and the pending tail is
// the caller's signal that the plan stopped early.
/// Lifecycle state of one result-channel slot.
enum class OpResultState : std::uint8_t {
  Unbound,  // no slot recorded for this handle
  Pending,  // prepared for execution; not yet completed
  Ready,    // completed and ok(); the value is readable
  Failed,   // completed with reasons; there is no value
};

// Caller-owned, fixed-capacity result channel keyed by OpHandle. Slots are
// dense -- slot index == handle.value, because FrameOps hands out handles
// 0..N-1 per frame -- so lookup is O(1) with no map. Externally
// synchronized like every tess scratch: the frame owner calls everything
// except the producer hooks, which the result-bearing execute wrappers
// invoke from worker threads on disjoint per-op slots. clear() must be
// called alongside FrameOps::clear(): handle assignment restarts at zero
// there, and a channel kept across it would alias new-frame handles onto
// stale slots.
//
// T must be default-constructible; the warm allocation-free contract
// additionally requires T's default construction and assignment to be
// allocation-free (as with any POD ack payload).
/// Fixed-capacity, handle-indexed completion channel with drain-once delivery.
template <typename T>
class ResultChannel {
  static_assert(std::is_default_constructible_v<T>,
                "result payloads are default-constructed into slots");

 public:
  ResultChannel() = default;
  ResultChannel(const ResultChannel&) = delete;
  auto operator=(const ResultChannel&) -> ResultChannel& = delete;
  ResultChannel(ResultChannel&&) = delete;
  auto operator=(ResultChannel&&) -> ResultChannel& = delete;

  // Cold-path capacity; warm frames stay allocation-free while the frame's
  // operation count fits within it.
  void reserve_operations(std::size_t count) { slots_.reserve(count); }

  // Frame reset: drops all slots (keeping capacity). Pair with
  // FrameOps::clear() -- see the class comment.
  void clear() noexcept {
    slots_.clear();
    ++generation_;
  }

  [[nodiscard]] auto state(OpHandle handle) const noexcept -> OpResultState {
    if (handle.value >= slots_.size()) {
      return OpResultState::Unbound;
    }
    return slots_[static_cast<std::size_t>(handle.value)].state;
  }

  // Completion lookup by value: a handle without a completed slot returns a
  // default OpCompletion, whose ok() is false by construction.
  [[nodiscard]] auto completion(OpHandle handle) const noexcept
      -> OpCompletion {
    if (handle.value >= slots_.size()) {
      return OpCompletion{};
    }
    return slots_[static_cast<std::size_t>(handle.value)].completion;
  }

  [[nodiscard]] auto size() const noexcept -> std::size_t {
    return slots_.size();
  }

  // Bumped by clear(); lets tests and long-lived callers assert the
  // paired-clear discipline.
  [[nodiscard]] auto generation() const noexcept -> std::uint64_t {
    return generation_;
  }

  // Visits every completed, not-yet-drained slot in handle (== enqueue)
  // order as visit(OpHandle, const OpCompletion&, const T* value); `value`
  // is null for Failed slots -- failures deliver reasons, not values. The
  // references are valid only until the visitor mutates this channel's
  // storage or lifecycle. Visited slots are marked drained (drain-once);
  // state()/completion() stay readable until clear(). A reentrant clear ends
  // the current drain. Returns the number of slots visited. Pending slots are
  // skipped, not consumed: after a partial plan execution they remain for the
  // caller to inspect.
  template <typename Visitor>
  auto drain_results(Visitor&& visit) -> std::size_t {
    std::size_t visited = 0;
    const auto drain_generation = generation_;
    for (std::size_t index = 0;
         generation_ == drain_generation && index < slots_.size(); ++index) {
      if (slots_[index].drained ||
          (slots_[index].state != OpResultState::Ready &&
           slots_[index].state != OpResultState::Failed)) {
        continue;
      }
      const auto has_value = slots_[index].state == OpResultState::Ready;
      // Preserve the hot-path store ordering while making delivery
      // transactional: an exceptional visitor restores the current-generation
      // slot so the caller can retry it. Do not retain a slot reference across
      // the visitor because it may reallocate or clear the channel.
      slots_[index].drained = true;
      try {
        visit(OpHandle{static_cast<std::uint64_t>(index)},
              slots_[index].completion,
              has_value ? &slots_[index].value : nullptr);
      } catch (...) {
        if (generation_ == drain_generation && index < slots_.size()) {
          slots_[index].drained = false;
        }
        throw;
      }
      ++visited;
    }
    return visited;
  }

 private:
  struct Slot {
    T value{};
    OpCompletion completion{};
    OpResultState state = OpResultState::Unbound;
    bool drained = false;
  };

  // Producer hooks, called only by the friended wrappers below. `ensure`
  // and `prepare_operation` run on the frame owner's thread before any
  // dispatch; `value_for` and `complete` run on whichever thread executes
  // the op, touching only that op's slot.
  void ensure_slot(OpHandle handle) {
    if (handle.value >= slots_.size()) {
      slots_.resize(static_cast<std::size_t>(handle.value) + 1);
    }
  }

  void prepare_operation(OpHandle handle, std::source_location source) {
    ensure_slot(handle);
    auto& slot = slots_[static_cast<std::size_t>(handle.value)];
    slot.value = T{};
    slot.completion = OpCompletion{};
    slot.completion.source = source;
    slot.state = OpResultState::Pending;
    slot.drained = false;
  }

  [[nodiscard]] auto value_for(OpHandle handle) noexcept -> T& {
    TESS_ASSERT(handle.value < slots_.size());
    return slots_[static_cast<std::size_t>(handle.value)].value;
  }

  void complete(OpHandle handle, PlannedExecutionResult result,
                std::source_location source) noexcept {
    TESS_ASSERT(handle.value < slots_.size());
    auto& slot = slots_[static_cast<std::size_t>(handle.value)];
    slot.completion.status = OperationStatus::Planned;
    slot.completion.failure = OperationFailure::None;
    slot.completion.execution = result.status;
    slot.completion.chunk_count = result.chunk_count;
    slot.completion.source = source;
    slot.completion.completed = true;
    slot.state = result.status == PlannedExecutionStatus::Executed
                     ? OpResultState::Ready
                     : OpResultState::Failed;
  }

  void fail_planned(OpHandle handle, const OperationReport& report) {
    ensure_slot(handle);
    auto& slot = slots_[static_cast<std::size_t>(handle.value)];
    slot.value = T{};
    slot.completion = OpCompletion{};
    slot.completion.status = report.status;
    slot.completion.failure = report.failure;
    slot.completion.chunk_count = report.chunk_count;
    slot.completion.source = report.source;
    slot.completion.completed = true;
    slot.state = OpResultState::Failed;
    slot.drained = false;
  }

  template <typename U>
  friend auto record_plan_completions(const ExecutionReport& report,
                                      ResultChannel<U>& channel) -> std::size_t;

  template <WritePolicy Policy, typename Executor, typename World, typename U,
            typename Fn>
  friend auto execute_phase_partitioned_dirty_with_results(
      Executor&& executor, World& world, const ExecutionPlan& plan,
      const ExecutionPhase& phase, PlannedPhaseExecutionScratch& scratch,
      ResultChannel<U>& channel, Fn&& fn) -> PlannedExecutionResult;

  template <WritePolicy Policy, typename World, typename U, typename Fn>
  friend auto execute_plan_deferred_dirty_with_results(
      World& world, const ExecutionPlan& plan, PlannedDirtyAccumulator& dirty,
      ResultChannel<U>& channel, Fn&& fn) -> PlannedExecutionResult;

  std::vector<Slot> slots_;
  std::uint64_t generation_ = 0;
};

template <typename T>
auto record_plan_completions(const ExecutionReport& report,
                             ResultChannel<T>& channel) -> std::size_t {
  std::size_t stamped = 0;
  for (const auto& operation : report.operations()) {
    if (operation.status == OperationStatus::Planned) {
      continue;
    }
    channel.fail_planned(operation.handle, operation);
    ++stamped;
  }
  return stamped;
}

// Result-bearing variant of execute_phase_partitioned_dirty_with: the
// caller's callback receives each chunk view PLUS a mutable reference to the
// operation's channel value (`fn(view, T& value)`), accumulated across the
// op's chunks on whichever thread executes it -- op-exclusive, so no
// synchronization. Every operation in the phase is prepared upfront on the
// caller's thread, so an execution that stops early (the serial executor
// aborts at the first failure) leaves a Pending tail rather than Unbound
// gaps, and each op's completion is stamped by its executing thread -- a
// post-barrier sweep over the scratch results would misread never-run
// operations as Executed, because PlannedExecutionResult default-constructs
// to that status. Aggregate return and dirty partitioning are identical to
// the resultless helper.
template <WritePolicy Policy, typename Executor, typename World, typename T,
          typename Fn>
/// Executes one phase while publishing per-operation payloads and completions.
auto execute_phase_partitioned_dirty_with_results(
    Executor&& executor, World& world, const ExecutionPlan& plan,
    const ExecutionPhase& phase, PlannedPhaseExecutionScratch& scratch,
    ResultChannel<T>& channel, Fn&& fn) -> PlannedExecutionResult {
  const auto operations = plan.operations();
  // Capability, world, and policy checks happen before touching either the
  // channel or scratch. A phase issued for another plan or world, or carrying
  // any other policy, therefore cannot publish partial completion state.
  const auto phase_validation =
      detail::execution_phase_validation_status<Policy>(world, plan, phase);
  if (phase_validation != PlannedExecutionStatus::Executed) {
    detail::record_execution_phase_validation_failure(phase_validation);
    return PlannedExecutionResult{
        phase_validation,
        0,
    };
  }

  TESS_DIAG_EVENT_VALUE(queued_phase_execute, phase.operation_count());
  TESS_DIAG_EVENT_VALUE(queued_partitioned_phase, phase.operation_count());
  for (const auto& operation :
       operations.subspan(phase.first_operation(), phase.operation_count())) {
    channel.prepare_operation(operation.handle, operation.source);
  }
  scratch.prepare(world, phase.operation_count());
  auto&& callback = fn;
  auto result = execute_operation_index_range(
      std::forward<Executor>(executor), executor_phase_range(phase),
      [&](std::size_t index) {
        const auto offset = index - phase.first_operation();
        const auto& operation = operations[index];
        auto& value = channel.value_for(operation.handle);
        auto operation_result =
            detail::execute_validated_phase_operation_deferred_dirty<Policy>(
                world, operation, scratch.dirty_for_operation(offset),
                [&](auto view) { callback(view, value); });
        channel.complete(operation.handle, operation_result, operation.source);
        scratch.record_result(offset, operation_result);
        return operation_result;
      });

  std::size_t chunk_count = 0;
  for (const auto operation_result : scratch.results()) {
    if (operation_result.status != PlannedExecutionStatus::Executed) {
      TESS_DIAG_EVENT(queued_phase_failure);
      return PlannedExecutionResult{
          operation_result.status,
          chunk_count,
      };
    }
    chunk_count += operation_result.chunk_count;
  }

  if (result.status != PlannedExecutionStatus::Executed) {
    TESS_DIAG_EVENT(queued_phase_failure);
    return PlannedExecutionResult{
        result.status,
        chunk_count,
    };
  }

  return PlannedExecutionResult{
      PlannedExecutionStatus::Executed,
      chunk_count,
  };
}

// Result-bearing variant of execute_plan_deferred_dirty: serial, whole-plan,
// aborting at the first non-Executed result with the same partial-execution
// contract (earlier writes kept, chunk counts reported). All operations are
// prepared upfront, so the aborted tail reads Pending through the channel.
/// Executes a serial plan while publishing per-operation results.
template <WritePolicy Policy, typename World, typename T, typename Fn>
auto execute_plan_deferred_dirty_with_results(World& world,
                                              const ExecutionPlan& plan,
                                              PlannedDirtyAccumulator& dirty,
                                              ResultChannel<T>& channel,
                                              Fn&& fn)
    -> PlannedExecutionResult {
  for (const auto& operation : plan.operations()) {
    channel.prepare_operation(operation.handle, operation.source);
  }
  std::size_t chunk_count = 0;
  auto&& callback = fn;
  for (const auto& operation : plan.operations()) {
    auto& value = channel.value_for(operation.handle);
    auto result = execute_planned_operation_deferred_dirty<Policy>(
        world, operation, dirty, [&](auto view) { callback(view, value); });
    channel.complete(operation.handle, result, operation.source);
    if (result.status != PlannedExecutionStatus::Executed) {
      return PlannedExecutionResult{
          result.status,
          chunk_count + result.chunk_count,
      };
    }
    chunk_count += result.chunk_count;
  }
  return PlannedExecutionResult{
      PlannedExecutionStatus::Executed,
      chunk_count,
  };
}

}  // namespace tess
