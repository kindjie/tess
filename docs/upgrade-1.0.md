# Upgrade to tess 1.0

Version 0.13 is the final breaking-change release before 1.0. Update code to
the forms below before adopting the first 1.0 release candidate.

## Path requests

Path, reachability, coarse-path, precheck, and distance-field APIs now receive
one `PathRequest` instead of adjacent `start` and `goal` arguments.

<!-- tess-snippet: upgrade-path-request source=tests/tess_upgrade_1_0_test.cc -->
```cpp
// Before:
// auto result = tess::reachable<Shape>(graph, start, goal, graph_scratch);

// After:
const auto result = tess::reachable<Shape>(
    graph, tess::PathRequest{start, goal}, graph_scratch);
```
<!-- /tess-snippet -->

Apply the same wrapping to `coarse_path`, `precheck_path`,
`distance_field_path`, and `weighted_distance_field_path`. Direct A* already
used `PathRequest` and does not change.

## Options and handles

Route-cache limits are now named:

<!-- tess-snippet: upgrade-route-cache-limits source=tests/tess_upgrade_1_0_test.cc -->
```cpp
// Before:
// cache.set_caps(max_entries, max_path_nodes);

// After:
cache.set_caps(tess::UnitRouteCacheLimits{max_entries, max_path_nodes});
```
<!-- /tess-snippet -->

Direct path-agent movement also uses a named options object:

<!-- tess-snippet: upgrade-agent-options source=tests/tess_upgrade_1_0_test.cc -->
```cpp
// Before:
// tess::advance_path_agents_with_movement<
//     World, PassableTag, OccupancyTag, ReservationTag>(
//     world, agents, runtime, max_steps, movement_dirty_mask);

// After:
const auto stats =
    tess::advance_path_agents_with_movement<World, PassableTag, OccupancyTag,
                                            ReservationTag>(
        world, agents, runtime,
        tess::PathAgentAdvanceOptions{max_steps, movement_dirty_mask});
```
<!-- /tess-snippet -->

The same `PathAgentAdvanceOptions` migration applies to the joint-movement and
PIBT direct-advance families. For tick APIs, put `movement_dirty_mask` inside
`PathAgentTickOptions` instead of passing a separate integer after the options
argument. `SimSchedulerOptions::movement_dirty_mask` was removed; migrate it to
the nested path-agent options:

<!-- tess-snippet: upgrade-scheduler-agent-options source=tests/tess_upgrade_1_0_test.cc -->
```cpp
// Before:
// tess::SimSchedulerOptions options{
//     .movement_dirty_mask = movement_dirty_mask,
// };

// After:
const auto scheduler_options = tess::SimSchedulerOptions{
    .path_agent_options =
        tess::PathAgentTickOptions{
            .movement_dirty_mask = movement_dirty_mask,
        },
};
```
<!-- /tess-snippet -->

Blocked retry exhaustion no longer claims geometric `NoPath` by default.
`PathAgentTickOptions::blocked_exhaustion_policy` defaults to
`BlockedAgentExhaustionPolicy::RemainBlocked`. Callers that intentionally used
the former timeout-as-terminal policy must select it explicitly:

<!-- tess-snippet: upgrade-blocked-exhaustion-policy source=tests/tess_upgrade_1_0_test.cc -->
```cpp
auto options = tess::PathAgentTickOptions{};
options.blocked_exhaustion_policy =
    tess::BlockedAgentExhaustionPolicy::MarkUnreachable;
```
<!-- /tess-snippet -->

GPU dispatch and readback descriptors now retain one generation-bearing
handle:

<!-- tess-snippet: upgrade-gpu-handle source=tests/tess_upgrade_1_0_test.cc -->
```cpp
// Before:
// tess::gpu::DispatchDesc dispatch{
//     .product_key = handle.key,
//     .product_generation = handle.generation,
// };

// After:
const auto dispatch = tess::gpu::DispatchDesc{.handle = handle};
```
<!-- /tess-snippet -->

The same replacement applies to `ReadbackDesc`.

## Parameter order

Output objects now precede scratch objects, and merge destinations precede
their partition sources:

<!-- tess-snippet: upgrade-parameter-order source=tests/tess_upgrade_1_0_test.cc -->
```cpp
// Before:
// tess::build_distance_field_product<World, PassableTag>(
//     world, goals, field_scratch, product);
// tess::merge_planned_dirty(world, partitions, dirty_scratch);
// tess::collect_render_tile_deltas(world, dirty_mask, render_deltas);

// After:
const auto field = tess::build_distance_field_product<World, PassableTag>(
    world, goals, product, field_scratch);
const auto merged =
    tess::merge_planned_dirty(world, dirty_scratch, partitions);
tess::collect_render_tile_deltas(render_deltas, world, dirty_mask);
```
<!-- /tess-snippet -->

`build_weighted_distance_field_product` and provider-aware field-product
overloads follow the same output-before-scratch order.

## Renamed and removed spellings

- Replace `pipeline.to_frontier(destination)` with the identical
  `pipeline.collect_into(destination)` terminal.
- Replace `trace.category_stats()` with `trace.all_stats()`. Per-category
  lookup remains `trace.stats(category)`. Replace
  `timing_snapshot.category(category)` with
  `timing_snapshot.stats(category)`.
- Replace `FrameOps` with `OperationBatch`. Operation handles and IDs are
  batch-local and remain valid only until that batch is cleared.
- Replace `LocalTopologyResult` with `TopologyBuildResult`. Replace result
  member `version` with the unsigned `topology_version_sum`; it is an
  aggregate comparison value, not a `TopologyVersion`.
