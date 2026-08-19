#include <tess/core/config.h>

#include <exception>

// [readme-astar]
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
