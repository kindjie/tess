#include <gtest/gtest.h>
#include <tess/tess.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <set>
#include <utility>
#include <vector>

#include "allocation_counter.h"

namespace {

struct PassableTag {};
struct CostTag {};

using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                 tess::Field<CostTag, std::uint32_t>>;
using Runtime2D = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{8, 8, 1}>;
using World = tess::AlwaysResidentWorld<Runtime2D, Schema>;
constexpr auto RuntimeTileCount =
    Runtime2D::size.x * Runtime2D::size.y * Runtime2D::size.z;

template <typename FieldTag, typename Value>
void fill_field(World& world, Value value) {
  for (auto& page : world.chunks()) {
    auto field = page.template field_span<FieldTag>();
    for (auto& tile : field) {
      tile = value;
    }
  }
}

void fill_world(World& world) {
  fill_field<PassableTag>(world, true);
  fill_field<CostTag>(world, 1u);
}

void mark_cost(World& world, tess::Coord3 coord, std::uint32_t cost) {
  world.template field<CostTag>(coord) = cost;
  world.mark_dirty(tess::chunk_key<Runtime2D>(tess::tile_key<Runtime2D>(coord)),
                   1u, tess::Box3{coord, tess::Extent3{1, 1, 1}});
}

TEST(TessPathRuntime, UnitCachedRequestsReuseAndInvalidateRoutes) {
  World world;
  fill_world(world);

  tess::PathRequestRuntime runtime;
  runtime.reserve_requests(3);
  runtime.reserve_search_nodes(RuntimeTileCount);
  runtime.reserve_path_nodes(128);
  runtime.reserve_unit_routes(4);

  const auto first = runtime.submit(
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 0, 0}});
  const auto second = runtime.submit(
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 0, 0}});
  (void)runtime.submit(
      tess::PathRequest{tess::Coord3{3, 0, 0}, tess::Coord3{7, 0, 0}});

  auto results = runtime.process_unit_cached<World, PassableTag>(world);
  ASSERT_EQ(results.size(), 3u);
  EXPECT_EQ(runtime.result(first).status, tess::PathStatus::Found);
  EXPECT_EQ(runtime.result(second).status, tess::PathStatus::Found);
  EXPECT_EQ(runtime.result(first).path.size(), 8u);
  EXPECT_EQ(runtime.result(second).path.size(), 8u);

  auto stats = runtime.stats();
  EXPECT_EQ(stats.found, 3u);
  EXPECT_EQ(stats.route_cache.entries, 1u);
  EXPECT_EQ(stats.route_cache.misses, 1u);
  EXPECT_EQ(stats.route_cache.hits, 1u);
  EXPECT_EQ(stats.route_cache.suffix_hits, 1u);

  world.template field<PassableTag>(tess::Coord3{1, 0, 0}) = false;
  world.mark_dirty(tess::ChunkKey{0}, 1u,
                   tess::Box3{tess::Coord3{1, 0, 0}, tess::Extent3{1, 1, 1}});

  results = runtime.process_unit_cached<World, PassableTag>(world);
  ASSERT_EQ(results.size(), 3u);
  EXPECT_EQ(runtime.result(first).status, tess::PathStatus::Found);
  EXPECT_EQ(runtime.result(first).path.size(), 10u);

  stats = runtime.stats();
  EXPECT_EQ(stats.world_cache_invalidations, 1u);
  EXPECT_EQ(stats.cache_clears, 0u);
  EXPECT_EQ(stats.route_cache.entries, 2u);
  EXPECT_EQ(stats.route_cache.misses, 3u);
  EXPECT_EQ(stats.route_cache.hits, 2u);
  EXPECT_EQ(stats.route_cache.suffix_hits, 1u);
}

