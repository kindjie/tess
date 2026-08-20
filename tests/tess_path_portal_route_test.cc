#include <gtest/gtest.h>
#include <tess/tess.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>

namespace {

struct PassableTag {};
struct CostTag {};

using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                 tess::Field<CostTag, std::uint32_t>>;
using Mid2D = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{8, 8, 1}>;
using World = tess::AlwaysResidentWorld<Mid2D, Schema>;
using WeightedMovement =
    tess::movement::MovementClass<tess::movement::Field<PassableTag>,
                                  tess::movement::FieldCost<CostTag>>;

void fill_world(World& world) {
  for (auto& page : world.chunks()) {
    auto passables = page.template field_span<PassableTag>();
    for (auto& tile : passables) {
      tile = true;
    }
    auto costs = page.template field_span<CostTag>();
    for (auto& tile : costs) {
      tile = 1;
    }
  }
}

auto build(const World& world, tess::PathRequest request, tess::PathScratch& s,
           tess::WeightedPortalRouteProduct& product) -> tess::PathResult {
  return tess::build_weighted_chunk_portal_route_product<World,
                                                         WeightedMovement>(
      world, request, s, product);
}

auto build_cached(const World& world, tess::PathRequest request,
                  tess::PathScratch& scratch,
                  tess::WeightedPortalSegmentCache& cache,
                  tess::WeightedPortalRouteProduct& product)
    -> tess::PathResult {
  return tess::build_weighted_chunk_portal_route_product_cached<
      World, WeightedMovement>(world, request, scratch, cache, product);
}

TEST(TessPathPortalRoute, InvalidEndpointsReportBeforeCandidateSearch) {
  World world;
  fill_world(world);

  tess::PathScratch scratch;
  tess::WeightedPortalRouteProduct product;

  const auto bad_start = build(
      world, tess::PathRequest{tess::Coord3{-1, 0, 0}, tess::Coord3{31, 31, 0}},
      scratch, product);
  EXPECT_EQ(bad_start.status, tess::PathStatus::InvalidStart);
  EXPECT_TRUE(bad_start.path.empty());
  EXPECT_EQ(product.route_candidates(), 0u);
  EXPECT_EQ(product.portal_scan_tiles(), 0u);

  const auto bad_goal = build(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{32, 0, 0}},
      scratch, product);
  EXPECT_EQ(bad_goal.status, tess::PathStatus::InvalidGoal);
  EXPECT_TRUE(bad_goal.path.empty());
  EXPECT_EQ(product.route_candidates(), 0u);
}

TEST(TessPathPortalRoute,
     ImpassableAndZeroCostEndpointsReportBeforeCandidateSearch) {
  World world;
  fill_world(world);
  constexpr auto start = tess::Coord3{0, 0, 0};
  constexpr auto goal = tess::Coord3{31, 31, 0};
  const auto check_both = [&](tess::PathStatus expected) {
    tess::PathScratch scratch;
    tess::WeightedPortalRouteProduct product;
    const auto uncached = build(world, {start, goal}, scratch, product);
    EXPECT_EQ(uncached.status, expected);
    EXPECT_TRUE(uncached.path.empty());
    EXPECT_EQ(product.route_candidates(), 0u);

    tess::WeightedPortalSegmentCache cache;
    const auto cached =
        build_cached(world, {start, goal}, scratch, cache, product);
    EXPECT_EQ(cached.status, expected);
    EXPECT_TRUE(cached.path.empty());
    EXPECT_EQ(product.route_candidates(), 0u);
  };

  world.field<PassableTag>(start) = false;
  check_both(tess::PathStatus::InvalidStart);
  world.field<PassableTag>(start) = true;

  world.field<CostTag>(start) = 0;
  check_both(tess::PathStatus::InvalidStart);
  world.field<CostTag>(start) = 1;

  world.field<PassableTag>(goal) = false;
  check_both(tess::PathStatus::InvalidGoal);
  world.field<PassableTag>(goal) = true;

  world.field<CostTag>(goal) = 0;
  check_both(tess::PathStatus::InvalidGoal);
}