- Replace `RouteCacheScratch`, `RouteCacheLimits`, and `RouteCacheStats` with
  `UnitRouteCache`, `UnitRouteCacheLimits`, and `UnitRouteCacheStats`.
- Replace `movement::WalkableField` with
  `movement::UnitCostFieldMovement`, and `movement::WalkableCostField` with
  `movement::PositiveCostFieldMovement`.
- Replace `ChunkVersionDependencies` with `ContentVersionDependencies` and
  generic public chunk-version member names with their `content_version`
  spellings.
- Replace `dirty_flags()` / `active_flags()` with `dirty_mask()` /
  `active_mask()`. `DirtyObservation::{flags, version, residency}` becomes
  `{mask, content_version, residency_generation}`.
- Replace `RenderTileDelta::dirty_flags`, `TileChunkDelta::dirty_flags`, and
  `TileDelta::dirty_flags` with `dirty_mask`; replace the two chunk delta
  records' `chunk_version` members with `content_version`.
- Replace `MovementVersionCheck::{from,to}_chunk_version` with
  `{from,to}_content_version`, and `LocalChunkTopology::version()` with
  `topology_version()`.
- Replace `MovementStatus::BlockedFrom`, `BlockedTo`, and `StaleVersion` with
  `ImpassableFrom`, `ImpassableTo`, and `StaleContent`, including matching
  counters. `MovementStatus::Blocked` now identifies an unavailable
  transition whose endpoints remain passable; missing provider topology is
  `StaleTopology`.
- The smallest queued path example is now `examples/queued_path.cc`; its CMake
  target is `tess_queued_path`.

## Storage metadata and field values

`Field<Tag, Value>` now requires `TileFieldValue<Value>`: an unqualified,
nothrow-default-constructible object that is trivially copyable, trivially
copy assignable, and trivially destructible. Owning strings, vectors,
references, cv-qualified values, and throwing assignment types must live in
application storage rather than per-tile page fields.

Metadata scalars are explicit domain types. Construct application masks as
`DirtyMask{bits}` and `ActiveMask{bits}`; use `.value` only at serialization,
hashing, or external-adapter boundaries. `ContentVersion`, `TopologyVersion`,
and `ResidencyGeneration` are 64-bit non-converting values. Zero is the invalid
residency-generation sentinel, and advancing any version or generation past
its maximum fails fast instead of wrapping.

`ChunkMaintenanceAdapter` follows the same public types. Its owned and marked
masks are `DirtyMask`; `ChunkProductToken::version` is now
`content_version`, and its residency generation is `ResidencyGeneration`.

`ChunkMeta::version` is now `content_version`. `ChunkState` and
`set_chunk_state` are removed: use `world.chunk_activity(key)` to read the
`ChunkActivity` derived from `world.active_mask(key)`. The redundant
`ChunkMeta::active_count` is removed; use
`world.active_category_count(key)`.

Map `ChunkState::ResidentSleeping` to `ChunkActivity::Sleeping` and
`ChunkState::ResidentActive` to `ChunkActivity::Active`. Non-resident state is
represented by a missing chunk, not by `ChunkActivity`.

World archives now use format v2. The chunk prefix stores only chunk key,
active mask, and entity count; activity derives from the active mask. Format
v1 returns `UnsupportedFormat`, so applications that retained pre-1.0 archives
must migrate them with the older library before upgrading.

## Weighted paths and sparse boundaries

Weighted APIs now take one explicit movement class. Remove
`movement::LegacyWeighted` and calls using separate
`<World, PassableTag, CostTag>` template arguments. The usual replacement is
`movement::PositiveCostFieldMovement<PassableTag, CostTag>`. To preserve
cost-agnostic passability deliberately, compose
`movement::MovementClass<movement::Field<PassableTag>,
movement::FieldCost<CostTag>>`.

`MissingChunkPolicy::TreatAsBlocked` and `Indeterminate` are now
`AssumeImpassable` and `ReportIndeterminate`. Public path APIs default to
`ReportIndeterminate`, and runtime, cache, batch, precheck, and path-agent
layers propagate the selected policy. Select `AssumeImpassable` explicitly
only when a policy-relative `NoPath` over the resident graph is acceptable.
`PathStatus::NoPath` no longer claims a whole-world proof when unknown chunks
were assumed impassable.

Default, cleared, stale, or model-mismatched products now report
`PathStatus::NotComputed`; this is never a reachability conclusion. Bounded or
heuristic strategies that do not find a candidate report
`PathStatus::NoCandidate`; run an exact search when an authoritative conclusion
is required.

Two-call distance-field readers retain an `Indeterminate` sparse build: a
reached start may still return `Found`, while an unreached or non-resident start
returns `Indeterminate`. A stale or inconsistent field gradient returns
`NotComputed` rather than manufacturing `NoPath`.

`PathAgentState::status` is now `last_result`, an optional `PathStatus`.
It is absent before any search and after route invalidation. Check the agent's
`PathAgentPhase` for lifecycle state; a present `NoPath` now always records an
actual policy-relative search result.

## Lifetime and identity changes

`DeltaFrame` record spans are private. Use `chunks()`, `tiles()`, `entities()`,
`overlays()`, and `overlay_nodes()`. These accessors and `empty()` fail fast if
the collector publishes, reserves, moves, or is destroyed after producing the
view. Read or copy the records before any invalidating operation.

`ResumableWorkQueue` and `ResumableWorkTask` are no longer copyable or movable.
Construct registered tasks and their queues at their final addresses;
store them behind address-stable ownership when a container would otherwise relocate
them.

Worker-pool nested dispatch, concurrent dispatch on one pool, and reservation
during dispatch now fail fast in every build. Use distinct pool objects for
independent concurrent dispatches and reserve before publishing work.
