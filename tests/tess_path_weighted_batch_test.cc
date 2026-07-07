#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include "allocation_counter.h"

namespace {

struct PassableTag {};
struct CostTag {};

using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                 tess::Field<CostTag, std::uint32_t>>;
using Small2D = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{4, 4, 1}>;
using Mid2D = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{8, 8, 1}>;
using SmallWorld = tess::AlwaysResidentWorld<Small2D, Schema>;
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

TEST(TessPathWeightedBatch, EmptyBatchYieldsNoResultsAndZeroStats) {
  MidWorld world;
  fill_world(world, true, 1);

  tess::WeightedPathBatchScratch scratch;
  const auto results =
      tess::weighted_path_batch<MidWorld, PassableTag, CostTag, 8>(world, {},
                                                                   scratch);
  EXPECT_TRUE(results.empty());

  const auto stats = scratch.stats();
  EXPECT_EQ(stats.requests, 0u);
  EXPECT_EQ(stats.unique_goals, 0u);
  EXPECT_EQ(stats.field_builds, 0u);
  EXPECT_EQ(stats.astar_fallbacks, 0u);
  EXPECT_EQ(stats.path_nodes, 0u);
}

TEST(TessPathWeightedBatch, AllDistinctGoalsFallBackToAstarWithoutFields) {
  MidWorld world;
  fill_world(world, true, 1);

  const auto requests = std::array{
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 0, 0}},
      tess::PathRequest{tess::Coord3{0, 1, 0}, tess::Coord3{7, 1, 0}},
      tess::PathRequest{tess::Coord3{0, 2, 0}, tess::Coord3{7, 2, 0}},
      tess::PathRequest{tess::Coord3{0, 3, 0}, tess::Coord3{7, 3, 0}},
  };
  tess::WeightedPathBatchScratch scratch;
  const auto results =
      tess::weighted_path_batch<MidWorld, PassableTag, CostTag, 8>(
          world, requests, scratch);
  ASSERT_EQ(results.size(), requests.size());
  for (std::size_t i = 0; i < results.size(); ++i) {
    EXPECT_EQ(results[i].status, tess::PathStatus::Found);
    EXPECT_EQ(results[i].cost, 7u);
    ASSERT_EQ(results[i].path.size(), 8u);
    EXPECT_EQ(results[i].path.front(), requests[i].start);
    EXPECT_EQ(results[i].path.back(), requests[i].goal);
  }

  const auto stats = scratch.stats();
  EXPECT_EQ(stats.requests, 4u);
  EXPECT_EQ(stats.unique_goals, 4u);
  EXPECT_EQ(stats.astar_fallbacks, 4u);
  EXPECT_EQ(stats.field_builds, 0u);
}

TEST(TessPathWeightedBatch, DuplicateIdenticalRequestsShareOneFieldBuild) {
  MidWorld world;
  fill_world(world, true, 1);

  const auto requests = std::array{
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{9, 4, 0}},
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{9, 4, 0}},
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{9, 4, 0}},
  };
  tess::WeightedPathBatchScratch scratch;
  const auto results =
      tess::weighted_path_batch<MidWorld, PassableTag, CostTag, 8>(
          world, requests, scratch);
  ASSERT_EQ(results.size(), 3u);
  for (const auto& result : results) {
    EXPECT_EQ(result.status, tess::PathStatus::Found);
    EXPECT_EQ(result.cost, 13u);
    ASSERT_EQ(result.path.size(), 14u);
    EXPECT_EQ(result.path.front(), (tess::Coord3{0, 0, 0}));
    EXPECT_EQ(result.path.back(), (tess::Coord3{9, 4, 0}));
  }

  const auto stats = scratch.stats();
  EXPECT_EQ(stats.unique_goals, 1u);
  EXPECT_EQ(stats.field_builds, 1u);
  EXPECT_EQ(stats.astar_fallbacks, 0u);
}

