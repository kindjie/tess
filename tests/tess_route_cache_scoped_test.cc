#include <gtest/gtest.h>
#include <tess/tess.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {

struct PassableTag {};
struct CostTag {};

using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                 tess::Field<CostTag, std::uint32_t>>;
using TopDown2D = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{4, 4, 1}>;

template <typename World>
void fill_passable(World& world, bool value) {
  for (auto& page : world.chunks()) {
    auto passable = page.template field_span<PassableTag>();
    for (auto& tile : passable) {
      tile = value;
    }
  }
}

// Every edit goes through mark_dirty: a raw field write does not advance
// meta().version, and scoped staleness (like the exact fingerprint) only
// sees marked writes.
template <typename World>
void set_passable_marked(World& world, tess::Coord3 coord, bool value) {
  using Shape = typename World::shape_type;
  world.template field<PassableTag>(coord) = value;
  world.mark_dirty(tess::chunk_key<Shape>(tess::tile_key<Shape>(coord)), 1u,
                   tess::Box3{coord, tess::Extent3{1, 1, 1}});
}

template <typename World>
auto route(const World& world, tess::Coord3 start, tess::Coord3 goal,
           tess::PathScratch& scratch, tess::RouteCacheScratch& cache)
    -> tess::PathResult {
  return tess::cached_astar_path<World, PassableTag>(
      world, tess::PathRequest{start, goal}, scratch, cache);
}

// Asserts a served route is legal under the CURRENT world: contiguous unit
// steps, every tile passable, and the reported cost is the route's true
// step count (the scoped-mode guarantee: legality plus truthful cost).
template <typename World>
void expect_legal(World& world, const tess::PathResult& result) {
  ASSERT_EQ(result.status, tess::PathStatus::Found);
  ASSERT_FALSE(result.path.empty());
  EXPECT_EQ(result.cost, result.path.size() - 1u);
  for (std::size_t i = 0; i < result.path.size(); ++i) {
    const auto tile = result.path[i];
    EXPECT_TRUE(world.template field<PassableTag>(tile))
        << "blocked tile served at index " << i;
    if (i > 0) {
      const auto prev = result.path[i - 1];
      const auto manhattan = std::abs(tile.x - prev.x) +
                             std::abs(tile.y - prev.y) +
                             std::abs(tile.z - prev.z);
      EXPECT_EQ(manhattan, 1) << "non-unit step at index " << i;
    }
  }
}

// A chunk the route never touches is edited: the entry must survive the
// refresh and serve as a hit with no recomputation.
TEST(TessRouteCacheScoped, OffFootprintEditKeepsEntryServing) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);

  tess::PathScratch scratch;
  tess::RouteCacheScratch cache;
  cache.set_staleness(tess::UnitRouteStaleness::ScopedFeasible);
  ASSERT_FALSE(cache.refresh_if_world_changed(world));

  // Column x=3 from (3,0) to (3,7): crosses chunks (0,0) and (0,1) only.
  const auto first = route(world, {3, 0, 0}, {3, 7, 0}, scratch, cache);
  ASSERT_EQ(first.status, tess::PathStatus::Found);
  ASSERT_EQ(cache.stats().entries, 1u);

  // Edit chunk (1,0) -- x>=4, y<4 -- which the route never enters.
  set_passable_marked(world, {6, 1, 0}, false);
  EXPECT_TRUE(cache.refresh_if_world_changed(world));

  const auto again = route(world, {3, 0, 0}, {3, 7, 0}, scratch, cache);
  expect_legal(world, again);
  EXPECT_EQ(cache.stats().hits, 1u);
  EXPECT_EQ(again.expanded_nodes, 0u);
  EXPECT_EQ(cache.stats().scoped_survivals, 1u);
  EXPECT_EQ(cache.stats().revalidations, 1u);
}

