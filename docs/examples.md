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

## Guided tutorials

The [tutorials index](tutorials.md) connects related artifacts into five
learning paths instead of presenting each model, host, and article as a
separate starting point:

- **Basic pathfinding** combines the
  [getting-started tutorial](getting-started.md), the
  [live pathfinder](../demo/), and [`quickstart.cc`][quickstart].
- **Colony simulation** grows from [`path_agents.cc`][path_agents] through the
  [colony composition tutorial](tutorial/colony-composition.md) into the
  [`colony_2d.cc` model][colony_2d], its native check, and the
  [`web_colony` host][web_colony_src].
- **Pathfinding strategies** connects the
  [`pathfinding_strategies_model.cc` model][pathfinding_strategies], the
  [interactive comparison](../demo/strategies/), and the
  [benchmark-backed article](pathfinding-strategy-comparison.md).
- **Congestion pricing** connects the
  [`congestion_pricing.cc` recipe][congestion_pricing], the
  [`web_congestion` model][web_congestion_src], the
  [decision guide](guide/congestion.md), and
  [Congestion Lab](../demo/congestion/).
- **Flow field steering** connects the
  [flow field tutorial](tutorial/flow-steering.md), its self-checking
  native [`web_flow_steering` model][web_flow_steering_src], and the embedded
  WebAssembly presentation.
- **Procedural sparse streaming** connects the
  [bounded-world tutorial](tutorial/procedural-sparse-stream.md), the native
  [`web_sparse_stream` model][web_sparse_stream_src], and its camera-followed
  WebAssembly presentation.

### Colony composition map

The colony model labels five reusable composition patterns directly in the
source: queueing a world edit, rebuilding derived topology on dirty input,
running bounded pathing before movement, consuming a `DeltaFrame` as
invalidation, and recovering a rejected frame with a full baseline. The
`ColonyModel` interface also makes the simulation/presentation boundary
explicit: tess owns integer-tile fixed-tick state, while the browser
interpolates read-only previous/current snapshots with the accumulator alpha.
The fractional coordinates never return to simulation state.

## Interactive labs

- [Live pathfinder](../demo/) — the basic routing learning path compiled to
  WebAssembly from the same C++20 headers as the library.
- [Live strategy comparison](../demo/strategies/) — four call shapes over one
  obstacle course with C++-reported paths and reuse counters, without browser
  timing claims.
- [Flow field steering](../demo/flow-steering/) — independent agents descend
  one shared dense distance product using a documented direction order.
- [Live colony](../demo/colony/) — up to 1,024 agents replanning around walls,
  with deterministic simulation state and presentation-only interpolation.
- [Live diagnostics](../demo/diagnostics/) — Dear ImGui path, queued-phase,
  timing, trace, and consumer allocation panels with mirrored HTML controls.
- [Procedural sparse stream](../demo/sparse-stream/) — a 4,096×4,096 bounded
  world shown through a 32-page LRU resident set and deterministic generator.
- [Congestion Lab](../demo/congestion/) — the colony workload with supported,
  rejected, and experimental pricing variants identified by the guide.
- [Traffic Lab](../demo/traffic/) — a deterministic 1024×512 crowd overview
  with static terrain caching and an eight-search planning budget.
- [Tower](../demo/tower/) — a six-floor world whose stair transitions create
  one routed three-dimensional topology rather than stacked maps.
- [WebGPU](../demo/webgpu/) — the optional GPU transport and submission
  boundary. It is an integration lab, not a GPU pathfinder.

Congestion Lab, Traffic Lab, Tower, and WebGPU remain distinct advanced labs
or integrations; they do not need dedicated tutorials yet.

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

## Focused C++ recipes

- [`quickstart.cc`][quickstart] — one world, one schema, and one A* query;
  this is the complete program shown on the [home page](index.md).
- [`queued_path.cc`][queued_path] — a small queued-edit plus A* pathfinding
  prototype.
- [`pathfinding_strategies_model.cc`][pathfinding_strategies] — plain A*,
  exact route caching, weighted batches, and shared-goal distance fields in
  one self-checking world.
- [`stairs_3d.cc`][stairs_3d] — a `StairTransitions` provider, reachability
  precheck, and incremental update after demolishing a stair.
- [`path_agents.cc`][path_agents] — goal assignment, dirty-driven replanning,
  bounded work, and blocked-path handling for multiple agents.
- [`colony_2d.cc`][colony_2d] — queued construction, OnDirty topology,
  movement-class agents, and `DeltaFrame` consumption in one schedule loop.
- [`ant_farm_vertical.cc`][ant_farm] — many agents sharing a cached distance
  field in a degenerate-axis vertical world.
- [`congestion_pricing.cc`][congestion_pricing] — separate terrain and
  congestion fields, affected-route replanning, and clean disable behavior
  using public APIs.
- [`sparse_stream.cc`][sparse_stream] — a 1,024-chunk world held to a 16-page
  budget whose query reports `Indeterminate` until required data is resident.
- [`chunk_maintenance.cc`][chunk_maintenance] — external ownership of a
  versioned derived summary with explicit immediate and deferred backends.
- [`render_delta_consumer.cc`][render_delta] — a standalone consumer that
  rebuilds a shadow grid from published `DeltaFrame` values.

## Optional integrations

- [`custom_ecs_min.cc`][custom_ecs] — the ECS adapter concepts implemented
  by a deliberately non-EnTT-shaped micro ECS.
- [`entt_pawns.cc`][entt_pawns] — the EnTT adapter driving registry-owned
  pawns (built when `TESS_ENABLE_ENTT` is on).
- [`flecs_pawns.cc`][flecs_pawns] — the Flecs adapter driving world-owned
  pawns (built when `TESS_ENABLE_FLECS` is on).
- [`web_pathfinder`][web_pathfinder] — the source of the live demo above:
  a single-threaded WebAssembly build with a small JavaScript shell.
- [`web_flow_steering`][web_flow_steering_src] — a shared native model, narrow
  Wasm adapter, and DOM-controlled presentation for distance-label steering.
- [`web_colony`][web_colony_src] — the colony model, native check, Wasm
  adapter, browser controller, and page kept in separate layers.
- [`web_congestion`][web_congestion_src] — screened congestion policies over
  the colony model, with the same model exercised by a native runner.
- [`web_traffic`][web_traffic_src] — the Traffic Lab model and its separately
  rendered browser presentation.
- [`web_diagnostics`][web_diagnostics] — a separately compiled diagnostics
  host over the shared colony model, with a native self-check and the Dear
  ImGui GLFW/WebGL2 [browser tutorial](../demo/diagnostics/).
- [`web_sparse_stream`][web_sparse_stream_src] — deterministic terrain,
  bounded sparse residency, native invariants, a narrow Wasm adapter, and an
  accessible camera-following presentation.

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
[web_flow_steering_src]: https://github.com/kindjie/tess/tree/main/examples/web_flow_steering
[web_diagnostics]: https://github.com/kindjie/tess/tree/main/examples/web_diagnostics
[web_sparse_stream_src]: https://github.com/kindjie/tess/tree/main/examples/web_sparse_stream
