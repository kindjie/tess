# Examples

Every example is a complete, self-checking program built by the `examples`
and `dev` presets as `tess_<name>`:

```sh
cmake --preset examples
cmake --build --preset examples
./build/examples/examples/tess_quickstart
```

## Start here

- [Live pathfinder](../demo/) — the interactive WebAssembly demo, built
  from the same C++20 headers as the library and published with this site.
- [`quickstart.cc`][quickstart] — the complete program on the
  [home page](index.md): a world, a schema, and an A* query.
- [`colony_2d.cc`][colony_2d] — the flagship composition: queued
  construction edits through the auto-exec schedule task, an OnDirty
  topology rebuild, movement-class agents routing around the new wall, and
  a DeltaFrame render consumer, all in one `tess::Schedule` loop.

## Pathfinding and topology

- [`mvp_path.cc`][mvp_path] — a small end-to-end queued-edit plus A*
  pathfinding prototype.
- [`path_agents.cc`][path_agents] — a multi-agent path-agent tick loop
  with goal assignment, dirty-driven replanning, and blocked-path
  handling.
- [`stairs_3d.cc`][stairs_3d] — the `StairTransitions` provider connecting
  two z-levels, with reachability, the path-runtime precheck, and an
  incremental update after demolishing the stair.
- [`ant_farm_vertical.cc`][ant_farm] — a degenerate-axis vertical world
  (x-z cross-section) sharing one distance-field product across ants via
  the byte-budgeted `FieldProductCache`.

## Integration boundaries

- [`custom_ecs_min.cc`][custom_ecs] — the ECS adapter concepts implemented
  by a deliberately non-EnTT-shaped micro ECS.
- [`entt_pawns.cc`][entt_pawns] — the EnTT adapter driving registry-owned
  pawns (built when `TESS_ENABLE_ENTT` is on).
- [`render_delta_consumer.cc`][render_delta] — a standalone DeltaFrame
  consumer rebuilding a shadow grid from published frames.
- [`web_pathfinder`][web_pathfinder] — the source of the live demo above:
  a single-threaded WebAssembly build with a small JavaScript shell.

[quickstart]: https://github.com/kindjie/tess/blob/main/examples/quickstart.cc
[colony_2d]: https://github.com/kindjie/tess/blob/main/examples/colony_2d.cc
[mvp_path]: https://github.com/kindjie/tess/blob/main/examples/mvp_path.cc
[path_agents]: https://github.com/kindjie/tess/blob/main/examples/path_agents.cc
[stairs_3d]: https://github.com/kindjie/tess/blob/main/examples/stairs_3d.cc
[ant_farm]: https://github.com/kindjie/tess/blob/main/examples/ant_farm_vertical.cc
[custom_ecs]: https://github.com/kindjie/tess/blob/main/examples/custom_ecs_min.cc
[entt_pawns]: https://github.com/kindjie/tess/blob/main/examples/entt_pawns.cc
[render_delta]: https://github.com/kindjie/tess/blob/main/examples/render_delta_consumer.cc
[web_pathfinder]: https://github.com/kindjie/tess/tree/main/examples/web_pathfinder