TEST(TessPathRuntime, UnitFieldProductsReuseRepeatedGoalsWhenEnabled) {
  World world;
  fill_world(world);

  tess::PathRequestRuntime runtime;
  runtime.reserve_requests(4);
  runtime.reserve_search_nodes(RuntimeTileCount);
  runtime.reserve_path_nodes(256);
  runtime.reserve_unit_routes(4);
  runtime.reserve_unit_field_products(2);
  runtime.reserve_unit_field_product_dependencies(World::chunk_count);

  for (std::int64_t y = 0; y < 3; ++y) {
    (void)runtime.submit(
        tess::PathRequest{tess::Coord3{0, y, 0}, tess::Coord3{31, 31, 0}});
  }
  (void)runtime.submit(
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 0, 0}});

  const auto policy = tess::PathRuntimeCachePolicy{
      .use_unit_field_product_cache = true,
      .unit_field_product_min_start_chunks = 1,
  };

  auto results = runtime.process_unit_cached<World, PassableTag>(world, policy);
  ASSERT_EQ(results.size(), 4u);
  for (const auto result : results) {
    EXPECT_EQ(result.status, tess::PathStatus::Found);
  }

  auto stats = runtime.stats();
  EXPECT_EQ(stats.found, 4u);
  EXPECT_EQ(stats.field_product_cache.entries, 1u);
  EXPECT_EQ(stats.field_product_cache.misses, 1u);
  EXPECT_GE(stats.field_product_cache.hits, 1u);
  EXPECT_EQ(stats.field_product_candidate_groups, 1u);
  EXPECT_EQ(stats.field_product_used_groups, 1u);
  EXPECT_EQ(stats.field_product_skipped_groups, 0u);
  EXPECT_EQ(stats.route_cache.misses, 1u);

  results = runtime.process_unit_cached<World, PassableTag>(world, policy);
  ASSERT_EQ(results.size(), 4u);
  stats = runtime.stats();
  EXPECT_EQ(stats.found, 4u);
  EXPECT_EQ(stats.field_product_cache.entries, 1u);
  EXPECT_GE(stats.field_product_cache.hits, 2u);
  EXPECT_EQ(stats.field_product_candidate_groups, 1u);
  EXPECT_EQ(stats.field_product_used_groups, 1u);
  EXPECT_EQ(stats.field_product_skipped_groups, 0u);
  EXPECT_EQ(stats.route_cache.hits, 1u);
}

TEST(TessPathRuntime, UnitFieldProductPolicySkipsSingleStartChunkGroups) {
  World world;
  fill_world(world);

  tess::PathRequestRuntime runtime;
  runtime.reserve_requests(3);
  runtime.reserve_search_nodes(RuntimeTileCount);
  runtime.reserve_path_nodes(256);
  runtime.reserve_unit_routes(3);
  runtime.reserve_unit_field_products(1);
  runtime.reserve_unit_field_product_dependencies(World::chunk_count);

  for (std::int64_t y = 0; y < 3; ++y) {
    (void)runtime.submit(
        tess::PathRequest{tess::Coord3{0, y, 0}, tess::Coord3{31, 31, 0}});
  }

  const auto policy = tess::PathRuntimeCachePolicy{
      .use_unit_field_product_cache = true,
  };

  const auto results =
      runtime.process_unit_cached<World, PassableTag>(world, policy);
  ASSERT_EQ(results.size(), 3u);
  for (const auto result : results) {
    EXPECT_EQ(result.status, tess::PathStatus::Found);
  }

  const auto stats = runtime.stats();
  EXPECT_EQ(stats.found, 3u);
  EXPECT_EQ(stats.field_product_candidate_groups, 1u);
  EXPECT_EQ(stats.field_product_used_groups, 0u);
  EXPECT_EQ(stats.field_product_skipped_groups, 1u);
  EXPECT_EQ(stats.field_product_cache.entries, 0u);
  EXPECT_EQ(stats.route_cache.misses, 3u);
}

TEST(TessPathRuntime, UnitFieldProductPolicyUsesScatteredStartChunks) {
  World world;
  fill_world(world);

  tess::PathRequestRuntime runtime;
  runtime.reserve_requests(3);
  runtime.reserve_search_nodes(RuntimeTileCount);
  runtime.reserve_path_nodes(256);
  runtime.reserve_unit_routes(3);
  runtime.reserve_unit_field_products(1);
  runtime.reserve_unit_field_product_dependencies(World::chunk_count);

  (void)runtime.submit(
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{31, 31, 0}});
  (void)runtime.submit(
      tess::PathRequest{tess::Coord3{8, 0, 0}, tess::Coord3{31, 31, 0}});
  (void)runtime.submit(
      tess::PathRequest{tess::Coord3{0, 8, 0}, tess::Coord3{31, 31, 0}});

  const auto policy = tess::PathRuntimeCachePolicy{
      .use_unit_field_product_cache = true,
  };

  const auto results =
      runtime.process_unit_cached<World, PassableTag>(world, policy);
  ASSERT_EQ(results.size(), 3u);
  for (const auto result : results) {
    EXPECT_EQ(result.status, tess::PathStatus::Found);
  }

  const auto stats = runtime.stats();
  EXPECT_EQ(stats.found, 3u);
  EXPECT_EQ(stats.field_product_candidate_groups, 1u);
  EXPECT_EQ(stats.field_product_used_groups, 1u);
  EXPECT_EQ(stats.field_product_skipped_groups, 0u);
  EXPECT_EQ(stats.field_product_cache.entries, 1u);
  EXPECT_EQ(stats.route_cache.misses, 0u);
}

