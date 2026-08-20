#pragma once

#include <tess/core/assert.h>
#include <tess/core/config.h>
#include <tess/core/fail_fast.h>
#include <tess/diagnostics/trace.h>
#include <tess/sim/event_stream.h>
#include <tess/sim/time.h>
#include <tess/storage/metadata_types.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <vector>

// Ordered phases of type-erased tasks driven by cadences
// that are pure functions of the fixed-tick counter and per-task pending
// dirty/event masks. The schedule itself never touches a world -- trigger
// bits are fed to it explicitly -- so "no hidden full-world scans" holds by
// construction. World-typed work lives in task objects the caller owns and
// registers by reference; type erasure is a function pointer plus a context
// pointer (no std::function, no allocation on dispatch).
//
// Threading: a Schedule is externally synchronized like every tess scratch.
// notify_dirty, notify_events, and request_run are frame-owner-thread calls and
// must never be made from queued-operation callbacks (those may run on pool
// workers); worker-produced triggers flow through task-result masks.
//
// Reentrancy: task bodies may call notify_dirty, notify_events, request_run,
// and set_enabled (field writes on address-stable storage, with the documented
// immediate-merge semantics). They must NOT call add_task, reserve_tasks, or
// run_tick -- registration/capacity changes after seal() could invalidate the
// task array mid-iteration, and a nested tick would double-advance every
// cadence. These violations fail fast in every build.
namespace tess {

/// Selects the deterministic trigger policy for a scheduled task.
enum class CadenceKind : std::uint8_t {
  EveryTick,
  EveryN,
  OnDirty,
  OnEvent,
  Background,
  Manual,
};

// Deterministic background bound: a due background task is offered at most
// max_items work units per run and reports how many it consumed plus
// whether work remains. There is deliberately no wall-clock budget in the
// current pre-1.0 release --
// a time valve would make tick outcomes nondeterministic, and every
// consumer bound is expressible in items; it returns with its first real
// consumer.
/// Bounds background work in deterministic item units per task invocation.
struct BackgroundBudget {
  std::uint32_t max_items = 1;
};

/// Configures when a task becomes due within the fixed-tick schedule.
struct Cadence {
  CadenceKind kind = CadenceKind::EveryTick;
  std::uint32_t every_n = 1;
  DirtyMask dirty_mask = {};
  BackgroundBudget budget{};
  std::uint32_t event_mask = 0;

  [[nodiscard]] static constexpr auto every_tick() noexcept -> Cadence {
    return Cadence{};
  }

  [[nodiscard]] static constexpr auto every_ticks(std::uint32_t n) noexcept
      -> Cadence {
    return Cadence{CadenceKind::EveryN, n == 0 ? 1u : n, {}, {}, 0};
  }

  [[nodiscard]] static constexpr auto on_dirty(DirtyMask mask) noexcept
      -> Cadence {
    return Cadence{CadenceKind::OnDirty, 1, mask, {}, 0};
  }

  [[nodiscard]] static constexpr auto on_event(std::uint32_t mask) noexcept
      -> Cadence {
    return Cadence{CadenceKind::OnEvent, 1, {}, {}, mask};
  }

  [[nodiscard]] static constexpr auto background(
      BackgroundBudget budget) noexcept -> Cadence {
    return Cadence{
        CadenceKind::Background,
        1,
        {},
        BackgroundBudget{budget.max_items == 0 ? 1u : budget.max_items},
        0};
  }

