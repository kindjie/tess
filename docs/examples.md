---
title: C++20 Grid Pathfinding and Simulation Examples
description: >-
  Explore complete, self-checking C++20 examples for grid pathfinding, sparse
  worlds, coordinated agent movement, simulation, and WebAssembly demos.
---

# Examples

Every example is a complete, self-checking program built as `tess_<name>`
by the dependency-free `examples` preset (the EnTT and Flecs adapter examples
additionally need their ECS gates, which the `dev` preset turns on):

```sh
cmake --preset examples
cmake --build --preset examples
./build/examples/examples/tess_quickstart
```

## Start here

- [Live pathfinder](../demo/) — the interactive
  WebAssembly demo, built from the same C++20 headers as the library and
  published with this site.
- [Live strategy comparison](../demo/strategies/) — the
  native pathfinding-strategy model compiled to WebAssembly: four call shapes,
  a shared obstacle course, C++-reported paths and reuse counters, and distinct
  route, batch, and map-wide field data products without browser timing claims.
- [Live colony](../demo/colony/) — the scale demo:
  up to 1,024 agents shuttling between the edges and replanning around
  walls you draw (walls survive resets), with completed and crowd-turnaround
  leg counters, smooth presentation-only movement, a C++-update cost readout,
  and a retained-routes vs replan-every-tick toggle.
- [Congestion lab](../demo/congestion/) — the
  colony simulation with the supported-coverage nearby-agent recipe,
  five additional screened policies, two rejected controls, and three
  period or price-cap variants selectable live, with a price overlay,
  wall painting, and the shipped equal-cost route-spreading toggle.
  The [congestion pricing guide](guide/congestion.md) states the
  evidence tier and boundary for each result; the native runner
  executes the same model without browser presentation.
- [Tower](../demo/tower/) — a six-floor world
  where solid slabs separate the floors and stairwell columns are the only
  tiles joining them, so an agent crossing floors follows one route
  through three dimensions rather than switching between stacked maps.
  Closing a stairwell prices it instead of sealing it, so routes divert
  while anyone on the stairs can still walk out. Isometric 2D canvas with
  touch controls; an early version whose geometry is deliberately simple.
- [Live diagnostics](../demo/diagnostics/) — real Dear ImGui
  panels over path, queued-phase, timing, trace, and consumer-instrumented
  allocation snapshots. Mirrored HTML controls keep the demo keyboard
  operable and make its runtime state visible to browser automation.
- [`quickstart.cc`][quickstart] — the complete program on the
  [home page](index.md): a world, a schema, and an A* query.
- [`colony_2d.cc`][colony_2d] — the flagship composition: queued
  construction edits through the auto-exec schedule task, an OnDirty
  topology rebuild, movement-class agents routing around the new wall, and
  a DeltaFrame render consumer, all in one `tess::Schedule` loop.

## Pathfinding and topology

- [`queued_path.cc`][queued_path] — a small end-to-end queued-edit plus A*
  pathfinding prototype.
- [`pathfinding_strategies_model.cc`][pathfinding_strategies] — one small
  world comparing plain A*, exact route caching, weighted batches, and
  shared-goal distance fields; its named regions feed the
  [strategy comparison](pathfinding-strategy-comparison.md).
- [`stairs_3d.cc`][stairs_3d] — the `StairTransitions` provider connecting
  two z-levels, with reachability, the path-runtime precheck, and an
  incremental update after demolishing the stair.

## Scale: many agents and large worlds

- [`path_agents.cc`][path_agents] — a multi-agent path-agent tick loop
  with goal assignment, dirty-driven replanning, and blocked-path
  handling; the focused subset of what [`colony_2d.cc`][colony_2d]
  composes into a full frame.
- [`ant_farm_vertical.cc`][ant_farm] — a degenerate-axis vertical world
  (x-z cross-section) where many ants share one distance-field product
  through the byte-budgeted `FieldProductCache` instead of searching
  independently.
- [`web_colony`][web_colony_src] — the source of the
  [live colony demo](../demo/colony/): the colony_2d
  composition compiled to WebAssembly. Its model, Wasm adapter, native
  self-check, browser controller, and page are separate so the library
  patterns are visible without platform glue interrupting them.
- [`congestion_pricing.cc`][congestion_pricing] — a compile-checked
  congestion-pricing recipe against public APIs only: it computes tile
  prices, marks the changed chunks, requests replans for affected
  retained routes through the experimental route-crossing helper, and
  clears the surcharge when disabled. Terrain and congestion live in
  separate fields summed by the movement class, so pricing never
  overwrites the caller's terrain. Snippet-synced into the
  [congestion pricing guide](guide/congestion.md).
- [`web_congestion`][web_congestion_src] — the source of the
  [congestion lab](../demo/congestion/): wraps
  the colony model through its native seam so the tutorial stays clean
  while every pricing experiment lives here.
- [`web_traffic`][web_traffic_src] — the source of the
  [Traffic Lab](../demo/traffic/): a deterministic
  1024×512 congestion overview with static terrain caching, separately
  rendered agents, and an eight-search planning budget. Static barrier
  scenarios supply their known gate crossings to exact weighted segments;
  open scenarios retain direct weighted A*.
- [`sparse_stream.cc`][sparse_stream] — budget-bounded sparse residency:
  a 1,024-chunk world held to a 16-page budget (64x less resident field
  storage), and a path query that reports `Indeterminate` until the
  missing bridge chunk is streamed in and the retry succeeds.

### Measuring Traffic Lab