TEST(TessPathRuntime, UnitFieldProductCacheRejectsStaleProductsAfterEdit) {
  World world;
  fill_world(world);

  tess::PathRequestRuntime runtime;
  runtime.reserve_requests(2);
  runtime.reserve_search_nodes(RuntimeTileCount);
  runtime.reserve_path_nodes(256);
  runtime.reserve_unit_field_products(1);
  runtime.reserve_unit_field_product_dependencies(World::chunk_count);

  (void)runtime.submit(
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{31, 31, 0}});
  (void)runtime.submit(
      tess::PathRequest{tess::Coord3{0, 1, 0}, tess::Coord3{31, 31, 0}});

  const auto policy = tess::PathRuntimeCachePolicy{
      .use_unit_field_product_cache = true,
      .unit_field_product_min_start_chunks = 1,
  };
  auto results = runtime.process_unit_cached<World, PassableTag>(world, policy);
  ASSERT_EQ(results.size(), 2u);
  EXPECT_EQ(runtime.stats().field_product_cache.entries, 1u);

  world.mark_dirty(tess::ChunkKey{0}, 1u,
                   tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}});
  results = runtime.process_unit_cached<World, PassableTag>(world, policy);
  ASSERT_EQ(results.size(), 2u);

  const auto stats = runtime.stats();
  EXPECT_EQ(stats.found, 2u);
  EXPECT_EQ(stats.world_cache_invalidations, 1u);
  EXPECT_EQ(stats.field_product_cache.entries, 1u);
  EXPECT_EQ(stats.field_product_cache.stale_rejections, 1u);
}

// An edit that opens a fully-blocked chunk never touches a flooded node, but
// it changes reachability: the cached product must be rejected as stale, not
// replayed as a wrong NoPath for starts inside the newly opened chunk.
TEST(TessPathRuntime,
     UnitFieldProductCacheRejectsStaleAfterUnreachedChunkEdit) {
  World world;
  fill_world(world);
  for (std::int64_t y = 0; y < 8; ++y) {
    for (std::int64_t x = 8; x < 16; ++x) {
      world.template field<PassableTag>(tess::Coord3{x, y, 0}) = false;
    }
  }

  tess::PathRequestRuntime runtime;
  runtime.reserve_requests(3);
  runtime.reserve_search_nodes(RuntimeTileCount);
  runtime.reserve_path_nodes(256);
  runtime.reserve_unit_field_products(1);
  runtime.reserve_unit_field_product_dependencies(World::chunk_count);

  (void)runtime.submit(
      tess::PathRequest{tess::Coord3{0, 31, 0}, tess::Coord3{0, 0, 0}});
  (void)runtime.submit(
      tess::PathRequest{tess::Coord3{31, 31, 0}, tess::Coord3{0, 0, 0}});

  const auto policy = tess::PathRuntimeCachePolicy{
      .use_unit_field_product_cache = true,
      .unit_field_product_min_start_chunks = 1,
  };
  auto results = runtime.process_unit_cached<World, PassableTag>(world, policy);
  ASSERT_EQ(results.size(), 2u);
  EXPECT_EQ(runtime.stats().found, 2u);
  EXPECT_EQ(runtime.stats().field_product_cache.entries, 1u);

  world.template field<PassableTag>(tess::Coord3{8, 0, 0}) = true;
  world.mark_dirty(tess::ChunkKey{1}, 1u,
                   tess::Box3{tess::Coord3{8, 0, 0}, tess::Extent3{1, 1, 1}});
  (void)runtime.submit(
      tess::PathRequest{tess::Coord3{8, 0, 0}, tess::Coord3{0, 0, 0}});

  results = runtime.process_unit_cached<World, PassableTag>(world, policy);
  ASSERT_EQ(results.size(), 3u);

  const auto stats = runtime.stats();
  EXPECT_EQ(stats.found, 3u);
  EXPECT_EQ(stats.no_path, 0u);
  EXPECT_EQ(stats.field_product_cache.stale_rejections, 1u);
}

