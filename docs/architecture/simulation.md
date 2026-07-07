# Simulation Integration MVP

The current simulation integration layer lives under `include/tess/sim/` and
is exported by `tess/tess.h`. It provides the first colony-sim-facing bridge
over storage, queued operations, path requests, movement validation, and render
deltas.

## Public Surface

- `MovementIntent` records one adjacent tile move from `from` to `to` plus
  optional expected chunk and topology versions.
- `MovementStatus` reports `Moved`, invalid endpoints, non-adjacent moves,
  blocked endpoints, occupied or reserved destinations, and stale chunk or
  topology versions.
- `validate_movement_intent<World, PassableTag, OccupancyTag,
  ReservationTag>(world, intent)` checks shape bounds, six-axis adjacency,
  passability, destination occupancy, destination reservation, and optional
  version guards without mutating the world.
- `commit_movement_intent<World, PassableTag, OccupancyTag, ReservationTag>(
  world, intent, dirty_mask)` validates the same intent, clears source
  occupancy, sets destination occupancy, clears destination reservation, and
  marks source and destination tiles dirty when `dirty_mask` is nonzero.
- `RenderTileDelta` records a changed tile coordinate, chunk key, local tile
  id, matching dirty flags, and chunk version.
- `collect_render_tile_deltas(world, dirty_mask, out)` appends one delta per
  dirty tile in each matching chunk dirty bound.
- `render_tile_deltas(world, dirty_mask)` returns an owning vector of render
  deltas for simple consumers.
- `clear_render_delta_dirty(world, dirty_mask)` clears the render-relevant
  dirty bits after a presentation layer has consumed them.
- `SimSchedulerState` owns the scheduler-adjacent state currently needed by
  the path-agent tick layer.
- `SimSchedulerOptions` configures which dirty masks should trigger path
  replanning, which dirty masks should produce render deltas, path-agent tick
  options, whether render dirty bits should be cleared after collection, and
  which movement commits should mark dirty tiles.
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

`MovementIntent` version guards are opt-in. They are useful when an external
system collected path or move intents before queued world edits were applied.
If a stored expected chunk version or topology version no longer matches, the
move fails with `StaleVersion` or `StaleTopology` before occupancy changes are
committed.

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
