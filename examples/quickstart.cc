#include <tess/core/config.h>

#include <exception>

// [readme-astar]
#include <tess/io.h>
#include <tess/pathfinding.h>

#include <cstdint>
#include <iostream>

// 1. Define a 4x4 2D grid and the data stored for each tile.
struct PassableTag {};
using Shape = tess::Shape<tess::Extent3{4, 4, 1}, tess::Extent3{4, 4, 1}>;
using Schema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

auto run_example() -> int {
  // 2. Create the world and mark the tiles that can be crossed.
  World world;  // Zero-initialized: every tile starts blocked.
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      world.field<PassableTag>(tess::Coord3{x, y, 0}) = 1;
    }
  }

  // 3. Reuse this scratch storage for repeated path queries.
  tess::PathScratch scratch;
  const auto result = tess::astar_path<World, PassableTag>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{2, 1, 0}},
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
// [readme-astar]

int main() {
#if TESS_HAS_EXCEPTIONS
  try {
    return run_example();
  } catch (const std::exception& error) {
    std::cerr << "quickstart failed: " << error.what() << "\n";
    return 1;
  }
#else
  return run_example();
#endif
}