TEST(TessPathRuntime, UnitFieldProductCacheClearsOnWorldChangeCadence) {
  World world;
  fill_world(world);

  tess::PathRequestRuntime runtime;
  runtime.reserve_requests(2);
  runtime.reserve_search_nodes(RuntimeTileCount);
  runtime.reserve_path_nodes(256);
  runtime.reserve_unit_field_products(1);
  runtime.reserve_unit_field_product_dependencies(World::chunk_count);

  (void)runtime.submit(
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{31, 31, 0}});
  (void)runtime.submit(
      tess::PathRequest{tess::Coord3{0, 1, 0}, tess::Coord3{31, 31, 0}});

  const auto policy = tess::PathRuntimeCachePolicy{
      .clear_every_world_change = 1,
      .use_unit_field_product_cache = true,
      .unit_field_product_min_start_chunks = 1,
  };
  (void)runtime.process_unit_cached<World, PassableTag>(world, policy);
  EXPECT_EQ(runtime.stats().cache_clears, 0u);

  world.mark_dirty(tess::ChunkKey{0}, 1u,
                   tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}});
  (void)runtime.process_unit_cached<World, PassableTag>(world, policy);

  const auto stats = runtime.stats();
  EXPECT_EQ(stats.found, 2u);
  EXPECT_EQ(stats.cache_clears, 1u);
  EXPECT_EQ(stats.field_product_cache.entries, 1u);
  EXPECT_EQ(stats.field_product_cache.stale_rejections, 0u);
}

TEST(TessPathRuntime, FieldProductCacheLookupPointerStableAcrossStores) {
  World world;
  fill_world(world);

  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(RuntimeTileCount);
  tess::FieldProductCache cache;

  tess::GoalSet held_goals;
  held_goals.add(tess::Coord3{31, 31, 0});
  tess::DistanceFieldProduct first;
  first.reserve_nodes(RuntimeTileCount);
  first.reserve_dependencies(World::chunk_count);
  ASSERT_EQ((tess::build_distance_field_product<World, PassableTag>(
                 world, held_goals, scratch, first))
                .status,
            tess::PathStatus::Found);
  ASSERT_TRUE((cache.store<World, PassableTag>(std::move(first))));

  const auto* held = cache.lookup<World, PassableTag>(world, held_goals);
  ASSERT_NE(held, nullptr);

  for (const std::int64_t x : {0, 8, 16, 24}) {
    tess::GoalSet other_goals;
    other_goals.add(tess::Coord3{x, 0, 0});
    tess::DistanceFieldProduct other;
    other.reserve_nodes(RuntimeTileCount);
    other.reserve_dependencies(World::chunk_count);
    ASSERT_EQ((tess::build_distance_field_product<World, PassableTag>(
                   world, other_goals, scratch, other))
                  .status,
              tess::PathStatus::Found);
    ASSERT_TRUE((cache.store<World, PassableTag>(std::move(other))));
  }
  ASSERT_EQ(cache.stats().entries, 5u);

  EXPECT_EQ(held->status(), tess::PathStatus::Found);
  ASSERT_EQ(held->goals().size(), 1u);
  EXPECT_EQ(held->goals().front(), (tess::Coord3{31, 31, 0}));

  const auto replay = tess::distance_field_product_path<World, PassableTag>(
      world, tess::Coord3{0, 0, 0}, *held, scratch);
  EXPECT_EQ(replay.status, tess::PathStatus::Found);

  EXPECT_EQ((cache.lookup<World, PassableTag>(world, held_goals)), held);
}

