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
// Self-checking; returns nonzero on any failed contract. Second
// review round added: the exact-mode baseline-refresh demonstration
// (F7), bounded maintenance drains via run_some, product freshness
// classification asserted at the flush point, post-load derived-product
// invalidation, and the weighted distance-field builder.
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
  // Baseline refresh BEFORE the first lookup: the first exact-mode
  // refresh only captures the fingerprint, so it must precede entry
  // population or pre-baseline entries survive a pre-refresh edit.
  (void)cache.refresh_if_world_changed(world);
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

// RC-1 FINDING F7 (recorded as current behavior, so a future behavior
// change flips this visibly): a fresh cache whose FIRST refresh happens
// only after an edit cannot detect that edit -- the first exact-mode
// refresh captures the post-edit fingerprint and keeps pre-baseline
// entries -- so the documented sequence (baseline refresh before the
// first lookup) is a requirement, not a nicety.
void route_cache_baseline_trap() {
  World world;
  fill(world);
  for (int y = 0; y < 32; ++y) {
    if (y != 16) world.field<PassableTag>(tess::Coord3{20, y, 0}) = false;
  }
  tess::PathScratch scratch;
  tess::UnitRouteCache cache;
  const tess::PathRequest req{{2, 16, 0}, {40, 16, 0}};
  // Populate WITHOUT a baseline refresh (the trap sequence).
  const auto seeded = tess::cached_astar_path<World, PassableTag>(
      world, req, scratch, cache, tess::MissingChunkPolicy::ReportIndeterminate);
  CHECK(seeded.status == tess::PathStatus::Found, "trap seed");
  world.field<PassableTag>(tess::Coord3{20, 16, 0}) = false;
  world.mark_content_changed(
      tess::chunk_key<Shape>(tess::chunk_coord<Shape>({20, 16, 0})));
  const bool detected = cache.refresh_if_world_changed(world);
  CHECK(!detected,
        "first-ever refresh cannot detect the pre-baseline edit (F7)");
  const auto stale = tess::cached_astar_path<World, PassableTag>(
      world, req, scratch, cache, tess::MissingChunkPolicy::ReportIndeterminate);
  bool through_closed = false;
  for (const auto step : stale.path) {
    if (step.x == 20 && step.y == 16) through_closed = true;
  }
  CHECK(stale.status == tess::PathStatus::Found && through_closed,
        "pre-baseline entry serves the pre-edit route (F7, current "
        "behavior)");
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
  // The flush-point contract is freshness, not mere presence: the view
  // must classify as Current, or a consumer cannot safely depend on it.
  CHECK(view.state == tess::maintenance::ChunkProductState::Current,
        "product classifies Current at the flush point");
  // A second flush with no marks is an idle no-op -- the documented
  // flush-point contract a consumer leans on before reading products.
  CHECK(adapter.flush() == tess::maintenance::DrainResult::Idle,
        "idle flush");

  // Second mark through the versioned-edit flow. MEASURED behavior of
  // this default (no worker pool) configuration, recorded rather than
  // assumed: the offer executes inline, so by the time mark_dirty
  // returns the owned dirty bit is already clear and the product is
  // republished at the advanced content version -- there is no
  // observable Stale window to assert against. Queued-drain semantics
  // belong to pool-backed configurations and are covered by the
  // repository's own maintenance tests, not claimed here.
  const auto version_before = world.meta(key).content_version;
  world.field<CostTag>(tess::Coord3{2, 2, 0}) = 7;
  world.mark_content_changed(key);
  CHECK(adapter.mark_dirty(key, cost_dirty,
                           tess::Box3{{2, 2, 0}, tess::Extent3{1, 1, 1}}) ==
            tess::maintenance::ChunkMarkResult::Accepted,
        "second mark accepted");
  const auto after_mark = adapter.product(key);
  CHECK(after_mark.state == tess::maintenance::ChunkProductState::Current &&
            after_mark.token.content_version.value >
                version_before.value &&
            after_mark.value != nullptr &&
            after_mark.value->total == (16 * 16 - 2) + 5 + 7,
        "inline-configuration mark republishes Current at the advanced "
        "version");
  // The budgeted-drain entry point on the reachable state: a settled
  // adapter must report Idle under a unit budget (and Idle must be
  // truthful -- the product above is Current, so nothing was skipped).
  CHECK(adapter.run_some(tess::maintenance::MaintenanceBudget{1}) ==
            tess::maintenance::DrainResult::Idle,
        "unit-budget run_some reports Idle on the settled adapter");
}

