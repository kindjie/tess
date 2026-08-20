---
title: C++20 Grid Pathfinding and Simulation Library
description: >-
  tess is a header-only C++20 library for grid pathfinding, tile-world
  storage, and deterministic simulation in games and headless systems.
---

<div class="tess-hero" markdown>

![tess](assets/tess-logo.svg#only-light){ width="520" }
![tess](assets/tess-logo-dark.svg#only-dark){ width="520" }

# C++20 grid pathfinding and simulation, without an engine

`tess` is a header-only C++20 library for bounded grid storage,
topology-aware pathfinding, and deterministic simulation updates. It supplies
the spatial substrate while leaving rendering, physics, and entity ownership
to your application.

This site documents the `v0.12.0` release and identifies unreleased changes
already landed on `main` in the [roadmap](roadmap.md). tess is pre-1.0 — see
[support and compatibility](support.md) for the stability policy.
{ .tess-version }

[Get started](getting-started.md){ .md-button .md-button--primary }
[Try the live pathfinder](demo/){ .md-button }
[Watch a colony scale](demo/colony/){ .md-button }
[Inspect live diagnostics](demo/diagnostics/){ .md-button }
[API reference](https://tess.owx.dev/api/){ .md-button }

</div>

## A complete path query

The core of a complete world-shape, field-schema, and A* example (compiled and
run in CI):

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
  World world;  // Zero-initialized: every tile starts blocked.
  world.fill_field<PassableTag>(1);  // Open every tile for this example.

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

It prints:

<!-- tess-output: quickstart source=examples/quickstart.cc -->
```text
path: [Coord3{0, 0, 0}, Coord3{1, 0, 0}, Coord3{2, 0, 0}, Coord3{2, 1, 0}]
cost: 3
```
<!-- /tess-output -->

Add it to your build with one CMake `FetchContent` block or an installed
package — see [Installation](packaging.md).

## Choose the smallest surface

- `<tess/pathfinding.h>` provides shapes, worlds, topology, and routing.
- `<tess/simulation.h>` adds queued operations, schedules, agents, and ECS
  concepts.
- `<tess/tess.h>` remains the compatibility umbrella.

All three are dependency-free. Optional EnTT and Flecs adapters, plus the
separate Dear ImGui panels, remain behind explicit integration headers and
compile definitions.

## Who is tess for?

- **Game and colony-sim developers** — construction edits invalidate
  routes mid-tick, many agents replan around the change, and the whole
  loop stays deterministic. Start with the
  [tutorial](getting-started.md) and `examples/colony_2d.cc`.
- **Engine integrators** — a substrate that owns execution, not your
  loop: versioned [DeltaFrames](architecture/simulation.md) feed your
  renderer and [adapter concepts](architecture/ecs.md) bind your ECS.
- **Robotics prototypers** — occupancy grids, feasibility prechecks, and
  dirty-driven replanning with reproducible fixed-step runs; see the
  [robotics walkthrough](use-cases.md).
- **Headless simulation and research** — agent-based models and servers
  run the same loop with no renderer at all; see
  [use cases](use-cases.md) and the machine-adoption recipe in
  [for agents](for-agents.md).

## Where next

<div class="grid cards" markdown>

- :material-school:{ .lg .middle } __Getting started__

    ---

    The concept ladder: shapes, schemas, worlds, writes, pathfinding,
    topology, and the schedule loop.

    [:octicons-arrow-right-24: Tutorial](getting-started.md)

- :material-play-box-multiple:{ .lg .middle } __Examples__

    ---

    An annotated catalog of self-checking programs, from the quickstart
    to the flagship colony simulation.

    [:octicons-arrow-right-24: Example catalog](examples.md)

- :material-layers-triple:{ .lg .middle } __Concepts__

    ---

    Maintained design notes for every layer, from shapes and storage to
    simulation and diagnostics.

    [:octicons-arrow-right-24: Architecture overview](architecture/README.md)

- :material-speedometer:{ .lg .middle } __Performance__

    ---

    Representative medians and CI-enforced benchmark ceilings.

    [:octicons-arrow-right-24: Benchmarks](performance.md)

</div>
