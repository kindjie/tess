#include <gtest/gtest.h>
#include <tess/tess.h>

#include <array>
#include <cstdint>
#include <vector>

namespace {

struct PassableTag {};
struct CostTag {};

using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                 tess::Field<CostTag, std::uint32_t>>;
using TopDown2D = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{4, 4, 1}>;
using Mid2D = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{8, 8, 1}>;

template <typename World>
void fill_passable(World& world, bool value) {
  for (auto& page : world.chunks()) {
    auto passable = page.template field_span<PassableTag>();
    for (auto& tile : passable) {
      tile = value;
    }
  }
}

template <typename World>
void fill_cost(World& world, std::uint32_t value) {
  for (auto& page : world.chunks()) {
    auto costs = page.template field_span<CostTag>();
    for (auto& tile : costs) {
      tile = value;
    }
  }
}

// Repeatedly editing the world and rebuilding the same portal route must not
// grow the segment cache without bound: stale entries are swept out and the
// entry count stays within the configured budget.
TEST(TessPathCache, PortalSegmentCacheStaysWithinBudgetAcrossWorldEdits) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::WeightedPortalSegmentCache cache;
  cache.set_segment_budget(8);
  tess::WeightedPortalRouteProduct product;
  product.reserve_waypoints(1);
  product.reserve_path_nodes(16);
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 0, 0}};
  const auto waypoints = std::array{tess::Coord3{3, 0, 0}};

  auto version = std::uint32_t{0};
  for (int i = 0; i < 50; ++i) {
    const auto built =
        tess::build_weighted_portal_route_product<decltype(world), PassableTag,
                                                  CostTag>(
            world, request, waypoints, scratch, cache, product);
    ASSERT_EQ(built.status, tess::PathStatus::Found);
    world.mark_dirty(tess::ChunkKey{0}, ++version,
                     tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}});
  }

  // Pre-fix red evidence: 100 entries after 50 edits (2 stale duplicates per
  // rebuild, growing linearly forever).
  const auto stats = cache.stats();
  EXPECT_LE(cache.size(), 8u);
  EXPECT_EQ(stats.entries, cache.size());
  // Path storage is compacted with the sweep: two live segments cover 9
  // nodes, so bounded entries must also bound the append arena.
  EXPECT_LE(stats.path_nodes, 96u);
  EXPECT_GT(stats.sweeps, 0u);
}

// Re-storing the same request after a world change must not accumulate a
// stale duplicate entry forever: the budget-triggered sweep removes the stale
// entry instead of keeping both.
TEST(TessPathCache, PortalSegmentCacheSweepRemovesStaleDuplicate) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::WeightedPortalSegmentCache cache;
  cache.set_segment_budget(1);
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 0, 0}};

  const auto first =
      tess::weighted_astar_path<decltype(world), PassableTag, CostTag>(
          world, request, scratch);
  ASSERT_EQ(first.status, tess::PathStatus::Found);
  cache.store(world, request, first);
  ASSERT_EQ(cache.size(), 1u);

  world.template field<CostTag>(tess::Coord3{3, 0, 0}) = 9;
  world.mark_dirty(tess::ChunkKey{0}, 1u,
                   tess::Box3{tess::Coord3{3, 0, 0}, tess::Extent3{1, 1, 1}});

  const auto second =
      tess::weighted_astar_path<decltype(world), PassableTag, CostTag>(
          world, request, scratch);
  ASSERT_EQ(second.status, tess::PathStatus::Found);
  cache.store(world, request, second);

  // Pre-fix red evidence: two identical (start, goal) entries, one stale.
  EXPECT_EQ(cache.size(), 1u);
  EXPECT_EQ(cache.stats().sweeps, 1u);
  EXPECT_EQ(cache.stats().evictions, 0u);

  // The surviving entry is the fresh one.
  std::vector<tess::Coord3> out;
  const auto hit = cache.lookup_append(world, request, out);
  ASSERT_TRUE(hit.found);
  EXPECT_EQ(hit.cost, second.cost);
}

