#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <new>
#include <span>
#include <vector>

#include "allocation_counter.h"

namespace {

struct PassableTag {};
struct CostTag {};
struct OtherCostTag {};
using PortalClass = tess::movement::LegacyWeighted<PassableTag, CostTag>;
using OtherPortalClass =
    tess::movement::LegacyWeighted<PassableTag, OtherCostTag>;

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

template <std::size_t Size>
auto found_path_result(const std::array<tess::Coord3, Size>& path)
    -> tess::PathResult {
  return tess::PathResult{
      tess::PathStatus::Found,
      static_cast<std::uint32_t>(Size - 1),
      0,
      0,
      tess::PathView{std::span<const tess::Coord3>{path}},
  };
}

void expect_portal_stats_eq(const tess::PortalSegmentCacheStats& actual,
                            const tess::PortalSegmentCacheStats& expected) {
  EXPECT_EQ(actual.entries, expected.entries);
  EXPECT_EQ(actual.path_nodes, expected.path_nodes);
  EXPECT_EQ(actual.sweeps, expected.sweeps);
  EXPECT_EQ(actual.evictions, expected.evictions);
  EXPECT_EQ(actual.stale_rejections, expected.stale_rejections);
  EXPECT_EQ(actual.class_rebinds, expected.class_rebinds);
}

template <typename CacheView, typename World, std::size_t Size>
void expect_segment(CacheView& cache, const World& world,
                    tess::PathRequest request,
                    const std::array<tess::Coord3, Size>& expected) {
  std::vector<tess::Coord3> out;
  const auto hit = cache.lookup_append(world, request, out);
  ASSERT_TRUE(hit.found);
  EXPECT_EQ(out.size(), expected.size());
  EXPECT_TRUE(
      std::equal(out.begin(), out.end(), expected.begin(), expected.end()));
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
  auto class_cache = cache.for_class<PortalClass>();
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 0, 0}};

  const auto first =
      tess::weighted_astar_path<decltype(world), PassableTag, CostTag>(
          world, request, scratch);
  ASSERT_EQ(first.status, tess::PathStatus::Found);
  class_cache.store(world, request, first);
  ASSERT_EQ(cache.size(), 1u);

  world.template field<CostTag>(tess::Coord3{3, 0, 0}) = 9;
  world.mark_dirty(tess::ChunkKey{0}, 1u,
                   tess::Box3{tess::Coord3{3, 0, 0}, tess::Extent3{1, 1, 1}});

  const auto second =
      tess::weighted_astar_path<decltype(world), PassableTag, CostTag>(
          world, request, scratch);
  ASSERT_EQ(second.status, tess::PathStatus::Found);
  class_cache.store(world, request, second);

  // Pre-fix red evidence: two identical (start, goal) entries, one stale.
  EXPECT_EQ(cache.size(), 1u);
  EXPECT_EQ(cache.stats().sweeps, 1u);
  EXPECT_EQ(cache.stats().evictions, 0u);

  // The surviving entry is the fresh one.
  std::vector<tess::Coord3> out;
  const auto hit = class_cache.lookup_append(world, request, out);
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
  auto class_cache = cache.for_class<PortalClass>();

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
    class_cache.store(world, request, result);
  }

  const auto stats = cache.stats();
  EXPECT_EQ(stats.entries, 2u);
  EXPECT_EQ(stats.sweeps, 1u);
  EXPECT_EQ(stats.evictions, 1u);

  std::vector<tess::Coord3> out;
  EXPECT_FALSE(class_cache.lookup_append(world, requests[0], out).found);
  EXPECT_TRUE(class_cache.lookup_append(world, requests[1], out).found);
  out.clear();
  EXPECT_TRUE(class_cache.lookup_append(world, requests[2], out).found);
}