TEST(TessPathPortalRoute, EveryStitchedBuilderRejectsAggregateCostOverflow) {
  World world;
  fill_world(world);
  constexpr auto start = tess::Coord3{0, 0, 0};
  constexpr auto waypoint = tess::Coord3{8, 0, 0};
  constexpr auto goal = tess::Coord3{16, 0, 0};
  constexpr auto high_cost =
      std::numeric_limits<std::uint32_t>::max() / 2u + 1u;
  world.field<CostTag>(waypoint) = high_cost;
  world.field<CostTag>(goal) = high_cost;

  const auto expect_overflow = [](const tess::PathResult& result) {
    EXPECT_EQ(result.status, tess::PathStatus::CostOverflow);
    EXPECT_EQ(result.cost, 0u);
    EXPECT_TRUE(result.path.empty());
  };

  tess::PathScratch scratch;
  tess::WeightedPortalRouteProduct product;
  const auto request = tess::PathRequest{start, goal};
  const auto waypoints = std::array{waypoint};

  expect_overflow(
      tess::build_weighted_portal_route_product<World, WeightedMovement>(
          world, request, waypoints, scratch, product));

  tess::WeightedPortalSegmentCache supplied_cache;
  expect_overflow(
      tess::build_weighted_portal_route_product<World, WeightedMovement>(
          world, request, waypoints, scratch, supplied_cache, product));

  expect_overflow(build(world, request, scratch, product));

  tess::WeightedPortalSegmentCache automatic_cache;
  expect_overflow(
      build_cached(world, request, scratch, automatic_cache, product));
}

TEST(TessPathPortalRoute, SealedStartChunkReportsNoCandidate) {
  World world;
  fill_world(world);
  // Seal the start chunk's outgoing seams on the neighbor side: the X
  // crossing target column x=8 and the Y crossing target row y=8.
  for (std::int64_t y = 0; y < 8; ++y) {
    world.template field<PassableTag>(tess::Coord3{8, y, 0}) = false;
  }
  for (std::int64_t x = 0; x < 8; ++x) {
    world.template field<PassableTag>(tess::Coord3{x, 8, 0}) = false;
  }

  tess::PathScratch scratch;
  tess::WeightedPortalRouteProduct product;
  const auto result = build(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{31, 31, 0}},
      scratch, product);
  EXPECT_EQ(result.status, tess::PathStatus::NoCandidate);
  EXPECT_TRUE(result.path.empty());
  EXPECT_TRUE(product.waypoints().empty());
  // Six axis orders plus the greedy candidate all ran and failed.
  EXPECT_EQ(product.route_candidates(), 7u);
  EXPECT_GT(product.portal_scan_tiles(), 0u);
}

TEST(TessPathPortalRoute, SegmentFailureClearsAssembledPath) {
  World world;
  fill_world(world);
  // The candidate seam scan only inspects seam tile pairs, so sealing the
  // goal inside a box still yields portal candidates; the second weighted
  // A* segment then fails after the first segment already appended path
  // nodes, and the assembled path must be cleared.
  for (const auto coord : {tess::Coord3{11, 4, 0}, tess::Coord3{13, 4, 0},
                           tess::Coord3{12, 3, 0}, tess::Coord3{12, 5, 0}}) {
    world.template field<PassableTag>(coord) = false;
  }

  tess::PathScratch scratch;
  tess::WeightedPortalRouteProduct product;
  const auto result = build(
      world, tess::PathRequest{tess::Coord3{0, 4, 0}, tess::Coord3{12, 4, 0}},
      scratch, product);
  EXPECT_EQ(result.status, tess::PathStatus::NoCandidate);
  EXPECT_TRUE(result.path.empty());
  EXPECT_GT(result.expanded_nodes, 0u);
  // The candidate search itself succeeded; failure came from segment
  // stitching.
  EXPECT_EQ(product.waypoints().size(), 1u);
  EXPECT_EQ(product.waypoints().front(), (tess::Coord3{8, 4, 0}));
}

