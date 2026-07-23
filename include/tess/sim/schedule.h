#pragma once

#include <tess/core/assert.h>
#include <tess/sim/event_stream.h>
#include <tess/sim/time.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

// The M5 schedule: ordered phases of type-erased tasks driven by cadences
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
// and set_enabled (field writes on stable storage, with the documented
// immediate-merge semantics). They must NOT call add_task or run_tick --
// registration after seal() would reallocate the task array mid-iteration
// and a nested tick would double-advance every cadence; both are asserted.
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
  std::uint32_t dirty_mask = 0;
  BackgroundBudget budget{};
  std::uint32_t event_mask = 0;

  [[nodiscard]] static constexpr auto every_tick() noexcept -> Cadence {
    return Cadence{};
  }

  [[nodiscard]] static constexpr auto every_ticks(std::uint32_t n) noexcept
      -> Cadence {
    return Cadence{CadenceKind::EveryN, n == 0 ? 1u : n, 0, {}, 0};
  }

  [[nodiscard]] static constexpr auto on_dirty(std::uint32_t mask) noexcept
      -> Cadence {
    return Cadence{CadenceKind::OnDirty, 1, mask, {}, 0};
  }

  [[nodiscard]] static constexpr auto on_event(std::uint32_t mask) noexcept
      -> Cadence {
    return Cadence{CadenceKind::OnEvent, 1, 0, {}, mask};
  }

  [[nodiscard]] static constexpr auto background(
      BackgroundBudget budget) noexcept -> Cadence {
    return Cadence{
        CadenceKind::Background, 1, 0,
        BackgroundBudget{budget.max_items == 0 ? 1u : budget.max_items}, 0};
  }

  [[nodiscard]] static constexpr auto manual() noexcept -> Cadence {
    return Cadence{CadenceKind::Manual, 1, 0, {}, 0};
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
  std::uint32_t pending_dirty = 0;
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
  std::uint32_t dirty_mask = 0;
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
  std::uint32_t dirty_mask_produced = 0;
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
    tasks_.reserve(count);
    phase_order_.reserve(count);
    dirty_task_ids_.reserve(count);
    event_task_ids_.reserve(count);
  }

  auto add_task(const ScheduleTaskDesc& desc, void* ctx, ScheduleTaskFn fn)
      -> TaskId {
    TESS_ASSERT(!sealed_);
    TESS_ASSERT(fn != nullptr);
    TESS_ASSERT(desc.phase != SimPhase::Count);
    // Cadence is a public aggregate, so hand-built descriptors can bypass
    // the factory clamps; re-clamp here or a zero EveryN would wrap its
    // countdown to ~4.29B ticks and a zero background budget would spin
    // in_progress forever with no progress.
    TESS_ASSERT(desc.cadence.kind != CadenceKind::EveryN ||
                desc.cadence.every_n != 0);
    TESS_ASSERT(desc.cadence.kind != CadenceKind::Background ||
                desc.cadence.budget.max_items != 0);
    auto record = TaskRecord{};
    record.desc = desc;
    record.ctx = ctx;
    record.fn = fn;
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

  // Registers a task OBJECT the caller owns; `task` must outlive the
  // schedule. T is any callable taking the context and returning a result.
  template <typename T>
  auto add_task(const ScheduleTaskDesc& desc, T& task) -> TaskId {
    return add_task(
        desc, static_cast<void*>(&task),
        [](void* ctx, const ScheduleTaskContext& context)
            -> ScheduleTaskResult { return (*static_cast<T*>(ctx))(context); });
  }

  // Freezes registration and builds the dispatch indexes: phase_order_
  // (phase-major, registration-stable -- the order run_tick always had,
  // now one pass instead of SimPhase::Count passes over every task) and
  // dirty/event subscription indexes. Trigger merges stop writing tasks that
  // never read the corresponding value. Storage is never reordered, so
  // TaskIds stay stable. A contract-
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

  void set_enabled(TaskId id, bool enabled) noexcept {
    TESS_ASSERT(id < tasks_.size());
    if (id < tasks_.size()) {
      tasks_[id].enabled = enabled;
    }
  }

  // Arms the task to be due on the next run_tick regardless of cadence --
  // the Manual trigger, and the initial trigger for Background tasks. An
  // OnDirty task poked this way runs with pending_dirty == 0: treat a
  // zero mask as a full-run request, not a no-op.
  void request_run(TaskId id) noexcept {
    TESS_ASSERT(id < tasks_.size());
    if (id < tasks_.size()) {
      tasks_[id].run_requested = true;
    }
  }

  // Merges external dirty bits into the pending masks that can consume
  // them (only OnDirty cadences read pending_mask; foreign bits within an
  // OnDirty task's mask sit inert). Frame-owner thread only; never call
  // from an op callback.
  void notify_dirty(std::uint32_t mask) noexcept {
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
    TESS_ASSERT(sealed_);
    TESS_ASSERT(!in_run_);
    // Scope guard rather than a trailing store: a throwing task callback
    // must not leave the schedule latched "in run", or every subsequent
    // tick would fail the reentrancy assert (audit 2026-07-11 C2).
    struct InRunGuard {
      bool& flag;
      ~InRunGuard() { flag = false; }
    };
    in_run_ = true;
    const InRunGuard guard{in_run_};
    auto stats = ScheduleTickStats{};
    stats.tick = advance_sim_tick(clock);

    for (const auto id : phase_order_) {
      run_task_if_due(tasks_[id], clock, stats);
    }
    return stats;
  }

  [[nodiscard]] auto task_stats(TaskId id) const noexcept -> ScheduleTaskStats {
    TESS_ASSERT(id < tasks_.size());
    if (id >= tasks_.size()) {
      return ScheduleTaskStats{};
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
    std::uint32_t pending_mask = 0;
    std::uint32_t pending_events = 0;
    std::uint32_t ticks_until_due = 0;
    bool run_requested = false;
    bool in_progress = false;
    bool enabled = true;
    ScheduleTaskStats stats{};
  };

  void run_task_if_due(TaskRecord& task, SimClock clock,
                       ScheduleTickStats& stats) {
    // Cadence bookkeeping advances even while a task is disabled, so
    // re-enabling never shifts the lockstep phase of EveryN tasks;
    // OnDirty/Manual/Background triggers PERSIST across disablement and
    // fire on the first enabled tick.
    auto due = false;
    auto fired_dirty = std::uint32_t{0};
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
        due = fired_dirty != 0 || task.run_requested;
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
    task.run_requested = false;

    auto context = ScheduleTaskContext{};
    context.clock = clock;
    context.pending_dirty = fired_dirty;
    context.budget_items = budget;
    context.pending_events = fired_events;
    const auto result = task.fn(task.ctx, context);
    TESS_ASSERT(task.desc.cadence.kind != CadenceKind::Background ||
                result.items_done <= budget);

    task.in_progress =
        task.desc.cadence.kind == CadenceKind::Background && result.more_work;
    if (result.dirty_mask != 0) {
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