// An edit inside a crossed chunk retires the entry; the recomputed
// replacement must be reachable on the next lookup (a dead occupant in the
// probe chain must not shadow it).
TEST(TessRouteCacheScoped, CrossedChunkEditRetiresAndFreshEntryServes) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);

  tess::PathScratch scratch;
  tess::RouteCacheScratch cache;
  cache.set_staleness(tess::UnitRouteStaleness::ScopedFeasible);
  ASSERT_FALSE(cache.refresh_if_world_changed(world));

  const auto first = route(world, {3, 0, 0}, {3, 7, 0}, scratch, cache);
  ASSERT_EQ(first.status, tess::PathStatus::Found);
  ASSERT_EQ(first.cost, 7u);

  // Block (3,4): inside crossed chunk (0,1) and on the route itself.
  set_passable_marked(world, {3, 4, 0}, false);
  EXPECT_TRUE(cache.refresh_if_world_changed(world));

  const auto detour = route(world, {3, 0, 0}, {3, 7, 0}, scratch, cache);
  expect_legal(world, detour);
  EXPECT_EQ(detour.cost, 9u);  // Two extra steps around the blocker.
  EXPECT_EQ(cache.stats().retired_entries, 1u);

  // The fresh entry, stored after the dead one, must serve as an exact hit.
  const auto served = route(world, {3, 0, 0}, {3, 7, 0}, scratch, cache);
  expect_legal(world, served);
  EXPECT_EQ(served.cost, 9u);
  EXPECT_EQ(served.expanded_nodes, 0u);
  EXPECT_EQ(cache.stats().hits, 1u);
}

// Suffix slots owned by a retired entry must be overwritten IN PLACE by the
// next store covering the same (node, goal): a dead occupant must not
// suppress suffix reuse forever. The sizes below keep the suffix table from
// growing between the two stores (growth would drop dead slots and bypass
// the overwrite branch this test pins): entry A (8 nodes) sizes the table
// to 32, entry B (11 nodes, same goal) grows it to 64 with 10 occupied
// slots, and B's 11-node re-store needs (10+11+1)*2 = 44 <= 64 — no growth.
TEST(TessRouteCacheScoped, DeadSuffixOccupantIsReplacedByFreshStore) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);

  tess::PathScratch scratch;
  tess::RouteCacheScratch cache;
  cache.set_staleness(tess::UnitRouteStaleness::ScopedFeasible);
  ASSERT_FALSE(cache.refresh_if_world_changed(world));

  // A: column x=3, footprint chunks (0,0)+(0,1). B: (5,0) joins A's column
  // at (3,0), footprint adds chunk (1,0); its exclusive suffix slots are
  // (5,0) and (4,0).
  const auto entry_a = route(world, {3, 0, 0}, {3, 7, 0}, scratch, cache);
  ASSERT_EQ(entry_a.status, tess::PathStatus::Found);
  const auto entry_b = route(world, {5, 0, 0}, {3, 7, 0}, scratch, cache);
  ASSERT_EQ(entry_b.status, tess::PathStatus::Found);
  ASSERT_EQ(cache.stats().entries, 2u);

  // Edit inside chunk (1,0), off B's tiles but inside its footprint: B
  // retires; A survives (its footprint excludes that chunk).
  set_passable_marked(world, {5, 1, 0}, false);
  EXPECT_TRUE(cache.refresh_if_world_changed(world));

  // Re-plan B: the same route re-stores, and its (5,0)/(4,0) suffix slots
  // find their DEAD former owner in the canonical slot — the in-place
  // overwrite branch, since no growth occurs at these sizes.
  const auto fresh_b = route(world, {5, 0, 0}, {3, 7, 0}, scratch, cache);
  expect_legal(world, fresh_b);
  EXPECT_EQ(cache.stats().retired_entries, 1u);

  // A suffix query at B's exclusive node must hit the LIVE fresh entry,
  // not miss forever behind the dead slot.
  const auto suffix = route(world, {4, 0, 0}, {3, 7, 0}, scratch, cache);
  expect_legal(world, suffix);
  EXPECT_EQ(cache.stats().suffix_hits, 1u);
}

