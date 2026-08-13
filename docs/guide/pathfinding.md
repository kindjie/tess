# Pathfinding strategy

**The decision:** what shape is your path workload? Walk the spine in
order and stop at the first match. When unsure, start with plain
`astar_path` — every other branch is an optimization you adopt when a
measured workload justifies it.

1. **Do many querents share a goal set?** Shared goals on unit-cost
   terrain → build one distance-field product and reuse it. The product
   family is dense-only: on a `SparseResidentWorld`, use the batch or
   per-request branches instead.
2. **Are requests weighted, with goals that repeat?** →
   `weighted_path_batch` amortizes one bounded field per repeated goal;
   all-distinct goals fall back to per-request weighted A*.
3. **Do identical routes repeat on a stable map?** →
   `cached_astar_path`, with caller-driven invalidation
   (`RouteCacheScratch::invalidate_if_world_changed`) — the
   [pathfinding note](../architecture/path.md) specifies the contract.
4. **Otherwise** — `astar_path`, or `weighted_astar_path` with a
   movement class when passability or cost differs per unit.

## Branches

| Workload | API | Example |
| --- | --- | --- |
| Few one-off unit-cost queries | `astar_path` | `examples/mvp_path.cc` |
| Per-unit rules or terrain costs | `MovementClass` + `weighted_astar_path` | `examples/path_agents.cc` |
| Many agents, shared goal set (dense worlds only) | distance-field product + `FieldProductCache` | `examples/ant_farm_vertical.cc` |
| Weighted per-tick batches, repeated goals | `weighted_path_batch` | below |
| Repeated identical routes, stable map | `cached_astar_path` | below |

The [pathfinding note](../architecture/path.md) holds the normative
workload charts and semantics; this page only routes into them.

## Thresholds

A single A* across an open 512x512 grid measures ~2.1 us (weighted
~2.4 us; see [performance](../performance.md)). Below a few hundred
queries per tick, plain searches are rarely the bottleneck — measure
before reaching for a sharing strategy. The
[live colony demo](https://tess.owx.dev/demo/colony/) makes the
difference tangible: toggle retained routes off and watch the per-tick
cost climb.

## What it looks like

<!-- tess-snippet: path-batch source=examples/documentation.cc -->
```cpp
tess::WeightedPathBatchScratch scratch;
const auto requests = std::array{
    tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{31, 31, 0}},
    tess::PathRequest{tess::Coord3{0, 1, 0}, tess::Coord3{31, 31, 0}},
    tess::PathRequest{tess::Coord3{0, 2, 0}, tess::Coord3{31, 31, 0}},
};
const auto results =
    tess::weighted_path_batch<World, PassableTag, CostTag, /*MaxCost=*/128>(
        world, requests, scratch);
```
<!-- /tess-snippet -->

<!-- tess-snippet: field-product source=examples/documentation.cc -->
```cpp
tess::GoalSet goals;
goals.add(tess::Coord3{31, 0, 0});
goals.add(tess::Coord3{31, 31, 0});

tess::DistanceFieldScratch scratch;
tess::DistanceFieldProduct product;
const auto built = tess::build_distance_field_product<World, PassableTag>(
    world, goals, product, scratch);

tess::FieldProductCache cache{1u << 20u};  // Byte-budgeted.
const auto stored = cache.store<World, PassableTag>(std::move(product));
const auto* shared = cache.lookup<World, PassableTag>(world, goals);
if (shared == nullptr) {
  return false;
}

const auto nearest = tess::nearest_target<World, PassableTag>(
    world, tess::Coord3{0, 31, 0}, *shared, scratch);
```
<!-- /tess-snippet -->

## Learn and specify

- Teach: [getting-started §5](../getting-started.md), rung 5.
- Specify: [pathfinding note](../architecture/path.md) — request/result
  contracts, scratch reuse, cache invalidation, batch statistics.

## Horizon

!!! note "Planned"
    Congestion, flow, and influence fields are designed but not shipped
    (see the [roadmap](../roadmap.md), which includes the interim
    cost-field fallback). Do not build agent code that assumes a
    congestion API.

    Routing itself is optimal per agent — a route is planned without
    reference to where other agents are. Contention is resolved at *move*
    time instead, by two shipped tiers. Joint movement admits a tick's
    moves together so agents never stack, and by default (`SwapPolicy::
    Forbid`) a mutually blocked pair stays blocked rather than exchanging
    tiles; `Permit` and `PermitOnDeadlock` relax that deliberately. The
    opt-in PIBT tier additionally lets a blocked agent yield *off* its
    route, which resolves a head-on that `Forbid` alone leaves blocked.
    Neither tier spreads a crowd across alternative routes, which is what
    a congestion field would do. See
    [simulation](../architecture/simulation.md).
