<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)"
            srcset="docs/assets/tess-logo-dark.svg">
    <img src="docs/assets/tess-logo.svg"
         alt="tess"
         width="520">
  </picture>
</p>

# Pathfinding and Simulation for 2D and 3D Grid Worlds

[![CI](https://github.com/kindjie/tess/actions/workflows/ci.yml/badge.svg?branch=main&event=push)](https://github.com/kindjie/tess/actions/workflows/ci.yml?query=branch%3Amain+event%3Apush)
[![Release](https://img.shields.io/github/v/release/kindjie/tess)](https://github.com/kindjie/tess/releases/latest)
[![License: MIT](https://img.shields.io/github/license/kindjie/tess)](LICENSE)
[![Docs](https://img.shields.io/badge/docs-tess.owx.dev-673ab7)](https://tess.owx.dev/)

`tess` is a fast, header-only C++20 library for pathfinding and deterministic
simulation in 2D and 3D grid worlds. It handles tile storage, paths, agent
movement, and change tracking while your application owns rendering, physics,
and game logic.

Use it for games, colony simulations, robotics prototypes, and headless
systems. tess is not an engine, renderer, physics system, navigation-mesh
library, or entity-component system.

[See A*, route caching, batches, and distance fields in
action →](https://tess.owx.dev/pathfinding-strategy-comparison/)

## Capabilities

- **[2D and 3D worlds][getting-started]** — use the same shape and coordinate
  model for top-down grids, vertical cross-sections, and full 3D spaces.
- **[Pathfinding][pathfinding-guide]** — run unit-cost or weighted A*, define
  custom movement rules, and reject unreachable routes before doing a full
  search.
- **[Reusable path work][strategy-comparison]** — share exact route caches,
  batches, and distance fields across repeated queries and groups of agents.
- **[Small or very large worlds][residency-guide]** — keep every chunk in
  memory or load chunks on demand within a fixed memory budget.
- **[Deterministic simulation][simulation-concepts]** — queue changes,
  schedule systems, move agents, and resolve conflicts in a repeatable order.
- **[Spatial data and persistence][concepts]** — store typed tile fields, run
  [spatial queries][query-concepts], and save
  [versioned world archives][persistence-concepts].
- **[Engine integration][examples]** — use the dependency-free core alone or
  add optional EnTT, Flecs, Dear ImGui, and WebGPU adapters.
- **[Rendering and diagnostics][presentation-guide]** — send versioned change
  sets to an external renderer and enable
  [instrumentation][diagnostics-guide] when it is needed.

[getting-started]: https://tess.owx.dev/getting-started/
[pathfinding-guide]: https://tess.owx.dev/guide/pathfinding/
[strategy-comparison]: https://tess.owx.dev/pathfinding-strategy-comparison/
[residency-guide]: https://tess.owx.dev/guide/residency/
[simulation-concepts]: https://tess.owx.dev/architecture/simulation/
[concepts]: https://tess.owx.dev/architecture/
[query-concepts]: https://tess.owx.dev/architecture/query/
[persistence-concepts]: https://tess.owx.dev/architecture/persistence/
[examples]: https://tess.owx.dev/examples/
[presentation-guide]: https://tess.owx.dev/guide/presentation/
[diagnostics-guide]: https://tess.owx.dev/guide/diagnostics/

## Start with A*

After including `<tess/pathfinding.h>` and the optional stream helpers in
`<tess/io.h>`, define a small world, open its tiles, and inspect the result
of an A* query. This excerpt comes from a complete example that is compiled
and run in CI:

<!-- tess-snippet: readme-astar source=examples/quickstart.cc -->
```cpp
#include <tess/io.h>
#include <tess/pathfinding.h>

#include <cstdint>
#include <iostream>

// 1. Define a 4x4 2D grid and the data stored for each tile.
struct PassableTag {};
using Shape = tess::Shape<tess::Extent3{4, 4}, tess::Extent3{4, 4}>;
using Schema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

auto run_example() -> int {
  // 2. Create the world and mark the tiles that can be crossed.
  World world;  // Zero-initialized: every tile starts impassable.
  world.fill_field<PassableTag>(1);  // All tiles passable for this example.

  // 3. Reuse this scratch storage for repeated path queries.
  tess::PathScratch scratch;
  const auto result = tess::astar_path<World, PassableTag>(
      world, tess::PathRequest{tess::Coord2{0, 0}, tess::Coord2{2, 1}},
      scratch);

  // 4. Check the status, then print the path coordinates and total cost.
  if (result.status != tess::PathStatus::Found || result.path.empty()) {
    std::cerr << "path query failed: " << result.status << '\n';
    return 1;
  }

  std::cout << "path: " << result.path << '\n';
  std::cout << "cost: " << result.cost << '\n';
  return 0;
}
```
<!-- /tess-snippet -->

The query prints:

<!-- tess-output: quickstart source=examples/quickstart.cc -->
```text
path: [Coord3{0, 0, 0}, Coord3{1, 0, 0}, Coord3{2, 0, 0}, Coord3{2, 1, 0}]
cost: 3
```
<!-- /tess-output -->

`result.path` is a read-only view of `tess::Coord3` values. It borrows the
scratch storage and remains valid only until that scratch is reused. The
[complete quickstart](examples/quickstart.cc) includes the executable error
boundary; the
[getting-started tutorial](https://tess.owx.dev/getting-started/) builds from
shapes and schemas through the schedule loop and render bridge.

## Use in your project

tess is header-only and requires a C++20 compiler. Choose the integration that
fits your project.

- **Portable archive:** For releases that provide one, download the `.zip` or
  `.tar.gz`, extract it anywhere, and add its `include` directory to your
  compiler search path.
- **CMake:** Pin tess with `FetchContent`, vendor it with `add_subdirectory`,
  or use an installed copy with `find_package`; then link `tess::tess`.
- **Package managers:** The repository includes a Conan 2 recipe and a vcpkg
  overlay for local use. Publication in their central registries is planned
  after 1.0.

The [installation guide](https://tess.owx.dev/packaging/) has the exact
commands and archive verification steps.

The latest release is `v0.13.0`. This checkout documents the unreleased
`v1.0.0` development API (release candidate `v1.0.0-rc.1`),
including its portable `.zip` and `.tar.gz` header archives. Before 1.0, public
APIs may change between minor versions. See
[support and compatibility](https://tess.owx.dev/support/) and the
[roadmap](https://tess.owx.dev/roadmap/).

## Performance

Representative single-threaded medians on an Apple M3 Max:

- Open 512x512 A*, corner to corner: ~2.1 us.
- Clean tick for 100 agents with retained routes: ~330 ns.
- Weighted batch of 100 near-goal requests on a 512x512 grid: ~50 us.

The [performance page](https://tess.owx.dev/performance/) records the benchmark
protocol, calibrated CI ceilings, and trend snapshots.

## Learn more

- [Choose your architecture](https://tess.owx.dev/guide/) — residency,
  writes, path strategy, topology, entities, presentation, and diagnostics.
- [Compare pathfinding
  strategies](https://tess.owx.dev/pathfinding-strategy-comparison/)
  — see A*, route caching, weighted batches, and distance fields operate over
  the same obstacle map.
- [Examples](https://tess.owx.dev/examples/) — annotated, self-checking
  programs from the quickstart to the flagship colony simulation.
- [API reference](https://tess.owx.dev/api/) — the supported C++ surface.
- [Roadmap](https://tess.owx.dev/roadmap/) — implemented, planned, and
  deliberately excluded work.

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md) for presets, quality gates,
benchmarks, and documentation tooling. Install the local hooks first with
`python3 tools/git_hooks.py install`.

## License

Licensed under the [MIT License](LICENSE).