TEST(TessPathRuntime, WeightedBatchHandlesManyAgentsAndCacheClearCadence) {
  World world;
  fill_world(world);

  tess::PathRequestRuntime runtime;
  runtime.reserve_requests(32);
  runtime.reserve_search_nodes(RuntimeTileCount);
  runtime.reserve_path_nodes(2048);
  runtime.reserve_unit_routes(8);

  for (std::int64_t y = 0; y < 16; ++y) {
    (void)runtime.submit(
        tess::PathRequest{tess::Coord3{0, y, 0}, tess::Coord3{31, 31, 0}});
  }

  const auto policy = tess::PathRuntimeCachePolicy{
      .clear_every_world_change = 2,
      .invalidate_unit_route_cache_on_world_change = true,
  };

  auto results = runtime.process_weighted_batch<World, PassableTag, CostTag, 8>(
      world, policy);
  ASSERT_EQ(results.size(), 16u);
  for (const auto result : results) {
    EXPECT_EQ(result.status, tess::PathStatus::Found);
    EXPECT_FALSE(result.path.empty());
  }

  auto stats = runtime.stats();
  EXPECT_EQ(stats.found, 16u);
  EXPECT_EQ(stats.weighted_batch.requests, 16u);
  EXPECT_EQ(stats.weighted_batch.unique_goals, 1u);
  EXPECT_EQ(stats.weighted_batch.field_builds, 1u);
  EXPECT_EQ(stats.cache_clears, 0u);

  mark_cost(world, tess::Coord3{1, 0, 0}, 4);
  results = runtime.process_weighted_batch<World, PassableTag, CostTag, 8>(
      world, policy);
  ASSERT_EQ(results.size(), 16u);
  stats = runtime.stats();
  EXPECT_EQ(stats.world_cache_invalidations, 1u);
  EXPECT_EQ(stats.cache_clears, 0u);

  mark_cost(world, tess::Coord3{2, 0, 0}, 5);
  results = runtime.process_weighted_batch<World, PassableTag, CostTag, 8>(
      world, policy);
  ASSERT_EQ(results.size(), 16u);
  stats = runtime.stats();
  EXPECT_EQ(stats.world_cache_invalidations, 1u);
  EXPECT_EQ(stats.cache_clears, 1u);
}

// The runtime's processing passes never populate the portal segment cache:
// portal route products are caller-driven through
// build_weighted_portal_route_product with the runtime-owned cache accessor.
// This pins the real runtime contract for that cache: stats report entries
// stored through the accessor, and clear_caches() drops them.
TEST(TessPathRuntime, PortalSegmentCacheStatsTrackAccessorStoresAndClear) {
  World world;
  fill_world(world);

  tess::PathRequestRuntime runtime;
  runtime.reserve_portal_segments(2);
  runtime.portal_segment_cache().reserve_path_nodes(128);

  tess::PathScratch scratch;
  scratch.reserve_nodes(RuntimeTileCount);
  tess::WeightedPortalRouteProduct product;
  product.reserve_waypoints(1);
  product.reserve_path_nodes(64);
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{31, 0, 0}};
  const auto waypoints = std::array{tess::Coord3{15, 0, 0}};

  const auto built =
      tess::build_weighted_portal_route_product<World, PassableTag, CostTag>(
          world, request, waypoints, scratch, runtime.portal_segment_cache(),
          product);

  ASSERT_EQ(built.status, tess::PathStatus::Found);
  EXPECT_EQ(runtime.stats().portal_segment_cache.entries, 2u);
  EXPECT_GT(runtime.stats().portal_segment_cache.path_nodes, 0u);

  runtime.clear_caches();
  EXPECT_EQ(runtime.stats().portal_segment_cache.entries, 0u);
  EXPECT_EQ(runtime.stats().portal_segment_cache.path_nodes, 0u);
}

TEST(TessPathRuntime, ClearRequestsStartsNewFrameWithFreshTickets) {
  World world;
  fill_world(world);

  tess::PathRequestRuntime runtime;
  const auto first = runtime.submit(
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{5, 0, 0}});
  (void)runtime.submit(
      tess::PathRequest{tess::Coord3{0, 1, 0}, tess::Coord3{5, 1, 0}});
  auto results = runtime.process_unit_cached<World, PassableTag>(world);
  ASSERT_EQ(results.size(), 2u);
  EXPECT_EQ(runtime.result(first).status, tess::PathStatus::Found);
  EXPECT_EQ(runtime.result(first).path.size(), 6u);

  runtime.clear_requests();
  EXPECT_TRUE(runtime.requests().empty());
  EXPECT_TRUE(runtime.results().empty());

  const auto resubmitted = runtime.submit(
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{9, 0, 0}});
  results = runtime.process_unit_cached<World, PassableTag>(world);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(runtime.result(resubmitted).status, tess::PathStatus::Found);
  EXPECT_EQ(runtime.result(resubmitted).path.size(), 10u);
  EXPECT_EQ(runtime.result(resubmitted).path.back(), (tess::Coord3{9, 0, 0}));

  const auto stats = runtime.stats();
  EXPECT_EQ(stats.submitted, 1u);
  EXPECT_EQ(stats.completed, 1u);
  EXPECT_EQ(stats.found, 1u);
}

