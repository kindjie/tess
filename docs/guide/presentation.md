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

Consumers never rescan the world: frame records carry either individual
changed tiles or box-granular dirty bounds, and the consumer re-reads
the named tiles — or repaints the recorded box — from the authoritative
world (or receives values over its own channel). Frame versions make
missed frames detectable and recovery explicit rather than silent.

## What it looks like

<!-- tess-snippet: getting-render-deltas source=examples/documentation.cc -->
```cpp
tess::collect_tile_deltas(deltas, world, kTerrainDirty);
const auto frame = deltas.publish();
```
<!-- /tess-snippet -->

The frame **borrows** the collector's storage; it does not own it. Its
spans stay valid until the next `publish()` or `reserve()` on that
collector — apply the frame, or copy what you need out of it, before
publishing again. Its `header` is a value and outlives all of that, which
is why version and gap checks are safe to keep.

That matters most for the branch this page recommends. "Different
cadence, thread, or process" describes the *consumer*, not the frame: a
`DeltaCollector` is externally synchronized like every other tess
scratch, so what crosses a thread or socket is applied shadow state or a
copy of the records — never the `DeltaFrame` itself, whose spans point
into memory the simulation thread is about to refill. Holding a frame
across a publish reads records that are being overwritten, and because
that storage stays live and owned throughout, no sanitizer will flag it.

## Learn and specify

- Teach: [getting-started §8](../getting-started.md), rung 8;
  `examples/render_delta_consumer.cc` rebuilds a shadow grid from frames
  alone, and `examples/colony_2d.cc` shows the bridge inside a full
  schedule loop.
- Specify: [simulation note](../architecture/simulation.md) — collection,
  publication, versioning, resync.