TEST(TessPathPortalRoute,
     SelectedCandidateCanFailWhileExactSearchFindsAnotherRoute) {
  World world;
  fill_world(world);
  // The straight seam pair remains passable but is isolated from both chunk
  // interiors. The heuristic still selects it, while an exact route can detour
  // to another crossing.
  for (const auto coord :
       {tess::Coord3{6, 4, 0}, tess::Coord3{7, 3, 0}, tess::Coord3{7, 5, 0},
        tess::Coord3{9, 4, 0}, tess::Coord3{8, 3, 0}, tess::Coord3{8, 5, 0}}) {
    world.template field<PassableTag>(coord) = false;
  }

  tess::PathScratch scratch;
  tess::WeightedPortalRouteProduct product;
  const auto request =
      tess::PathRequest{tess::Coord3{0, 4, 0}, tess::Coord3{12, 4, 0}};
  const auto heuristic = build(world, request, scratch, product);
  EXPECT_EQ(heuristic.status, tess::PathStatus::NoCandidate);
  EXPECT_TRUE(heuristic.path.empty());

  const auto exact = tess::weighted_astar_path<World, WeightedMovement>(
      world, request, scratch);
  EXPECT_EQ(exact.status, tess::PathStatus::Found);
  EXPECT_EQ(exact.path.front(), request.start);
  EXPECT_EQ(exact.path.back(), request.goal);
}

// Every monotone axis-order candidate is blocked, but the greedy candidate
// interleaves X and Y chunk steps around the blocked seams and must win.
TEST(TessPathPortalRoute, GreedyCandidateWinsWhereAxisOrdersFail) {
  World world;
  fill_world(world);
  // Kill X-then-Y orders: block the (1,0) -> (2,0) crossing target column.
  for (std::int64_t y = 0; y < 8; ++y) {
    world.template field<PassableTag>(tess::Coord3{16, y, 0}) = false;
  }
  // Kill Y-then-X orders: block the (0,1) -> (0,2) crossing target row.
  for (std::int64_t x = 0; x < 8; ++x) {
    world.template field<PassableTag>(tess::Coord3{x, 16, 0}) = false;
  }

  tess::PathScratch scratch;
  tess::WeightedPortalRouteProduct product;
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{31, 31, 0}};
  const auto result = build(world, request, scratch, product);
  ASSERT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(product.route_candidates(), 7u);
  ASSERT_FALSE(result.path.empty());
  EXPECT_EQ(result.path.front(), request.start);
  EXPECT_EQ(result.path.back(), request.goal);
  for (std::size_t i = 1; i < result.path.size(); ++i) {
    EXPECT_EQ(tess::manhattan_distance(result.path[i - 1], result.path[i]), 1u);
  }

  const auto replay = tess::weighted_portal_route_product_path(world, product);
  EXPECT_EQ(replay.status, tess::PathStatus::Found);
  EXPECT_EQ(replay.cost, result.cost);
}

TEST(TessPathPortalRoute, MultiSeamLShapedRouteCrossesChunkBoundaries) {
  World world;
  fill_world(world);

  tess::PathScratch scratch;
  tess::WeightedPortalRouteProduct product;
  const auto request =
      tess::PathRequest{tess::Coord3{2, 2, 0}, tess::Coord3{18, 10, 0}};
  const auto result = build(world, request, scratch, product);
  ASSERT_EQ(result.status, tess::PathStatus::Found);
  // Chunk (0,0) to chunk (2,1): two X crossings plus one Y crossing.
  EXPECT_EQ(product.waypoints().size(), 3u);
  ASSERT_FALSE(result.path.empty());
  EXPECT_EQ(result.path.front(), request.start);
  EXPECT_EQ(result.path.back(), request.goal);
  EXPECT_EQ(result.cost, result.path.size() - 1u);
  for (std::size_t i = 1; i < result.path.size(); ++i) {
    ASSERT_EQ(tess::manhattan_distance(result.path[i - 1], result.path[i]), 1u);
  }

  std::set<std::uint64_t> chunks;
  for (const auto coord : result.path) {
    chunks.insert(tess::chunk_key<Mid2D>(tess::tile_key<Mid2D>(coord)).value);
  }
  EXPECT_GE(chunks.size(), 4u);
}

}  // namespace
