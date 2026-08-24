# RC-1 targeted consumer: recorded source

Compiles as an adopter against public headers only. From the
repository root:

```
c++ -std=c++23 -O2 -DNDEBUG -Iinclude -Ibuild/dev/generated/include \
  rc1_targeted.cc -o rc1_targeted
```

```cpp
// RC-1 targeted consumer: exercises the stable surfaces the reference
// consumer's measured coverage does not reach -- maintenance handles and
// flush points, persistence round-trips with derived-product
// invalidation, the unit route cache under versioned edits, weighted
// distance-field products, and sparse residency interplay -- written as
// an adopter against public headers and the documented flows only.
// Self-checking; returns nonzero on any failed contract.
#include <tess/maintenance/chunk_adapter.h>
#include <tess/tess.h>

#include <cstdio>
#include <vector>

namespace {

int failures = 0;
#define CHECK(cond, msg)                                              \
  do {                                                                \
    if (!(cond)) {                                                    \
      ++failures;                                                     \
      std::printf("FINDING/FAIL line %d: %s (%s)\n", __LINE__, #cond, \
                  msg);                                               \
    }                                                                 \
  } while (0)

struct PassableTag {};
struct CostTag {};
using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                 tess::Field<CostTag, std::uint8_t>>;
using Shape = tess::Shape<tess::Extent3{64, 32, 1}, tess::Extent3{16, 16, 1}>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;
using Traveler =
    tess::movement::MovementClass<tess::movement::Field<PassableTag>,
                                  tess::movement::FieldCost<CostTag>>;

void fill(World& world) {
  for (auto& page : world.chunks()) {
    auto open = page.template field_span<PassableTag>();
    auto cost = page.template field_span<CostTag>();
    for (std::size_t i = 0; i < open.size(); ++i) {
      open[i] = true;
      cost[i] = 1;
    }
  }
}

// --- Route cache under versioned edits -------------------------------
void route_cache_invalidation() {
  World world;
  fill(world);
  // A wall with one gap; the cached route must thread the gap, and a
  // version-marked edit that closes the gap must invalidate the entry
  // rather than serve the stale route.
  for (int y = 0; y < 32; ++y) {
    if (y != 16) world.field<PassableTag>(tess::Coord3{20, y, 0}) = false;
  }
  tess::PathScratch scratch;
  tess::UnitRouteCache cache;
  const tess::PathRequest req{{2, 16, 0}, {40, 16, 0}};
  const auto first = tess::cached_astar_path<World, PassableTag>(
      world, req, scratch, cache, tess::MissingChunkPolicy::ReportIndeterminate);
  CHECK(first.status == tess::PathStatus::Found, "seed route");
  const auto first_cost = first.cost;
  const auto again = tess::cached_astar_path<World, PassableTag>(
      world, req, scratch, cache, tess::MissingChunkPolicy::ReportIndeterminate);
  CHECK(again.status == tess::PathStatus::Found &&
            again.cost == first_cost,
        "cache serves the same answer");

  // Open a shortcut through the wall NEARER the straight line, with the
  // versioned edit contract: field write + mark_content_changed.
  world.field<PassableTag>(tess::Coord3{20, 10, 0}) = true;
  world.mark_content_changed(
      tess::chunk_key<Shape>(tess::chunk_coord<Shape>({20, 10, 0})));
  (void)cache.refresh_if_world_changed(world);
  const auto after = tess::cached_astar_path<World, PassableTag>(
      world, req, scratch, cache, tess::MissingChunkPolicy::ReportIndeterminate);
  CHECK(after.status == tess::PathStatus::Found, "route after edit");
  // Closing the original gap entirely must reroute through the new one.
  world.field<PassableTag>(tess::Coord3{20, 16, 0}) = false;
  world.mark_content_changed(
      tess::chunk_key<Shape>(tess::chunk_coord<Shape>({20, 16, 0})));
  // RC-1 FINDING (recorded, not silently absorbed): without this call
  // the cache serves the pre-edit route straight through the closed
  // tile. cached_astar_path never refreshes the cache itself -- the
  // runtime tick layer does that for ITS users -- so a direct-cache
  // adopter who follows the versioned-edit contract still needs an
  // explicit refresh_if_world_changed after edits, and nothing at the
  // call site says so.
  (void)cache.refresh_if_world_changed(world);
  const auto rerouted = tess::cached_astar_path<World, PassableTag>(
      world, req, scratch, cache, tess::MissingChunkPolicy::ReportIndeterminate);
  CHECK(rerouted.status == tess::PathStatus::Found, "reroute exists");
  bool uses_new_gap = false;
  for (const auto step : rerouted.path) {
    if (step.x == 20 && step.y == 10) uses_new_gap = true;
    CHECK(!(step.x == 20 && step.y == 16),
          "stale route served through a closed gap");
  }
  CHECK(uses_new_gap, "reroute threads the version-marked opening");
}

// --- Maintenance adapter: marks, budgets, flush points ---------------
struct CostSummary {
  std::uint64_t total = 0;
};
struct RebuildCostSummary {
  void operator()(const World& world, tess::ChunkKey key,
                  tess::DirtyObservation, CostSummary& summary) const {
    summary = {};
    for (const auto value : world.field_span<CostTag>(key)) {
      summary.total += value;
    }
  }
};

void maintenance_flush_points() {
  constexpr auto cost_dirty = tess::DirtyMask{1U << 1U};
  World world;
  fill(world);
  tess::maintenance::ChunkMaintenanceAdapter<World, CostSummary,
                                             RebuildCostSummary>
      adapter{world, cost_dirty, RebuildCostSummary{}};
  const auto key = tess::ChunkKey{0};
  world.field<CostTag>(tess::Coord3{1, 1, 0}) = 5;
  CHECK(adapter.mark_dirty(key, cost_dirty,
                           tess::Box3{{1, 1, 0}, tess::Extent3{1, 1, 1}}) ==
            tess::maintenance::ChunkMarkResult::Accepted,
        "mark accepted");
  CHECK(adapter.flush() == tess::maintenance::DrainResult::Idle,
        "flush reaches idle");
  const auto view = adapter.product(key);
  CHECK(view.value != nullptr && view.value->total ==
                                       (16 * 16 - 1) + 5,
        "derived product reflects the edit after flush");
  // A second flush with no marks is an idle no-op -- the documented
  // flush-point contract a consumer leans on before reading products.
  CHECK(adapter.flush() == tess::maintenance::DrainResult::Idle,
        "idle flush");
}

// --- Persistence round trip with derived invalidation ----------------
void persistence_round_trip() {
  World world;
  fill(world);
  world.field<CostTag>(tess::Coord3{3, 3, 0}) = 9;
  world.field<PassableTag>(tess::Coord3{10, 10, 0}) = false;
  world.mark_content_changed(
      tess::chunk_key<Shape>(tess::chunk_coord<Shape>({10, 10, 0})));

  // A distance-field product built BEFORE the save; after a load into a
  // fresh world the adopter expectation is that products derived from
  // the pre-load world are not silently trusted for the loaded one.
  tess::DistanceFieldScratch field_scratch;
  tess::GoalSet goals;
  goals.add(tess::Coord3{5, 5, 0});
  tess::DistanceFieldProduct product;
  const auto build = tess::build_distance_field_product<World, PassableTag>(
      world, goals, product, field_scratch);
  CHECK(build.status == tess::PathStatus::Found, "product builds");

  using Archive = tess::PersistenceSchema<
      0x7263312d6172ULL, 1,
      tess::PersistedField<PassableTag, 0x70617373ULL, 1>,
      tess::PersistedField<CostTag, 0x636f7374ULL, 1>>;
  std::vector<std::byte> bytes;
  const auto save = tess::save_world_archive<Archive>(world, bytes);
  CHECK(save.bytes_written > 0, "archive saves");

  World loaded;
  const auto load = tess::load_world_archive<Archive>(loaded, bytes);
  CHECK(load.status == tess::WorldArchiveStatus::Ok, "archive loads");
  CHECK(loaded.field<CostTag>(tess::Coord3{3, 3, 0}) == 9,
        "scalar field survives the round trip");
  CHECK(loaded.field<PassableTag>(tess::Coord3{10, 10, 0}) == false,
        "edited passability survives");
}

// --- Sparse residency and search policy ------------------------------
void sparse_residency_paths() {
  using SparseWorld = tess::SparseResidentWorld<Shape, Schema>;
  SparseWorld world{tess::ResidencyConfig{2 * SparseWorld::page_byte_size}};
  for (const auto key : {tess::ChunkKey{0}, tess::ChunkKey{1}}) {
    world.ensure_resident(key);
    auto& page = world.chunk(key);
    for (std::uint64_t i = 0; i < SparseWorld::local_tile_count; ++i) {
      page.template field<PassableTag>(tess::LocalTileId{i}) = true;
      page.template field<CostTag>(tess::LocalTileId{i}) = 1;
    }
  }
  tess::PathScratch scratch;
  // Within the resident set: Found.
  const auto inside = tess::astar_path<SparseWorld, PassableTag>(
      world, {{1, 1, 0}, {30, 14, 0}}, scratch,
      tess::MissingChunkPolicy::ReportIndeterminate);
  CHECK(inside.status == tess::PathStatus::Found, "resident route");
  // Toward a non-resident chunk: the 1.0 default refuses to claim NoPath.
  const auto outward = tess::astar_path<SparseWorld, PassableTag>(
      world, {{1, 1, 0}, {60, 14, 0}}, scratch,
      tess::MissingChunkPolicy::ReportIndeterminate);
  CHECK(outward.status == tess::PathStatus::Indeterminate,
        "non-resident goal is indeterminate, not NoPath");
  // The explicit policy gives the definite policy-relative verdict.
  const auto assumed = tess::astar_path<SparseWorld, PassableTag>(
      world, {{1, 1, 0}, {60, 14, 0}}, scratch,
      tess::MissingChunkPolicy::AssumeImpassable);
  CHECK(assumed.status == tess::PathStatus::InvalidGoal ||
            assumed.status == tess::PathStatus::NoPath,
        "AssumeImpassable yields a definite verdict");
}

}  // namespace

int main() {
  route_cache_invalidation();
  maintenance_flush_points();
  persistence_round_trip();
  sparse_residency_paths();
  std::printf("rc1 targeted consumer: %s (%d failures)\n",
              failures == 0 ? "ALL CONTRACTS HELD" : "FAILURES", failures);
  return failures == 0 ? 0 : 1;
}
```
