#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {

struct PassableTag {};
struct CostTag {};
using WeightedMovement =
    tess::movement::MovementClass<tess::movement::Field<PassableTag>,
                                  tess::movement::FieldCost<CostTag>>;

using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                 tess::Field<CostTag, std::uint32_t>>;
using Mid2D = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{8, 8, 1}>;
using MidWorld = tess::AlwaysResidentWorld<Mid2D, Schema>;

template <typename World>
void fill_world(World& world, bool passable, std::uint32_t cost) {
  for (auto& page : world.chunks()) {
    auto passables = page.template field_span<PassableTag>();
    for (auto& tile : passables) {
      tile = passable;
    }
    auto costs = page.template field_span<CostTag>();
    for (auto& tile : costs) {
      tile = cost;
    }
  }
}

// Near-solid vertical barriers with sparse cost-4 gaps, scaled down from
// the goal-churn bench map: portal routes exist everywhere but carry a
// cost premium over the optimum, giving the premium cap something real
// to accept and reject.
template <typename World>
void carve_barriers(World& world) {
  for (std::int64_t x = 8; x < 32; x += 9) {
    for (std::int64_t y = 0; y < 32; ++y) {
      if ((y + x) % 7 != 0) {
        world.template field<PassableTag>(tess::Coord3{x, y, 0}) = false;
      } else {
        world.template field<CostTag>(tess::Coord3{x, y, 0}) = 4;
      }
    }
  }
}

template <typename World>
void expect_legal_weighted(World& world, std::span<const tess::Coord3> path) {
  ASSERT_FALSE(path.empty());
  for (std::size_t i = 0; i < path.size(); ++i) {
    const auto tile = path[i];
    EXPECT_TRUE(world.template field<PassableTag>(tile))
        << "impassable tile at index " << i;
    if (i > 0) {
      const auto prev = path[i - 1];
      const auto manhattan = std::abs(tile.x - prev.x) +
                             std::abs(tile.y - prev.y) +
                             std::abs(tile.z - prev.z);
      EXPECT_EQ(manhattan, 1) << "non-unit step at index " << i;
    }
  }
}

// PathResult::path borrows the scratch, so the reference copies it out
// before the scratch dies.
struct ExactReference {
  tess::PathStatus status = tess::PathStatus::NotComputed;
  std::uint32_t cost = 0;
  std::uint32_t cost_scale = 1;
  std::size_t expanded_nodes = 0;
  std::size_t reached_nodes = 0;
  std::vector<tess::Coord3> path;
};

auto exact_reference(MidWorld& world, tess::PathRequest request)
    -> ExactReference {
  tess::PathScratch scratch;
  scratch.reserve_nodes(std::size_t{32} * 32);
  const auto result = tess::weighted_astar_path<MidWorld, WeightedMovement>(
      world, request, scratch);
  return ExactReference{
      result.status,        result.cost,
      result.cost_scale,    result.expanded_nodes,
      result.reached_nodes, {result.path.begin(), result.path.end()},
  };
}

// The composed cached builder must agree with the uncached chunk builder
// on status, cost, and route when the cache is cold, and serve segments
// from the cache on a rebuild (no fresh expansions).
TEST(TessPortalFirstReplan, CachedBuilderMatchesUncachedAndReusesSegments) {
  MidWorld world;
  fill_world(world, true, 1);
  carve_barriers(world);

  const auto request =
      tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{30, 28, 0}};
  tess::PathScratch scratch;
  scratch.reserve_nodes(std::size_t{32} * 32);

  tess::WeightedPortalRouteProduct uncached;
  const auto baseline =
      tess::build_weighted_chunk_portal_route_product<MidWorld,
                                                      WeightedMovement>(
          world, request, scratch, uncached);
  ASSERT_EQ(baseline.status, tess::PathStatus::Found);

  tess::WeightedPortalSegmentCache cache;
  tess::WeightedPortalRouteProduct product;
  const auto cold =
      tess::build_weighted_chunk_portal_route_product_cached<MidWorld,
                                                             WeightedMovement>(
          world, request, scratch, cache, product);
  ASSERT_EQ(cold.status, tess::PathStatus::Found);
  EXPECT_EQ(cold.cost, baseline.cost);
  ASSERT_EQ(cold.path.size(), baseline.path.size());
  for (std::size_t i = 0; i < cold.path.size(); ++i) {
    EXPECT_EQ(cold.path[i], baseline.path[i]);
  }

  tess::WeightedPortalRouteProduct warm_product;
  const auto warm =
      tess::build_weighted_chunk_portal_route_product_cached<MidWorld,
                                                             WeightedMovement>(
          world, request, scratch, cache, warm_product);
  ASSERT_EQ(warm.status, tess::PathStatus::Found);
  EXPECT_EQ(warm.cost, cold.cost);
  EXPECT_EQ(warm.expanded_nodes, 0u);  // Every segment served from cache.
}