// Members of a failed shared-goal group must report the status the
// single-request weighted A* would give them, not blanket-copy the group
// goal's failure status. weighted_astar_path validates the start (contains,
// passable) before any goal check, so a member with an invalid start is
// InvalidStart even when the shared goal is also invalid.
TEST(TessPathWeightedBatch, FailedGroupFanOutReportsPerMemberStartStatus) {
  MidWorld world;
  fill_world(world, true, 1);
  const auto goal = tess::Coord3{20, 20, 0};
  world.template field<PassableTag>(goal) = false;
  const auto blocked_start = tess::Coord3{3, 3, 0};
  world.template field<PassableTag>(blocked_start) = false;

  const auto requests = std::array{
      tess::PathRequest{tess::Coord3{0, 0, 0}, goal},
      tess::PathRequest{blocked_start, goal},
      tess::PathRequest{tess::Coord3{-1, 0, 0}, goal},
  };
  tess::WeightedPathBatchScratch scratch;
  const auto results =
      tess::weighted_path_batch<MidWorld, PassableTag, CostTag, 8>(
          world, requests, scratch);
  ASSERT_EQ(results.size(), 3u);
  EXPECT_EQ(results[0].status, tess::PathStatus::InvalidGoal);
  EXPECT_EQ(results[1].status, tess::PathStatus::InvalidStart);
  EXPECT_EQ(results[2].status, tess::PathStatus::InvalidStart);
  for (const auto& result : results) {
    EXPECT_TRUE(result.path.empty());
    EXPECT_EQ(result.cost, 0u);
  }
  EXPECT_EQ(scratch.stats().field_builds, 1u);
}

// Zero-entry-cost tiles follow the same precedence: weighted_astar_path
// checks the start entry cost before the goal entry cost, so a
// cost-blocked start beats a cost-blocked goal, while a passability-blocked
// goal still beats a cost-blocked start.
TEST(TessPathWeightedBatch, FailedGroupFanOutHonorsEntryCostPrecedence) {
  MidWorld world;
  fill_world(world, true, 1);
  const auto goal = tess::Coord3{20, 20, 0};
  world.template field<CostTag>(goal) = 0;
  const auto zero_cost_start = tess::Coord3{3, 3, 0};
  world.template field<CostTag>(zero_cost_start) = 0;

  const auto requests = std::array{
      tess::PathRequest{tess::Coord3{0, 0, 0}, goal},
      tess::PathRequest{zero_cost_start, goal},
  };
  tess::WeightedPathBatchScratch scratch;
  const auto results =
      tess::weighted_path_batch<MidWorld, PassableTag, CostTag, 8>(
          world, requests, scratch);
  ASSERT_EQ(results.size(), 2u);
  EXPECT_EQ(results[0].status, tess::PathStatus::InvalidGoal);
  EXPECT_EQ(results[1].status, tess::PathStatus::InvalidStart);
}

// A mandatory corridor tile whose entry cost exceeds MaxCost forces the
// bounded ring build to hand off to the unbounded weighted build. The ring
// only spans MaxCost + 1 buckets, so it cannot even queue a +9 relaxation;
// terminating with the exact 9-cost route is evidence the fallback engaged.
TEST(TessPathWeightedBatch, OverMaxCostCorridorEngagesUnboundedFallback) {
  SmallWorld world;
  fill_world(world, true, 1);
  for (std::int64_t y = 1; y < 8; ++y) {
    for (std::int64_t x = 0; x < 8; ++x) {
      world.template field<PassableTag>(tess::Coord3{x, y, 0}) = false;
    }
  }
  world.template field<CostTag>(tess::Coord3{4, 0, 0}) = 9;

  const auto goal = tess::Coord3{7, 0, 0};
  const auto requests = std::array{
      tess::PathRequest{tess::Coord3{0, 0, 0}, goal},
      tess::PathRequest{tess::Coord3{1, 0, 0}, goal},
  };
  tess::WeightedPathBatchScratch scratch;
  const auto results =
      tess::weighted_path_batch<SmallWorld, PassableTag, CostTag, 4>(
          world, requests, scratch);
  ASSERT_EQ(results.size(), 2u);
  EXPECT_EQ(results[0].status, tess::PathStatus::Found);
  EXPECT_EQ(results[0].cost, 15u);
  EXPECT_EQ(results[1].status, tess::PathStatus::Found);
  EXPECT_EQ(results[1].cost, 14u);
  EXPECT_EQ(scratch.stats().field_builds, 1u);

  // Direct evidence: the bounded build returns the unbounded build's exact
  // result (status and node counts) on this world.
  tess::DistanceFieldScratch bounded_scratch;
  tess::DistanceFieldScratch unbounded_scratch;
  const auto bounded =
      tess::build_bounded_weighted_distance_field<SmallWorld, PassableTag,
                                                  CostTag, 4>(world, goal,
                                                              bounded_scratch);
  const auto unbounded =
      tess::build_weighted_distance_field<SmallWorld, PassableTag, CostTag>(
          world, goal, unbounded_scratch);
  EXPECT_EQ(bounded.status, tess::PathStatus::Found);
  EXPECT_EQ(bounded.status, unbounded.status);
  EXPECT_EQ(bounded.expanded_nodes, unbounded.expanded_nodes);
  EXPECT_EQ(bounded.reached_nodes, unbounded.reached_nodes);
  const auto replay =
      tess::weighted_distance_field_path<SmallWorld, PassableTag, CostTag>(
          world, tess::Coord3{0, 0, 0}, goal, bounded_scratch);
  EXPECT_EQ(replay.status, tess::PathStatus::Found);
  EXPECT_EQ(replay.cost, 15u);
}

