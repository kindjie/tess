#include <gtest/gtest.h>
#include <tess/tess.h>

#include <array>
#include <cstdint>

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

template <typename World>
void fill_cost(World& world, std::uint32_t value) {
  for (auto& page : world.chunks()) {
    auto costs = page.template field_span<CostTag>();
    for (auto& tile : costs) {
      tile = value;
    }
  }
}

// A product that has never captured dependencies must not replay as
// vacuously valid: an empty dependency set means "never validated", not
// "depends on nothing".
TEST(TessPathProduct, ProductsWithoutDependenciesAreInvalid) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);

  const tess::WeightedRouteProduct route;
  EXPECT_FALSE(route.is_valid(world));
  const tess::WeightedPortalRouteProduct portal_route;
  EXPECT_FALSE(portal_route.is_valid(world));
  const tess::DistanceFieldProduct field;
  EXPECT_FALSE(field.is_valid(world));
}

// A NoPath product must not replay after a world edit that could open the
// route: failure results capture every chunk version, so any edit
// invalidates them.
TEST(TessPathProduct, WeightedRouteProductNoPathInvalidatesAfterOpeningEdit) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);
  for (std::int64_t y = 0; y < 8; ++y) {
    world.template field<PassableTag>(tess::Coord3{4, y, 0}) = false;
  }

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::WeightedRouteProduct product;
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 0, 0}};

  const auto built =
      tess::build_weighted_route_product<decltype(world), PassableTag, CostTag>(
          world, request, scratch, product);
  ASSERT_EQ(built.status, tess::PathStatus::NoPath);
  EXPECT_EQ(product.dependencies().size(), decltype(world)::chunk_count);
  EXPECT_TRUE(product.is_valid(world));

  world.template field<PassableTag>(tess::Coord3{4, 0, 0}) = true;
  world.mark_dirty(tess::ChunkKey{1}, 1u,
                   tess::Box3{tess::Coord3{4, 0, 0}, tess::Extent3{1, 1, 1}});
  EXPECT_FALSE(product.is_valid(world));

  const auto rebuilt =
      tess::build_weighted_route_product<decltype(world), PassableTag, CostTag>(
          world, request, scratch, product);
  EXPECT_EQ(rebuilt.status, tess::PathStatus::Found);
}

TEST(TessPathProduct,
     WeightedPortalRouteProductFailureInvalidatesAfterOpeningEdit) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);
  for (std::int64_t y = 0; y < 8; ++y) {
    world.template field<PassableTag>(tess::Coord3{4, y, 0}) = false;
  }

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::WeightedPortalRouteProduct product;
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 0, 0}};
  const auto waypoints = std::array{tess::Coord3{2, 2, 0}};

  const auto built =
      tess::build_weighted_portal_route_product<decltype(world), PassableTag,
                                                CostTag>(
          world, request, waypoints, scratch, product);
  ASSERT_EQ(built.status, tess::PathStatus::NoPath);
  EXPECT_EQ(product.dependencies().size(), decltype(world)::chunk_count);

  world.template field<PassableTag>(tess::Coord3{4, 0, 0}) = true;
  world.mark_dirty(tess::ChunkKey{1}, 1u,
                   tess::Box3{tess::Coord3{4, 0, 0}, tess::Extent3{1, 1, 1}});
  EXPECT_FALSE(product.is_valid(world));

  const auto rebuilt =
      tess::build_weighted_portal_route_product<decltype(world), PassableTag,
                                                CostTag>(
          world, request, waypoints, scratch, product);
  EXPECT_EQ(rebuilt.status, tess::PathStatus::Found);
}