// PortalFirst on an eligible singleton: Found, legal, within the pinned
// cap, and counted as an accepted portal attempt.
TEST(TessPortalFirstReplan, AcceptedSingletonIsLegalWithinCap) {
  MidWorld world;
  fill_world(world, true, 1);
  carve_barriers(world);

  tess::PathRequestRuntime runtime;
  const auto request =
      tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{30, 28, 0}};
  (void)runtime.submit(request);

  auto policy = tess::PathRuntimeCachePolicy{};
  policy.weighted_replan_strategy = tess::WeightedReplanStrategy::PortalFirst;
  policy.portal_premium_limit_num = 4;
  policy.portal_premium_limit_den = 3;
  const auto results =
      runtime.process_weighted_batch<MidWorld, WeightedMovement, 16>(world,
                                                                     policy);
  ASSERT_EQ(results.size(), 1u);
  ASSERT_EQ(results[0].status, tess::PathStatus::Found);
  expect_legal_weighted(world, results[0].path.span());

  const auto lower_bound =
      static_cast<std::uint64_t>(std::abs(request.goal.x - request.start.x) +
                                 std::abs(request.goal.y - request.start.y));
  EXPECT_LE(static_cast<std::uint64_t>(results[0].cost) * 3u, lower_bound * 4u);

  const auto stats = runtime.stats().portal_replan;
  EXPECT_EQ(stats.attempts, 1u);
  EXPECT_EQ(stats.accepted, 1u);
  EXPECT_EQ(stats.exact_fallbacks, 0u);
}

// A cap of 1/1 can only accept routes that meet the Manhattan lower
// bound; the barrier map's portal routes cannot, so every attempt is
// premium-rejected and the served result must byte-match exact A*.
TEST(TessPortalFirstReplan, PremiumRejectionFallsBackToExactParity) {
  MidWorld world;
  fill_world(world, true, 1);
  carve_barriers(world);

  const auto request =
      tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{30, 28, 0}};
  const auto reference = exact_reference(world, request);
  ASSERT_EQ(reference.status, tess::PathStatus::Found);

  tess::PathRequestRuntime runtime;
  (void)runtime.submit(request);
  auto policy = tess::PathRuntimeCachePolicy{};
  policy.weighted_replan_strategy = tess::WeightedReplanStrategy::PortalFirst;
  policy.portal_premium_limit_num = 1;
  policy.portal_premium_limit_den = 1;
  const auto results =
      runtime.process_weighted_batch<MidWorld, WeightedMovement, 16>(world,
                                                                     policy);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].status, reference.status);
  EXPECT_EQ(results[0].cost, reference.cost);
  EXPECT_EQ(results[0].cost_scale, reference.cost_scale);
  EXPECT_EQ(results[0].expanded_nodes, reference.expanded_nodes);
  EXPECT_EQ(results[0].reached_nodes, reference.reached_nodes);
  ASSERT_EQ(results[0].path.size(), reference.path.size());
  for (std::size_t i = 0; i < results[0].path.size(); ++i) {
    EXPECT_EQ(results[0].path[i], reference.path[i]);
  }

  const auto stats = runtime.stats().portal_replan;
  EXPECT_EQ(stats.attempts, 1u);
  EXPECT_EQ(stats.premium_rejections, 1u);
  EXPECT_EQ(stats.accepted, 0u);
  EXPECT_EQ(stats.exact_fallbacks, 1u);
}

