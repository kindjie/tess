# TDD: Flow / Distance Fields

## 1. Summary

This document defines flow fields, distance fields, influence fields, congestion fields, and related spatial products.

These products answer:

- cheapest direction toward a shared goal set
- cost from each tile to nearest valid target
- dangerous, exposed, crowded, or valuable tiles
- bottleneck movement guidance
- when many path requests should share one product instead of running A* independently

## 2. Goals

- Support reverse BFS and reverse Dijkstra distance fields.
- Support shared goal sets.
- Support partial fields: region, chunk-window, corridor, box, radius-limited.
- Support field reuse through versioned cache keys.
- Support congestion/bottleneck products for crowded movement.
- Support influence/danger/visibility-style scalar fields.
- Support tactical query building blocks without owning combat AI.
- Support CPU implementation in v1 and future GPU backend interfaces.
- Avoid full-world/full-volume fields by default.
- Avoid heap churn, fragmentation, and ownership leaks.

## 3. Non-goals

- No complete combat AI.
- No complete crowd simulation.
- No exact multi-agent path planner.
- No mandatory GPU implementation in v1.
- No full-volume fields over huge worlds unless explicitly allowed.
- No per-agent field rebuild by default.
- No dynamic runtime world dimensions.

## 4. Core concepts

- DistanceField
- FlowField
- GoalSet
- FieldKey
- FieldProduct
- InfluenceField
- CongestionField
- FieldPlanner
- FieldProductCache

## 5. Field types

- Distance field
- Flow field
- Influence field
- Congestion field

## 6. Algorithms

- reverse BFS for uniform cost
- reverse Dijkstra for weighted cost
- bucket queue Dijkstra for small bounded integer costs
- corridor field
- chunk-window field
- future incremental repair

## 7. Field bounds

Supported bounds:

- FullWorld, explicit only
- ResidentChunks
- Box3
- Region
- ChunkWindow
- Corridor
- Radius
- GoalExpanded
- AgentGoalUnion

Full-volume 3D fields are rejected by default.

## 8. Field planner

Planner decides A* vs field, cached vs new field, full vs partial field, algorithm, CPU vs future GPU backend, sync vs async build, and defer/cancel behavior.

Heuristic:

```text
benefit = expected_users * expected_astar_cost
          - field_build_cost
          - field_memory_cost
          - staleness_risk
```

## 9. When to use fields

Use fields when many agents share goal sets, bottlenecks, or stable constraints. Use A* for unique/urgent/exact/short/pawn-specific requests. Use A* + fields for global route + local guidance.

## 10. Field cache

Use a byte-budgeted LRU or weighted-LRU cache.

FieldKey includes:

- field kind
- movement class
- goal set id/version
- cost profile
- topology/cost versions
- bounds
- backend
- approximation mode

Product stores memory bytes, build cost, last used tick, use count, invalidation reasons, and ownership metadata.

## 11. Memory management requirements

Field products must avoid per-request and per-build heap churn.

Rules:

- no allocation per expanded tile
- no allocation per frontier element
- no allocation per neighbor list
- no unbounded temporary vectors inside field builders
- no ownership cycles between cached products, tickets, and world/chunk state
- all products have explicit lifetime, owner, memory budget, and eviction policy

Use:

- executor arenas
- reusable scratch buffers
- chunk-local slabs
- object pools for product records
- ring buffers for transient frontiers/events
- generation counters instead of clearing large arrays
- compact per-chunk bitsets

Avoid:

- shared_ptr graphs
- per-tile heap nodes
- vector growth during hot execution
- duplicated TileKey lists for similar requests

## 12. Ownership model

- World owns authoritative chunk storage.
- FieldProductCache owns derived products.
- ExecutorScratch owns transient arenas/frontiers.
- ChunkPage may own chunk-local product slabs only for resident products.
- Result channels reference products by handle/version, not shared ownership.
- Derived products may pin residency only through explicit registered dependencies.

## 13. Allocation strategy

- long-lived cached products: stable pools/slabs
- per-tick transient: monotonic arena reset after execute
- per-worker scratch arenas/frontier buffers
- per-field product: chunked slabs sized by chunk tile count
- handles: generational

## 14. Nearest target queries

Nearest queries can use reverse fields. Field provides reachability/cost; game rules choose final target using permissions, reservations, ownership, faction, item type, capacity, etc.

## 15. Crowded movement and bottlenecks

Layered strategy:

- global route: A*/topology/cached route
- shared congestion guidance: local field near bottleneck
- local movement: steering/reservation/wait/yield
- fallback: wait/repath/ignore soft collision

Generate congestion fields when paths cluster through the same portal/corridor/stair or movement failures accumulate.

## 16. Tactical fields and future assignment

The substrate supports tactical field products such as cover, exposure, enemy danger, ally density, flanking desirability, retreat distance, and friendly-fire lanes.

Future TDD: Tactical Queries and Assignment.

That future module handles scarce candidate assignment, e.g. many pawns contending for fewer cover spots, using scoring, reservation, matching/auction/greedy repair, and path requests.

## 17. GPU interface

GPU implementation is deferred. Future GPU fields are derived products. CPU remains authoritative. Readback is explicit and small by default.

## 18. Queued API examples

```cpp
ops.request_field(...);
ops.query_nearest(...);
ops.request_congestion_field(...);
```

## 19. Validation

Compile-time validation covers field kind, movement class, cost type, storage format, backend, and full-world product permissions.

Runtime validation covers goal sets, bounds, residency, versions, cache keys, memory budget, and superseded requests.

## 20. Diagnostics

Report field builds, cache hits/misses, bounds chosen, algorithm, tiles/chunks visited, memory bytes, build time, reuse count, invalidation reason, A* avoided, stale fields, full-world warnings, and congestion triggers.

## 21. Performance concerns

- field size
- staleness
- queue strategy
- frontier materialization
- 3D/full-volume explosion
- allocation and fragmentation

## 22. Tests

Test BFS, Dijkstra, vertical 2D, 3D stairs, unreachable tiles, version invalidation, partial bounds, cache hit/miss, sparse chunks, congestion triggers, and tactical field scores.

## 23. Benchmarks

- BFS/Dijkstra fields
- bucket vs heap
- full vs region/window/corridor fields
- sparse 3D fields
- shared goal reuse
- congestion fields
- cache thrash/reuse
- allocation strict mode

## 24. Acceptance criteria

- Reverse BFS and Dijkstra work over TransitionProvider.
- Fields support 2D, vertical 2D, and 3D.
- Products are versioned, cacheable, and byte-budgeted.
- Planner can choose A* vs fields.
- Full-world/full-volume fields are not accidental.
- Nearest-target queries can use shared fields.
- Congestion fields are represented as local products.
- Dynamic blockers are not baked into global topology/fields.
- Future tactical query/assignment support is accounted for.