TEST(TessPathRuntime, EmptyRequestListProcessesToEmptyResults) {
  World world;
  fill_world(world);

  tess::PathRequestRuntime runtime;
  const auto policy = tess::PathRuntimeCachePolicy{
      .use_unit_field_product_cache = true,
  };
  const auto unit_results =
      runtime.process_unit_cached<World, PassableTag>(world, policy);
  EXPECT_TRUE(unit_results.empty());

  const auto weighted_results =
      runtime.process_weighted_batch<World, PassableTag, CostTag, 8>(world);
  EXPECT_TRUE(weighted_results.empty());

  const auto stats = runtime.stats();
  EXPECT_EQ(stats.submitted, 0u);
  EXPECT_EQ(stats.completed, 0u);
  EXPECT_EQ(stats.found, 0u);
  EXPECT_EQ(stats.field_product_candidate_groups, 0u);
  EXPECT_EQ(stats.weighted_batch.requests, 0u);
}

TEST(TessPathRuntime, FailureStatusCountersTallyMixedBatches) {
  World world;
  fill_world(world);
  // Blocked start tile, blocked goal tile, and a sealed pocket around
  // (16, 16) for a NoPath result.
  world.template field<PassableTag>(tess::Coord3{2, 2, 0}) = false;
  world.template field<PassableTag>(tess::Coord3{20, 20, 0}) = false;
  for (const auto coord : {tess::Coord3{15, 15, 0}, tess::Coord3{16, 15, 0},
                           tess::Coord3{17, 15, 0}, tess::Coord3{15, 16, 0},
                           tess::Coord3{17, 16, 0}, tess::Coord3{15, 17, 0},
                           tess::Coord3{16, 17, 0}, tess::Coord3{17, 17, 0}}) {
    world.template field<PassableTag>(coord) = false;
  }

  const auto submit_mixed = [](tess::PathRequestRuntime& runtime) {
    (void)runtime.submit(
        tess::PathRequest{tess::Coord3{2, 2, 0}, tess::Coord3{5, 5, 0}});
    (void)runtime.submit(
        tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{20, 20, 0}});
    (void)runtime.submit(
        tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{16, 16, 0}});
    (void)runtime.submit(
        tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{5, 0, 0}});
  };

  tess::PathRequestRuntime unit_runtime;
  submit_mixed(unit_runtime);
  const auto unit_results =
      unit_runtime.process_unit_cached<World, PassableTag>(world);
  ASSERT_EQ(unit_results.size(), 4u);
  auto stats = unit_runtime.stats();
  EXPECT_EQ(stats.invalid_start, 1u);
  EXPECT_EQ(stats.invalid_goal, 1u);
  EXPECT_EQ(stats.no_path, 1u);
  EXPECT_EQ(stats.found, 1u);

  tess::PathRequestRuntime weighted_runtime;
  submit_mixed(weighted_runtime);
  const auto weighted_results =
      weighted_runtime.process_weighted_batch<World, PassableTag, CostTag, 8>(
          world);
  ASSERT_EQ(weighted_results.size(), 4u);
  stats = weighted_runtime.stats();
  EXPECT_EQ(stats.invalid_start, 1u);
  EXPECT_EQ(stats.invalid_goal, 1u);
  EXPECT_EQ(stats.no_path, 1u);
  EXPECT_EQ(stats.found, 1u);
}