TEST(TessPathCache, PortalSegmentCacheAppliesLowerBudgetImmediately) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::WeightedPortalSegmentCache cache;
  cache.set_segment_budget(4);
  auto class_cache = cache.for_class<PortalClass>();
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
    class_cache.store(world, request, result);
  }
  ASSERT_EQ(cache.size(), 3u);

  cache.set_segment_budget(2);
  EXPECT_EQ(cache.size(), 2u);
  EXPECT_EQ(cache.stats().evictions, 1u);
  std::vector<tess::Coord3> out;
  EXPECT_FALSE(class_cache.lookup_append(world, requests[0], out).found);
  EXPECT_TRUE(class_cache.lookup_append(world, requests[1], out).found);

  cache.set_segment_budget(0);
  EXPECT_EQ(cache.size(), 0u);
  EXPECT_EQ(cache.stats().path_nodes, 0u);
  EXPECT_EQ(cache.stats().evictions, 3u);
  out.clear();
  EXPECT_FALSE(class_cache.lookup_append(world, requests[2], out).found);
}

TEST(TessPathCache, PortalSegmentCacheRebindDropsOtherMovementClass) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::WeightedPortalSegmentCache cache;
  auto class_cache = cache.for_class<PortalClass>();
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{3, 0, 0}};
  const auto result =
      tess::weighted_astar_path<decltype(world), PassableTag, CostTag>(
          world, request, scratch);
  ASSERT_EQ(result.status, tess::PathStatus::Found);
  class_cache.store(world, request, result);
  ASSERT_EQ(cache.size(), 1u);

  auto other_class_cache = cache.for_class<OtherPortalClass>();
  EXPECT_EQ(cache.size(), 0u);
  EXPECT_EQ(cache.stats().class_rebinds, 1u);
  std::vector<tess::Coord3> out;
  EXPECT_FALSE(other_class_cache.lookup_append(world, request, out).found);

  // Even a view retained across another class's bind cannot alias its entry.
  other_class_cache.store(world, request, result);
  ASSERT_EQ(cache.size(), 1u);
  EXPECT_FALSE(class_cache.lookup_append(world, request, out).found);
  EXPECT_EQ(cache.size(), 0u);
  EXPECT_EQ(cache.stats().class_rebinds, 2u);
}

TEST(TessPathCache, PortalSegmentCacheStoreHasStrongAllocationGuarantee) {
  if (!tess_test::allocation_failure_injection_supported()) {
    GTEST_SKIP() << "allocation failure injection is unavailable with this "
                    "allocator/runtime configuration";
  }

  constexpr auto first_path = std::array{
      tess::Coord3{0, 0, 0},
      tess::Coord3{1, 0, 0},
      tess::Coord3{2, 0, 0},
      tess::Coord3{3, 0, 0},
  };
  constexpr auto second_path = std::array{
      tess::Coord3{4, 0, 0},
      tess::Coord3{5, 0, 0},
      tess::Coord3{6, 0, 0},
      tess::Coord3{7, 0, 0},
  };
  constexpr auto candidate_path = std::array{
      tess::Coord3{0, 4, 0},
      tess::Coord3{1, 4, 0},
      tess::Coord3{2, 4, 0},
      tess::Coord3{3, 4, 0},
  };
  constexpr auto first_request =
      tess::PathRequest{first_path.front(), first_path.back()};
  constexpr auto second_request =
      tess::PathRequest{second_path.front(), second_path.back()};
  constexpr auto candidate_request =
      tess::PathRequest{candidate_path.front(), candidate_path.back()};

  auto saw_failure = false;
  auto reached_success = false;
  for (std::size_t failure_index = 0; failure_index < 16; ++failure_index) {
    tess::AlwaysResidentWorld<TopDown2D, Schema> world;
    tess::WeightedPortalSegmentCache cache;
    cache.set_segment_budget(2);
    cache.reserve_segments(3);
    cache.reserve_path_nodes(12);
    auto class_cache = cache.for_class<PortalClass>();
    class_cache.store(world, first_request, found_path_result(first_path));
    class_cache.store(world, second_request, found_path_result(second_path));
    const auto before = cache.stats();

    auto threw = false;
    {
      tess_test::ScopedAllocationFailure failure{failure_index};
      try {
        class_cache.store(world, candidate_request,
                          found_path_result(candidate_path));
      } catch (const std::bad_alloc&) {
        threw = true;
      }
    }

    if (!threw) {
      reached_success = true;
      EXPECT_EQ(cache.size(), 2u);
      EXPECT_EQ(cache.stats().sweeps, 1u);
      EXPECT_EQ(cache.stats().evictions, 1u);
      std::vector<tess::Coord3> out;
      EXPECT_FALSE(class_cache.lookup_append(world, first_request, out).found);
      expect_segment(class_cache, world, second_request, second_path);
      expect_segment(class_cache, world, candidate_request, candidate_path);
      break;
    }

    saw_failure = true;
    expect_portal_stats_eq(cache.stats(), before);
    expect_segment(class_cache, world, first_request, first_path);
    expect_segment(class_cache, world, second_request, second_path);
    std::vector<tess::Coord3> out;
    EXPECT_FALSE(
        class_cache.lookup_append(world, candidate_request, out).found);
    EXPECT_TRUE(out.empty());
  }

  EXPECT_TRUE(saw_failure);
  EXPECT_TRUE(reached_success);
}