// At budget with all-live entries, the oldest entry is evicted in insertion
// order.
TEST(TessPathCache, PortalSegmentCacheEvictsOldestLiveEntryAtBudget) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::WeightedPortalSegmentCache cache;
  cache.set_segment_budget(2);

  const auto requests = std::array{
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{3, 0, 0}},
      tess::PathRequest{tess::Coord3{0, 1, 0}, tess::Coord3{3, 1, 0}},
      tess::PathRequest{tess::Coord3{0, 2, 0}, tess::Coord3{3, 2, 0}},
  };
  for (const auto request : requests) {
    const auto result =
        tess::weighted_astar_path<decltype(world), PassableTag, CostTag>(
            world, request, scratch);
    ASSERT_EQ(result.status, tess::PathStatus::Found);
    cache.store(world, request, result);
  }

  const auto stats = cache.stats();
  EXPECT_EQ(stats.entries, 2u);
  EXPECT_EQ(stats.sweeps, 1u);
  EXPECT_EQ(stats.evictions, 1u);

  std::vector<tess::Coord3> out;
  EXPECT_FALSE(cache.lookup_append(world, requests[0], out).found);
  EXPECT_TRUE(cache.lookup_append(world, requests[1], out).found);
  out.clear();
  EXPECT_TRUE(cache.lookup_append(world, requests[2], out).found);
}

// A lookup that matches (start, goal) but fails chunk-version validation
// counts a stale rejection.
TEST(TessPathCache, PortalSegmentCacheCountsStaleRejections) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::WeightedPortalSegmentCache cache;
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{3, 0, 0}};

  const auto result =
      tess::weighted_astar_path<decltype(world), PassableTag, CostTag>(
          world, request, scratch);
  ASSERT_EQ(result.status, tess::PathStatus::Found);
  cache.store(world, request, result);
  ASSERT_EQ(cache.stats().stale_rejections, 0u);

  world.mark_dirty(tess::ChunkKey{0}, 1u,
                   tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}});

  std::vector<tess::Coord3> out;
  EXPECT_FALSE(cache.lookup_append(world, request, out).found);
  EXPECT_TRUE(out.empty());
  EXPECT_EQ(cache.stats().stale_rejections, 1u);

  cache.reset_stats();
  EXPECT_EQ(cache.stats().stale_rejections, 0u);
}

// Two cached entries can both contain the queried suffix node when a stale
// world edit is not reported to the cache. The earliest stored entry must win
// deterministically: this pins the pre-index linear-scan semantics.
TEST(TessPathCache, CachedAStarSuffixFirstStoredEntryWins) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  // Wall along y=1 with a single gap at x=3 funnels all row-0 starts
  // through (3,1) and up column x=3.
  for (std::int64_t x = 0; x < 8; ++x) {
    if (x != 3) {
      world.template field<PassableTag>(tess::Coord3{x, 1, 0}) = false;
    }
  }

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::RouteCacheScratch cache;
  cache.reserve_routes(4);
  cache.reserve_path_nodes(64);

  // First entry: straight column x=3 from (3,0) to (3,7).
  const auto first = tess::cached_astar_path<decltype(world), PassableTag>(
      world, tess::PathRequest{tess::Coord3{3, 0, 0}, tess::Coord3{3, 7, 0}},
      scratch, cache);
  ASSERT_EQ(first.status, tess::PathStatus::Found);
  ASSERT_EQ(first.path.size(), 8u);
  for (std::int64_t y = 0; y < 8; ++y) {
    ASSERT_EQ(first.path[static_cast<std::size_t>(y)], (tess::Coord3{3, y, 0}));
  }

  // Stale edit the cache is deliberately not told about: block (3,5).
  world.template field<PassableTag>(tess::Coord3{3, 5, 0}) = false;

  // Second entry: from (0,0) the only forward step after the (3,1) gap is
  // (3,2), so this path shares (3,2) with the first entry but detours
  // around the new blocker afterward.
  const auto second = tess::cached_astar_path<decltype(world), PassableTag>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{3, 7, 0}},
      scratch, cache);
  ASSERT_EQ(second.status, tess::PathStatus::Found);
  ASSERT_EQ(cache.stats().entries, 2u);

  // Suffix query at the shared node: the first stored entry must win, so the
  // returned route is the straight (now stale) column suffix, not the newer
  // detour.
  const auto suffix = tess::cached_astar_path<decltype(world), PassableTag>(
      world, tess::PathRequest{tess::Coord3{3, 2, 0}, tess::Coord3{3, 7, 0}},
      scratch, cache);
  ASSERT_EQ(suffix.status, tess::PathStatus::Found);
  ASSERT_EQ(suffix.path.size(), 6u);
  EXPECT_EQ(suffix.cost, 5u);
  for (std::int64_t y = 2; y < 8; ++y) {
    EXPECT_EQ(suffix.path[static_cast<std::size_t>(y - 2)],
              (tess::Coord3{3, y, 0}));
  }
  EXPECT_EQ(cache.stats().suffix_hits, 1u);
}