// --- Persistence round trip with derived invalidation ----------------
void persistence_round_trip() {
  World world;
  fill(world);
  world.field<CostTag>(tess::Coord3{3, 3, 0}) = 9;
  world.field<PassableTag>(tess::Coord3{10, 10, 0}) = false;
  world.mark_content_changed(
      tess::chunk_key<Shape>(tess::chunk_coord<Shape>({10, 10, 0})));

  tess::DistanceFieldScratch field_scratch;
  tess::GoalSet goals;
  goals.add(tess::Coord3{5, 5, 0});

  using Archive = tess::PersistenceSchema<
      0x7263312d6172ULL, 1,
      tess::PersistedField<PassableTag, 0x70617373ULL, 1>,
      tess::PersistedField<CostTag, 0x636f7374ULL, 1>>;
  std::vector<std::byte> bytes;
  const auto save = tess::save_world_archive<Archive>(world, bytes);
  CHECK(save.bytes_written > 0, "archive saves");

  // Derived-product invalidation across a load: warm a product against
  // the LOAD TARGET before the load, then assert the load invalidates
  // it -- an archive restore that left warmed products validating would
  // let a consumer trust pre-load derivations for post-load content.
  World loaded;
  fill(loaded);  // passable warm target; the load then overwrites it
  tess::DistanceFieldProduct warmed;
  const auto warm = tess::build_distance_field_product<World, PassableTag>(
      loaded, goals, warmed, field_scratch);
  CHECK(warm.status == tess::PathStatus::Found, "pre-load product warms");
  CHECK(warmed.is_valid(loaded), "warmed product validates before load");
  const auto load = tess::load_world_archive<Archive>(loaded, bytes);
  CHECK(load.status == tess::WorldArchiveStatus::Ok, "archive loads");
  CHECK(!warmed.is_valid(loaded),
        "archive load invalidates the pre-load product");
  CHECK(loaded.field<CostTag>(tess::Coord3{3, 3, 0}) == 9,
        "scalar field survives the round trip");
  CHECK(loaded.field<PassableTag>(tess::Coord3{10, 10, 0}) == false,
        "edited passability survives");
}

// --- Weighted distance-field product ---------------------------------
void weighted_field_product() {
  World world;
  fill(world);
  // A cheap detour vs an expensive straight line: the weighted builder
  // must price CostTag, so the weighted distance at the probe exceeds
  // the unit-cost distance.
  for (int x = 1; x < 12; ++x) {
    world.field<CostTag>(tess::Coord3{x, 8, 0}) = 9;
  }
  tess::GoalSet goals;
  goals.add(tess::Coord3{0, 8, 0});
  tess::DistanceFieldScratch scratch;
  tess::DistanceFieldProduct unit_product;
  const auto unit = tess::build_distance_field_product<World, PassableTag>(
      world, goals, unit_product, scratch);
  CHECK(unit.status == tess::PathStatus::Found, "unit product builds");
  tess::DistanceFieldProduct weighted_product;
  const auto weighted =
      tess::build_weighted_distance_field_product<World, Traveler>(
          world, goals, weighted_product, scratch);
  CHECK(weighted.status == tess::PathStatus::Found,
        "weighted product builds");
  const tess::Coord3 probe{12, 8, 0};
  const auto unit_d = unit_product.distance_at<World>(probe);
  const auto weighted_d = weighted_product.distance_at<World>(probe);
  CHECK(unit_d == 12, "unit distance counts steps");
  CHECK(weighted_d > unit_d,
        "weighted distance prices the CostTag field (cost-sensitive)");
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
  route_cache_baseline_trap();
  maintenance_flush_points();
  weighted_field_product();
  persistence_round_trip();
  sparse_residency_paths();
  std::printf("rc1 targeted consumer: %s (%d failures)\n",
              failures == 0 ? "ALL CONTRACTS HELD" : "FAILURES", failures);
  return failures == 0 ? 0 : 1;
}
```
