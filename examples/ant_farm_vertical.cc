// A degenerate-axis vertical world: the classic glass-walled ant farm as
// an x-z cross-section (y extent 1). One shared distance-field product is
// flooded from every food chamber at once, each ant reads its nearest
// chamber from it with a single gradient descent, and the byte-budgeted
// FieldProductCache turns the second wave of ants into pure cache hits.
#include <tess/tess.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct PassableTag {};

using Shape = tess::Shape<tess::Extent3{24, 1, 12}, tess::Extent3{8, 1, 4}>;
using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

// The farm, top row first (surface at z=11): '#' dirt, '.' tunnel,
// 'F' food chamber. Two shafts drop from the surface into a branching
// gallery; the chambers sit at different depths.
constexpr std::array<std::string_view, 12> kFarm = {
    "........................",  // z = 11, the surface
    "####.#############.#####", "####.#############.#####",
    "####......########.#####",
    "####.#####.####....F####",  // east chamber, z = 7
    "####.#####.####.########", "####.#####....#.########",
    "####.########.#.########",
    "####F########.....######",  // west chamber, z = 3
    "#############.##########",
    "#############F##########",  // deep chamber, z = 1
    "########################",
};

auto tile_char(std::int64_t x, std::int64_t z) -> char {
  return kFarm.at(static_cast<std::size_t>(11 - z))
      .at(static_cast<std::size_t>(x));
}

auto run() -> int {
  World world;
  tess::GoalSet chambers;
  chambers.reserve(4);
  for (std::int64_t z = 0; z < 12; ++z) {
    for (std::int64_t x = 0; x < 24; ++x) {
      const auto glyph = tile_char(x, z);
      if (glyph == '#') {
        continue;
      }
      world.field<PassableTag>(tess::Coord3{x, 0, z}) = true;
      if (glyph == 'F') {
        chambers.add(tess::Coord3{x, 0, z});
      }
    }
  }

  // One flood serves every ant: the product is a reverse field from all
  // chambers over the dug tunnels.
  constexpr auto kTileCount = std::size_t{24} * 12;
  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(kTileCount);
  tess::DistanceFieldProduct product;
  product.reserve_goals(chambers.size());
  product.reserve_nodes(kTileCount);
  product.reserve_dependencies(World::chunk_count);
  const auto build = tess::build_distance_field_product<World, PassableTag>(
      world, chambers, scratch, product);
  if (build.status != tess::PathStatus::Found) {
    std::cerr << "flood found no chamber\n";
    return 1;
  }

  tess::FieldProductCache cache;
  cache.reserve_entries(2);
  if (!cache.store<World, PassableTag>(std::move(product))) {
    std::cerr << "cache rejected the product\n";
    return 1;
  }

  const auto ants = std::array<tess::Coord3, 3>{
      tess::Coord3{0, 0, 11},   // surface, far west
      tess::Coord3{22, 0, 11},  // surface, above the east shaft
      tess::Coord3{11, 0, 5},   // already inside the gallery
  };
  std::vector<tess::Coord3> first_route;
  for (const auto& ant : ants) {
    const auto* shared = cache.lookup<World, PassableTag>(world, chambers);
    if (shared == nullptr) {
      std::cerr << "expected a cache hit for the shared goal set\n";
      return 1;
    }
    const auto found =
        tess::nearest_target<World, PassableTag>(world, ant, *shared, scratch);
    if (found.status != tess::PathStatus::Found) {
      std::cerr << "an ant found no chamber\n";
      return 1;
    }
    std::cout << "ant at (" << ant.x << "," << ant.z << ") -> chamber ("
              << found.target.x << "," << found.target.z << "), " << found.cost
              << " steps\n";
    if (first_route.empty()) {
      first_route.assign(found.path.begin(), found.path.end());
    }
  }
  if (cache.stats().hits < ants.size()) {
    std::cerr << "the shared product was rebuilt instead of reused\n";
    return 1;
  }

  // The cross-section, with the first ant's route drawn in.
  std::cout << "\n";
  for (std::int64_t depth = 0; depth < 12; ++depth) {
    const auto z = 11 - depth;
    std::string row;
    for (std::int64_t x = 0; x < 24; ++x) {
      auto glyph = tile_char(x, z);
      for (const auto& step : first_route) {
        if (step.x == x && step.z == z && glyph == '.') {
          glyph = '*';
        }
      }
      row.push_back(glyph);
    }
    std::cout << row << "\n";
  }
  return 0;
}

}  // namespace

auto main() -> int {
  try {
    return run();
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << "\n";
    return 1;
  }
}
