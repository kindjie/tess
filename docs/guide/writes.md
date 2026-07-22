# Writes

**The decision:** does anything downstream need to *react* to this edit?
The litmus test: if a topology rebuild, a replanner, or a renderer must
notice the edit, queue it — dirty metadata only exists on the queued
path. When unsure during simulation, queue.

## Branches

| Branch | Pick when | You commit to |
| --- | --- | --- |
| Direct field writes | setup, loading, tests, tools — nothing consumes dirty state | keeping these writes out of the simulation loop |
| Queued operations | sim-time edits; dirty masks drive topology, replanning, or render deltas; parallel execution may ever matter | declaring a domain, touched fields, dirty mask, and `WritePolicy` per operation |

The declared `WritePolicy` is what licenses parallel execution later —
declare it honestly even while running serial. Queued operations also
report back through result channels: typed per-operation completion
records, drained once per frame.

## What it looks like

<!-- tess-snippet: getting-direct-write source=examples/documentation.cc -->
```cpp
world.field<PassableTag>(tess::Coord3{4, 2, 0}) = 1;
```
<!-- /tess-snippet -->

## Learn and specify

- Teach: [getting-started §4](../getting-started.md), rung 4;
  `examples/mvp_path.cc` is the smallest queued edit,
  `examples/colony_2d.cc` the schedule-integrated form.
- Specify: [queued-operations note](../architecture/queued-operations.md)
  (planning, write policies, result channels).
