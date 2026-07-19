// [quickstart]
#include <tess/pathfinding.h>

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
// [quickstart]