// The runtime policy byte budget must drive real eviction inside
// process_unit_cached: two world-sized products cannot coexist under a
// budget sized for one, so alternating goal groups evict each other.
TEST(TessPathRuntime, UnitFieldProductByteBudgetEvictsThroughProcessing) {
  World world;
  fill_world(world);

  tess::PathRequestRuntime runtime;
  runtime.reserve_search_nodes(RuntimeTileCount);
  const auto policy = tess::PathRuntimeCachePolicy{
      .use_unit_field_product_cache = true,
      .unit_field_product_min_start_chunks = 1,
      .unit_field_product_cache_byte_budget = 6144,
  };

  const auto submit_group = [&runtime](tess::Coord3 goal) {
    (void)runtime.submit(tess::PathRequest{tess::Coord3{0, 0, 0}, goal});
    (void)runtime.submit(tess::PathRequest{tess::Coord3{8, 0, 0}, goal});
    (void)runtime.submit(tess::PathRequest{tess::Coord3{0, 8, 0}, goal});
  };

  submit_group(tess::Coord3{31, 31, 0});
  (void)runtime.process_unit_cached<World, PassableTag>(world, policy);
  auto cache_stats = runtime.stats().field_product_cache;
  EXPECT_EQ(cache_stats.entries, 1u);
  EXPECT_EQ(cache_stats.evictions, 0u);
  EXPECT_LE(cache_stats.bytes, 6144u);

  runtime.clear_requests();
  submit_group(tess::Coord3{31, 0, 0});
  (void)runtime.process_unit_cached<World, PassableTag>(world, policy);
  cache_stats = runtime.stats().field_product_cache;
  EXPECT_EQ(cache_stats.entries, 1u);
  EXPECT_EQ(cache_stats.evictions, 1u);

  // The evicted first goal misses again and evicts the second in turn.
  runtime.clear_requests();
  submit_group(tess::Coord3{31, 31, 0});
  (void)runtime.process_unit_cached<World, PassableTag>(world, policy);
  cache_stats = runtime.stats().field_product_cache;
  EXPECT_EQ(cache_stats.entries, 1u);
  EXPECT_EQ(cache_stats.evictions, 2u);
  EXPECT_EQ(cache_stats.misses, 3u);
  EXPECT_EQ(runtime.stats().field_product_used_groups, 1u);
}

// Seeded randomized equivalence: grouped field-product processing must
// return the same statuses and costs as per-request A*, and the group
// counters must match a straightforward reference computation. This pins
// the grouping semantics across the flat-map grouping rewrite.
TEST(TessPathRuntime, RepeatedGoalGroupingMatchesAstarOracle) {
  for (const auto seed : {11u, 29u, 47u}) {
    for (const auto min_start_chunks : {std::size_t{1}, std::size_t{2}}) {
      World world;
      fill_world(world);
      std::mt19937 rng(seed);
      std::uniform_int_distribution<std::int64_t> coord_dist(0, 31);
      for (int i = 0; i < 60; ++i) {
        const auto coord = tess::Coord3{coord_dist(rng), coord_dist(rng), 0};
        world.template field<PassableTag>(coord) = false;
      }

      std::vector<tess::Coord3> passable;
      for (std::int64_t y = 0; y < 32; ++y) {
        for (std::int64_t x = 0; x < 32; ++x) {
          const auto coord = tess::Coord3{x, y, 0};
          if (world.template field<PassableTag>(coord)) {
            passable.push_back(coord);
          }
        }
      }
      ASSERT_GT(passable.size(), 64u);
      auto blocked_goal = tess::Coord3{0, 0, 0};
      auto blocked_goal_found = false;
      for (std::int64_t y = 0; y < 32 && !blocked_goal_found; ++y) {
        for (std::int64_t x = 0; x < 32 && !blocked_goal_found; ++x) {
          const auto coord = tess::Coord3{x, y, 0};
          if (!world.template field<PassableTag>(coord)) {
            blocked_goal = coord;
            blocked_goal_found = true;
          }
        }
      }
      ASSERT_TRUE(blocked_goal_found);

      std::uniform_int_distribution<std::size_t> pick(0, passable.size() - 1);
      std::vector<tess::Coord3> goal_pool;
      goal_pool.reserve(7);
      for (int i = 0; i < 6; ++i) {
        goal_pool.push_back(passable[pick(rng)]);
      }
      goal_pool.push_back(blocked_goal);

      std::vector<tess::PathRequest> requests;
      std::uniform_int_distribution<std::size_t> goal_pick(
          0, goal_pool.size() - 1);
      std::uniform_int_distribution<int> unique_dist(0, 99);
      for (int i = 0; i < 120; ++i) {
        const auto start = passable[pick(rng)];
        const auto goal = unique_dist(rng) < 25 ? passable[pick(rng)]
                                                : goal_pool[goal_pick(rng)];
        requests.push_back(tess::PathRequest{start, goal});
      }

      tess::PathRequestRuntime runtime;
      runtime.reserve_search_nodes(RuntimeTileCount);
      for (const auto& request : requests) {
        (void)runtime.submit(request);
      }
      const auto policy = tess::PathRuntimeCachePolicy{
          .use_unit_field_product_cache = true,
          .unit_field_product_min_start_chunks = min_start_chunks,
      };
      const auto results =
          runtime.process_unit_cached<World, PassableTag>(world, policy);
      ASSERT_EQ(results.size(), requests.size());

      tess::PathScratch oracle_scratch;
      for (std::size_t i = 0; i < requests.size(); ++i) {
        const auto oracle = tess::astar_path<World, PassableTag>(
            world, requests[i], oracle_scratch);
        ASSERT_EQ(results[i].status, oracle.status)
            << "seed " << seed << " chunks " << min_start_chunks << " request "
            << i;
        ASSERT_EQ(results[i].cost, oracle.cost)
            << "seed " << seed << " chunks " << min_start_chunks << " request "
            << i;
        if (oracle.status == tess::PathStatus::Found) {
          ASSERT_FALSE(results[i].path.empty());
          EXPECT_EQ(results[i].path.front(), requests[i].start);
          EXPECT_EQ(results[i].path.back(), requests[i].goal);
        }
      }

      // Reference group counters over first-occurrence goal groups.
      std::vector<tess::Coord3> goals;
      std::vector<std::vector<tess::Coord3>> starts;
      for (const auto& request : requests) {
        auto found = false;
        for (std::size_t g = 0; g < goals.size(); ++g) {
          if (goals[g] == request.goal) {
            starts[g].push_back(request.start);
            found = true;
            break;
          }
        }
        if (!found) {
          goals.push_back(request.goal);
          starts.push_back({request.start});
        }
      }
      auto candidates = std::size_t{0};
      auto used = std::size_t{0};
      auto skipped = std::size_t{0};
      for (std::size_t g = 0; g < goals.size(); ++g) {
        if (starts[g].size() < 2) {
          continue;
        }
        ++candidates;
        std::set<std::uint64_t> chunks;
        for (const auto start : starts[g]) {
          chunks.insert(
              tess::chunk_key<Runtime2D>(tess::tile_key<Runtime2D>(start))
                  .value);
        }
        if (chunks.size() < min_start_chunks ||
            !world.template field<PassableTag>(goals[g])) {
          ++skipped;
        } else {
          ++used;
        }
      }
      const auto stats = runtime.stats();
      EXPECT_EQ(stats.field_product_candidate_groups, candidates)
          << "seed " << seed << " chunks " << min_start_chunks;
      EXPECT_EQ(stats.field_product_used_groups, used)
          << "seed " << seed << " chunks " << min_start_chunks;
      EXPECT_EQ(stats.field_product_skipped_groups, skipped)
          << "seed " << seed << " chunks " << min_start_chunks;
    }
  }
}