// Rebuilding a product from its own waypoints() span must not read the
// cleared vector it points into.
TEST(TessPathProduct, WeightedPortalRouteProductRebuildsFromOwnWaypoints) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::WeightedPortalRouteProduct product;
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}};
  const auto waypoints = std::array{tess::Coord3{4, 3, 0}};

  const auto built =
      tess::build_weighted_portal_route_product<decltype(world), PassableTag,
                                                CostTag>(
          world, request, waypoints, scratch, product);
  ASSERT_EQ(built.status, tess::PathStatus::Found);
  const auto first_cost = built.cost;
  const auto first_size = built.path.size();

  const auto rebuilt =
      tess::build_weighted_portal_route_product<decltype(world), PassableTag,
                                                CostTag>(
          world, request, product.waypoints(), scratch, product);
  ASSERT_EQ(rebuilt.status, tess::PathStatus::Found);
  EXPECT_EQ(rebuilt.cost, first_cost);
  EXPECT_EQ(rebuilt.path.size(), first_size);
  ASSERT_EQ(product.waypoints().size(), 1u);
  EXPECT_EQ(product.waypoints().front(), (tess::Coord3{4, 3, 0}));
}

// The segment-cache overload must uphold the same failure-dependency
// contract as the direct builder.
TEST(TessPathProduct, SegmentCacheOverloadFailureInvalidatesAfterOpeningEdit) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);
  for (std::int64_t y = 0; y < 8; ++y) {
    world.template field<PassableTag>(tess::Coord3{4, y, 0}) = false;
  }

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::WeightedPortalSegmentCache cache;
  tess::WeightedPortalRouteProduct product;
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 0, 0}};
  const auto waypoints = std::array{tess::Coord3{2, 2, 0}};

  const auto built =
      tess::build_weighted_portal_route_product<decltype(world), PassableTag,
                                                CostTag>(
          world, request, waypoints, scratch, cache, product);
  ASSERT_EQ(built.status, tess::PathStatus::NoPath);
  EXPECT_EQ(product.dependencies().size(), decltype(world)::chunk_count);

  world.template field<PassableTag>(tess::Coord3{4, 0, 0}) = true;
  world.mark_dirty(tess::ChunkKey{1}, 1u,
                   tess::Box3{tess::Coord3{4, 0, 0}, tess::Extent3{1, 1, 1}});
  EXPECT_FALSE(product.is_valid(world));

  const auto rebuilt =
      tess::build_weighted_portal_route_product<decltype(world), PassableTag,
                                                CostTag>(
          world, request, waypoints, scratch, cache, product);
  EXPECT_EQ(rebuilt.status, tess::PathStatus::Found);
}

// The segment-cache overload must also survive rebuild-from-own-waypoints.
TEST(TessPathProduct, SegmentCacheOverloadRebuildsFromOwnWaypoints) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::WeightedPortalSegmentCache cache;
  tess::WeightedPortalRouteProduct product;
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}};
  const auto waypoints = std::array{tess::Coord3{4, 3, 0}};

  const auto built =
      tess::build_weighted_portal_route_product<decltype(world), PassableTag,
                                                CostTag>(
          world, request, waypoints, scratch, cache, product);
  ASSERT_EQ(built.status, tess::PathStatus::Found);
  const auto first_cost = built.cost;
  const auto first_size = built.path.size();

  const auto rebuilt =
      tess::build_weighted_portal_route_product<decltype(world), PassableTag,
                                                CostTag>(
          world, request, product.waypoints(), scratch, cache, product);
  ASSERT_EQ(rebuilt.status, tess::PathStatus::Found);
  EXPECT_EQ(rebuilt.cost, first_cost);
  EXPECT_EQ(rebuilt.path.size(), first_size);
  ASSERT_EQ(product.waypoints().size(), 1u);
  EXPECT_EQ(product.waypoints().front(), (tess::Coord3{4, 3, 0}));
}