// Many entries whose coordinates share low bits (stride-8 lattice) must all
// stay individually retrievable as exact hits.
TEST(TessPathCache, CachedAStarFindsAllEntriesUnderAliasedCoordinates) {
  tess::AlwaysResidentWorld<Mid2D, Schema> world;
  fill_passable(world, true);

  tess::PathScratch scratch;
  scratch.reserve_nodes(2048);
  tess::RouteCacheScratch cache;
  cache.reserve_routes(64);
  cache.reserve_path_nodes(4096);

  // Distinct goals everywhere: suffix reuse requires a shared goal, so every
  // request below exercises the exact-match path.
  std::vector<tess::PathRequest> requests;
  for (std::int64_t sy = 0; sy < 32; sy += 8) {
    for (std::int64_t sx = 0; sx < 32; sx += 8) {
      requests.push_back(tess::PathRequest{tess::Coord3{sx, sy, 0},
                                           tess::Coord3{31 - sx, 31 - sy, 0}});
    }
  }
  ASSERT_EQ(requests.size(), 16u);

  std::vector<std::uint32_t> costs;
  for (const auto request : requests) {
    const auto result = tess::cached_astar_path<decltype(world), PassableTag>(
        world, request, scratch, cache);
    ASSERT_EQ(result.status, tess::PathStatus::Found);
    costs.push_back(result.cost);
  }
  ASSERT_EQ(cache.stats().entries, requests.size());

  for (std::size_t i = 0; i < requests.size(); ++i) {
    const auto result = tess::cached_astar_path<decltype(world), PassableTag>(
        world, requests[i], scratch, cache);
    ASSERT_EQ(result.status, tess::PathStatus::Found);
    EXPECT_EQ(result.cost, costs[i]);
    EXPECT_EQ(result.expanded_nodes, 0u);
    EXPECT_EQ(result.path.front(), requests[i].start);
    EXPECT_EQ(result.path.back(), requests[i].goal);
  }
  EXPECT_EQ(cache.stats().hits, requests.size());
  EXPECT_EQ(cache.stats().misses, requests.size());
}

// Exceeding the entry cap on insert invalidates the whole cache (the same
// lifecycle as a world-change invalidation) and then stores the new route.
TEST(TessPathCache, RouteCacheEntryCapInvalidatesWholeCache) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::RouteCacheScratch cache;
  cache.set_caps(4, tess::RouteCacheScratch::default_max_path_nodes);

  // Distinct goals so every request is a fresh miss.
  std::vector<tess::PathRequest> requests;
  for (std::int64_t y = 0; y < 6; ++y) {
    requests.push_back(
        tess::PathRequest{tess::Coord3{0, y, 0}, tess::Coord3{7, y, 0}});
  }
  for (const auto request : requests) {
    const auto result = tess::cached_astar_path<decltype(world), PassableTag>(
        world, request, scratch, cache);
    ASSERT_EQ(result.status, tess::PathStatus::Found);
  }

  const auto stats = cache.stats();
  EXPECT_EQ(stats.cap_invalidations, 1u);
  EXPECT_EQ(stats.entries, 2u);
  EXPECT_EQ(stats.misses, requests.size());

  // Entries stored before the cap-triggered invalidation are gone.
  const auto recomputed = tess::cached_astar_path<decltype(world), PassableTag>(
      world, requests[0], scratch, cache);
  ASSERT_EQ(recomputed.status, tess::PathStatus::Found);
  EXPECT_GT(recomputed.expanded_nodes, 0u);
  // Entries stored after it are served from the cache.
  const auto cached = tess::cached_astar_path<decltype(world), PassableTag>(
      world, requests[5], scratch, cache);
  ASSERT_EQ(cached.status, tess::PathStatus::Found);
  EXPECT_EQ(cached.expanded_nodes, 0u);
}