// A warm second frame over identical requests must not allocate: grouping
// scratch, result storage, and cache lookups all reuse runtime-owned
// storage.
TEST(TessPathRuntime, RepeatedGoalGroupingWarmFrameIsAllocationFree) {
  World world;
  fill_world(world);

  tess::PathRequestRuntime runtime;
  runtime.reserve_requests(16);
  runtime.reserve_search_nodes(RuntimeTileCount);
  runtime.reserve_path_nodes(4096);
  runtime.reserve_unit_routes(16);
  runtime.reserve_unit_field_products(2);
  runtime.reserve_unit_field_product_dependencies(World::chunk_count);

  const auto submit_frame = [&runtime]() {
    for (std::int64_t i = 0; i < 4; ++i) {
      (void)runtime.submit(tess::PathRequest{tess::Coord3{i * 8, 0, 0},
                                             tess::Coord3{31, 31, 0}});
      (void)runtime.submit(
          tess::PathRequest{tess::Coord3{0, i * 8, 0}, tess::Coord3{31, 0, 0}});
      (void)runtime.submit(
          tess::PathRequest{tess::Coord3{i, 0, 0}, tess::Coord3{i, 31, 0}});
    }
  };
  const auto policy = tess::PathRuntimeCachePolicy{
      .use_unit_field_product_cache = true,
  };

  submit_frame();
  auto results = runtime.process_unit_cached<World, PassableTag>(world, policy);
  ASSERT_EQ(results.size(), 12u);
  runtime.clear_requests();
  submit_frame();
  results = runtime.process_unit_cached<World, PassableTag>(world, policy);
  ASSERT_EQ(results.size(), 12u);

  runtime.clear_requests();
  submit_frame();
  {
    tess_test::ScopedAllocationCounter counter;
    results = runtime.process_unit_cached<World, PassableTag>(world, policy);
    ASSERT_EQ(results.size(), 12u);
    EXPECT_EQ(counter.count(), 0u);
    EXPECT_EQ(counter.bytes(), 0u);
  }
  for (const auto result : results) {
    EXPECT_EQ(result.status, tess::PathStatus::Found);
  }
}

}  // namespace