// A cap of one entry makes the first retirement cross the half-dead
// threshold and compact (invalidate) from inside retire() while the serve
// path still holds the entry reference: the reference must never be touched
// after a failed validation. ASan pins the boundary.
TEST(TessRouteCacheScoped, RetireCompactionDuringServeIsSafe) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);

  tess::PathScratch scratch;
  tess::RouteCacheScratch cache;
  cache.set_caps(1, tess::RouteCacheScratch::default_max_path_nodes);
  cache.set_staleness(tess::UnitRouteStaleness::ScopedFeasible);
  ASSERT_FALSE(cache.refresh_if_world_changed(world));

  const auto first = route(world, {3, 0, 0}, {3, 7, 0}, scratch, cache);
  ASSERT_EQ(first.status, tess::PathStatus::Found);

  set_passable_marked(world, {3, 4, 0}, false);
  EXPECT_TRUE(cache.refresh_if_world_changed(world));

  const auto after = route(world, {3, 0, 0}, {3, 7, 0}, scratch, cache);
  expect_legal(world, after);
  EXPECT_EQ(cache.stats().retired_entries, 1u);
}

// Seeded arbitrary-edit property: across toggling (blocking AND unblocking)
// edit sequences, every served Found route is legal under the current world
// with a truthful cost. This is the test that catches incomplete dependency
// capture — a route surviving an edit inside a chunk it crosses would serve
// an illegal step here.
TEST(TessRouteCacheScoped, ToggledEditsAlwaysServeLegalRoutes) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);

  tess::PathScratch scratch;
  tess::RouteCacheScratch cache;
  cache.set_staleness(tess::UnitRouteStaleness::ScopedFeasible);
  ASSERT_FALSE(cache.refresh_if_world_changed(world));

  const std::array<tess::PathRequest, 4> requests = {
      tess::PathRequest{{0, 0, 0}, {7, 7, 0}},
      tess::PathRequest{{3, 0, 0}, {3, 7, 0}},
      tess::PathRequest{{7, 0, 0}, {0, 7, 0}},
      tess::PathRequest{{0, 3, 0}, {7, 4, 0}},
  };

  auto rng = std::uint64_t{0x9e3779b97f4a7c15ull};
  const auto next = [&rng] {
    rng ^= rng << 13u;
    rng ^= rng >> 7u;
    rng ^= rng << 17u;
    return rng;
  };
  for (int step = 0; step < 200; ++step) {
    // Toggle a tile that is never a request endpoint.
    const auto pick = next();
    const auto x = static_cast<std::int64_t>(pick % 8u);
    const auto y = static_cast<std::int64_t>((pick >> 8u) % 8u);
    const auto endpoint = [&] {
      for (const auto& request : requests) {
        if ((request.start.x == x && request.start.y == y) ||
            (request.goal.x == x && request.goal.y == y)) {
          return true;
        }
      }
      return false;
    }();
    if (!endpoint) {
      const auto coord = tess::Coord3{x, y, 0};
      const auto current = world.template field<PassableTag>(coord);
      set_passable_marked(world, coord, !current);
    }
    (void)cache.refresh_if_world_changed(world);
    for (const auto& request : requests) {
      const auto result =
          route(world, request.start, request.goal, scratch, cache);
      if (result.status == tess::PathStatus::Found) {
        expect_legal(world, result);
      }
    }
  }
}

// Non-Found results are cached within an epoch (repeat requests hit) and
// retired unconditionally on ANY epoch change, even one from an edit in an
// unrelated chunk -- they carry whole-world sensitivity, not an empty
// dependency list.
TEST(TessRouteCacheScoped, NoPathCachedPerEpochAndRetiredOnAnyChange) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  // Wall off the goal corner cell (7,7).
  set_passable_marked(world, {6, 7, 0}, false);
  set_passable_marked(world, {7, 6, 0}, false);

  tess::PathScratch scratch;
  tess::RouteCacheScratch cache;
  cache.set_staleness(tess::UnitRouteStaleness::ScopedFeasible);
  ASSERT_FALSE(cache.refresh_if_world_changed(world));

  const auto blocked = route(world, {0, 0, 0}, {7, 7, 0}, scratch, cache);
  ASSERT_EQ(blocked.status, tess::PathStatus::NoPath);

  // Same epoch: the failure is served from the cache.
  const auto repeat = route(world, {0, 0, 0}, {7, 7, 0}, scratch, cache);
  EXPECT_EQ(repeat.status, tess::PathStatus::NoPath);
  EXPECT_EQ(cache.stats().hits, 1u);

  // An edit in a chunk UNRELATED to the failure (top-left corner, chunk
  // (0,0) tile far from the walls) must still retire it: non-Found entries
  // carry whole-world sensitivity, not endpoint sensitivity. The next
  // request misses and recomputes — still NoPath, the walls are intact.
  set_passable_marked(world, {1, 1, 0}, false);
  EXPECT_TRUE(cache.refresh_if_world_changed(world));
  const auto recomputed = route(world, {0, 0, 0}, {7, 7, 0}, scratch, cache);
  EXPECT_EQ(recomputed.status, tess::PathStatus::NoPath);
  EXPECT_EQ(cache.stats().retired_entries, 1u);
  EXPECT_EQ(cache.stats().misses, 2u);

  // Unblock the corner: the stale NoPath must NOT be served -- the fresh
  // search finds the opened route.
  set_passable_marked(world, {6, 7, 0}, true);
  EXPECT_TRUE(cache.refresh_if_world_changed(world));
  const auto opened = route(world, {0, 0, 0}, {7, 7, 0}, scratch, cache);
  expect_legal(world, opened);
}

