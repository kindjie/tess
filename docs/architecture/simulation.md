# Simulation Integration MVP

The current simulation integration layer lives under `include/tess/sim/` and
is exported by `tess/tess.h`. It provides the first colony-sim-facing bridge
over storage, queued operations, path requests, movement validation, and render
deltas.

## Public Surface

### Movement

- `MovementIntent` records one adjacent tile move from `from` to `to` plus
  an optional `MovementVersionCheck`.
- `MovementVersionCheck` carries optional expected `from`/`to` chunk
  versions and `from`/`to` topology versions. Unset fields are not checked.
- `MovementStatus` reports `Moved`, invalid endpoints (`InvalidFrom`,
  `InvalidTo`, `NotAdjacent`), blocked endpoints (`BlockedFrom`,
  `BlockedTo`), `Occupied` or `Reserved` destinations, and stale
  `StaleVersion` / `StaleTopology` version guards.
- `MovementResult` returns the status plus the echoed `from`/`to`
  coordinates.
- `is_transient_movement_failure(status)` classifies failures: blocked,
  occupied, reserved, and stale statuses are transient (the world can
  legitimately change under a routed agent; re-path and retry), while
  invalid endpoints and non-adjacent steps indicate a caller bug and are
  terminal.
- `MovementFailureCounts` aggregates failures into `invalid`, `blocked`,
  `occupied`, `reserved`, `stale_version`, and `stale_topology` buckets;
  `record_movement_failure(counts, status)` maps each non-`Moved` status
  into its bucket.
- `movement_versions_match(world, intent)` checks only the optional version
  guards (chunk versions first, then topology versions) and returns `Moved`
  when every set guard matches. It resolves both endpoints unchecked, so
  callers must validate coordinates first.
- `validate_movement_intent<World, PassableTag, OccupancyTag,
  ReservationTag>(world, intent)` checks shape bounds, six-axis adjacency
  (`manhattan_distance == 1`), passability of both endpoints, destination
  occupancy, destination reservation, and the optional version guards, in
  that order, without mutating the world.
- `commit_movement_intent<World, PassableTag, OccupancyTag, ReservationTag>(
  world, intent, dirty_mask)` validates the same intent, clears source
  occupancy, sets destination occupancy, clears destination reservation, and
  marks source and destination tiles dirty when `dirty_mask` is nonzero.

### Render Deltas

- `RenderTileDelta` records a changed tile coordinate, chunk key, local tile
  id, matching dirty flags, and chunk version.
- `collect_render_tile_deltas(world, dirty_mask, out)` appends one delta per
  dirty tile in each matching chunk dirty bound. On a dense world it scans every
  chunk; on a sparse world it scans only the resident set (a non-resident chunk
  holds no data and cannot be dirty, so this misses no delta and never reads a
  non-resident slot or runs a full `chunk_count` scan).
- `render_tile_deltas(world, dirty_mask)` returns an owning vector of render
  deltas for simple consumers.
- `clear_render_delta_dirty(world, dirty_mask)` clears the render-relevant
  dirty bits after a presentation layer has consumed them; it iterates the
  resident set on a sparse world.

### Path-Agent Batch Helpers

- `PathAgentState` stores an agent's position, goal, `PathTicket`, path
  index, last `PathStatus`, `PathAgentPhase`, active-goal flag, and
  `blocked_retries` count.
- `PathAgentPhase` is the agent lifecycle, decoupled from the last
  `PathStatus`: `Idle` (no goal or arrived), `NeedsPath` (goal assigned, no
  route yet), `Following` (walking a `Found` route), `Blocked` (transient
  failure; re-paths until the retry budget runs out), and `Unreachable`
  (terminal until a new goal is assigned).
- `set_path_agent_goal(agent, goal)` arms the lifecycle (`NeedsPath`, retry
  count reset); `clear_path_agent_goal(agent)` returns the agent to `Idle`.
