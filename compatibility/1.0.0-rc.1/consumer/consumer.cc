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

// The snapshot's name inventory records the bare spelling `OverlayCost`
// and nothing else, so it cannot tell a compatible change from an
// incompatible one that keeps the name. These declarations pin the
// namespace, the template arity, and the operand order; the checks in
// main() pin the semantics.
struct TerrainTag {};
struct SurchargeTag {};
using PricedSchema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                       tess::Field<TerrainTag, std::uint8_t>,
                                       tess::Field<SurchargeTag, std::uint8_t>>;
using PricedWorld = tess::AlwaysResidentWorld<Shape, PricedSchema>;
using PricedCost =
    tess::movement::OverlayCost<tess::movement::FieldCost<TerrainTag>,
                                tess::movement::FieldCost<SurchargeTag>>;
using Priced = tess::movement::MovementClass<
    tess::movement::AllOf<tess::movement::Field<PassableTag>,
                          tess::movement::NotZero<TerrainTag>>,
    PricedCost>;
using SaturatedCost =
    tess::movement::OverlayCost<tess::movement::ConstantCost<0xFFFFFF00U>,
                                tess::movement::ConstantCost<0x00000400U>>;
// Deliberately omits the NotZero<TerrainTag> term that `Priced` carries,
// so absorption is the only thing that can close a zero-terrain tile.
// This is a probe for observing that, not a recipe: without the term,
// region labelling would disagree with the weighted search.
using AbsorbOnly =
    tess::movement::MovementClass<tess::movement::Field<PassableTag>,
                                  PricedCost>;

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

  // OverlayCost: additive, saturating, and absorbing on a zero base.
  // The absorbing leg is the one that matters -- an overlay must never
  // make impassable ground enterable -- so it is checked twice: once on
  // the expression, and once through a search whose passability term is
  // satisfied on the tile, where only absorption can reject it.
  PricedWorld priced;
  for (auto& page : priced.chunks()) {
    auto open = page.template field_span<PassableTag>();
    auto terrain = page.template field_span<TerrainTag>();
    auto surcharge = page.template field_span<SurchargeTag>();
    for (std::size_t i = 0; i < open.size(); ++i) {
      open[i] = true;
      terrain[i] = 1;
      surcharge[i] = 0;
    }
  }
  const tess::Coord3 wall{16, 4, 0};
  // Off the corridor the traversal check below searches, deliberately.
  // A priced tile on that corridor gives the search a second reason to
  // leave the straight route, and the wall-crossing and wall-avoiding
  // detours can then cost the same -- letting a tie-break decide a
  // check that is supposed to be deciding absorption.
  const tess::Coord3 tolled{20, 20, 0};
  priced.field<TerrainTag>(wall) = 0;
  // Deliberately 1, not a large toll: the wall sits on the straight
  // route, so without absorption its entry cost would equal its
  // neighbours' and the search would prefer it. A larger surcharge
  // would make the detour cheaper on price alone and the traversal
  // check below would pass whether or not absorption survives.
  priced.field<SurchargeTag>(wall) = 1;
  priced.field<TerrainTag>(tolled) = 3;
  priced.field<SurchargeTag>(tolled) = 4;
  priced.mark_content_changed(
      tess::chunk_key<Shape>(tess::chunk_coord<Shape>(wall)));
  priced.mark_content_changed(
      tess::chunk_key<Shape>(tess::chunk_coord<Shape>(tolled)));

  // The two tiles are deliberately in different chunks, so each is
  // evaluated against its own page rather than a shared one.
  const auto& wall_page =
      priced.chunk(tess::chunk_key<Shape>(tess::chunk_coord<Shape>(wall)));
  const auto& tolled_page =
      priced.chunk(tess::chunk_key<Shape>(tess::chunk_coord<Shape>(tolled)));
  const auto local_of = [](tess::Coord3 coord) {
    return tess::local_tile_id<Shape>(tess::local_coord<Shape>(coord));
  };
  CHECK(PricedCost::eval(tolled_page, local_of(tolled)) == 7,
        "OverlayCost adds terrain and surcharge");
  CHECK(PricedCost::eval(wall_page, local_of(wall)) == 0,
        "OverlayCost absorbs a zero base");
  CHECK(SaturatedCost::eval(tolled_page, local_of(tolled)) == 0xFFFFFFFFU,
        "OverlayCost saturates at the 32-bit maximum");

  // Absorption must reach traversal, not just eval. The wall's boolean
  // passable field is true and AbsorbOnly's predicate reads nothing
  // else, so under the weighted entry point a search that crosses the
  // wall is the observable failure a non-absorbing OverlayCost gives.
  // The minimum-step entry points deliberately do not see this: they
  // substitute UnitCost for the class's cost expression, which is why
  // the passability term carries the terrain in `Priced` below.
  const tess::PathRequest across{{16, 2, 0}, {16, 8, 0}};
  const auto absorbed = tess::weighted_astar_path<PricedWorld, AbsorbOnly>(
      priced, across, scratch, tess::MissingChunkPolicy::ReportIndeterminate);
  CHECK(absorbed.status == tess::PathStatus::Found,
        "a route around the wall exists");
  bool crosses_wall = false;
  for (const auto step : absorbed.path) {
    if (step.x == wall.x && step.y == wall.y) crosses_wall = true;
  }
  CHECK(!crosses_wall, "a surcharge never opens impassable ground");

  // The shape callers should copy keeps the terrain in the predicate as
  // well, so the minimum-step entry points agree with the weighted one.
  const auto safe = tess::astar_path<PricedWorld, Priced>(
      priced, across, scratch, tess::MissingChunkPolicy::ReportIndeterminate);
  CHECK(safe.status == tess::PathStatus::Found, "priced class plans a route");
  bool safe_crosses_wall = false;
  for (const auto step : safe.path) {
    if (step.x == wall.x && step.y == wall.y) safe_crosses_wall = true;
  }
  CHECK(!safe_crosses_wall, "the terrain term closes the wall unweighted");

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