// Two singletons with DISTINCT goals in one batch: both portal-accepted,
// both served routes legal and ending at their own goals — the borrowed-
// product aliasing hazard the design review flagged.
TEST(TessPortalFirstReplan, TwoDistinctGoalSingletonsDoNotAlias) {
  MidWorld world;
  fill_world(world, true, 1);
  carve_barriers(world);

  const auto request_a =
      tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{30, 28, 0}};
  const auto request_b =
      tess::PathRequest{tess::Coord3{2, 3, 0}, tess::Coord3{28, 30, 0}};

  tess::PathRequestRuntime runtime;
  (void)runtime.submit(request_a);
  (void)runtime.submit(request_b);
  auto policy = tess::PathRuntimeCachePolicy{};
  policy.weighted_replan_strategy = tess::WeightedReplanStrategy::PortalFirst;
  policy.portal_premium_limit_num = 4;
  policy.portal_premium_limit_den = 3;
  const auto results =
      runtime.process_weighted_batch<MidWorld, WeightedMovement, 16>(world,
                                                                     policy);
  ASSERT_EQ(results.size(), 2u);
  for (std::size_t i = 0; i < 2; ++i) {
    ASSERT_EQ(results[i].status, tess::PathStatus::Found) << i;
    expect_legal_weighted(world, results[i].path.span());
  }
  EXPECT_EQ(results[0].path.span().front(), request_a.start);
  EXPECT_EQ(results[0].path.span().back(), request_a.goal);
  EXPECT_EQ(results[1].path.span().front(), request_b.start);
  EXPECT_EQ(results[1].path.span().back(), request_b.goal);

  const auto stats = runtime.stats().portal_replan;
  EXPECT_EQ(stats.attempts, 2u);
  EXPECT_EQ(stats.accepted, 2u);
}

// Multi-goal groups bypass the portal pass entirely; a sealed goal takes
// the no-candidate fallback with exact parity; and the stats identity
// attempts == accepted + no_candidates + verification_failures +
// premium_rejections holds across a mixed batch.
TEST(TessPortalFirstReplan, MixedBatchStatsConserveAndFallbacksMatchExact) {
  MidWorld world;
  fill_world(world, true, 1);
  carve_barriers(world);
  // Seal the top-right chunk's seams so candidates fail toward (30, 2).
  for (std::int64_t x = 24; x < 32; ++x) {
    world.template field<PassableTag>(tess::Coord3{x, 8, 0}) = false;
    world.template field<PassableTag>(tess::Coord3{x, 7, 0}) = false;
  }
  for (std::int64_t y = 0; y < 8; ++y) {
    world.template field<PassableTag>(tess::Coord3{23, y, 0}) = false;
    world.template field<PassableTag>(tess::Coord3{24, y, 0}) = false;
  }

  const auto sealed_goal = tess::Coord3{30, 2, 0};
  const auto sealed_request =
      tess::PathRequest{tess::Coord3{1, 1, 0}, sealed_goal};
  const auto sealed_reference = exact_reference(world, sealed_request);

  tess::PathRequestRuntime runtime;
  (void)runtime.submit(sealed_request);
  // A shared-goal pair: goal_count == 2, never a singleton.
  (void)runtime.submit(
      tess::PathRequest{tess::Coord3{1, 5, 0}, tess::Coord3{5, 30, 0}});
  (void)runtime.submit(
      tess::PathRequest{tess::Coord3{3, 6, 0}, tess::Coord3{5, 30, 0}});
  // An ordinary accepted singleton.
  (void)runtime.submit(
      tess::PathRequest{tess::Coord3{1, 9, 0}, tess::Coord3{30, 28, 0}});

  auto policy = tess::PathRuntimeCachePolicy{};
  policy.weighted_replan_strategy = tess::WeightedReplanStrategy::PortalFirst;
  policy.portal_premium_limit_num = 4;
  policy.portal_premium_limit_den = 3;
  const auto results =
      runtime.process_weighted_batch<MidWorld, WeightedMovement, 16>(world,
                                                                     policy);
  ASSERT_EQ(results.size(), 4u);

  EXPECT_EQ(results[0].status, sealed_reference.status);
  EXPECT_EQ(results[0].cost, sealed_reference.cost);

  const auto stats = runtime.stats().portal_replan;
  EXPECT_EQ(stats.attempts, 2u);  // The two singletons only.
  EXPECT_EQ(stats.attempts, stats.accepted + stats.no_candidates +
                                stats.verification_failures +
                                stats.premium_rejections);
  EXPECT_GE(stats.exact_fallbacks, 1u);
}

