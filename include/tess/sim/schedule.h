#pragma once

#include <tess/core/assert.h>
#include <tess/sim/time.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

// The M5 schedule: ordered phases of type-erased tasks driven by cadences
// that are pure functions of the fixed-tick counter and per-task pending
// dirty masks. The schedule itself never touches a world -- dirty bits are
// FED to it (by task results and notify_dirty) -- so "no hidden full-world
// scans" holds by construction. World-typed work lives in task objects the
// caller owns and registers by reference; type erasure is a function
// pointer plus a context pointer (no std::function, no allocation on
// dispatch).
//
// Threading: a Schedule is externally synchronized like every tess scratch.
// notify_dirty and request_run are frame-owner-thread calls and must never
// be made from queued-operation callbacks (those may run on pool workers);
// worker-produced dirty flows exclusively through the task-result mask.
namespace tess {

enum class CadenceKind : std::uint8_t {
  EveryTick,
  EveryN,
  OnDirty,
  Background,
  Manual,
};

// Deterministic background bound: a due background task is offered at most
// max_items work units per run and reports how many it consumed plus
// whether work remains. There is deliberately no wall-clock budget in v1 --
// a time valve would make tick outcomes nondeterministic, and every
// consumer bound is expressible in items; it returns with its first real
// consumer.
struct BackgroundBudget {
  std::uint32_t max_items = 1;
};

struct Cadence {
  CadenceKind kind = CadenceKind::EveryTick;
  std::uint32_t every_n = 1;
  std::uint32_t dirty_mask = 0;
  BackgroundBudget budget{};

  [[nodiscard]] static constexpr auto every_tick() noexcept -> Cadence {
    return Cadence{};
  }

  [[nodiscard]] static constexpr auto every_ticks(std::uint32_t n) noexcept
      -> Cadence {
    return Cadence{CadenceKind::EveryN, n == 0 ? 1u : n, 0, {}};
  }

  [[nodiscard]] static constexpr auto on_dirty(std::uint32_t mask) noexcept
      -> Cadence {
    return Cadence{CadenceKind::OnDirty, 1, mask, {}};
  }

  [[nodiscard]] static constexpr auto background(
      BackgroundBudget budget) noexcept -> Cadence {
    return Cadence{
        CadenceKind::Background, 1, 0,
        BackgroundBudget{budget.max_items == 0 ? 1u : budget.max_items}};
  }

  [[nodiscard]] static constexpr auto manual() noexcept -> Cadence {
    return Cadence{CadenceKind::Manual, 1, 0, {}};
  }
};

// Fixed phase list, executed in declaration order every tick. Tasks run in
// registration order within a phase. The set matches the simulation TDD's
// phase vocabulary; custom phase lists are deferred until a consumer needs
// one.
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

struct ScheduleTaskContext {
  SimClock clock{};
  // OnDirty: the bits (within the task's own mask) that made it due; they
  // are consumed before the task runs, so bits raised DURING the run re-arm
  // it for the next tick.
  std::uint32_t pending_dirty = 0;
  // Background: the item budget for this run.
  std::uint32_t budget_items = 0;
};

struct ScheduleTaskResult {
  // Dirty bits this run produced; the schedule merges them into every
  // OnDirty task's pending mask immediately, so later-phase tasks can fire
  // in the same tick and earlier-phase tasks fire next tick.
  std::uint32_t dirty_mask = 0;
  // Background: work units consumed (at most the offered budget).
  std::uint32_t items_done = 0;
  // Background: true keeps the task due next tick without a new trigger.
  bool more_work = false;
};

using ScheduleTaskFn = ScheduleTaskResult (*)(void* ctx,
                                              const ScheduleTaskContext&);

struct ScheduleTaskDesc {
  // Static-storage label (same rule as diagnostics trace labels).
  std::string_view name;
  SimPhase phase = SimPhase::PreUpdate;
  Cadence cadence{};
};

struct ScheduleTaskStats {
  std::uint64_t runs = 0;
  // Ticks on which the task was due but disabled.
  std::uint64_t skipped = 0;
  std::uint64_t background_items = 0;
  std::uint64_t last_run_tick = 0;
};

struct ScheduleTickStats {
  std::uint64_t tick = 0;
  std::uint32_t tasks_due = 0;
  std::uint32_t tasks_run = 0;
  std::uint32_t tasks_skipped = 0;
  std::uint32_t background_items = 0;
  // Union of every task result's dirty mask this tick.
  std::uint32_t dirty_mask_produced = 0;
};

class Schedule {
 public:
  using TaskId = std::uint32_t;

