# tess

[![CI](https://github.com/kindjie/tess/actions/workflows/ci.yml/badge.svg)](https://github.com/kindjie/tess/actions/workflows/ci.yml)

`tess` is a performance-first, header-only C++20 tile and path simulation
substrate: small spatial pieces composed into large, structured worlds for
fast simulation, topology, and pathfinding.

Use tess when a simulation needs bounded grid storage, topology-aware routing,
or deterministic queued updates without committing to an engine. It is a good
fit for games, colony simulations, robotics prototypes, and headless spatial
models. It is not a renderer, physics engine, navigation-mesh generator, or
drop-in ECS, and its pre-1.0 API is still evolving.

The latest release is `v0.3.0`. All `0.x` releases are pre-stable:
public APIs and data layouts may change without compatibility shims while
the design is still being validated. Release notes live in
[`CHANGELOG.md`](CHANGELOG.md). Repository provenance across the
pre-public rewrite is described in [`docs/history.md`](docs/history.md).

This checkout documents the unreleased `v0.4.0` development API. The
`v0.3.0` tag and its README remain the authority for the latest release.

## Features

- Constexpr world shapes with one model for 2D, vertical 2D, and 3D,
  including degenerate axes.
- Chunk-local SoA field storage with optional sparse residency and a
  byte-budgeted residency manager.
- Queued operations with write-policy enforcement, result channels, and
  plan-driven parallel execution.
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

The installed library is header-only, and installing it needs no network
access and builds no code. GoogleTest, Google Benchmark, and EnTT are
development or optional integration dependencies fetched only by
developer presets; ordinary consumers do not link them through
`tess::tess`.

## Install and consume

The `consumer` preset configures a headers-only install: no tests,
examples, benchmarks, warnings-as-errors, or network fetches.

```sh
cmake --preset consumer
cmake --install build/consumer --prefix "$HOME/.local"
```

When installing to a non-system prefix, point the consuming configure step at
it:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="$HOME/.local"
```

(Equivalently, without presets:
`cmake -B build -DTESS_BUILD_TESTING=OFF -DTESS_BUILD_EXAMPLES=OFF`
followed by `cmake --install build --prefix ...`.)

The commands above install this checkout's development package:

```cmake
find_package(tess 0.4 CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE tess::tess)
```

For the latest released source, use CMake's `FetchContent` and pin its tag:

```cmake
include(FetchContent)
FetchContent_Declare(
  tess
  GIT_REPOSITORY https://github.com/kindjie/tess.git
  GIT_TAG v0.3.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(tess)
target_link_libraries(my_target PRIVATE tess::tess)
```

Pin a release tag or commit rather than a moving branch. Tests, examples, and
benchmarks default off when tess is a subproject, so this path does not fetch
its development dependencies.

The public CMake target is `tess::tess`. In the development API, start with
`<tess/pathfinding.h>` for worlds and routing, `<tess/simulation.h>` for the
full simulation stack, or `<tess/tess.h>` for the all-in-one compatibility
umbrella. The facade headers are new in the unreleased `v0.4.0` line;
`v0.3.0` consumers should include `<tess/tess.h>`.
The EnTT adapter and Dear ImGui panels are opt-in headers that consumers
include after their corresponding third-party header; see
`docs/architecture/ecs.md` and `docs/architecture/diagnostics.md`. In
compile-sensitive code, prefer the narrowest public header that owns the API.

## Quickstart

Declare a world shape and field schema, open some tiles, and run A*:

<!-- tess-snippet: quickstart source=examples/quickstart.cc -->
```cpp
#include <tess/tess.h>

#include <cstdint>
#include <iostream>

struct PassableTag {};

using Shape = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{4, 4, 1}>;
using Schema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

int main() {
  World world;  // Zero-initialized: every tile starts blocked.
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      world.field<PassableTag>(tess::Coord3{x, y, 0}) = 1;
    }
  }

  tess::PathScratch scratch;
  const auto result = tess::astar_path<World, PassableTag>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}},
      scratch);
  if (result.status != tess::PathStatus::Found) {
    std::cerr << "path not found\n";
    return 1;
  }

  std::cout << "path cost: " << result.cost << "\n";
  std::cout << "expanded nodes: " << result.expanded_nodes << "\n";
  return 0;
}
```
<!-- /tess-snippet -->

Build every dependency-free example without downloading test dependencies:

```sh
cmake --preset examples
cmake --build --preset examples
./build/examples/examples/tess_quickstart
```

Expected output:

```text
path cost: 14
expanded nodes: 15
```

Chunk dimensions must be powers of two that evenly divide the world
dimensions. From here, [`docs/getting-started.md`](docs/getting-started.md)
walks the full concept ladder up to the schedule loop, and
`examples/mvp_path.cc` shows the same path query driven through queued,
write-policy-checked edits.

## Examples

The `examples` and `dev` presets build the examples; each is a self-checking
binary (built as `tess_<name>`), smoke-run in CI:

- [`examples/web_pathfinder`](examples/web_pathfinder) — an interactive,
  single-threaded WebAssembly pathfinder built from the same C++20 headers and
  published with the documentation site.
- [`examples/quickstart.cc`](examples/quickstart.cc) — the complete program
  shown above.
- [`examples/mvp_path.cc`](examples/mvp_path.cc) — a small end-to-end
  queued-edit plus A*
  pathfinding prototype.
- [`examples/path_agents.cc`](examples/path_agents.cc) — a multi-agent
  path-agent tick loop with
  goal assignment, dirty-driven replanning, and blocked-path handling.
- [`examples/colony_2d.cc`](examples/colony_2d.cc) — the flagship
  composition: queued
  construction edits through the auto-exec schedule task, an OnDirty
  topology rebuild, movement-class agents routing around the new wall,
  and a DeltaFrame render consumer, all in one `tess::Schedule` loop.
- [`examples/ant_farm_vertical.cc`](examples/ant_farm_vertical.cc) — a
  degenerate-axis vertical world
  (x-z cross-section) sharing one distance-field product across ants via
  the byte-budgeted `FieldProductCache`.
- [`examples/stairs_3d.cc`](examples/stairs_3d.cc) — the
  `StairTransitions` provider connecting
  two z-levels, with reachability, the path-runtime precheck, and an
  incremental update after demolishing the stair.
- [`examples/custom_ecs_min.cc`](examples/custom_ecs_min.cc) — the ECS
  adapter concepts implemented
  by a deliberately non-EnTT-shaped micro ECS.
- [`examples/entt_pawns.cc`](examples/entt_pawns.cc) — the EnTT adapter
  driving registry-owned
  pawns (built when `TESS_ENABLE_ENTT` is on).
- [`examples/render_delta_consumer.cc`](examples/render_delta_consumer.cc) — a
  standalone DeltaFrame
  consumer rebuilding a shadow grid from published frames.

## Documentation

- [`docs/getting-started.md`](docs/getting-started.md): tutorial from
  shapes and schemas to the schedule loop and render bridge.
- [`docs/architecture/README.md`](docs/architecture/README.md):
  maintained design notes tracking the current implementation.
- [`docs/history.md`](docs/history.md): repository provenance and how to
  interpret retained pre-public pull requests.

A local Doxygen API reference can be generated with
`cmake --preset consumer -DTESS_BUILD_DOCS=ON` followed by
`cmake --build build/consumer --target tess_docs` (requires Doxygen);
output lands in `build/consumer/docs/html`.

## Benchmarks

For scale, representative medians from the benchmark suite on an
Apple M3 Max (single-threaded):

- A* across an open 512x512 grid, corner to corner (a 1,022-step path,
  ~1,023 nodes expanded): ~2.1 us; the weighted variant: ~2.4 us.
- One clean tick of 100 path agents with retained routes: ~330 ns.

Every suite is also gated in CI with calibrated per-benchmark ceilings,
so these characteristics are enforced, not aspirational.

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