The native runner can emit one row per fixed tick without formatting inside
the measured loop. The campaign tool launches a fresh process per repetition,
retains raw samples, and suppresses p50, p95, or p99 until it has respectively
20, 200, or 2,000 samples:

```sh
cmake --preset release
cmake --build --preset release --target tess_web_traffic_model
tools/measure_traffic_lab.py \
  --binary build/release/examples/tess_web_traffic_model \
  --ticks 128 --repetitions 16 --output /tmp/traffic-lab.json
```

Timing artifacts are advisory. Detailed work attribution is a separate pass;
build `tess_web_traffic_diagnostics_model` and pass `--counter-pass` to the
same tool. It fails closed if an instrumented binary is used for timing or an
uninstrumented binary is used for counters.

Running `tess_web_traffic_model` without arguments performs the native
acceptance check. It exhaustively compares every guided barrier route with
direct weighted A*, then verifies deterministic 512- and 1,600-tick crowd
checkpoints. Debug builds take materially longer because the oracle
intentionally reproduces the former full-map searches.

The default Debug, ASan/UBSan, GCC, Windows, and coverage test presets run the
cheap scenario checks and crowd checkpoints but skip the exact route oracle.
Run that oracle locally in the same optimized configuration that owns it in
pull requests:

```sh
cmake --preset bench
cmake --build --preset bench --target tess_web_traffic_model
ctest --preset bench -L '^config:optimized-exhaustive$'
```

Browser capture is also opt-in: append `?measure=1` to the Traffic Lab URL,
then use **Snapshot latency** or call
`window.tessTrafficMetrics.snapshot()` in the developer console.
The bounded 4,096-sample capture keeps update/planning, render, and JavaScript
frame-callback distributions separate and records catch-up frames. Normal
previews retain only the inexpensive live exponential averages.

## Colony tutorial map

The colony model labels five reusable composition patterns directly in the
source: queueing a world edit, rebuilding derived topology on dirty input,
running bounded pathing before movement, consuming a `DeltaFrame` as
invalidation, and recovering a rejected frame with a full baseline. The
`ColonyModel` interface also makes the simulation/presentation boundary
explicit: tess owns integer-tile fixed-tick state, while the browser
interpolates read-only previous/current snapshots with the accumulator alpha.
The fractional coordinates never return to simulation state.

## Integration boundaries

- [`chunk_maintenance.cc`][chunk_maintenance] — a stable external owner that
  rebuilds a versioned derived summary, checks dirty-mask and content-version
  state, and keeps scheduler handles out of the world. Its default immediate
  backend is synchronous; deferred FIFO,
  queued-coalescing, and dirty-bit backends remain explicit experiments. The
  install smoke builds and runs a self-contained version of this workflow
  against the installed package.
- [`custom_ecs_min.cc`][custom_ecs] — the ECS adapter concepts implemented
  by a deliberately non-EnTT-shaped micro ECS.
- [`entt_pawns.cc`][entt_pawns] — the EnTT adapter driving registry-owned
  pawns (built when `TESS_ENABLE_ENTT` is on).
- [`flecs_pawns.cc`][flecs_pawns] — the Flecs adapter driving world-owned
  pawns (built when `TESS_ENABLE_FLECS` is on).
- [`render_delta_consumer.cc`][render_delta] — a standalone DeltaFrame
  consumer rebuilding a shadow grid from published frames.
- [`web_pathfinder`][web_pathfinder] — the source of the live demo above:
  a single-threaded WebAssembly build with a small JavaScript shell.
- [`web_diagnostics`][web_diagnostics] — a dependency-free native model
  self-check plus the Dear ImGui GLFW/WebGL2 browser host used by the
  [live diagnostics demo](../demo/diagnostics/).

[quickstart]: https://github.com/kindjie/tess/blob/main/examples/quickstart.cc
[colony_2d]: https://github.com/kindjie/tess/blob/main/examples/colony_2d.cc
[queued_path]: https://github.com/kindjie/tess/blob/main/examples/queued_path.cc
[pathfinding_strategies]: https://github.com/kindjie/tess/blob/main/examples/pathfinding_strategies_model.cc
[path_agents]: https://github.com/kindjie/tess/blob/main/examples/path_agents.cc
[stairs_3d]: https://github.com/kindjie/tess/blob/main/examples/stairs_3d.cc
[ant_farm]: https://github.com/kindjie/tess/blob/main/examples/ant_farm_vertical.cc
[sparse_stream]: https://github.com/kindjie/tess/blob/main/examples/sparse_stream.cc
[chunk_maintenance]: https://github.com/kindjie/tess/blob/main/examples/chunk_maintenance.cc
[web_colony_src]: https://github.com/kindjie/tess/tree/main/examples/web_colony
[congestion_pricing]: https://github.com/kindjie/tess/blob/main/examples/congestion_pricing.cc
[web_congestion_src]: https://github.com/kindjie/tess/tree/main/examples/web_congestion
[web_traffic_src]: https://github.com/kindjie/tess/tree/main/examples/web_traffic
[custom_ecs]: https://github.com/kindjie/tess/blob/main/examples/custom_ecs_min.cc
[entt_pawns]: https://github.com/kindjie/tess/blob/main/examples/entt_pawns.cc
[flecs_pawns]: https://github.com/kindjie/tess/blob/main/examples/flecs_pawns.cc
[render_delta]: https://github.com/kindjie/tess/blob/main/examples/render_delta_consumer.cc
[web_pathfinder]: https://github.com/kindjie/tess/tree/main/examples/web_pathfinder
[web_diagnostics]: https://github.com/kindjie/tess/tree/main/examples/web_diagnostics