TEST(TessPathCache, PortalSegmentCacheFailedStoreDefersStaleStats) {
  if (!tess_test::allocation_failure_injection_supported()) {
    GTEST_SKIP() << "allocation failure injection is unavailable with this "
                    "allocator/runtime configuration";
  }

  constexpr auto original_path = std::array{
      tess::Coord3{0, 0, 0},
      tess::Coord3{1, 0, 0},
      tess::Coord3{2, 0, 0},
      tess::Coord3{3, 0, 0},
  };
  constexpr auto replacement_path = std::array{
      tess::Coord3{0, 0, 0}, tess::Coord3{0, 1, 0}, tess::Coord3{0, 2, 0},
      tess::Coord3{0, 3, 0}, tess::Coord3{0, 4, 0}, tess::Coord3{1, 4, 0},
      tess::Coord3{2, 4, 0}, tess::Coord3{3, 4, 0}, tess::Coord3{3, 3, 0},
      tess::Coord3{3, 2, 0}, tess::Coord3{3, 1, 0}, tess::Coord3{3, 0, 0},
  };
  constexpr auto request =
      tess::PathRequest{original_path.front(), original_path.back()};

  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  tess::WeightedPortalSegmentCache cache;
  auto class_cache = cache.for_class<PortalClass>();
  class_cache.store(world, request, found_path_result(original_path));
  world.mark_dirty(tess::ChunkKey{0}, 1u,
                   tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}});
  const auto before = cache.stats();

  auto threw = false;
  {
    tess_test::ScopedAllocationFailure failure{0};
    try {
      class_cache.store(world, request, found_path_result(replacement_path));
    } catch (const std::bad_alloc&) {
      threw = true;
    }
  }

  EXPECT_TRUE(threw);
  expect_portal_stats_eq(cache.stats(), before);
  EXPECT_EQ(cache.size(), 1u);

  class_cache.store(world, request, found_path_result(replacement_path));
  EXPECT_EQ(cache.size(), 2u);
  EXPECT_EQ(cache.stats().stale_rejections, 1u);
  expect_segment(class_cache, world, request, replacement_path);
}