// A span into product.path_ (a previously returned PathResult.path) is also
// product-owned storage; rebuilding through it must not read cleared or
// freshly overwritten path nodes.
TEST(TessPathProduct, WeightedPortalRouteProductRebuildsFromOwnPathSpan) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::WeightedPortalRouteProduct product;
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}};
  const auto waypoints = std::array{tess::Coord3{4, 3, 0}};

  const auto built =
      tess::build_weighted_portal_route_product<decltype(world), PassableTag,
                                                CostTag>(
          world, request, waypoints, scratch, product);
  ASSERT_EQ(built.status, tess::PathStatus::Found);
  const auto replay = tess::weighted_portal_route_product_path(world, product);
  ASSERT_EQ(replay.status, tess::PathStatus::Found);
  const auto node_count = replay.path.size();

  const auto rebuilt =
      tess::build_weighted_portal_route_product<decltype(world), PassableTag,
                                                CostTag>(
          world, request, replay.path, scratch, product);
  ASSERT_EQ(rebuilt.status, tess::PathStatus::Found);
  EXPECT_EQ(product.waypoints().size(), node_count);
}

// Trivially invalid requests must not depend on every chunk: an InvalidGoal
// product depends only on the offending tile's chunk, so unrelated edits
// keep the replay valid.
TEST(TessPathProduct, InvalidGoalProductDependsOnlyOnOffendingChunk) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);
  world.template field<PassableTag>(tess::Coord3{7, 7, 0}) = false;

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::WeightedRouteProduct product;
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}};

  const auto built =
      tess::build_weighted_route_product<decltype(world), PassableTag, CostTag>(
          world, request, scratch, product);
  ASSERT_EQ(built.status, tess::PathStatus::InvalidGoal);
  EXPECT_LT(product.dependencies().size(), decltype(world)::chunk_count);

  world.mark_dirty(tess::ChunkKey{1}, 1u,
                   tess::Box3{tess::Coord3{4, 0, 0}, tess::Extent3{1, 1, 1}});
  EXPECT_TRUE(product.is_valid(world));
  EXPECT_EQ(tess::weighted_route_product_path(world, product).status,
            tess::PathStatus::InvalidGoal);

  world.template field<PassableTag>(tess::Coord3{7, 7, 0}) = true;
  world.mark_dirty(tess::ChunkKey{3}, 1u,
                   tess::Box3{tess::Coord3{7, 7, 0}, tess::Extent3{1, 1, 1}});
  EXPECT_FALSE(product.is_valid(world));

  const auto rebuilt =
      tess::build_weighted_route_product<decltype(world), PassableTag, CostTag>(
          world, request, scratch, product);
  EXPECT_EQ(rebuilt.status, tess::PathStatus::Found);
}

// A fully-blocked chunk is never touched by the flood, but its chunks must
// still invalidate the product: an edit that opens it changes reachability.
TEST(TessPathProduct, DistanceFieldProductInvalidatesAfterUnreachedChunkEdit) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);
  for (std::int64_t y = 0; y < 4; ++y) {
    for (std::int64_t x = 4; x < 8; ++x) {
      world.template field<PassableTag>(tess::Coord3{x, y, 0}) = false;
    }
  }

  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(64);
  tess::GoalSet goals;
  goals.add(tess::Coord3{0, 0, 0});
  tess::DistanceFieldProduct product;

  ASSERT_EQ((tess::build_distance_field_product<decltype(world), PassableTag>(
                 world, goals, scratch, product))
                .status,
            tess::PathStatus::Found);
  ASSERT_EQ((tess::distance_field_product_path<decltype(world), PassableTag>(
                 world, tess::Coord3{0, 7, 0}, product, scratch))
                .status,
            tess::PathStatus::Found);

  world.template field<PassableTag>(tess::Coord3{4, 0, 0}) = true;
  world.mark_dirty(tess::ChunkKey{1}, 1u,
                   tess::Box3{tess::Coord3{4, 0, 0}, tess::Extent3{1, 1, 1}});
  EXPECT_FALSE(product.is_valid(world));

  ASSERT_EQ((tess::build_distance_field_product<decltype(world), PassableTag>(
                 world, goals, scratch, product))
                .status,
            tess::PathStatus::Found);
  EXPECT_EQ((tess::distance_field_product_path<decltype(world), PassableTag>(
                 world, tess::Coord3{4, 0, 0}, product, scratch))
                .status,
            tess::PathStatus::Found);
}

}  // namespace