// Blocking-only edit sequences preserve optimality for surviving entries:
// the served cost equals a fresh recomputation's cost at every step.
TEST(TessRouteCacheScoped, MonotoneEditsServeFreshOptimalCost) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);

  tess::PathScratch scratch;
  tess::RouteCacheScratch cache;
  cache.set_staleness(tess::UnitRouteStaleness::ScopedFeasible);
  ASSERT_FALSE(cache.refresh_if_world_changed(world));

  const std::array<tess::Coord3, 3> blockers = {
      tess::Coord3{6, 1, 0}, tess::Coord3{5, 6, 0}, tess::Coord3{3, 4, 0}};
  for (const auto blocker : blockers) {
    set_passable_marked(world, blocker, false);
    ASSERT_TRUE(cache.refresh_if_world_changed(world));
    const auto served = route(world, {3, 0, 0}, {3, 7, 0}, scratch, cache);
    expect_legal(world, served);

    tess::PathScratch fresh_scratch;
    const auto fresh = tess::astar_path<decltype(world), PassableTag>(
        world, tess::PathRequest{{3, 0, 0}, {3, 7, 0}}, fresh_scratch,
        tess::MissingChunkPolicy::TreatAsBlocked);
    ASSERT_EQ(fresh.status, tess::PathStatus::Found);
    EXPECT_EQ(served.cost, fresh.cost);
  }
}

// A single route whose collapsed dependency count alone exceeds the
// dependency cap is skipped without evicting resident entries, mirroring
// the oversized-path rule.
TEST(TessRouteCacheScoped, OversizedDependencyFootprintIsSkipped) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);

  tess::PathScratch scratch;
  tess::RouteCacheScratch cache;
  cache.set_staleness(tess::UnitRouteStaleness::ScopedFeasible);
  ASSERT_FALSE(cache.refresh_if_world_changed(world));

  // Resident short route: one chunk pair footprint.
  const auto resident = route(world, {0, 0, 0}, {2, 0, 0}, scratch, cache);
  ASSERT_EQ(resident.status, tess::PathStatus::Found);
  ASSERT_EQ(cache.stats().entries, 1u);

  // Cap of 1 dependency pair: the cross-world route (footprint >= 2 chunks)
  // cannot be admitted; the resident entry must survive.
  cache.set_dependency_cap(1);
  const auto oversized = route(world, {0, 0, 0}, {7, 7, 0}, scratch, cache);
  ASSERT_EQ(oversized.status, tess::PathStatus::Found);
  EXPECT_EQ(cache.stats().entries, 1u);
  EXPECT_EQ(cache.stats().oversized_skips, 1u);

  const auto still = route(world, {0, 0, 0}, {2, 0, 0}, scratch, cache);
  expect_legal(world, still);
  EXPECT_EQ(cache.stats().hits, 1u);
}

