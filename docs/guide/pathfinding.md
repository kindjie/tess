---
title: C++ Grid Pathfinding Strategies for Game Workloads
description: >-
  Start with A*, then choose weighted routing, route caches, batches, or
  distance fields in the header-only tess C++20 grid pathfinding library.
---

# Pathfinding strategy

tess is a header-only C++20 grid pathfinding library for games, simulations,
and other headless spatial systems. It provides optimal unit-cost and weighted
A* over caller-owned tile data, plus reuse strategies for workloads with many
agents or repeated goals. It does not require an engine, renderer, or ECS.

## Start with A*

[Install tess](../packaging.md), include `<tess/pathfinding.h>`, define a world
and passability field, then reuse one scratch object across queries:

<!-- tess-snippet: getting-astar source=examples/documentation.cc -->
```cpp
tess::PathScratch scratch;
const auto result = tess::astar_path<World, PassableTag>(
    world, tess::PathRequest{start, goal}, scratch);
```
<!-- /tess-snippet -->

Check `result.status` before using its cost or path. The returned
`tess::PathView` borrows the scratch storage and remains valid only until the
next query that reuses it. The [getting-started tutorial](../getting-started.md)
builds the complete world and field schema; the
[quickstart](https://github.com/kindjie/tess/blob/main/examples/quickstart.cc)
is a complete program.

## Choose a strategy

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
3. **Do identical routes repeat on an unchanged map?** →
   `cached_astar_path`, with caller-driven invalidation
   (`UnitRouteCache::invalidate_if_world_changed`) — the
   [pathfinding note](../architecture/path.md) specifies the contract.
4. **Otherwise** — `astar_path`, or `weighted_astar_path` with a
   movement class when passability or cost differs per unit.

## Branches

| Workload | API | Example |
| --- | --- | --- |
| Few one-off unit-cost queries | `astar_path` | `examples/queued_path.cc` |
| Per-unit rules or terrain costs | `MovementClass` + `weighted_astar_path` | `examples/path_agents.cc` |
| Many agents, shared goal set (dense worlds only) | distance-field product + `FieldProductCache` | `examples/ant_farm_vertical.cc` |
| Weighted per-tick batches, repeated goals | `weighted_path_batch` | below |
| Repeated identical routes, unchanged map | `cached_astar_path` | below |

The [pathfinding note](../architecture/path.md) holds the normative
workload charts and semantics; this page only routes into them.

## Capabilities and boundaries

- **Routing rules:** plain A* handles unit-cost terrain;
  `weighted_astar_path` and movement classes add positive terrain costs and
  per-unit passability rules.
- **Workload reuse:** route caches serve repeated identical routes, batches
  amortize repeated weighted goals, and distance fields serve many starts that
  share a goal set.
- **World storage:** single-shot A*, route caches, and weighted batches support
  dense and sparse-resident worlds. Persistent distance-field products are
  dense-only; use a supported batch or per-request strategy when sparse
  residency is required.
- **Scope:** tess plans grid routes. It is not a navigation-mesh generator or a
  continuous-space planner, and it does not compute globally optimal
  multi-agent plans. Joint movement and PIBT resolve contention when agents
  move instead.

The [interactive pathfinder](https://tess.owx.dev/latest/demo/) shows the basic A*
query, while the [colony demo](https://tess.owx.dev/latest/demo/colony/) exercises
retained routes and multi-agent movement. Reproducible timing and memory
evidence lives on the [performance page](../performance.md).
The [C++ grid pathfinding benchmark comparison](../pathfinding-strategy-comparison.md)
shows the four call shapes over one compiled, self-checking example.

## Thresholds

A single A* across an open 512x512 grid measures ~2.1 us (weighted
~2.4 us; see [performance](../performance.md)). Plain searches can remain
cheap in absolute terms even when a repeated-route cache or shared-goal batch
has already crossed over at a single-digit request count. Use the
[C++ grid pathfinding benchmark comparison](../pathfinding-strategy-comparison.md)
to identify the matching reuse shape, then measure the complete application
before adding retained state. The
[live colony demo](https://tess.owx.dev/latest/demo/colony/) makes the
difference tangible: toggle retained routes off and watch the per-tick
cost climb.

## What it looks like

<!-- tess-snippet: path-batch source=examples/documentation.cc -->
```cpp
tess::WeightedPathBatchScratch scratch;
const auto requests = std::array{
    tess::PathRequest{tess::Coord2{0, 0}, tess::Coord2{31, 31}},
    tess::PathRequest{tess::Coord2{0, 1}, tess::Coord2{31, 31}},
    tess::PathRequest{tess::Coord2{0, 2}, tess::Coord2{31, 31}},
};
const auto results =
    tess::weighted_path_batch<World, WeightedMovement, /*MaxCost=*/128>(
        world, requests, scratch);
```
<!-- /tess-snippet -->

<!-- tess-snippet: field-product source=examples/documentation.cc -->
```cpp
tess::GoalSet goals;
goals.add(tess::Coord2{31, 0});
goals.add(tess::Coord2{31, 31});

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
    world, tess::Coord2{0, 31}, *shared, scratch);
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