  [[nodiscard]] static constexpr auto manual() noexcept -> Cadence {
    return Cadence{CadenceKind::Manual, 1, {}, {}, 0};
  }
};

// Fixed phase list, executed in declaration order every tick. Tasks run in
// registration order within a phase. The set matches the simulation TDD's
// phase vocabulary; custom phase lists are deferred until a consumer needs
// one.
/// Selects the fixed order in which task groups execute each tick.
enum class SimPhase : std::uint8_t {
  Input,
  PreUpdate,
  AI,
  Pathing,
  Movement,
  Commit,
  Topology,
  Fields,
  Background,
  RenderDelta,
  Diagnostics,
  Count,
};

/// Supplies a task with the current tick, trigger bits, and work allowance.
struct ScheduleTaskContext {
  SimClock clock{};
  // OnDirty: the bits (within the task's own mask) that made it due; they
  // are consumed before the task runs, so bits raised DURING the run re-arm
  // it for the next tick.
  DirtyMask pending_dirty{};
  // Background: the item budget for this run.
  std::uint32_t budget_items = 0;
  // OnEvent: the subscribed bits that made the task due. Multiple
  // notifications coalesce until this invocation consumes them.
  std::uint32_t pending_events = 0;
};

/// Returns produced dirty bits and bounded background progress to the schedule.
struct ScheduleTaskResult {
  // Dirty bits this run produced; the schedule merges them into every
  // OnDirty task's pending mask immediately, so later-phase tasks can fire
  // in the same tick and earlier-phase tasks fire next tick.
  DirtyMask dirty_mask = {};
  // Background: work units consumed (at most the offered budget).
  std::uint32_t items_done = 0;
  // Background: true keeps the task due next tick without a new trigger.
  bool more_work = false;
  // Event bits produced by this run. Later phases observe them in the same
  // tick; earlier phases observe them on the next tick.
  std::uint32_t event_mask = 0;
};

/// Type-erased, non-owning callback signature used by `Schedule`.
using ScheduleTaskFn = ScheduleTaskResult (*)(void* ctx,
                                              const ScheduleTaskContext&);

/// Type-erased callback signature preserving an explicit no-throw contract.
using ScheduleNoThrowTaskFn =
    ScheduleTaskResult (*)(void* ctx, const ScheduleTaskContext&) noexcept;

/// Describes a task's static label, phase, and cadence.
struct ScheduleTaskDesc {
  // Static-storage label (same rule as diagnostics trace labels).
  std::string_view name;
  SimPhase phase = SimPhase::PreUpdate;
  Cadence cadence{};
};

/// Holds cumulative execution counters for one task.
struct ScheduleTaskStats {
  std::uint64_t runs = 0;
  // Ticks on which the task was due but disabled.
  std::uint64_t skipped = 0;
  std::uint64_t background_items = 0;
  std::uint64_t last_run_tick = 0;
};

/// Summarizes task dispatch and dirty propagation for one fixed tick.
struct ScheduleTickStats {
  std::uint64_t tick = 0;
  std::uint32_t tasks_due = 0;
  std::uint32_t tasks_run = 0;
  std::uint32_t tasks_skipped = 0;
  std::uint32_t background_items = 0;
  // Union of every task result's dirty mask this tick.
  DirtyMask dirty_mask_produced{};
  // Union of every task result's event mask this tick.
  std::uint32_t event_mask_produced = 0;
};

/// Executes non-owning task callbacks in deterministic phase order.
///
/// Registering tasks may allocate until `seal`; dispatch performs no
/// allocation. Instances require external synchronization and callbacks must
/// outlive the schedule.
class Schedule {
 public:
  using TaskId = std::uint32_t;

  // Setup-time capacity; add_task within it never reallocates, and run_tick
  // never allocates at all.
  void reserve_tasks(std::size_t count) {
    if (sealed_) {
      detail::fail_fast("Schedule::reserve_tasks called after seal()");
    }
    tasks_.reserve(count);
    phase_order_.reserve(count);
    dirty_task_ids_.reserve(count);
    event_task_ids_.reserve(count);
  }

  auto add_task(const ScheduleTaskDesc& desc, void* ctx, ScheduleTaskFn fn)
      -> TaskId {
    return add_task_record(desc, ctx, fn, nullptr);
  }

  auto add_task(const ScheduleTaskDesc& desc, void* ctx,
                ScheduleNoThrowTaskFn fn) -> TaskId {
    return add_task_record(desc, ctx, nullptr, fn);
  }

  // Preserve the original null-callback assertion path now that two erased
  // function-pointer overloads exist; a bare nullptr must not be ambiguous.
  auto add_task(const ScheduleTaskDesc& desc, void* ctx, std::nullptr_t)
      -> TaskId {
    return add_task_record(desc, ctx, nullptr, nullptr);
  }

