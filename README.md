# tess

[![CI](https://github.com/kindjie/tess/actions/workflows/ci.yml/badge.svg)](https://github.com/kindjie/tess/actions/workflows/ci.yml)

`tess` is a performance-first, header-only C++20 tile and path simulation
substrate: small spatial pieces composed into large, structured worlds for
fast simulation, topology, and pathfinding.

The development version is `0.3.0`. All `0.x` releases are pre-stable:
public APIs and data layouts may change without compatibility shims while
the design is still being validated. Release notes live in
[`CHANGELOG.md`](CHANGELOG.md). Repository provenance across the
pre-public rewrite is described in [`docs/history.md`](docs/history.md).

## Features

- Constexpr world shapes with one model for 2D, vertical 2D, and 3D,
  including degenerate axes.
- Chunk-local SoA field storage with optional sparse residency and a
  byte-budgeted residency manager.
- Queued operations with write-policy enforcement, result channels, and
  planned parallel execution.
- A simulation schedule with cadences, budgets, auto-exec tasks, and a
  selectable parallel phase executor.
- Movement classes with per-class topology, transition providers (for
  example stairs across z-levels), and a region-graph reachability
  precheck that rejects impossible queries before search.
- A* and weighted routing with route caches, portal-segment caches, and
  shared distance-field products.
- An ECS adapter defined by concepts (EnTT adapter included, gated
  behind `TESS_ENABLE_ENTT`).
- A versioned DeltaFrame render bridge for decoupled render consumers.
- Compile-gated diagnostics with optional Dear ImGui panels.
- A GPU backend interface (interface only in the current release).

## Requirements

- A C++20 compiler (Clang, GCC, AppleClang, or MSVC)
- CMake 3.25 or newer
- Git and network access for developer presets that fetch test dependencies

The installed library is header-only. GoogleTest, Google Benchmark, and EnTT
are development or optional integration dependencies; ordinary consumers do
not link them through `tess::tess`.

## Install and consume

```sh
cmake --preset release
cmake --build --preset release
cmake --install build/release --prefix "$HOME/.local"
```

```cmake
find_package(tess 0.3 CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE tess::tess)
```

For source-tree integration, add this repository with `add_subdirectory` and
link the same `tess::tess` target. Set `TESS_BUILD_TESTING`,
`TESS_BUILD_EXAMPLES`, and `TESS_BUILD_BENCHMARKS` to `OFF` in a parent build;
they are project-development facilities rather than consumer requirements.

The public CMake target is `tess::tess`. `tess/tess.h` is the
dependency-free umbrella for the core library. The EnTT adapter and Dear
ImGui panels are opt-in headers that consumers include explicitly after
their corresponding third-party header; see `docs/architecture/ecs.md` and
`docs/architecture/diagnostics.md`. In compile-sensitive code, prefer the
narrowest public header that owns the API.

## Quickstart

Declare a world shape and field schema, open some tiles, and run A*:

```cpp
#include <tess/tess.h>

#include <cstdint>

struct PassableTag {};

using Shape = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{4, 4, 1}>;
using Schema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

int main() {
  World world;  // zero-initialized: every tile starts blocked
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 8; ++x)
      world.field<PassableTag>(tess::Coord3{x, y, 0}) = 1;

  tess::PathScratch scratch;
  const auto result = tess::astar_path<World, PassableTag>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}},
      scratch);
  if (result.status == tess::PathStatus::Found) {
    // result.path: Coord3 view from start to goal (borrows `scratch`)
  }
}
```

Chunk dimensions must be powers of two that evenly divide the world
dimensions. From here, [`docs/getting-started.md`](docs/getting-started.md)
walks the full concept ladder up to the schedule loop, and
`examples/mvp_path.cc` shows the same path query driven through queued,
write-policy-checked edits.

## Examples

The `dev` preset builds the examples; each is a self-checking binary,
smoke-run in CI:

- `examples/tess_mvp_path` — a small end-to-end queued-edit plus A*
  pathfinding prototype.
- `examples/tess_path_agents` — a multi-agent path-agent tick loop with
  goal assignment, dirty-driven replanning, and blocked-path handling.
- `examples/tess_colony_2d` — the flagship composition: queued
  construction edits through the auto-exec schedule task, an OnDirty
  topology rebuild, movement-class agents routing around the new wall,
  and a DeltaFrame render consumer, all in one `tess::Schedule` loop.
- `examples/tess_ant_farm_vertical` — a degenerate-axis vertical world
  (x-z cross-section) sharing one distance-field product across ants via
  the byte-budgeted `FieldProductCache`.
- `examples/tess_stairs_3d` — the `StairTransitions` provider connecting
  two z-levels, with reachability, the path-runtime precheck, and an
  incremental update after demolishing the stair.
- `examples/tess_custom_ecs_min` — the ECS adapter concepts implemented
  by a deliberately non-EnTT-shaped micro ECS.
- `examples/tess_entt_pawns` — the EnTT adapter driving registry-owned
  pawns (built when `TESS_ENABLE_ENTT` is on).
- `examples/tess_render_delta_consumer` — a standalone DeltaFrame
  consumer rebuilding a shadow grid from published frames.

## Documentation

- [`docs/getting-started.md`](docs/getting-started.md): tutorial from
  shapes and schemas to the schedule loop and render bridge.
- [`docs/architecture/README.md`](docs/architecture/README.md):
  maintained design notes tracking the current implementation.
- [`docs/history.md`](docs/history.md): repository provenance and how to
  interpret retained pre-public pull requests.

## Benchmarks

![Benchmark trend snapshot](docs/assets/benchmark-trends.svg)

A labeled snapshot from CI benchmark runs; it may be stale by a few
commits. See [`docs/performance.md`](docs/performance.md) for the trend
workflow and [`CONTRIBUTING.md`](CONTRIBUTING.md) for the threshold gates
and calibration policy.

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md) for the developer workflow:
presets, quality gates, benchmarks, and Steam Deck testing. Install the
local git hooks first with `python3 tools/git_hooks.py install`.

## Name

`tess` is named after tesserae and tessellation: small spatial pieces composed
into large, structured worlds for fast simulation, topology, and pathfinding.

## License

Licensed under the [MIT License](LICENSE).