// Default policy is byte-for-byte unchanged: ExactAStar must be the
// default strategy and produce identical results to an explicit exact
// run, with zero portal stats.
TEST(TessPortalFirstReplan, DefaultStrategyIsExactWithZeroPortalStats) {
  MidWorld world;
  fill_world(world, true, 1);
  carve_barriers(world);

  const auto request =
      tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{30, 28, 0}};
  const auto reference = exact_reference(world, request);

  tess::PathRequestRuntime runtime;
  (void)runtime.submit(request);
  const auto results =
      runtime.process_weighted_batch<MidWorld, WeightedMovement, 16>(
          world, tess::PathRuntimeCachePolicy{});
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].status, reference.status);
  EXPECT_EQ(results[0].cost, reference.cost);
  EXPECT_EQ(results[0].cost_scale, reference.cost_scale);
  EXPECT_EQ(results[0].expanded_nodes, reference.expanded_nodes);
  EXPECT_EQ(results[0].reached_nodes, reference.reached_nodes);
  ASSERT_EQ(results[0].path.size(), reference.path.size());
  for (std::size_t i = 0; i < results[0].path.size(); ++i) {
    EXPECT_EQ(results[0].path[i], reference.path[i]);
  }

  const auto stats = runtime.stats().portal_replan;
  EXPECT_EQ(stats.attempts, 0u);
  EXPECT_EQ(stats.accepted, 0u);
  EXPECT_EQ(stats.ineligible_fallbacks, 0u);
}

// A stitched total at or above the uint32 sentinel must never be served
// as Found: the builder reports CostOverflow, the pass counts a
// verification failure, and the exact fallback's own CostOverflow is
// what the caller sees.
TEST(TessPortalFirstReplan, AggregateCostOverflowFallsBackToExact) {
  MidWorld world;
  fill_world(world, true, 150000000u);

  const auto request =
      tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{30, 28, 0}};
  tess::PathRequestRuntime runtime;
  (void)runtime.submit(request);
  auto policy = tess::PathRuntimeCachePolicy{};
  policy.weighted_replan_strategy = tess::WeightedReplanStrategy::PortalFirst;
  policy.portal_premium_limit_num = 0;  // No cap: only the sentinel guards.
  const auto results =
      runtime.process_weighted_batch<MidWorld, WeightedMovement, 16>(world,
                                                                     policy);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].status, tess::PathStatus::CostOverflow);

  const auto stats = runtime.stats().portal_replan;
  EXPECT_EQ(stats.attempts, 1u);
  EXPECT_EQ(stats.accepted, 0u);
  EXPECT_EQ(stats.verification_failures, 1u);
}

// A zero denominator is a misconfiguration, not a no-cap sentinel: it
// normalizes to 1, so the cap still rejects the barrier map's premium
// routes instead of silently accepting everything.
TEST(TessPortalFirstReplan, ZeroDenominatorDoesNotDisableTheCap) {
  MidWorld world;
  fill_world(world, true, 1);
  carve_barriers(world);

  const auto request =
      tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{30, 28, 0}};
  tess::PathRequestRuntime runtime;
  (void)runtime.submit(request);
  auto policy = tess::PathRuntimeCachePolicy{};
  policy.weighted_replan_strategy = tess::WeightedReplanStrategy::PortalFirst;
  policy.portal_premium_limit_num = 1;
  policy.portal_premium_limit_den = 0;
  const auto results =
      runtime.process_weighted_batch<MidWorld, WeightedMovement, 16>(world,
                                                                     policy);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].status, tess::PathStatus::Found);

  const auto stats = runtime.stats().portal_replan;
  EXPECT_EQ(stats.premium_rejections, 1u);
  EXPECT_EQ(stats.accepted, 0u);
}

