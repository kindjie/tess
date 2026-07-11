# ECS Integration

The ECS layer (M10) binds path agents to entity-component stores without
tying the core to any ECS library. `include/tess/ecs/adapter.h` is the
dependency-free, always-on layer exported by `tess/tess.h`; concrete
adapters (the EnTT adapter under `include/tess/ecs/entt/`, or a game's
own) implement its concepts and reuse its components, batch scratch,
occupancy index, and tick pipeline.

The seam is "agents in deterministic order in, state write-back out".
Request submission, tickets, retry budgets, and exactly-once result
application stay inside the `PathAgentState` lifecycle
(`sim/path_agent.h`); adapters copy agent state into a batch, let the
tess pipeline mutate it, and mirror it back. They never re-implement the
lifecycle.

## Public Surface

- `EntityHandle` is the opaque, ECS-agnostic entity identity: a 64-bit
  value with `is_null()` and equality, null by default
  (`kNullEntityHandle`). Adapters pack their native id (including
  generation/version bits) into it losslessly; tess never interprets it.
- `EntityHandleAdapter<A>` requires `A::entity_type` plus noexcept
  `to_handle(entity)` / `to_entity(handle)` conversions that map the
  ECS's null entity to `kNullEntityHandle` and back.
- `PositionAdapter<A, Entity>` reads and writes a game-defined position
  component as tile coordinates: `position(entity) -> Coord3` and
  `set_position(entity, coord)`. Games own their position representation.
- `PathAgentSource<S>` fills a `PathAgentBatch` via
  `collect(batch) -> PathAgentCollectInfo` in a deterministic order of
  the source's choosing. The order must be stable across storage-packing
  history and replays: sort by a monotonic spawn id (`AgentId`), never by
  native entity value or pool order. `PathAgentCollectInfo` reports the
  collected `count` and `pathing_dirty` (true iff any goal was armed or
  re-armed; goal clears do not set it, since cleared agents are skipped
  by every processing pass).
- `PathAgentSink<S>` writes batch state back to the ECS via
  `apply(batch)`. Sinks are idempotent mirrors: exactly-once result
  application lives in the tess tick pipeline behind the pathing-dirty
  gate, never in the sink.
- Shared POD components, reusable by any ECS: `AgentId` (monotonic
  per-spawn id, never recycled -- the deterministic sort key; persist the
  minting counter alongside the world for replay determinism across
  save/load), `TilePosition` (ECS-visible mirror of the agent's current
  tile, written by sinks), `PathGoal` (pathing input; presence means
  "want to be there"), `PathState` (the full `PathAgentState` lifecycle
  as one component -- deliberately not decomposed, because its fields
  form one invariant unit games must never mutate directly), and
  `OffBoard` (tag for parked agents that hold no tile, claim no
  occupancy, and are excluded from collection until placed back).
- `PathAgentBatch` is the per-tick SoA scratch: `reserve`, `clear`,
  `push(handle, agent)`, `size`, and lockstep `agents()` / `handles()`
  spans. Reserve once; clear-and-refill per tick is allocation-free once
  warm.
- `TileOccupancyIndex` is the injective tile-to-entity occupancy map, the
  ECS-side mirror of a bool occupancy field: `reserve` (load factor at
  most 0.5), `insert(tile, entity)` (refuses, without mutating, a tile
  already mapped to a different entity -- uniqueness is structural),
  `erase(tile)` (returns the erased handle; backward-shift deletion, no
  tombstones), `move(from, to, entity)` (the movement-commit hot path,
  debug-asserted, never rehashes), `entity_at(tile)`, `size`, and
  `clear`. Steady state performs no allocation; growth beyond the
  reserved capacity rehashes as a setup-time event. Box/radius queries
  and per-chunk buckets are deferred post-v1: an open-addressing table
  answers area queries only by probing every coordinate in the box, which
  is not a useful spatial query, and `entity_at` is the primitive
  consumers need.
- `advance_path_agents_with_index<World, ClassOrTag, OccupancyTag,
  ReservationTag>(world, batch, runtime, index, max_steps, dirty_mask)`
  runs `advance_path_agents_with_movement` over the batch with the commit
  observer keeping `index` synchronized: every successful commit moves
  the committing agent's mapping, failed validations touch neither the
  world nor the index, and arrivals need no extra work because the
  arrival step itself was a commit.
- `tick_ecs_unit_path_agents<World, ClassOrTag, OccupancyTag,
  ReservationTag>(state, world, source, sink, batch, runtime, index,
  options, dirty_mask, graph)` is the ECS-agnostic full tick:
  `source.collect`, dirty-gated path processing (the exactly-once seam,
  identical to the `tick_*` drivers), index-synchronized movement, then
  `sink.apply`. `tick_ecs_path_agents` provides the weighted
  movement-class form (`<World, Class, MaxCost, ...>`) and the legacy
  passable/cost tag-pair form (`<World, PassableTag, CostTag, MaxCost,
  ...>`).

## Invariants

- Index/position sync: after any pipeline entry point returns,
  `index.entity_at(agent.position)` is the agent's handle for every
  on-board agent in the batch.
- One-directional field mirror: a tile mapped in the index implies the
  occupancy field is set. The converse is a consumer-side property only
  -- games may set occupancy from non-agent sources the index never
  sees.
- Occupancy uniqueness is structural: `insert` refuses occupied tiles
  and `commit_movement_intent` rejects occupied destinations, so the
  index stays injective without advisory checks.
- Exactly-once application: `apply_path_agent_results` runs only inside
  the dirty-gated processing step. Sinks mirror state and never re-read
  results.

## Runtime Exclusivity

The `PathRequestRuntime` passed to the ECS tick must be exclusive to that
agent system. Tickets persist inside collected `PathState` components
across non-processing ticks and are generation-checked; any other
submitter calling `clear_requests` (or processing its own batch through
the same runtime) between the agent system's processed ticks stales every
Following agent's ticket -- asserted in debug builds, silently stalled
movement in release. One runtime per (world, movement class, agent
system), extending the standing one-runtime-per-(world, class) contract.

## Known Cost

Any processing tick re-paths every active agent: `submit_path_agents`
resubmits all agents with goals into a fresh generation. This is the
standing lifecycle behavior, not an adapter property, but ECS-scale agent
counts make it the relevant cost cliff -- one goal change among 100k
Following agents re-paths all of them on that tick. The ecs benchmark
family tracks collect/apply/tick costs at 1k-100k agents.