  // Registers a task OBJECT the caller owns; `task` must outlive the
  // schedule. T is any callable taking the context and returning a result.
  template <typename T>
  auto add_task(const ScheduleTaskDesc& desc, T& task) -> TaskId {
    if constexpr (std::is_nothrow_invocable_r_v<ScheduleTaskResult, T&,
                                                const ScheduleTaskContext&>) {
      return add_task(desc, static_cast<void*>(&task),
                      [](void* ctx, const ScheduleTaskContext& context) noexcept
                          -> ScheduleTaskResult {
                        return (*static_cast<T*>(ctx))(context);
                      });
    } else {
      return add_task(
          desc, static_cast<void*>(&task),
          [](void* ctx,
             const ScheduleTaskContext& context) -> ScheduleTaskResult {
            return (*static_cast<T*>(ctx))(context);
          });
    }
  }

  // Freezes registration and builds the dispatch indexes: phase_order_
  // (phase-major, registration-stable -- the order run_tick always had,
  // now one pass instead of SimPhase::Count passes over every task) and
  // dirty/event subscription indexes. Trigger merges stop writing tasks that
  // never read the corresponding value. Storage is never reordered, so
  // TaskIds remain valid for the schedule's lifetime. A contract-
  // violating add_task after seal() asserts in debug builds; under NDEBUG
  // the late task registers but never dispatches (it is absent from the
  // frozen indexes).
  void seal() {
    // Idempotent: registration is frozen after the first seal, so there is
    // nothing to rebuild -- and a redundant seal() from inside a task
    // callback must not rebuild phase_order_ while run_tick iterates it
    // (Codex review of the audit3 W3 change).
    if (sealed_) {
      return;
    }
    phase_order_.clear();
    dirty_task_ids_.clear();
    event_task_ids_.clear();
    for (std::uint8_t phase = 0;
         phase < static_cast<std::uint8_t>(SimPhase::Count); ++phase) {
      for (std::size_t i = 0; i < tasks_.size(); ++i) {
        if (static_cast<std::uint8_t>(tasks_[i].desc.phase) == phase) {
          phase_order_.push_back(static_cast<TaskId>(i));
        }
      }
    }
    for (std::size_t i = 0; i < tasks_.size(); ++i) {
      if (tasks_[i].desc.cadence.kind == CadenceKind::OnDirty) {
        dirty_task_ids_.push_back(static_cast<TaskId>(i));
      }
      if (tasks_[i].desc.cadence.kind == CadenceKind::OnEvent) {
        event_task_ids_.push_back(static_cast<TaskId>(i));
      }
    }
    sealed_ = true;
  }

  [[nodiscard]] auto sealed() const noexcept -> bool { return sealed_; }

  // A TaskId only ever comes from add_task, so an out-of-range one is a
  // caller bug, not a runtime condition. Asserting and then silently
  // succeeding meant a release build accepted the bug and left the task in
  // whatever state it already had, so the symptom surfaced later as a task
  // that inexplicably would not turn off.
  void set_enabled(TaskId id, bool enabled) noexcept {
    if (id >= tasks_.size()) {
      detail::fail_fast("Schedule::set_enabled called with an unknown TaskId");
    }
    tasks_[id].enabled = enabled;
  }

  // Arms the task to be due on the next run_tick regardless of cadence --
  // the Manual trigger, and the initial trigger for Background tasks. An
  // OnDirty task poked this way runs with pending_dirty == 0: treat a
  // zero mask as a full-run request, not a no-op.
  void request_run(TaskId id) noexcept {
    if (id >= tasks_.size()) {
      detail::fail_fast("Schedule::request_run called with an unknown TaskId");
    }
    tasks_[id].run_requested = true;
  }

  // Merges external dirty bits into the pending masks that can consume
  // them (only OnDirty cadences read pending_mask; foreign bits within an
  // OnDirty task's mask sit inert). Frame-owner thread only; never call
  // from an op callback.
  void notify_dirty(DirtyMask mask) noexcept {
    if (sealed_) {
      for (const auto id : dirty_task_ids_) {
        tasks_[id].pending_mask |= mask;
      }
      return;
    }
    for (auto& task : tasks_) {
      task.pending_mask |= mask;
    }
  }

