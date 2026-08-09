# Topology and precheck

**The decision:** is a failed search expensive enough, often enough, to
pay for region-graph upkeep? When unsure, skip the precheck until
profiling shows failed searches in the hot path.

## Branches

| Branch | Pick when |
| --- | --- |
| No precheck | goals are almost always reachable, so failures stay rare — a *successful* search is cheap (see [performance](../performance.md)), but a *failed* one floods the start's entire reachable component before reporting `NoPath`, which can cost orders of magnitude more |
| Precheck before searching | goals are frequently unreachable — doors, mining, multi-floor — or components are large enough that failure floods dominate the path budget |

The gate only ever prunes: the [pathfinding note](../architecture/path.md)
specifies the verdict semantics that make it safe to skip A* solely on
`Unreachable`.

## Rebuild cadence

| Cadence | Pick when |
| --- | --- |
| `Cadence::on_dirty(mask)` | edits are bursty; rebuild exactly when terrain changed (the `colony_2d.cc` pattern) |
| Every N ticks | edits are continuous and you want bounded, predictable rebuild cost |

Either way `update_region_graph` refreshes only dirty chunks. One graph
serves one movement class — keep the pair aligned, or the mismatch
fallback specified in the [topology note](../architecture/topology.md)
silently wastes the precheck.

## What it looks like

<!-- tess-snippet: getting-topology source=examples/documentation.cc -->
```cpp
tess::LocalTopologyScratch scratch;
tess::RegionGraph graph;
tess::build_region_graph<World, Walker>(world, scratch, graph);

const auto verdict =
    tess::precheck_path<Walker>(graph, world, start, goal, precheck_scratch);
```
<!-- /tess-snippet -->

## Learn and specify

- Teach: [getting-started §6](../getting-started.md), rung 6;
  `examples/stairs_3d.cc` shows the precheck agreeing with A* before and
  after demolition.
- Specify: [topology note](../architecture/topology.md) — verdict
  semantics, incremental updates, transition providers.