TEST(TessPathCache, PortalSegmentCacheSweepHasStrongAllocationGuarantee) {
  if (!tess_test::allocation_failure_injection_supported()) {
    GTEST_SKIP() << "allocation failure injection is unavailable with this "
                    "allocator/runtime configuration";
  }

  constexpr auto stale_path = std::array{
      tess::Coord3{0, 0, 0},
      tess::Coord3{1, 0, 0},
      tess::Coord3{2, 0, 0},
      tess::Coord3{3, 0, 0},
  };
  constexpr auto live_path = std::array{
      tess::Coord3{4, 0, 0},
      tess::Coord3{5, 0, 0},
      tess::Coord3{6, 0, 0},
      tess::Coord3{7, 0, 0},
  };
  constexpr auto stale_request =
      tess::PathRequest{stale_path.front(), stale_path.back()};
  constexpr auto live_request =
      tess::PathRequest{live_path.front(), live_path.back()};

  auto saw_failure = false;
  auto reached_success = false;
  for (std::size_t failure_index = 0; failure_index < 16; ++failure_index) {
    tess::AlwaysResidentWorld<TopDown2D, Schema> world;
    tess::WeightedPortalSegmentCache cache;
    cache.reserve_segments(2);
    cache.reserve_path_nodes(8);
    auto class_cache = cache.for_class<PortalClass>();
    class_cache.store(world, stale_request, found_path_result(stale_path));
    class_cache.store(world, live_request, found_path_result(live_path));
    world.mark_dirty(tess::ChunkKey{0}, 1u,
                     tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}});
    const auto before = cache.stats();

    auto threw = false;
    {
      tess_test::ScopedAllocationFailure failure{failure_index};
      try {
        cache.sweep_stale(world);
      } catch (const std::bad_alloc&) {
        threw = true;
      }
    }

    if (!threw) {
      reached_success = true;
      EXPECT_EQ(cache.size(), 1u);
      EXPECT_EQ(cache.stats().sweeps, 1u);
      expect_segment(class_cache, world, live_request, live_path);
      std::vector<tess::Coord3> out;
      EXPECT_FALSE(class_cache.lookup_append(world, stale_request, out).found);
      break;
    }

    saw_failure = true;
    expect_portal_stats_eq(cache.stats(), before);
    expect_segment(class_cache, world, live_request, live_path);

    // Retrying after a failed compaction must still read the original source
    // offsets and dependencies, then commit the same result as a fresh sweep.
    cache.sweep_stale(world);
    EXPECT_EQ(cache.size(), 1u);
    expect_segment(class_cache, world, live_request, live_path);
    std::vector<tess::Coord3> out;
    EXPECT_FALSE(class_cache.lookup_append(world, stale_request, out).found);
  }

  EXPECT_TRUE(saw_failure);
  EXPECT_TRUE(reached_success);
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
  auto class_cache = cache.for_class<PortalClass>();
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{3, 0, 0}};

  const auto result =
      tess::weighted_astar_path<decltype(world), PassableTag, CostTag>(
          world, request, scratch);
  ASSERT_EQ(result.status, tess::PathStatus::Found);
  class_cache.store(world, request, result);
  ASSERT_EQ(cache.stats().stale_rejections, 0u);

  world.mark_dirty(tess::ChunkKey{0}, 1u,
                   tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}});

  std::vector<tess::Coord3> out;
  EXPECT_FALSE(class_cache.lookup_append(world, request, out).found);
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
  requests.reserve(6);
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

TEST(TessPathCache, RouteCacheAppliesLowerCapsImmediately) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::RouteCacheScratch cache;
  const auto first =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 0, 0}};
  const auto second =
      tess::PathRequest{tess::Coord3{0, 1, 0}, tess::Coord3{7, 1, 0}};
  ASSERT_EQ((tess::cached_astar_path<decltype(world), PassableTag>(
                 world, first, scratch, cache))
                .status,
            tess::PathStatus::Found);
  ASSERT_EQ((tess::cached_astar_path<decltype(world), PassableTag>(
                 world, second, scratch, cache))
                .status,
            tess::PathStatus::Found);
  ASSERT_EQ(cache.stats().entries, 2u);

  cache.set_caps(1, tess::RouteCacheScratch::default_max_path_nodes);
  EXPECT_EQ(cache.stats().entries, 0u);
  EXPECT_EQ(cache.stats().path_nodes, 0u);
  EXPECT_EQ(cache.stats().cap_invalidations, 1u);

  const auto recomputed = tess::cached_astar_path<decltype(world), PassableTag>(
      world, first, scratch, cache);
  EXPECT_EQ(recomputed.status, tess::PathStatus::Found);
  EXPECT_GT(recomputed.expanded_nodes, 0u);

  cache.set_caps(0, 0);
  EXPECT_EQ(cache.stats().entries, 0u);
  EXPECT_EQ(cache.stats().cap_invalidations, 2u);
  const auto disabled = tess::cached_astar_path<decltype(world), PassableTag>(
      world, first, scratch, cache);
  EXPECT_EQ(disabled.status, tess::PathStatus::Found);
  EXPECT_GT(disabled.expanded_nodes, 0u);
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