  // Coalesces external event wakeups into subscribed tasks. The event mask
  // is only a deterministic scheduler trigger; applications retain exact
  // payloads and tick ordering in EventStream<T>.
  void notify_events(std::uint32_t mask) noexcept {
    if (sealed_) {
      for (const auto id : event_task_ids_) {
        tasks_[id].pending_events |= mask;
      }
      return;
    }
    for (auto& task : tasks_) {
      task.pending_events |= mask;
    }
  }

  // Publishes an exact payload before arming its coalesced scheduler mask.
  // A full stream rejects the payload and deliberately does not wake tasks.
  template <typename T>
  [[nodiscard]] bool publish_event(std::uint32_t mask, EventStream<T>& stream,
                                   std::uint64_t tick, const T& value) {
    if (!stream.publish(tick, value)) {
      return false;
    }
    notify_events(mask);
    return true;
  }

  auto run_tick(SimClock& clock) -> ScheduleTickStats {
#if TESS_DIAGNOSTICS_ENABLED
    diagnostics::ScopedTimer tick_timer{diagnostics::TraceCategory::Scheduler,
                                        "schedule_tick"};
#endif
    if (!sealed_) {
      detail::fail_fast("Schedule::run_tick called before seal()");
    }
    if (in_run_) {
      detail::fail_fast("Schedule::run_tick rejected a reentrant run_tick");
    }
    // Scope guard rather than a trailing store: a throwing task callback
    // must not leave the schedule latched "in run", or every subsequent
    // tick would fail the reentrancy contract.
    struct InRunGuard {
      bool& flag;
      ~InRunGuard() { flag = false; }
    };
    in_run_ = true;
    const InRunGuard guard{in_run_};
    auto stats = ScheduleTickStats{};
    stats.tick = advance_sim_tick(clock);

    for (std::size_t position = 0; position < phase_order_.size(); ++position) {
#if TESS_HAS_EXCEPTIONS
      try {
        run_task_if_due(tasks_[phase_order_[position]], clock, stats);
      } catch (...) {
        // The fixed tick happened even though its remaining callbacks did
        // not. Advance their EveryN counters without consuming manual or
        // event triggers so tasks on opposite sides of the thrower keep the
        // same cadence phase on later ticks.
        for (++position; position < phase_order_.size(); ++position) {
          advance_aborted_tick_cadence(tasks_[phase_order_[position]]);
        }
        throw;
      }
#else
      run_task_if_due(tasks_[phase_order_[position]], clock, stats);
#endif
    }
    return stats;
  }

  // Same precondition as set_enabled, and the silent fallback was worse
  // here: a default-constructed ScheduleTaskStats is all zeroes, which is
  // exactly what a real, registered task that has never run reports. The
  // caller could not tell "you passed a bad id" from "this task is idle".
  [[nodiscard]] auto task_stats(TaskId id) const noexcept -> ScheduleTaskStats {
    if (id >= tasks_.size()) {
      detail::fail_fast("Schedule::task_stats called with an unknown TaskId");
    }
    return tasks_[id].stats;
  }

  [[nodiscard]] auto task_count() const noexcept -> std::size_t {
    return tasks_.size();
  }

 private:
  struct TaskRecord {
    ScheduleTaskDesc desc{};
    void* ctx = nullptr;
    ScheduleTaskFn fn = nullptr;
    ScheduleNoThrowTaskFn no_throw_fn = nullptr;
    DirtyMask pending_mask{};
    std::uint32_t pending_events = 0;
    std::uint32_t ticks_until_due = 0;
    bool run_requested = false;
    bool in_progress = false;
    bool enabled = true;
    ScheduleTaskStats stats{};
  };

