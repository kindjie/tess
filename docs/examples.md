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

- [Live pathfinder](https://tess.owx.dev/demo/) — the interactive
  WebAssembly demo, built from the same C++20 headers as the library and
  published with this site.
- [Live colony](https://tess.owx.dev/demo/colony/) — the scale demo:
  up to 1,024 agents shuttling between the edges and replanning around
  walls you draw (walls survive resets), with completed and crowd-turnaround
  leg counters, smooth presentation-only movement, a C++-update cost readout,
  and a retained-routes vs replan-every-tick toggle.
- [Live diagnostics](https://tess.owx.dev/demo/diagnostics/) — real Dear ImGui
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

- [`mvp_path.cc`][mvp_path] — a small end-to-end queued-edit plus A*
  pathfinding prototype.
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
  [live colony demo](https://tess.owx.dev/demo/colony/): the colony_2d
  composition compiled to WebAssembly. Its model, Wasm adapter, native
  self-check, browser controller, and page are separate so the library
  patterns are visible without platform glue interrupting them.
- [`sparse_stream.cc`][sparse_stream] — budget-bounded sparse residency:
  a 1,024-chunk world held to a 16-page budget (64x less resident field
  storage), and a path query that reports `Indeterminate` until the
  missing bridge chunk is streamed in and the retry succeeds.

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
  [live diagnostics demo](https://tess.owx.dev/demo/diagnostics/).

[quickstart]: https://github.com/kindjie/tess/blob/main/examples/quickstart.cc
[colony_2d]: https://github.com/kindjie/tess/blob/main/examples/colony_2d.cc
[mvp_path]: https://github.com/kindjie/tess/blob/main/examples/mvp_path.cc
[path_agents]: https://github.com/kindjie/tess/blob/main/examples/path_agents.cc
[stairs_3d]: https://github.com/kindjie/tess/blob/main/examples/stairs_3d.cc
[ant_farm]: https://github.com/kindjie/tess/blob/main/examples/ant_farm_vertical.cc
[sparse_stream]: https://github.com/kindjie/tess/blob/main/examples/sparse_stream.cc
[web_colony_src]: https://github.com/kindjie/tess/tree/main/examples/web_colony
[custom_ecs]: https://github.com/kindjie/tess/blob/main/examples/custom_ecs_min.cc
[entt_pawns]: https://github.com/kindjie/tess/blob/main/examples/entt_pawns.cc
[flecs_pawns]: https://github.com/kindjie/tess/blob/main/examples/flecs_pawns.cc
[render_delta]: https://github.com/kindjie/tess/blob/main/examples/render_delta_consumer.cc
[web_pathfinder]: https://github.com/kindjie/tess/tree/main/examples/web_pathfinder
[web_diagnostics]: https://github.com/kindjie/tess/tree/main/examples/web_diagnostics
