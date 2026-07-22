<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)"
            srcset="docs/assets/tess-logo-dark.svg">
    <img src="docs/assets/tess-logo.svg"
         alt="tess"
         width="520">
  </picture>
</p>

# tess

[![CI](https://github.com/kindjie/tess/actions/workflows/ci.yml/badge.svg)](https://github.com/kindjie/tess/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/kindjie/tess)](https://github.com/kindjie/tess/releases/latest)
[![License: MIT](https://img.shields.io/github/license/kindjie/tess)](LICENSE)
[![Docs](https://img.shields.io/badge/docs-tess.owx.dev-673ab7)](https://tess.owx.dev/)

`tess` is a performance-first, header-only C++20 tile and path simulation
substrate: small spatial pieces composed into large, structured worlds for
fast simulation, topology, and pathfinding.

Use tess when a simulation needs bounded grid storage, topology-aware routing,
or deterministic queued updates without committing to an engine. It is a good
fit for games, colony simulations, robotics prototypes, and headless spatial
models. It is not a renderer, physics engine, navigation-mesh generator, or
drop-in ECS.

The latest release is `v0.4.0`; this checkout documents the
`v0.4.0` release. tess is pre-1.0 — see
[support and compatibility](https://tess.owx.dev/support/) for the stability
policy. Release notes live in [`CHANGELOG.md`](CHANGELOG.md).

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

Build and run it, along with every other dependency-free example:

```sh
cmake --preset examples
cmake --build --preset examples
./build/examples/examples/tess_quickstart
```

Expected output:

<!-- tess-output: quickstart source=examples/quickstart.cc -->
```text
path cost: 14
expanded nodes: 15
```
<!-- /tess-output -->

Chunk dimensions must be powers of two that evenly divide the world
dimensions. From here, the
[getting-started tutorial](https://tess.owx.dev/getting-started/) walks the
full concept ladder up to the schedule loop and render bridge.

## Use in your project

tess is header-only and needs a C++20 compiler and CMake 3.25 or newer:

```cmake
include(FetchContent)
FetchContent_Declare(
  tess
  GIT_REPOSITORY https://github.com/kindjie/tess.git
  GIT_TAG v0.4.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(tess)
target_link_libraries(my_target PRIVATE tess::tess)
```

For an installed `find_package` package, install prefixes, include-surface
guidance, and package-manager status, see
[Installation](https://tess.owx.dev/packaging/).

## Documentation

- [tess.owx.dev](https://tess.owx.dev/) — the documentation site, including
  the interactive WebAssembly pathfinding demo.
- [Getting started](https://tess.owx.dev/getting-started/) — tutorial from
  shapes and schemas to the schedule loop and render bridge.
- [API reference](https://tess.owx.dev/api/) — generated documentation for
  the supported C++ surface.
- [Examples](https://tess.owx.dev/examples/) — the annotated catalog of
  self-checking example programs.

## Examples

- [`examples/quickstart.cc`](examples/quickstart.cc) — the complete program
  shown above.
- [`examples/colony_2d.cc`](examples/colony_2d.cc) — the flagship
  composition: queued construction edits, an OnDirty topology rebuild,
  movement-class agents routing around the new wall, and a DeltaFrame render
  consumer, all in one `tess::Schedule` loop.
- [`examples/web_pathfinder`](examples/web_pathfinder) — the interactive
  WebAssembly pathfinder, [live on the documentation
  site](https://tess.owx.dev/demo/).

Every example is annotated in the
[example catalog](https://tess.owx.dev/examples/).

## Performance

Representative medians on an Apple M3 Max (single-threaded), enforced by
calibrated CI ceilings:

- A* across an open 512x512 grid, corner to corner: ~2.1 us.
- One clean tick of 100 path agents with retained routes: ~330 ns.
- One `weighted_path_batch` plan of 100 near-goal requests on a 512x512
  grid: ~50 us.

Details and trend snapshots: [Performance](https://tess.owx.dev/performance/).

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md) for the developer workflow:
presets, quality gates, benchmarks, and documentation tooling. Install the
local git hooks first with `python3 tools/git_hooks.py install`.

## Name

`tess` is named after tesserae and tessellation: small spatial pieces composed
into large, structured worlds for fast simulation, topology, and pathfinding.

## License

Licensed under the [MIT License](LICENSE).