  auto add_task_record(const ScheduleTaskDesc& desc, void* ctx,
                       ScheduleTaskFn fn, ScheduleNoThrowTaskFn no_throw_fn)
      -> TaskId {
    if (sealed_) {
      detail::fail_fast("Schedule::add_task called after seal()");
    }
    if (fn == nullptr && no_throw_fn == nullptr) {
      detail::fail_fast("Schedule::add_task received a null callback");
    }
    if (static_cast<std::uint8_t>(desc.phase) >=
        static_cast<std::uint8_t>(SimPhase::Count)) {
      detail::fail_fast("Schedule::add_task received an invalid SimPhase");
    }
    if (static_cast<std::uint8_t>(desc.cadence.kind) >
        static_cast<std::uint8_t>(CadenceKind::Manual)) {
      detail::fail_fast("Schedule::add_task received an invalid CadenceKind");
    }
    if (desc.cadence.kind == CadenceKind::EveryN && desc.cadence.every_n == 0) {
      detail::fail_fast(
          "Schedule::add_task EveryN cadence requires every_n > 0; use "
          "Cadence::every_ticks() to normalize input");
    }
    if (desc.cadence.kind == CadenceKind::Background &&
        desc.cadence.budget.max_items == 0) {
      detail::fail_fast(
          "Schedule::add_task Background cadence requires a nonzero budget; "
          "use Cadence::background() to normalize input");
    }
    auto record = TaskRecord{};
    record.desc = desc;
    record.ctx = ctx;
    record.fn = fn;
    record.no_throw_fn = no_throw_fn;
    if (record.desc.cadence.every_n == 0) {
      record.desc.cadence.every_n = 1;
    }
    if (record.desc.cadence.budget.max_items == 0) {
      record.desc.cadence.budget.max_items = 1;
    }
    if (desc.cadence.kind == CadenceKind::EveryN) {
      record.ticks_until_due = record.desc.cadence.every_n;
    }
    tasks_.push_back(record);
    return static_cast<TaskId>(tasks_.size() - 1);
  }

  static void advance_aborted_tick_cadence(TaskRecord& task) noexcept {
    if (task.desc.cadence.kind != CadenceKind::EveryN) {
      return;
    }
    if (--task.ticks_until_due == 0) {
      task.ticks_until_due = task.desc.cadence.every_n;
    }
  }

