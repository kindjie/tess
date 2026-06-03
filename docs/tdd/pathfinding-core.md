# TDD: Pathfinding Core

## 1. Summary

This document defines the core pathfinding system.

Pathfinding is a primary optimization target. The system supports exact path requests, asynchronous/budgeted execution, topology reachability prechecks, reusable scratch memory, path tickets, PathView for UI/debug overlays, path invalidation, and integration with flow/distance fields.

## 2. Goals

- Provide exact pathfinding over the topology transition model.
- Use topology/region graph prechecks before search.
- Avoid allocation per path request.
- Support batched path requests through the queued planner.
- Support urgent, synchronous, deferred, and async path requests.
- Support 2D, vertical 2D, and true 3D worlds.
- Support sparse/resident chunk policies.
- Support dynamic blocker and reservation hooks.
- Version path results against topology/cost/residency changes.
- Provide path tickets and stable result channels for ECS integration.
- Expose planned paths for UI/debug display.

## 3. Non-goals

- No pawn AI decision-making.
- No crowd simulation.
- No local steering system beyond hooks.
- No full flow/distance field planner here.
- No GPU pathfinding implementation in v1.
- No unbounded coordinate space.
- No per-agent allocation in hot path.
- No assumption that movement is simple grid movement.

## 4. Core concepts

- PathRequest
- PathTicket
- PathResult
- PathView
- PathScratch
- OpenSet
- TransitionProvider
- Topology
- PathCache
- DynamicBlockerPolicy
- PathBudget

## 5. Path request model

PathRequest includes requester, start, goal, movement class, cost profile, exactness, priority, max distance/cost, missing chunk policy, blocker policy, reservation policy, result channel, and source location.

Statuses include Pending, Ready, Unreachable, InvalidStart, InvalidGoal, MissingChunks, RequiresGeneration, BudgetExceeded, Cancelled, Superseded, and Stale.

## 6. Request planning flow

1. Validate start and goal.
2. Validate chunks/residency.
3. Validate movement class.
4. Check topology reachability.
5. Try cached exact path.
6. Try compatible flow/distance field.
7. Run A* for exact/urgent/unique requests.
8. Use coarse topology route and corridor search for long paths.
9. Return pending/deferred/failure based on priority and budget.

## 7. A* search

A* expands through TransitionProvider, not hard-coded neighbors.

Requirements:

- no allocation per request after warmup
- reusable scratch
- deterministic tie-breaking when requested
- topology precheck before search
- missing chunk handling based on policy
- dynamic blocker/reservation hooks

## 8. Heuristics

Defaults:

- top-down 2D: Manhattan or octile
- vertical 2D: Manhattan over effective axes
- 3D: Manhattan for axis-aligned movement
- special transitions: conservative heuristic or coarse topology guidance

## 9. Game-defined movement vocabulary

Pathfinding consumes compiled topology rules: movement classes, masks, tile tags, passability rules, and transition tables.

The substrate sees opaque bit patterns; the game sees named compile-time types/constants.

## 10. PathScratch

Scratch owns reusable memory:

- g_score/f_score or cost arrays
- parent links
- open set
- closed/visited generation markers
- touched node list
- temporary corridor/chunk lists
- output path buffer

Dense/all-resident worlds may use dense arrays. Sparse/huge worlds use chunked/sparse structures bounded by search domain. Generation arrays are preferred over clearing large arrays.

## 11. Path representation and PathView

v1 path representation: explicit TileKey list, with optional compressed views later.

PathSystem exposes a versioned read-only PathView for UI/debug:

```cpp
struct PathView {
  PathTicket ticket;
  PathStatus status;
  TopologyVersion topology_version;
  MovementCostVersion cost_version;
  Span<const TileKey> tiles;
  Span<const PathSegment> segments;
};
```

UI/debug consumers may copy into caller scratch. They must not keep raw spans beyond product lifetime.

## 12. Path cache

Optional in v1, but design supports exact paths, path segments, and corridors. Cache keys include movement class, cost profile, topology/cost versions, goals, and exactness mode.

## 13. Dynamic blockers and reservations

Do not rebuild topology for every moving pawn.

Recommended default:

- topology ignores dynamic blockers
- A* uses blocker/reservation policy near path/final target
- movement step validates occupancy/reservation again

## 14. Crowded movement layering

For crowds, use layers:

- A* / portal graph for global route
- flow/congestion fields near bottlenecks when available
- local steering/reservation for immediate avoidance
- wait/repath/ignore soft collisions as fallback

This belongs across Pathfinding, Flow/Distance Fields, and future Crowd/Local Steering docs.

## 15. Budgeting and async

Budgets can be max requests, node expansions, chunks touched, deterministic work units, or wall-clock for background/non-authoritative work.

Urgent requests finish or fail. Async requests return tickets and continue later.

## 16. Determinism

Requires stable request ordering, open-set tie-breaking, neighbor ordering, budgets, and path reconstruction.

## 17. Interaction with flow/distance fields

Path core can follow fields, validate field paths, refine with A*, or use fields as heuristics/corridors. Planner decides when.

## 18. API sketch

```cpp
class PathSystem {
public:
  PathTicket submit(PathRequest request);
  void submit_batch(Span<PathRequest> requests);
  void step(PathBudget budget);
  optional<PathResult> result(PathTicket ticket) const;
  PathView view(PathTicket ticket) const;
  optional<TileKey> next_step(PathTicket ticket, TileKey current) const;
  void invalidate(TopologyVersion version);
};
```

## 19. Validation

Compile-time validation covers movement class, required fields, transition provider, heuristic, scratch/open set, result storage, and blocker policy fields.

Runtime validation covers bounds, residency, passability, versions, stale tickets, entity validity, and final reservation/occupancy.

## 20. Diagnostics

Report requests, batches, precheck failures, expansions, open set max size, scratch memory, cache hits, field reuse, missing chunks, budget exhaustion, path invalidations, dynamic blocker decisions, stale tickets, and p95/p99 latency.

## 21. Memory/performance concerns

- failed searches are expensive; use topology precheck
- no per-request allocation
- too many unique paths require batching and priority
- 3D branching requires fast transition masks
- u128 TileKeys may affect open/frontier performance

## 22. Tests

Test simple paths, blocked paths, unreachable precheck, vertical 2D, stairs, flying, swimming, blockers, reservations, budgets, stale tickets, no allocations, and deterministic paths.

## 23. Benchmarks

- A* short/medium/long
- open field
- maze/corridor
- rooms
- vertical 2D
- 3D stairwell
- many agents unique/shared goals
- bottlenecks
- missing chunks
- open set comparisons
- scratch reuse

## 24. Acceptance criteria

- A* works over TransitionProvider.
- Topology precheck is used.
- Path requests can be batched.
- No normal per-request allocation after warmup.
- Results are versioned and stale-safe.
- PathView exposes planned paths for debug/UI.
- 2D, vertical 2D, and 3D paths are supported.
- Dynamic blockers are hooks, not baked into topology.
