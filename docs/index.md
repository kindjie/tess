# Tile worlds and paths, without an engine

`tess` is a header-only C++20 library for bounded grid storage,
topology-aware pathfinding, and deterministic simulation updates. It supplies
the spatial substrate while leaving rendering, physics, and entity ownership
to your application.

This site documents the unreleased `v0.4.0` development API. For the latest
release, use the
[`v0.3.0` documentation](https://github.com/kindjie/tess/tree/v0.3.0).

[Get started](getting-started.md){ .md-button .md-button--primary }
[Try the live pathfinder](demo/){ .md-button }
[Browse the source](https://github.com/kindjie/tess){ .md-button }

## A complete path query

This program is compiled and run in CI. The copy below is synchronized with
its canonical source file, so documentation drift fails the build.

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

It prints:

```text
path cost: 14
expanded nodes: 15
```

## Choose the smallest surface

- `<tess/pathfinding.h>` provides shapes, worlds, topology, and routing.
- `<tess/simulation.h>` adds queued operations, schedules, agents, and ECS
  concepts.
- `<tess/tess.h>` remains the compatibility umbrella.

The two facade headers are new in the unreleased `v0.4.0` API; `v0.3.0`
consumers use `<tess/tess.h>`. All three are dependency-free. Optional EnTT
and Dear ImGui adapters remain behind explicit integration headers and
compile definitions.