// Seeded random-cost stress: bounded builds must match the unbounded build
// for every start. This drives the bucket ring through many wrap cycles
// (costs span [1, MaxCost]); the ring's defensive stale-pop re-queue branch
// stays unreachable because tiles costing more than MaxCost divert to the
// unbounded fallback before any over-wide relaxation can be queued.
TEST(TessPathWeightedBatch, BoundedFieldMatchesUnboundedAcrossRandomCosts) {
  for (const auto seed : {17u, 43u}) {
    MidWorld world;
    fill_world(world, true, 1);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<std::uint32_t> cost_dist(1, 8);
    std::uniform_int_distribution<int> block_dist(0, 99);
    for (std::int64_t y = 0; y < 32; ++y) {
      for (std::int64_t x = 0; x < 32; ++x) {
        const auto coord = tess::Coord3{x, y, 0};
        world.template field<CostTag>(coord) = cost_dist(rng);
        if (block_dist(rng) < 8 && coord != (tess::Coord3{31, 31, 0})) {
          world.template field<PassableTag>(coord) = false;
        }
      }
    }

    const auto goal = tess::Coord3{31, 31, 0};
    tess::DistanceFieldScratch bounded_scratch;
    tess::DistanceFieldScratch unbounded_scratch;
    const auto bounded =
        tess::build_bounded_weighted_distance_field<MidWorld, PassableTag,
                                                    CostTag, 8>(
            world, goal, bounded_scratch);
    const auto unbounded =
        tess::build_weighted_distance_field<MidWorld, PassableTag, CostTag>(
            world, goal, unbounded_scratch);
    ASSERT_EQ(bounded.status, unbounded.status);
    ASSERT_EQ(bounded.status, tess::PathStatus::Found);

    std::uniform_int_distribution<std::int64_t> coord_dist(0, 31);
    for (int i = 0; i < 60; ++i) {
      const auto start = tess::Coord3{coord_dist(rng), coord_dist(rng), 0};
      const auto lhs =
          tess::weighted_distance_field_path<MidWorld, PassableTag, CostTag>(
              world, start, goal, bounded_scratch);
      const auto rhs =
          tess::weighted_distance_field_path<MidWorld, PassableTag, CostTag>(
              world, start, goal, unbounded_scratch);
      ASSERT_EQ(lhs.status, rhs.status) << "seed " << seed << " start " << i;
      ASSERT_EQ(lhs.cost, rhs.cost) << "seed " << seed << " start " << i;
    }
  }
}

