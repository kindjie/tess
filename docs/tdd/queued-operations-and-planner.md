# TDD: Queued Operations and Planner

## 1. Summary

This document defines the queued operation system: the primary game-facing interface for submitting tile, path, topology, movement, field, residency, render-delta, and query intents.

Game code submits operations into `FrameOps`. The planner validates those operations, batches compatible work, selects algorithms, builds an execution graph, and executes within deterministic phase and budget constraints.

## 2. Goals

- Provide the primary public API for game systems.
- Batch path, nearest-target, field, topology, movement, and residency work.
- Validate read/write hazards.
- Make domains explicit to avoid accidental full-world scans.
- Choose path algorithms: cached path, A*, distance field, corridor field.
- Schedule chunk/block work in locality-preserving order.
- Insert barriers and commit phases.
- Support fixed tick scheduling and background budgets.
- Produce useful diagnostics explaining planner decisions.
- Support future GPU backend selection without requiring GPU implementation in v1.

## 3. Non-goals

- No runtime world dimensions.
- No direct ownership of ECS state.
- No renderer/presentation ownership.
- No hidden full-world materialization.
- No arbitrary callbacks as the primary API.
- No GPU execution required in v1.
- No direct structural ECS mutation during planning/execution.

## 4. Core concepts

- FrameOps
- Operation
- Domain
- Planner
- ExecutionPlan
- Executor
- ExecutionReport
- ResultChannel

## 5. User-facing API style

Game systems submit intent:

```cpp
ops.query_paths(...);
ops.query_nearest(...);
ops.move_entities(...);
ops.update_field<TemperatureStep>();
ops.rebuild_topology(...);
ops.ensure_resident(...);
ops.publish_render_deltas(...);
```

Direct iteration remains available through expert block APIs, not as the normal path.

## 6. Operation categories

- mutations: terrain, building, passability, occupancy, dirty masks
- queries: path, nearest, reachability, radius/region
- field kernels
- derived products
- residency operations
- render delta publication

## 7. Operation declaration

Every operation declares:

- operation type
- name/tag
- read fields/masks
- write fields/masks
- domain
- write policy
- required versions
- invalidated products
- priority
- budget policy
- backend eligibility
- exactness requirement
- source_location

## 8. Domains

Supported domains:

- AllTiles
- Box
- DirtyChunks(mask)
- ActiveChunks(mask)
- ResidentChunks
- VisibleChunks
- Frontier(buffer)
- GoalSet(goals)
- Region(region_id)
- Corridor(coarse_path)
- EntityBuckets(spatial_index)
- QueryBatch(requests)

Prefer the narrowest correct domain.

## 9. Write policies

- ReadOnly
- UniquePerTile
- UniquePerChunk
- DoubleBuffered
- Atomic
- ThreadLocalThenMerge
- AppendOnly
- Unsafe

Planner rejects ambiguous parallel writes.

## 10. Priority and budget

Priorities:

- Immediate
- GameplayCritical
- VisibleSoon
- Background
- Maintenance

Budget policies:

- MustRun
- CanDefer
- CanSkipIfSuperseded
- BudgetedIncremental

## 11. Planning pipeline

Planner steps:

1. Normalize operations.
2. Resolve typed handles and schemas.
3. Validate shape/storage capabilities.
4. Attach source locations.
5. Validate read/write hazards.
6. Validate domains and residency.
7. Expand domains to chunks/blocks.
8. Group compatible operations.
9. Select algorithms and products.
10. Choose backend.
11. Build execution graph.
12. Insert barriers.
13. Assign result channels.
14. Estimate cost.
15. Execute or defer.
16. Commit writes.
17. Publish report.

## 12. Grouping rules

Group by compatible domain, backend, read/write sets, movement mask, goal set, topology/cost versions, priority, and budget policy.

## 13. Path query planning

Flow:

1. Validate start/goal.
2. Ensure chunks/residency if policy allows.
3. Check topology reachability.
4. Use cached path if valid.
5. Use compatible field if valid.
6. Build shared reverse field when beneficial.
7. Run A* for unique/urgent/exact paths.
8. Use coarse topology route/corridor for long paths.
9. Defer or fail if budget insufficient.

## 14. Field product planning

Products include distance fields, flow fields, reachability masks, influence fields, topology regions, and chunk summaries.

Full-world products over huge/sparse worlds are rejected unless explicitly allowed.

## 15. Execution graph

Phases include validation, generation/loading, mutations, topology, products, field kernels, path queries, movement commit, render delta publication, event merge, diagnostics.

Barriers are inserted for hazards, topology version changes, movement commits, field swaps, chunk lifecycle transitions, ECS result application, and deterministic boundaries.

## 16. Result channels

Results are typed and versioned. Results may be immediate, ready after execute, pending async, failed, cancelled, superseded, or stale.

## 17. Diagnostics

Every op records source location, domain, reads/writes, priority, backend, versions, planner decision, and result channel.

Warnings include full scans, unsafe writes, missing policies, unexpected materialization, repeated similar fields, stale paths, sync residency stalls, and excessive deferral.

## 18. API sketch

```cpp
class FrameOps {
public:
  template<class Kernel>
  OpHandle update_field(Domain domain, Kernel kernel);

  PathBatchHandle query_paths(PathBatchDesc desc);
  NearestBatchHandle query_nearest(NearestBatchDesc desc);
  OpHandle move_entities(MoveBatchDesc desc);
  OpHandle rebuild_topology(TopologyRebuildDesc desc);
  OpHandle ensure_resident(ResidencyDesc desc);
  OpHandle mark_dirty(MaskHandle mask, Box3 bounds);
  OpHandle publish_render_deltas(RenderDeltaDesc desc);
};
```

## 19. Validation

Compile-time validation catches invalid fields, domains, write policies, backends, and schemas.

Runtime validation handles missing chunks, stale versions, invalid entities/goals, budget exhaustion, failed residency, and failed reservations.

## 20. Performance concerns

- planner overhead
- over-fusing
- too many tiny ops
- accidental full-world work
- async latency

## 21. Tests

Unit, compile-time, integration, and property tests cover schemas, domains, hazards, phase ordering, result routing, path grouping, field grouping, missing chunks, and deterministic ordering.

## 22. Benchmarks

- planner overhead
- many small ops
- many path requests
- dirty/sparse domain expansion
- result channel overhead
- diagnostics off/on
- fused vs separate kernels
- path grouping vs per-request A*

## 23. Acceptance criteria

- Game systems can express common work via queued intents.
- Planner validates hazards and domains.
- Path requests are batchable.
- Field kernels run over chunk/block domains.
- Topology rebuilds and path invalidation are ordered correctly.
- Movement/occupancy commits happen safely.
- Result channels are typed and versioned.
- Diagnostics explain important planner decisions.