// Exceeding the path-node cap on insert also invalidates the whole cache.
TEST(TessPathCache, RouteCachePathNodeCapInvalidatesWholeCache) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::RouteCacheScratch cache;
  cache.set_caps(tess::RouteCacheScratch::default_max_entries, 20);

  // Straight 8-node routes: the third insert would reach 24 stored nodes.
  for (std::int64_t y = 0; y < 3; ++y) {
    const auto result = tess::cached_astar_path<decltype(world), PassableTag>(
        world, tess::PathRequest{tess::Coord3{0, y, 0}, tess::Coord3{7, y, 0}},
        scratch, cache);
    ASSERT_EQ(result.status, tess::PathStatus::Found);
    ASSERT_EQ(result.path.size(), 8u);
  }

  const auto stats = cache.stats();
  EXPECT_EQ(stats.cap_invalidations, 1u);
  EXPECT_EQ(stats.entries, 1u);
  EXPECT_EQ(stats.path_nodes, 8u);
}

// A single route larger than the node cap can never fit, so it is skipped
// outright instead of invalidating resident entries and then violating the
// cap anyway.
TEST(TessPathCache, RouteCacheSkipsRouteLargerThanPathNodeCap) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::RouteCacheScratch cache;
  cache.set_caps(tess::RouteCacheScratch::default_max_entries, 4);

  const auto small =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{3, 0, 0}};
  ASSERT_EQ((tess::cached_astar_path<decltype(world), PassableTag>(
                 world, small, scratch, cache))
                .status,
            tess::PathStatus::Found);
  ASSERT_EQ(cache.stats().entries, 1u);

  const auto oversized =
      tess::PathRequest{tess::Coord3{0, 1, 0}, tess::Coord3{7, 1, 0}};
  const auto computed = tess::cached_astar_path<decltype(world), PassableTag>(
      world, oversized, scratch, cache);
  ASSERT_EQ(computed.status, tess::PathStatus::Found);
  ASSERT_EQ(computed.path.size(), 8u);

  const auto stats = cache.stats();
  EXPECT_EQ(stats.entries, 1u);
  EXPECT_EQ(stats.path_nodes, 4u);
  EXPECT_EQ(stats.cap_invalidations, 0u);
  EXPECT_EQ(stats.oversized_skips, 1u);

  const auto hit = tess::cached_astar_path<decltype(world), PassableTag>(
      world, small, scratch, cache);
  ASSERT_EQ(hit.status, tess::PathStatus::Found);
  EXPECT_EQ(hit.expanded_nodes, 0u);
}

// Cap value 0 disables storage, matching the portal segment cache's budget
// semantics; it does not mean "unlimited".
TEST(TessPathCache, RouteCacheZeroCapsDisableStorage) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::RouteCacheScratch cache;
  cache.set_caps(0, 0);

  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 0, 0}};
  for (int i = 0; i < 2; ++i) {
    const auto result = tess::cached_astar_path<decltype(world), PassableTag>(
        world, request, scratch, cache);
    ASSERT_EQ(result.status, tess::PathStatus::Found);
    EXPECT_GT(result.expanded_nodes, 0u);
  }

  const auto stats = cache.stats();
  EXPECT_EQ(stats.entries, 0u);
  EXPECT_EQ(stats.hits, 0u);
  EXPECT_EQ(stats.misses, 2u);
}

}  // namespace