- `PathAgentFrameStats` counts submitted, completed, found, invalid-start,
  invalid-goal, no-path, and indeterminate results plus `precheck_ruled_out`,
  advanced steps, arrivals, blocked waits, and a `MovementFailureCounts`.
  `precheck_ruled_out` is the number of agents whose goal an optional topology
  precheck proved unreachable before A* (a subset of `no_path`; see the path
  runtime's `precheck_ruled_out`). `add_path_agent_stats(lhs, rhs)` accumulates
  two frames; `record_path_agent_status(stats, status)` buckets one path result.
- `submit_path_agents(agents, runtime)` starts a new runtime request set and
  submits one request per agent with an active goal, skipping `Unreachable`
  agents and clearing agents that already stand on their goal (counted as
  arrived).
- `apply_path_agent_results(agents, runtime)` copies ticketed results back:
  `Found` enters `Following` and resets the retry count; planner failures
  enter `Blocked` so the tick driver's retry budget governs them.
- `advance_path_agents(agents, runtime, max_steps)` walks agents with a
  `Found` result up to `max_steps` nodes along runtime-owned paths without
  touching world fields.
- `advance_path_agents_with_movement<World, PassableTag, OccupancyTag,
  ReservationTag>(world, agents, runtime, max_steps, movement_dirty_mask)`
  commits each step through `commit_movement_intent` (no version guards).
  A transient failure leaves the `Found` route intact, moves the agent to
  `Blocked`, consumes one retry, and counts a blocked wait; a structural
  failure is terminal `Unreachable`. Arrival clears the goal and counts an
  arrival.
- `process_unit_path_agents<World, PassableTag>(...)` and
  `process_weighted_path_agents<World, PassableTag, CostTag, MaxCost>(...)`
  run submit, runtime processing (cached unit or weighted batch), and result
  application as one synchronous pass. Both take an optional trailing
  `const RegionGraphT<World::residency_type>*` (default `nullptr`) that they
  forward to the runtime's precheck gate; when supplied, goals the region graph
  proves unreachable are resolved without A* and surfaced in
  `PathAgentFrameStats::precheck_ruled_out`.

### Path-Agent Tick

- `SimClock` holds the current tick; `advance_sim_tick(clock)` increments
  and returns it.
- `PathAgentTickState` owns the clock plus the `pathing_dirty` flag;
  `mark_pathing_dirty(state)` requests a conservative replan on the next
  tick, and the three-argument `set_path_agent_goal(state, agent, goal)`
  assigns a goal and marks pathing dirty in one call.
- `PathAgentTickOptions` carries `max_steps` per tick, the runtime
  `PathRuntimeCachePolicy`, and `max_blocked_retries` (default 8).
- `PathAgentTickStats` reports the tick value, whether paths were processed,
  separate pathing and movement `PathAgentFrameStats`, and the
  `repaths_requested` / `repath_exhausted` counts.
- `prepare_path_agent_processing(agents, options, stats)` scans agents ahead
  of path processing: `NeedsPath` agents (goals assigned through either
  `set_path_agent_goal` overload) request processing with no manual dirty
  mark, and `Blocked` agents consume one re-path attempt per processed tick
  until the retry budget runs out, at which point they turn terminally
  `Unreachable` with `PathStatus::NoPath`.
- `tick_unit_path_agents<World, PassableTag>(...)`,
  `tick_weighted_path_agents<World, PassableTag, CostTag, MaxCost>(...)`,
  `tick_unit_path_agents_with_movement<World, PassableTag, OccupancyTag,
  ReservationTag>(...)`, and `tick_weighted_path_agents_with_movement<...>`
  advance the clock, re-process paths when `pathing_dirty` is set or any
  agent requested processing, then advance agents — either freely or
  through movement commits with the supplied `movement_dirty_mask`. Each
  accepts an optional trailing `const RegionGraphT<World::residency_type>*`
  (default `nullptr`) forwarded to the runtime precheck gate, so a caller that
  maintains a region graph can skip A* for goals proven unreachable.

### Scheduler

- `SimSchedulerState` owns the scheduler-adjacent state currently needed by
  the path-agent tick layer.
- `SimSchedulerOptions` configures which dirty masks should trigger path
  replanning, which dirty masks should produce render deltas, path-agent tick
  options, whether render dirty bits should be cleared after collection, and
  which movement commits should mark dirty tiles.
- `SimSchedulerStats` reports one tick: the tick value, whether operations
  were planned (`planned_ops`) and executed (`executed_ops`), the queued
  `ExecutionReport` and `PlannedExecutionResult`, the variant's
  `PathAgentTickStats`, and the number of render deltas appended this tick
  (`render_delta_count`).
- `run_queued_operations<World, Policy>(world, ops, fn)` is the shared
  plan-then-execute step: it plans the frame's operations, returns without
  executing when validation fails, and otherwise executes the plan through
  the serial block bridge and reports whether execution completed.
- `tick_unit_scheduler<World, PassableTag, Policy>(...)` executes planned
  queued operations through the existing serial block bridge, marks pathing
  dirty when planned work dirtied configured pathing fields, ticks unit-cost
  path agents, and emits render deltas.
- `tick_unit_movement_scheduler<World, PassableTag, OccupancyTag,
  ReservationTag, Policy>(...)` runs the same sequence and commits agent
  movement through `commit_movement_intent`, marking moved-agent chunks
  dirty with the configured movement dirty mask.
- `tick_weighted_scheduler<World, PassableTag, CostTag, MaxCost, Policy>(...)`
  runs the same sequence through the weighted path-agent batch tick.
- `tick_weighted_movement_scheduler<World, PassableTag, CostTag, MaxCost,
  OccupancyTag, ReservationTag, Policy>(...)` combines the weighted batch
  tick with movement commits and the movement dirty mask.

All four scheduler variants share one internal tick sequence
(`detail::tick_scheduler_core`); they differ only in the path-agent tick
they run. When queued operations fail planning, the tick reports
`planned_ops` without `executed_ops`, leaves the world untouched, and still
ticks path agents.

### Fixed-Step Time

- `SimSpeed` is `Paused`, `Speed1x`, `Speed2x`, or `Speed4x`;
  `SimTimeControl` carries the current speed.
- `sim_speed_multiplier(speed)` returns the integer multiplier (0/1/2/4) and
  `effective_tps(base_tps, speed)` returns the multiplied tick rate,
  saturating at the `std::uint32_t` maximum.
- `FixedStepAccumulator(base_tps, max_ticks_per_frame)` converts variable
  real frame deltas into whole simulation ticks. `consume(delta, control)`
  banks speed-scaled time (paused, zero-tps, and zero-cap configurations
  produce no ticks; NaN and negative deltas contribute nothing) and returns
  a `FixedStepFrame`.
- `FixedStepFrame` reports the `ticks` to run this frame, the
  interpolation `alpha` (fraction of one step still banked, clamped to
  [0, 1]), and `dropped_seconds` — sim-time seconds discarded because the
  frame hit `max_ticks_per_frame` with more than one step of backlog
  remaining. When the tick cap is hit, backlog beyond one step is dropped
  instead of banked: retained debt would force max-tick catch-up frames or
  an unrecoverable spiral, while one step of carry preserves alpha
  continuity. Sim time slows instead, and a nonzero `dropped_seconds`
  means the simulation is running behind real time.

The scheduler does not consume `FixedStepAccumulator` itself; callers use
it to decide how many scheduler ticks to run in one rendered frame.

## Behavior

The scheduler is deterministic and synchronous. It does not own worker
threads, async handles, or an event loop. Callers still own entity storage,
game-specific job logic, AI decisions, UI state, and content rules.

The intended per-frame order for current consumers is:

1. Enqueue field edits in `FrameOps` with accurate `FieldAccessDesc` masks.
2. Call a scheduler tick with a callback that applies each planned chunk view.
3. Let the scheduler mark pathing dirty when executed operations dirtied
   configured movement-relevant fields.
4. Let the path-agent tick submit/process active requests only when dirty.
5. Consume render deltas from dirty chunk bounds instead of full snapshots.
6. Commit accepted movement intents through `commit_movement_intent` when a
   game system needs occupancy and reservation validation.

The `PathAgentPhase` lifecycle ties the layers together. Assigning a goal
arms `NeedsPath`, which requests path processing on the next tick even
without a world edit. Planner failures and transient movement failures both
land in `Blocked`; each processed tick consumes one of
`max_blocked_retries` until the agent either finds a route (`Following`,
retries reset) or exhausts the budget and turns terminally `Unreachable`.
Structural movement failures (invalid endpoints, non-adjacent steps) skip
the retry budget entirely. Only a new goal re-arms an `Unreachable` agent.

`MovementIntent` version guards are opt-in. They are useful when an external
system collected path or move intents before queued world edits were applied.
If a stored expected chunk version or topology version no longer matches, the
move fails with `StaleVersion` or `StaleTopology` before occupancy changes are
committed. The scheduler's own movement ticks submit intents without version
guards; their steps are validated against live world state instead.

Render deltas are based on current chunk dirty bounds. If multiple dirty masks
share a chunk, the current dirty bound is the union maintained by storage. A
caller that needs per-field exact rectangles should keep its own field-level
presentation data or drain deltas before broadening the chunk dirty bound with
unrelated edits. Collection clips each chunk's dirty bounds to the chunk's
own world-space box before visiting tiles, so bounds that span chunk borders
or leave the shape emit deltas only for tiles the chunk owns.

## Deliberate Limits

This slice is still a synchronous MVP. It does not implement async execution,
worker scheduling, persistent queued kernels, result channels, ECS storage
adapters, local avoidance, multi-agent collision resolution, permission
layers, doors, vertical transition policies, topology-aware route planning, or
region-selective path cache invalidation.

Movement validation currently uses a boolean-like passability field plus
boolean-like occupancy and reservation fields. Weighted terrain remains part of
path selection, not movement commit validation. Games with doors, factions,
construction phases, vehicles, or multi-tile entities should layer those rules
around this narrow helper until the movement vocabulary is expanded.
