# Topology and precheck

**The decision:** is a failed search expensive enough, often enough, to
pay for region-graph upkeep? When unsure, skip the precheck until
profiling shows failed searches in the hot path.

## Branches

| Branch | Pick when |
| --- | --- |
| No precheck | goals are almost always reachable, so failures stay rare — a *successful* search is cheap (~2 us on an open 512x512), but a *failed* one floods the start's entire reachable component before reporting `NoPath`, which can cost orders of magnitude more |
| Precheck before searching | goals are frequently unreachable — doors, mining, multi-floor — or components are large enough that failure floods dominate the path budget |

The gate is asymmetric by design: only `Unreachable` proves failure and
skips the search; every other verdict means "run A*". It can never turn
a solvable query into a wrong failure — only occasionally waste its own
lookup.

## Rebuild cadence

| Cadence | Pick when |
| --- | --- |
| `Cadence::on_dirty(mask)` | edits are bursty; rebuild exactly when terrain changed (the `colony_2d.cc` pattern) |
| Every N ticks | edits are continuous and you want bounded, predictable rebuild cost |

Either way `update_region_graph` refreshes only dirty chunks. One graph
serves one movement class: a class/graph mismatch reports `GraphStale`
and falls back to A* — safe, but silently wasted work, so keep the pair
aligned.

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
