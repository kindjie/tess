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

This site documents the unreleased `v1.0.0` development API (release
candidate `v1.0.0-rc.1`) and the path to 1.0 in the
[roadmap](roadmap.md). tess is pre-1.0 — see
[support and compatibility](support.md) for the stability policy.
{ .tess-version }

[Get started](getting-started.md){ .md-button .md-button--primary }
[Explore tutorials](tutorials.md){ .md-button }
[API reference](api/){ .md-button }

</div>

## Live demonstrations

<div class="grid cards tess-demo-cards" markdown>

- :material-map-marker-path:{ .lg .middle } __Live pathfinder__

    ---

    Move endpoints, paint obstacles, and inspect a real A* query compiled from
    the C++20 library.

    [:octicons-arrow-right-24: Open pathfinder](demo/)

- :material-routes:{ .lg .middle } __Strategy comparison__

    ---

    Compare A*, route caching, weighted batches, and shared-goal fields by
    their call shape and reuse counters.

    [:octicons-arrow-right-24: Compare strategies](demo/strategies/)

- :material-city:{ .lg .middle } __Colony simulation__

    ---

    Watch up to 1,024 agents replan around queued wall edits in a deterministic
    fixed-step model.

    [:octicons-arrow-right-24: Run colony](demo/colony/)

- :material-chart-timeline-variant:{ .lg .middle } __Live diagnostics__

    ---

    Inspect path, queued-phase, trace, timing, and consumer allocation
    snapshots through Dear ImGui and mirrored HTML controls.

    [:octicons-arrow-right-24: Inspect diagnostics](demo/diagnostics/)

- :material-transit-connection-variant:{ .lg .middle } __Congestion Lab__

    ---

    Explore supported, rejected, and experimental congestion-pricing variants
    over the colony workload.

    [:octicons-arrow-right-24: Open Congestion Lab](demo/congestion/)

- :material-highway:{ .lg .middle } __Traffic Lab__

    ---

    Run a large deterministic crowd model with explicit planning budgets and
    separately reported browser presentation costs.

    [:octicons-arrow-right-24: Open Traffic Lab](demo/traffic/)

- :material-tower-fire:{ .lg .middle } __Tower__

    ---

    Route through one six-floor world whose stair transitions connect the
    vertical topology.

    [:octicons-arrow-right-24: Climb the tower](demo/tower/)

- :material-expansion-card-variant:{ .lg .middle } __WebGPU integration__

    ---

    Exercise the optional GPU transport and submission boundary; pathfinding
    itself remains on the CPU.

    [:octicons-arrow-right-24: Inspect WebGPU](demo/webgpu/)

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

- :material-school:{ .lg .middle } __Tutorials__

    ---

    Connected learning paths for basic routing, colony composition, strategy
    selection, and congestion pricing.

    [:octicons-arrow-right-24: Explore tutorials](tutorials.md)

- :material-book-open-variant:{ .lg .middle } __Reference__

    ---

    Architecture contracts, terminology, compatibility evidence, and the
    generated C++ API.

    [:octicons-arrow-right-24: Reference map](reference.md)

- :material-speedometer:{ .lg .middle } __Performance__

    ---

    Representative medians and CI-enforced benchmark ceilings.

    [:octicons-arrow-right-24: Benchmarks](performance.md)

</div>
