# Presentation

**The decision:** does anything observe the world on a different cadence,
thread, or process than the simulation tick? If yes, publish versioned
`DeltaFrame`s; if no, omit the render bridge — it is optional, and
nothing else depends on it.

## Branches

| Branch | Pick when |
| --- | --- |
| Headless | servers, batch experiments, tests: no observer exists, so skip `DeltaCollector` entirely |
| DeltaFrame consumer | any renderer, UI, or network mirror: the consumer owns shadow state, applies immutable versioned frames, and resynchronizes on gaps |

Consumers never rescan the world: frame records name which tiles
changed, and the consumer re-reads exactly those tiles from the
authoritative world (or receives values over its own channel). Frame
versions make missed frames detectable and recovery explicit rather
than silent.

## What it looks like

<!-- tess-snippet: getting-render-deltas source=examples/documentation.cc -->
```cpp
```
<!-- /tess-snippet -->

## Learn and specify

- Teach: [getting-started §8](../getting-started.md), rung 8;
  `examples/render_delta_consumer.cc` rebuilds a shadow grid from frames
  alone, and `examples/colony_2d.cc` shows the bridge inside a full
  schedule loop.
- Specify: [simulation note](../architecture/simulation.md) — collection,
  publication, versioning, resync.
