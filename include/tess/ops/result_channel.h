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
// v1 scope is deliberately drain-only: results are consumed through
// drain_results(visitor) in handle (== enqueue) order. There is no future
// type -- the pipeline has no asynchronous execution path yet, so a future
// could never be pending across a caller-visible boundary; one returns with
// budget-deferred execution. This is a recorded divergence from the TDD's
// full handle vocabulary, exactly like the deferred cancelled/superseded
// states.
namespace tess {

template <typename T>
class ResultChannel;

// Copies every plan-time rejection out of `report` into failed channel
// slots, so validation failures deliver their reasons through the same
// drain as executed results (never values). Returns the number of slots
// stamped. Call once per plan, before executing.
template <typename T>
auto record_plan_completions(const ExecutionReport& report,
                             ResultChannel<T>& channel) -> std::size_t;

// Completion record for one queued operation, carrying both failure
// domains: plan-time verdicts (status/failure, from the OperationReport)
// and run-time verdicts (execution). `completed` distinguishes a stamped
// record from a default-constructed one, so a never-completed lookup can
// never read as success. `execution` is meaningful only for operations
// that reached execution; plan-time rejections keep its default.
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
  // references are valid only during the visit. Visited slots are marked
  // drained (drain-once); state()/completion() stay readable until
  // clear(). Returns the number of slots visited. Pending slots are
  // skipped, not consumed: after a partial plan execution they remain for
  // the caller to inspect.
  template <typename Visitor>
  auto drain_results(Visitor&& visit) -> std::size_t {
    std::size_t visited = 0;
    for (std::size_t index = 0; index < slots_.size(); ++index) {
      auto& slot = slots_[index];
      if (slot.drained || (slot.state != OpResultState::Ready &&
                           slot.state != OpResultState::Failed)) {
        continue;
      }
      slot.drained = true;
      const auto* value =
          slot.state == OpResultState::Ready ? &slot.value : nullptr;
      visit(OpHandle{static_cast<std::uint64_t>(index)}, slot.completion,
            value);
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
      ExecutionPhase phase, PlannedPhaseExecutionScratch& scratch,
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

}  // namespace tess