  void run_task_if_due(TaskRecord& task, SimClock clock,
                       ScheduleTickStats& stats) {
    // Cadence bookkeeping advances even while a task is disabled, so
    // re-enabling never shifts the lockstep phase of EveryN tasks;
    // OnDirty/Manual/Background triggers PERSIST across disablement and
    // fire on the first enabled tick.
    auto due = false;
    auto fired_dirty = DirtyMask{};
    auto fired_events = std::uint32_t{0};
    auto budget = std::uint32_t{0};
    switch (task.desc.cadence.kind) {
      case CadenceKind::EveryTick:
        due = true;
        break;
      case CadenceKind::EveryN: {
        // The countdown advances independently of manual pokes, so a
        // request_run never shifts the lockstep phase -- it just adds one
        // extra run.
        const auto counted = --task.ticks_until_due == 0;
        if (counted) {
          task.ticks_until_due = task.desc.cadence.every_n;
        }
        due = counted || task.run_requested;
        break;
      }
      case CadenceKind::OnDirty:
        fired_dirty = task.pending_mask & task.desc.cadence.dirty_mask;
        due = static_cast<bool>(fired_dirty) || task.run_requested;
        break;
      case CadenceKind::OnEvent:
        fired_events = task.pending_events & task.desc.cadence.event_mask;
        due = fired_events != 0 || task.run_requested;
        break;
      case CadenceKind::Background:
        due = task.in_progress || task.run_requested;
        budget = task.desc.cadence.budget.max_items;
        break;
      case CadenceKind::Manual:
        due = task.run_requested;
        break;
    }
    if (!due) {
      return;
    }
    ++stats.tasks_due;
    if (!task.enabled) {
      // EveryN consumed its countdown above (already reset); persistent
      // triggers stay armed for the first enabled tick.
      ++stats.tasks_skipped;
      ++task.stats.skipped;
      return;
    }

    // Consume triggers BEFORE invoking, so anything raised during the run
    // re-arms the task for the next tick instead of being lost.
    task.pending_mask &= ~fired_dirty;
    task.pending_events &= ~fired_events;
#if TESS_HAS_EXCEPTIONS
    const auto consumed_request = task.run_requested;
#endif
    task.run_requested = false;

    auto context = ScheduleTaskContext{};
    context.clock = clock;
    context.pending_dirty = fired_dirty;
    context.budget_items = budget;
    context.pending_events = fired_events;
    auto result = ScheduleTaskResult{};
#if TESS_DIAGNOSTICS_ENABLED
    diagnostics::ScopedTimer task_timer{diagnostics::TraceCategory::Scheduler,
                                        task.desc.name};
#endif
#if TESS_HAS_EXCEPTIONS
    if (task.no_throw_fn != nullptr) {
      result = task.no_throw_fn(task.ctx, context);
    } else {
      try {
        result = task.fn(task.ctx, context);
      } catch (...) {
        // A failed callback did not complete the work represented by its
        // coalesced triggers. Merge rather than assign: the callback may have
        // raised the same or additional triggers before it threw.
        task.pending_mask |= fired_dirty;
        task.pending_events |= fired_events;
        task.run_requested = task.run_requested || consumed_request;
        throw;
      }
    }
#else
    if (task.no_throw_fn != nullptr) {
      result = task.no_throw_fn(task.ctx, context);
    } else {
      result = task.fn(task.ctx, context);
    }
#endif
    if (task.desc.cadence.kind == CadenceKind::Background &&
        result.items_done > budget) {
      detail::fail_fast(
          "Schedule task reported more background items than offered");
    }
    if (task.desc.cadence.kind != CadenceKind::Background &&
        result.items_done != 0) {
      detail::fail_fast(
          "Schedule non-background task reported background items");
    }

    task.in_progress =
        task.desc.cadence.kind == CadenceKind::Background && result.more_work;
    if (result.dirty_mask) {
      // Immediate merge: later-phase OnDirty tasks see it this tick,
      // earlier-phase (and this) tasks next tick. Only OnDirty tasks
      // consume pending_mask, so only they receive it.
      for (const auto id : dirty_task_ids_) {
        tasks_[id].pending_mask |= result.dirty_mask;
      }
      stats.dirty_mask_produced |= result.dirty_mask;
    }
    if (result.event_mask != 0) {
      for (const auto id : event_task_ids_) {
        tasks_[id].pending_events |= result.event_mask;
      }
      stats.event_mask_produced |= result.event_mask;
    }

    ++stats.tasks_run;
    stats.background_items += result.items_done;
    ++task.stats.runs;
    task.stats.background_items += result.items_done;
    task.stats.last_run_tick = clock.tick;
  }

  std::vector<TaskRecord> tasks_;
  // Built at seal(): phase-major dispatch order and trigger subscribers.
  std::vector<TaskId> phase_order_;
  std::vector<TaskId> dirty_task_ids_;
  std::vector<TaskId> event_task_ids_;
  bool sealed_ = false;
  bool in_run_ = false;
};

// Frame -> ticks bridge: consumes real frame time through the accumulator
// (honoring SimSpeed and the per-frame tick cap) and runs the schedule once
// per granted fixed tick. Cadences therefore count FIXED TICKS, never
// frames: an EveryN task at 2x speed fires twice as often in real time and
// exactly as often in sim time, and a backlogged frame that grants several
// ticks advances every cadence through each of them.
/// Summarizes all fixed ticks consumed during one rendered frame.
struct ScheduleFrameSummary {
  std::size_t ticks = 0;
  double alpha = 0.0;
  double dropped_seconds = 0.0;
  // Stats of the LAST tick this frame (zero ticks leaves it default).
  ScheduleTickStats last_tick{};
};

/// Consumes one real-time frame and runs the schedule for every granted tick.
inline auto run_schedule_frame(Schedule& schedule, SimClock& clock,
                               FixedStepAccumulator& accumulator,
                               double real_delta_seconds,
                               SimTimeControl control) -> ScheduleFrameSummary {
  const auto frame = accumulator.consume(real_delta_seconds, control);
  auto summary = ScheduleFrameSummary{};
  summary.ticks = frame.ticks;
  summary.alpha = frame.alpha;
  summary.dropped_seconds = frame.dropped_seconds;
  for (std::size_t i = 0; i < frame.ticks; ++i) {
    summary.last_tick = schedule.run_tick(clock);
  }
  return summary;
}

}  // namespace tess
