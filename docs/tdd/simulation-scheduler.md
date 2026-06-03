# TDD: Simulation Scheduler

## 1. Summary

This document defines the fixed-tick simulation scheduler.

The scheduler decides which systems are due on each simulation tick and emits queued operations into FrameOps. It separates simulation TPS from render FPS and supports different subsystem cadences, dirty/event-triggered work, budgeted background work, and variable simulation speed.

## 2. Goals

- Support fixed timestep simulation.
- Allow systems to run at different tick cadences.
- Support dirty-triggered and event-triggered work.
- Support background/budgeted tasks.
- Preserve deterministic phase ordering where requested.
- Integrate with queued operations and result consumption.
- Avoid requiring every system to run every tick.
- Keep render FPS independent of simulation TPS.
- Support speed multipliers such as pause, 1x, 2x, and 5x.

## 3. Non-goals

- No renderer frame scheduler.
- No game-specific pawn AI.
- No ECS ownership.
- No direct tile iteration as main scheduling mechanism.
- No guarantee background work completes in one tick.
- No dynamic runtime world dimensions.

## 4. Core concepts

- SimClock
- Schedule
- ScheduledTask
- Cadence
- Phase
- FrameOps
- ExecutionReport

## 5. Tick model

Simulation advances in fixed ticks. Rendering may run faster or slower.

```cpp
accumulator += real_dt * sim_speed;

while (accumulator >= fixed_dt && ticks_this_frame < max_ticks_per_frame) {
  sim_tick();
  accumulator -= fixed_dt;
}

alpha = accumulator / fixed_dt;
render(alpha);
```

The fixed timestep does not change at high speed. Higher speed processes more fixed ticks per real second.

## 6. Variable simulation speed

Supported examples:

- paused: 0x
- normal: 1x
- fast: 2x
- very fast: 5x

Rules:

- fixed_dt remains constant
- cadences remain tick-based
- every_ticks<5> means every 5 simulation ticks
- deterministic mode is preserved if ordering and budgets are deterministic
- background/noncritical work may be throttled at high speed

Overload policy when requested speed cannot keep up:

- clamp max ticks per render frame
- degrade to slower effective speed
- skip/render less often
- pause background work
- reduce noncritical scheduled tasks
- enter catch-up mode

## 7. Scheduler flow

Each simulation tick:

1. Advance SimClock.
2. Determine due tasks.
3. Due tasks inspect ECS/world state as needed.
4. Tasks enqueue operations into FrameOps.
5. Planner/executor runs queued operations.
6. Results are committed.
7. Result consumers update ECS/game state.
8. Render deltas are published.
9. Background/deferred tickets are retained or advanced.

## 8. Cadence types

- Every tick
- Every N ticks
- When dirty
- Event-triggered
- Background
- Manual

## 9. Phases

Default phases:

- Input
- PreUpdate
- AI
- Pathing
- Movement
- Commit
- Topology
- Fields
- Background
- RenderDelta
- Diagnostics

Phases are ordered. Tasks within deterministic phases use stable ordering.

## 10. Task declaration

A scheduled task declares:

- name
- phase
- cadence
- priority
- budget policy
- dependencies
- dirty masks/events
- deterministic requirements
- whether it can be skipped/deferred
- result consumption hook

## 11. Result consumption

Tasks may have producer and consumer phases. Producers emit queued ops before execute. Consumers read ExecutionReport after execute.

## 12. Dirty/event triggered scheduling

Dirty tasks subscribe to masks or versions. Event tasks subscribe to tick-stamped event streams. Dirty work should be coalesced and should enqueue precise domains rather than full scans.

## 13. Background scheduling

Background tasks have budgets and continuation state. Gameplay-critical work should not be blocked by background tasks.

## 14. Sparse chunk interaction

Tasks declare whether they operate on active, resident, sleeping, generated metadata, visible chunks, or chunks near entities/query bounds.

## 15. Sleeping chunks

Sleeping chunks do not participate in regular expensive simulation.

On wake, systems may apply analytic catch-up based on elapsed ticks and mark fields dirty.

## 16. Determinism

Deterministic mode requires stable task ordering, operation ordering, chunk ordering, reductions, tie-breaking, deterministic budgets, and no nondeterministic gameplay-affecting async commits.

## 17. Budget model

Budgets can be wall-clock, operation count, node expansions, chunks, bytes, jobs, or deterministic work units.

Gameplay-affecting tasks should prefer deterministic budgets.

## 18. API sketch

```cpp
class Schedule {
public:
  template<class Task> TaskBuilder every_tick();
  template<uint32_t N, class Task> TaskBuilder every_ticks();
  template<class Mask, class Task> TaskBuilder when_dirty();
  template<class Event, class Task> TaskBuilder on_event();
  template<class Task> TaskBuilder background();
};
```

## 19. Diagnostics

Report due/skipped/deferred tasks, background progress, enqueue/consume time, ops emitted, dirty triggers, event triggers, budget exhaustion, sim speed, ticks processed per render frame, and catch-up/overload behavior.

## 20. Tests

- every_tick
- every_N
- dirty task coalescing
- event task
- background budget
- phase ordering
- result consumption
- sim speed 0x/1x/2x/5x
- overload policy
- deterministic ordering

## 21. Benchmarks

- 10/100/1000 tasks
- cadence checks
- dirty/event dispatch
- background continuation
- phase sorting
- deterministic overhead
- high-speed tick processing

## 22. Acceptance criteria

- Scheduler runs fixed-TPS simulation independent of rendering.
- Systems can run at varied cadences.
- Scheduler emits queued operations rather than direct tile loops.
- Result consumption happens at explicit phases.
- Variable sim speed works by processing more fixed ticks, not larger ticks.
- Background tasks can be budgeted and resumed.
- Diagnostics explain due/skipped/deferred work.