  // Setup-time capacity; add_task within it never reallocates, and run_tick
  // never allocates at all.
  void reserve_tasks(std::size_t count) { tasks_.reserve(count); }

  auto add_task(const ScheduleTaskDesc& desc, void* ctx, ScheduleTaskFn fn)
      -> TaskId {
    TESS_ASSERT(!sealed_);
    TESS_ASSERT(fn != nullptr);
    TESS_ASSERT(desc.phase != SimPhase::Count);
    auto record = TaskRecord{};
    record.desc = desc;
    record.ctx = ctx;
    record.fn = fn;
    if (desc.cadence.kind == CadenceKind::EveryN) {
      record.ticks_until_due = desc.cadence.every_n;
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

  // Freezes registration. Tasks already run in phase-major, then
  // registration order without reordering storage, so TaskIds stay stable.
  void seal() noexcept { sealed_ = true; }

  [[nodiscard]] auto sealed() const noexcept -> bool { return sealed_; }

  void set_enabled(TaskId id, bool enabled) noexcept {
    TESS_ASSERT(id < tasks_.size());
    if (id < tasks_.size()) {
      tasks_[id].enabled = enabled;
    }
  }

  // Arms the task to be due on the next run_tick regardless of cadence --
  // the Manual trigger, and the initial trigger for Background tasks.
  void request_run(TaskId id) noexcept {
    TESS_ASSERT(id < tasks_.size());
    if (id < tasks_.size()) {
      tasks_[id].run_requested = true;
    }
  }

  // Merges external dirty bits into every task's pending mask (OnDirty
  // tasks fire when their own mask bits are present; foreign bits sit
  // inert). Frame-owner thread only; never call from an op callback.
  void notify_dirty(std::uint32_t mask) noexcept {
    for (auto& task : tasks_) {
      task.pending_mask |= mask;
    }
  }

  auto run_tick(SimClock& clock) -> ScheduleTickStats {
    TESS_ASSERT(sealed_);
    auto stats = ScheduleTickStats{};
    stats.tick = advance_sim_tick(clock);

    for (std::uint8_t phase = 0;
         phase < static_cast<std::uint8_t>(SimPhase::Count); ++phase) {
      for (auto& task : tasks_) {
        if (static_cast<std::uint8_t>(task.desc.phase) != phase) {
          continue;
        }
        run_task_if_due(task, clock, stats);
      }
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
    auto budget = std::uint32_t{0};
    switch (task.desc.cadence.kind) {
      case CadenceKind::EveryTick:
        due = true;
        break;
      case CadenceKind::EveryN:
        if (--task.ticks_until_due == 0) {
          task.ticks_until_due = task.desc.cadence.every_n;
          due = true;
        }
        break;
      case CadenceKind::OnDirty:
        fired_dirty = task.pending_mask & task.desc.cadence.dirty_mask;
        due = fired_dirty != 0 || task.run_requested;
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
    task.run_requested = false;

    auto context = ScheduleTaskContext{};
    context.clock = clock;
    context.pending_dirty = fired_dirty;
    context.budget_items = budget;
    const auto result = task.fn(task.ctx, context);
    TESS_ASSERT(task.desc.cadence.kind != CadenceKind::Background ||
                result.items_done <= budget);

    task.in_progress =
        task.desc.cadence.kind == CadenceKind::Background && result.more_work;
    if (result.dirty_mask != 0) {
      // Immediate merge: later-phase OnDirty tasks see it this tick,
      // earlier-phase (and this) tasks next tick.
      for (auto& other : tasks_) {
        other.pending_mask |= result.dirty_mask;
      }
      stats.dirty_mask_produced |= result.dirty_mask;
    }

    ++stats.tasks_run;
    stats.background_items += result.items_done;
    ++task.stats.runs;
    task.stats.background_items += result.items_done;
    task.stats.last_run_tick = clock.tick;
  }

  std::vector<TaskRecord> tasks_;
  bool sealed_ = false;
};

// Frame -> ticks bridge: consumes real frame time through the accumulator
// (honoring SimSpeed and the per-frame tick cap) and runs the schedule once
// per granted fixed tick. Cadences therefore count FIXED TICKS, never
// frames: an EveryN task at 2x speed fires twice as often in real time and
// exactly as often in sim time, and a backlogged frame that grants several
// ticks advances every cadence through each of them.
struct ScheduleFrameSummary {
  std::size_t ticks = 0;
  double alpha = 0.0;
  double dropped_seconds = 0.0;
  // Stats of the LAST tick this frame (zero ticks leaves it default).
  ScheduleTickStats last_tick{};
};

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
