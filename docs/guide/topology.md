# Topology and precheck

**The decision:** is a failed search expensive enough, often enough, to
pay for region-graph upkeep? When unsure, skip the precheck until
profiling shows failed searches in the hot path.

## Branches

| Branch | Pick when |
| --- | --- |
| No precheck | small worlds or few agents: a failed A* floods at most the start's component (~2 us on an open 512x512), which costs less than maintaining the graph |
| Precheck before searching | goals are frequently unreachable — doors, mining, multi-floor — or worlds are large enough that failure floods hurt |

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
```
<!-- /tess-snippet -->

## Learn and specify

- Teach: [getting-started §6](../getting-started.md), rung 6;
  `examples/stairs_3d.cc` shows the precheck agreeing with A* before and
  after demolition.
- Specify: [topology note](../architecture/topology.md) — verdict
  semantics, incremental updates, transition providers.