// The dependency budget is a TOTAL: a store whose footprint fits alone but
// not beside the resident blob invalidates the whole cache first, the same
// lifecycle as the entry and path-node caps.
TEST(TessRouteCacheScoped, AggregateDependencyBudgetInvalidates) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);

  tess::PathScratch scratch;
  tess::RouteCacheScratch cache;
  cache.set_staleness(tess::UnitRouteStaleness::ScopedFeasible);
  cache.set_dependency_cap(2);
  ASSERT_FALSE(cache.refresh_if_world_changed(world));

  // Two single-chunk routes fill the budget exactly.
  ASSERT_EQ(route(world, {0, 0, 0}, {2, 0, 0}, scratch, cache).status,
            tess::PathStatus::Found);
  ASSERT_EQ(route(world, {4, 0, 0}, {6, 0, 0}, scratch, cache).status,
            tess::PathStatus::Found);
  ASSERT_EQ(cache.stats().entries, 2u);
  ASSERT_EQ(cache.stats().cap_invalidations, 0u);

  // A third single-chunk route fits alone but not beside the residents:
  // whole-cache invalidation, then the fresh store.
  ASSERT_EQ(route(world, {0, 4, 0}, {2, 4, 0}, scratch, cache).status,
            tess::PathStatus::Found);
  EXPECT_EQ(cache.stats().cap_invalidations, 1u);
  EXPECT_EQ(cache.stats().entries, 1u);
}

// Flipping the staleness mode invalidates the whole cache: entries stored
// under one mode's semantics are never served under the other's.
TEST(TessRouteCacheScoped, ModeFlipDropsEntries) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);

  tess::PathScratch scratch;
  tess::RouteCacheScratch cache;
  const auto first = route(world, {3, 0, 0}, {3, 7, 0}, scratch, cache);
  ASSERT_EQ(first.status, tess::PathStatus::Found);
  ASSERT_EQ(cache.stats().entries, 1u);

  cache.set_staleness(tess::UnitRouteStaleness::ScopedFeasible);
  EXPECT_EQ(cache.stats().entries, 0u);

  cache.set_staleness(tess::UnitRouteStaleness::WholeWorldExact);
  EXPECT_EQ(cache.stats().entries, 0u);
}

// Identical (call, edit) sequences produce identical PER-CALL traces: after
// every serve the full (status, cost, stats-delta) tuple is recorded, so
// two runs that reach the same totals through different hit/miss orders
// would still differ.
TEST(TessRouteCacheScoped, IdenticalSequencesProduceIdenticalTraces) {
  const auto run = [] {
    tess::AlwaysResidentWorld<TopDown2D, Schema> world;
    fill_passable(world, true);
    tess::PathScratch scratch;
    tess::RouteCacheScratch cache;
    cache.set_staleness(tess::UnitRouteStaleness::ScopedFeasible);
    (void)cache.refresh_if_world_changed(world);

    std::vector<std::uint32_t> trace;
    auto previous = cache.stats();
    const auto record = [&](const tess::PathResult& result) {
      const auto now = cache.stats();
      trace.push_back(static_cast<std::uint32_t>(result.status));
      trace.push_back(result.status == tess::PathStatus::Found ? result.cost
                                                               : 0xffffu);
      trace.push_back(static_cast<std::uint32_t>(now.hits - previous.hits));
      trace.push_back(
          static_cast<std::uint32_t>(now.suffix_hits - previous.suffix_hits));
      trace.push_back(static_cast<std::uint32_t>(now.misses - previous.misses));
      trace.push_back(static_cast<std::uint32_t>(now.retired_entries -
                                                 previous.retired_entries));
      trace.push_back(static_cast<std::uint32_t>(now.revalidations -
                                                 previous.revalidations));
      previous = now;
    };

    const std::array<tess::Coord3, 4> edits = {
        tess::Coord3{6, 1, 0}, tess::Coord3{3, 4, 0}, tess::Coord3{1, 6, 0},
        tess::Coord3{5, 2, 0}};
    for (const auto edit : edits) {
      set_passable_marked(world, edit, false);
      (void)cache.refresh_if_world_changed(world);
      for (std::int64_t x = 0; x < 8; x += 2) {
        record(route(world, {x, 0, 0}, {3, 7, 0}, scratch, cache));
      }
    }
    return trace;
  };

  EXPECT_EQ(run(), run());
}

}  // namespace
