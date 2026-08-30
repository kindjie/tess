---
title: Tutorials and Learning Paths
description: >-
  Follow tess learning paths for basic pathfinding, flow-style steering,
  colony simulation, strategy selection, and congestion-aware movement.
---

# Tutorials

Start with the existing [getting-started tutorial](getting-started.md), then
use the [live pathfinder](../demo/) to change endpoints and obstacles while
the same core query runs in WebAssembly. Together they are the basic
pathfinding learning path; this page does not introduce a competing beginner
sequence.

## Flow-style steering

The [flow-style steering tutorial](tutorial/flow-steering.md) builds one
dense distance product for a shared goal, then moves independent agents by
reading public distance labels. Its compiled native model and embedded
WebAssembly view demonstrate deterministic next-step selection, goal rebuilds,
and explicit at-goal and unreachable states.

Use this path after basic pathfinding when many independent agents share a
destination and retaining complete paths would duplicate guidance.

## Colony simulation

The colony family shows how focused agent movement grows into a complete
fixed-step composition:

1. [`path_agents.cc`][path_agents] isolates goal assignment, bounded planning,
   movement, and blocked-path handling.
2. [`colony_2d.cc`][colony] adds queued construction, dirty-driven topology,
   scheduling, and `DeltaFrame` presentation.
3. The [`web_colony` host][web_colony] compiles the same model for the
   [live colony](../demo/colony/) while keeping interpolation in the browser.
4. The native model check exercises the composition without a renderer.

Use this family when you want to understand the boundary between simulation
state, retained paths, and presentation state.

## Pathfinding strategies

The strategy family connects API call shape to measured workload shape:

1. [`pathfinding_strategies_model.cc`][strategies] runs plain A*, exact route
   caching, weighted batches, and shared-goal distance fields on one map.
2. The [interactive comparison](../demo/strategies/) exposes the paths and
   reuse counters without treating browser timing as a benchmark.
3. The [strategy comparison article](pathfinding-strategy-comparison.md)
   explains the decision boundary and links to the benchmark evidence.

Use the [pathfinding decision guide](guide/pathfinding.md) for a compact
branch-by-branch choice after working through the comparison.

## Congestion pricing

The congestion family keeps the supported recipe, evidence, and experimental
lab visibly separate:

1. [`congestion_pricing.cc`][congestion_recipe] is the focused public-API
   recipe for a separate surcharge field and affected-route replanning.
2. The [`web_congestion` model][web_congestion] layers screened policies over
   the colony model and retains a native self-check.
3. The [congestion pricing guide](guide/congestion.md) states which results
   are supported, rejected, or experimental.
4. [Congestion Lab](../demo/congestion/) makes those variants explorable.

Congestion pricing changes route cost. Reservations, collision avoidance,
and local steering remain separate coordination concerns.

## Advanced labs and integrations

These experiences are useful next steps, but do not need dedicated tutorials
yet:

- [Congestion Lab](../demo/congestion/) compares pricing policies.
- [Traffic Lab](../demo/traffic/) shows a large deterministic crowd workload.
- [Tower](../demo/tower/) explores one routed three-dimensional world.
- [WebGPU](../demo/webgpu/) demonstrates the optional GPU transport boundary,
  not GPU pathfinding.

The [examples catalog](examples.md) groups every compiled model, lab, recipe,
and optional integration by how it is best used.

[path_agents]: https://github.com/kindjie/tess/blob/main/examples/path_agents.cc
[colony]: https://github.com/kindjie/tess/blob/main/examples/colony_2d.cc
[web_colony]: https://github.com/kindjie/tess/tree/main/examples/web_colony
[strategies]: https://github.com/kindjie/tess/blob/main/examples/pathfinding_strategies_model.cc
[congestion_recipe]: https://github.com/kindjie/tess/blob/main/examples/congestion_pricing.cc
[web_congestion]: https://github.com/kindjie/tess/tree/main/examples/web_congestion