// Out-of-shape endpoints are the batch's to classify: the portal pass
// skips them without counting an attempt.
TEST(TessPortalFirstReplan, InvalidEndpointsCountNoAttempt) {
  MidWorld world;
  fill_world(world, true, 1);

  tess::PathRequestRuntime runtime;
  (void)runtime.submit(
      tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{99, 99, 0}});
  auto policy = tess::PathRuntimeCachePolicy{};
  policy.weighted_replan_strategy = tess::WeightedReplanStrategy::PortalFirst;
  const auto results =
      runtime.process_weighted_batch<MidWorld, WeightedMovement, 16>(world,
                                                                     policy);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].status, tess::PathStatus::InvalidGoal);

  const auto stats = runtime.stats().portal_replan;
  EXPECT_EQ(stats.attempts, 0u);
  EXPECT_EQ(stats.ineligible_fallbacks, 0u);
}

// A diagonal-step movement class is compile-time ineligible: PortalFirst
// requests take the exact path and the ineligible counter — not silence —
// records the misconfiguration.
TEST(TessPortalFirstReplan, IneligibleClassBypassesWithVisibleStats) {
  MidWorld world;
  fill_world(world, true, 1);

  using DiagonalMovement = tess::movement::MovementClass<
      tess::movement::AllOf<tess::movement::Field<PassableTag>,
                            tess::movement::NotZero<CostTag>>,
      tess::movement::FieldCost<CostTag>, tess::movement::DiagonalSteps<>>;
  tess::PathRequestRuntime runtime;
  (void)runtime.submit(
      tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{30, 28, 0}});
  auto policy = tess::PathRuntimeCachePolicy{};
  policy.weighted_replan_strategy = tess::WeightedReplanStrategy::PortalFirst;
  const auto results =
      runtime.process_weighted_batch<MidWorld, DiagonalMovement, 16>(world,
                                                                     policy);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].status, tess::PathStatus::Found);

  const auto stats = runtime.stats().portal_replan;
  EXPECT_EQ(stats.attempts, 0u);
  EXPECT_EQ(stats.ineligible_fallbacks, 1u);
}

// Identical batch sequences produce identical results and portal stats:
// the strategy's decisions are deterministic.
TEST(TessPortalFirstReplan, IdenticalBatchesAreDeterministic) {
  const auto run = [] {
    MidWorld world;
    fill_world(world, true, 1);
    carve_barriers(world);
    tess::PathRequestRuntime runtime;
    std::vector<std::uint64_t> trace;
    for (int round = 0; round < 3; ++round) {
      (void)runtime.submit(
          tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{30, 28, 0}});
      (void)runtime.submit(tess::PathRequest{
          tess::Coord3{2, 3, 0},
          tess::Coord3{28, static_cast<std::int64_t>(20 + round), 0}});
      auto policy = tess::PathRuntimeCachePolicy{};
      policy.weighted_replan_strategy =
          tess::WeightedReplanStrategy::PortalFirst;
      policy.portal_premium_limit_num = 4;
      policy.portal_premium_limit_den = 3;
      const auto results =
          runtime.process_weighted_batch<MidWorld, WeightedMovement, 16>(
              world, policy);
      for (const auto& result : results) {
        trace.push_back(static_cast<std::uint64_t>(result.status));
        trace.push_back(result.cost);
      }
      const auto stats = runtime.stats().portal_replan;
      trace.push_back(stats.attempts);
      trace.push_back(stats.accepted);
      trace.push_back(stats.premium_rejections);
    }
    return trace;
  };
  EXPECT_EQ(run(), run());
}

}  // namespace
