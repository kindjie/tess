#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstddef>
#include <cstdint>

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

TEST(TessPathRuntime, WeightedBatchHandlesManyAgentsAndCacheClearCadence) {
  World world;
  fill_world(world);

  tess::PathRequestRuntime runtime;
  runtime.reserve_requests(32);
  runtime.reserve_search_nodes(RuntimeTileCount);
  runtime.reserve_path_nodes(2048);
  runtime.reserve_unit_routes(8);
  runtime.reserve_portal_segments(8);
  runtime.portal_segment_cache().reserve_path_nodes(64);

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
  EXPECT_EQ(stats.portal_segment_cache_entries, 0u);
}

}  // namespace