// Seeded randomized equivalence: batch grouping must return exactly the
// statuses and costs the single-request weighted A* returns, and the
// grouping stats must match a straightforward reference count. This pins
// the grouping behavior across the goal-counting rewrite.
TEST(TessPathWeightedBatch, SeededBatchesMatchSingleRequestOracle) {
  for (const auto seed : {101u, 202u, 303u}) {
    MidWorld world;
    fill_world(world, true, 1);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<std::uint32_t> cost_dist(1, 6);
    std::uniform_int_distribution<int> block_dist(0, 99);
    for (std::int64_t y = 0; y < 32; ++y) {
      for (std::int64_t x = 0; x < 32; ++x) {
        const auto coord = tess::Coord3{x, y, 0};
        world.template field<CostTag>(coord) = cost_dist(rng);
        if (block_dist(rng) < 6) {
          world.template field<PassableTag>(coord) = false;
        }
      }
    }
    const auto blocked_goal = tess::Coord3{15, 15, 0};
    world.template field<PassableTag>(blocked_goal) = false;

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

    std::uniform_int_distribution<std::size_t> pick(0, passable.size() - 1);
    std::vector<tess::Coord3> goal_pool;
    goal_pool.reserve(11);
    for (int i = 0; i < 10; ++i) {
      goal_pool.push_back(passable[pick(rng)]);
    }
    goal_pool.push_back(blocked_goal);

    std::vector<tess::PathRequest> requests;
    std::uniform_int_distribution<std::size_t> goal_pick(0,
                                                         goal_pool.size() - 1);
    std::uniform_int_distribution<int> unique_dist(0, 99);
    for (int i = 0; i < 150; ++i) {
      const auto start = passable[pick(rng)];
      const auto goal = unique_dist(rng) < 30 ? passable[pick(rng)]
                                              : goal_pool[goal_pick(rng)];
      requests.push_back(tess::PathRequest{start, goal});
    }

    tess::WeightedPathBatchScratch scratch;
    const auto results =
        tess::weighted_path_batch<MidWorld, PassableTag, CostTag, 8>(
            world, requests, scratch);
    ASSERT_EQ(results.size(), requests.size());

    tess::PathScratch oracle_scratch;
    for (std::size_t i = 0; i < requests.size(); ++i) {
      const auto oracle =
          tess::weighted_astar_path<MidWorld, PassableTag, CostTag>(
              world, requests[i], oracle_scratch);
      ASSERT_EQ(results[i].status, oracle.status)
          << "seed " << seed << " request " << i;
      ASSERT_EQ(results[i].cost, oracle.cost)
          << "seed " << seed << " request " << i;
      if (results[i].status == tess::PathStatus::Found) {
        ASSERT_FALSE(results[i].path.empty());
        EXPECT_EQ(results[i].path.front(), requests[i].start);
        EXPECT_EQ(results[i].path.back(), requests[i].goal);
      }
    }

    // Reference grouping counts.
    std::vector<tess::Coord3> goals;
    std::vector<std::size_t> counts;
    for (const auto& request : requests) {
      auto found = false;
      for (std::size_t g = 0; g < goals.size(); ++g) {
        if (goals[g] == request.goal) {
          ++counts[g];
          found = true;
          break;
        }
      }
      if (!found) {
        goals.push_back(request.goal);
        counts.push_back(1);
      }
    }
    auto singleton_goals = std::size_t{0};
    for (const auto count : counts) {
      if (count == 1) {
        ++singleton_goals;
      }
    }
    const auto stats = scratch.stats();
    EXPECT_EQ(stats.requests, requests.size());
    EXPECT_EQ(stats.unique_goals, goals.size()) << "seed " << seed;
    EXPECT_EQ(stats.astar_fallbacks, singleton_goals) << "seed " << seed;
    EXPECT_EQ(stats.field_builds, goals.size() - singleton_goals)
        << "seed " << seed;
  }
}

// A warm second batch over the same requests must not allocate: the goal
// grouping pass and all result storage reuse scratch owned by the batch
// scratch struct.
TEST(TessPathWeightedBatch, WarmRepeatBatchIsAllocationFree) {
  MidWorld world;
  fill_world(world, true, 1);

  std::vector<tess::PathRequest> requests;
  for (std::int64_t y = 0; y < 8; ++y) {
    requests.push_back(
        tess::PathRequest{tess::Coord3{0, y, 0}, tess::Coord3{31, 31, 0}});
    requests.push_back(
        tess::PathRequest{tess::Coord3{y, 0, 0}, tess::Coord3{31, y, 0}});
  }

  tess::WeightedPathBatchScratch scratch;
  scratch.reserve_requests(requests.size());
  scratch.reserve_search_nodes(std::size_t{32} * 32);
  scratch.reserve_path_nodes(4096);

  auto results = tess::weighted_path_batch<MidWorld, PassableTag, CostTag, 8>(
      world, requests, scratch);
  ASSERT_EQ(results.size(), requests.size());

  tess_test::ScopedAllocationCounter counter;
  results = tess::weighted_path_batch<MidWorld, PassableTag, CostTag, 8>(
      world, requests, scratch);
  ASSERT_EQ(results.size(), requests.size());
  EXPECT_EQ(counter.count(), 0u);
  EXPECT_EQ(counter.bytes(), 0u);
}

}  // namespace
