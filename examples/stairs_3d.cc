// Vertical worlds with the StairTransitions provider: two z-levels whose
// chunks never touch through passable faces, connected only by an offset
// stair (foot tile -> one step sideways, one step up). The region graph
// built with the default adjacent-only provider says the platform is
// unreachable; the same graph built with StairTransitions restores the
// route, the path-runtime precheck agrees, and an incremental update
// after demolishing the stair severs it again.
#include <tess/tess.h>

#include <cstdint>
#include <exception>
#include <iostream>

namespace {

struct PassableTag {};
struct StairTag {};

// Two z-levels in separate chunks (chunk z extent 1), so every stair
// crosses a chunk boundary -- the case the provider exists for.
using Shape = tess::Shape<tess::Extent3{8, 8, 2}, tess::Extent3{4, 4, 1}>;
using Schema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>,
                                 tess::Field<StairTag, std::uint8_t>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;
using Stairs = tess::StairTransitions<StairTag>;

constexpr auto kStairFoot = tess::Coord3{2, 1, 0};

// Ground floor open on rows y < 2; an upper platform over impassable
// ground (rows y in [2,4)) so no vertical face adjacency connects the
// levels; the offset stair from the open ground row is the only link.
void build_keep(World& world) {
  for (std::int64_t y = 0; y < 8; ++y) {
    for (std::int64_t x = 0; x < 8; ++x) {
      world.field<PassableTag>(tess::Coord3{x, y, 0}) = 1;
    }
  }
  for (std::int64_t y = 2; y < 4; ++y) {
    for (std::int64_t x = 0; x < 4; ++x) {
      world.field<PassableTag>(tess::Coord3{x, y, 1}) = 1;
      world.field<PassableTag>(tess::Coord3{x, y, 0}) = 0;
    }
  }
  // Foot on the open ground row; the landing is one step +y and one
  // step up: (2,2,1) on the platform.
  world.field<StairTag>(kStairFoot) =
      static_cast<std::uint8_t>(tess::StairDirection::PositiveY);
}

auto name(tess::ReachabilityStatus status) -> const char* {
  switch (status) {
    case tess::ReachabilityStatus::Reachable:
      return "reachable";
    case tess::ReachabilityStatus::Unreachable:
      return "unreachable";
    default:
      return "indeterminate";
  }
}

auto run() -> int {
  World world;
  build_keep(world);

  tess::LocalTopologyScratch scratch;
  tess::RegionGraphScratch reach;
  const auto courtyard = tess::Coord3{6, 6, 0};
  const auto battlement = tess::Coord3{1, 2, 1};

  // Adjacent-only topology: the platform floats in its own component.
  tess::RegionGraph plain;
  (void)tess::build_region_graph<World, PassableTag>(world, scratch, plain);
  const auto without =
      tess::reachable<Shape>(plain, courtyard, battlement, reach).status;
  std::cout << "without stairs: battlement is " << name(without) << "\n";
  if (without != tess::ReachabilityStatus::Unreachable) {
    std::cerr << "expected the plain graph to isolate the platform\n";
    return 1;
  }

  // The same world through the stair provider: the foot tile's direction
  // field emits the directed cross-chunk portals, both ways.
  tess::RegionGraph stairs;
  (void)tess::build_region_graph<World, PassableTag>(world, scratch, stairs,
                                                     Stairs{});
  const auto up =
      tess::reachable<Shape>(stairs, courtyard, battlement, reach).status;
  const auto down =
      tess::reachable<Shape>(stairs, battlement, courtyard, reach).status;
  std::cout << "with stairs: up is " << name(up) << ", down is " << name(down)
            << "\n";
  if (up != tess::ReachabilityStatus::Reachable ||
      down != tess::ReachabilityStatus::Reachable) {
    std::cerr << "expected the stair to connect both directions\n";
    return 1;
  }

  // The path runtime's precheck reads the same graph, so an agent asked
  // to climb gets a Reachable verdict instead of a doomed A* search.
  const auto verdict = tess::precheck_path<PassableTag>(
      stairs, world, courtyard, battlement, reach);
  std::cout << "precheck verdict: "
            << (verdict == tess::PrecheckStatus::Reachable ? "reachable"
                                                           : "other")
            << "\n";
  if (verdict != tess::PrecheckStatus::Reachable) {
    std::cerr << "expected the precheck to agree with the stair graph\n";
    return 1;
  }

  // Demolish the stair and update ONLY its chunk: the incremental update
  // re-derives the provider portals, so no stale link survives.
  world.field<StairTag>(kStairFoot) =
      static_cast<std::uint8_t>(tess::StairDirection::None);
  const auto dirty =
      tess::chunk_key<Shape>(tess::chunk_coord<Shape>(kStairFoot));
  (void)tess::update_region_graph<World, PassableTag>(
      world, scratch, stairs, std::span<const tess::ChunkKey>{&dirty, 1},
      Stairs{});
  const auto after =
      tess::reachable<Shape>(stairs, courtyard, battlement, reach).status;
  std::cout << "after demolition: battlement is " << name(after) << "\n";
  if (after != tess::ReachabilityStatus::Unreachable) {
    std::cerr << "expected demolition to sever the levels\n";
    return 1;
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
