// Immutable compatibility consumer: exercises the 1.x stable surface
// through the installed package (find_package(tess CONFIG) +
// tess::tess), stable headers only. Self-checking; nonzero on any
// failed contract. This file is frozen at snapshot creation: it must
// keep compiling and passing against every later 1.x candidate.
#include <tess/tess.h>

#include <cstdio>
#include <vector>

namespace {

int failures = 0;
#define CHECK(cond, msg)                                \
  do {                                                  \
    if (!(cond)) {                                      \
      ++failures;                                       \
      std::printf("FAIL line %d: %s\n", __LINE__, msg); \
    }                                                   \
  } while (0)

struct PassableTag {};
struct CostTag {};
using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                 tess::Field<CostTag, std::uint8_t>>;
using Shape = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{16, 16, 1}>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;
using Traveler =
    tess::movement::MovementClass<tess::movement::Field<PassableTag>,
                                  tess::movement::FieldCost<CostTag>>;

}  // namespace

int main() {
  World world;
  for (auto& page : world.chunks()) {
    auto open = page.template field_span<PassableTag>();
    auto cost = page.template field_span<CostTag>();
    for (std::size_t i = 0; i < open.size(); ++i) {
      open[i] = true;
      cost[i] = 1;
    }
  }
  // Versioned edit + plain A* through the stable entry point.
  world.field<PassableTag>(tess::Coord3{16, 10, 0}) = false;
  world.mark_content_changed(
      tess::chunk_key<Shape>(tess::chunk_coord<Shape>({16, 10, 0})));
  tess::PathScratch scratch;
  const auto direct = tess::astar_path<World, PassableTag>(
      world, tess::PathRequest{{2, 10, 0}, {30, 10, 0}}, scratch,
      tess::MissingChunkPolicy::ReportIndeterminate);
  CHECK(direct.status == tess::PathStatus::Found, "stable astar_path");

  // Weighted movement class over the cost field.
  const auto weighted = tess::astar_path<World, Traveler>(
      world, tess::PathRequest{{2, 2, 0}, {30, 30, 0}}, scratch,
      tess::MissingChunkPolicy::ReportIndeterminate);
  CHECK(weighted.status == tess::PathStatus::Found, "movement-class path");

  // Distance-field product + stamped read.
  tess::GoalSet goals;
  goals.add(tess::Coord3{5, 5, 0});
  tess::DistanceFieldScratch field_scratch;
  tess::DistanceFieldProduct product;
  const auto built = tess::build_distance_field_product<World, PassableTag>(
      world, goals, product, field_scratch);
  CHECK(built.status == tess::PathStatus::Found, "field product builds");
  CHECK(product.is_valid(world), "product validates");
  CHECK(product.distance_at<World>(tess::Coord3{5, 8, 0}) == 3,
        "product distance");

  // Route cache under the documented baseline-then-edit contract:
  // baseline refresh, seed an entry, version-mark an edit that closes
  // a tile on the returned route, refresh, and require the served
  // route to reflect the edit -- the direct-adopter contract the RC-1
  // evaluation surfaced (F2/F7).
  tess::UnitRouteCache cache;
  (void)cache.refresh_if_world_changed(world);
  const tess::PathRequest cached_request{{2, 20, 0}, {30, 20, 0}};
  const auto cached = tess::cached_astar_path<World, PassableTag>(
      world, cached_request, scratch, cache,
      tess::MissingChunkPolicy::ReportIndeterminate);
  CHECK(cached.status == tess::PathStatus::Found, "cached path seeds");
  CHECK(cached.path.size() > 2, "seeded route long enough to cut");
  const auto cut = cached.path[cached.path.size() / 2];
  world.field<PassableTag>(cut) = false;
  world.mark_content_changed(
      tess::chunk_key<Shape>(tess::chunk_coord<Shape>(cut)));
  CHECK(cache.refresh_if_world_changed(world),
        "refresh detects the version-marked edit");
  const auto rerouted = tess::cached_astar_path<World, PassableTag>(
      world, cached_request, scratch, cache,
      tess::MissingChunkPolicy::ReportIndeterminate);
  CHECK(rerouted.status == tess::PathStatus::Found, "reroute exists");
  bool avoids_cut = true;
  for (const auto step : rerouted.path) {
    if (step.x == cut.x && step.y == cut.y) avoids_cut = false;
  }
  CHECK(avoids_cut, "served route reflects the post-edit world");

  // Persistence round trip through the stable archive surface.
  using Archive = tess::PersistenceSchema<
      0x736e617073686f74ULL, 1,
      tess::PersistedField<PassableTag, 0x70617373ULL, 1>,
      tess::PersistedField<CostTag, 0x636f7374ULL, 1>>;
  std::vector<std::byte> bytes;
  CHECK(tess::save_world_archive<Archive>(world, bytes).bytes_written > 0,
        "archive saves");
  World loaded;
  CHECK(tess::load_world_archive<Archive>(loaded, bytes).status ==
            tess::WorldArchiveStatus::Ok,
        "archive loads");
  CHECK(loaded.field<PassableTag>(tess::Coord3{16, 10, 0}) == false,
        "edit survives the round trip");

  std::printf("snapshot consumer: %s (%d)\n",
              failures == 0 ? "ALL CONTRACTS HELD" : "FAILURES", failures);
  return failures == 0 ? 0 : 1;
}
