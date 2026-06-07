#include <gtest/gtest.h>
#include <tess/tess.h>

#include <array>
#include <cstdint>
#include <span>

#include "allocation_counter.h"

namespace {

struct PassableTag {};
struct CostTag {};

using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                 tess::Field<CostTag, std::uint32_t>>;
using TopDown2D = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{4, 4, 1}>;
using Vertical2D = tess::Shape<tess::Extent3{1, 8, 8}, tess::Extent3{1, 4, 4}>;
using Chunked3D = tess::Shape<tess::Extent3{4, 4, 4}, tess::Extent3{2, 2, 2}>;

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

TEST(TessPath, FindsTopDown2DPathAroundBlockedTiles) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  world.template field<PassableTag>(tess::Coord3{1, 0, 0}) = false;
  world.template field<PassableTag>(tess::Coord3{1, 1, 0}) = false;
  world.template field<PassableTag>(tess::Coord3{1, 2, 0}) = false;

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);

  const auto result = tess::astar_path<decltype(world), PassableTag>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{3, 0, 0}},
      scratch);

  ASSERT_EQ(result.status, tess::PathStatus::Found);
  ASSERT_FALSE(result.path.empty());
  EXPECT_EQ(result.path.front(), (tess::Coord3{0, 0, 0}));
  EXPECT_EQ(result.path.back(), (tess::Coord3{3, 0, 0}));
  EXPECT_GT(result.cost, 3u);
  EXPECT_GE(result.expanded_nodes, result.path.size());
  EXPECT_GE(result.reached_nodes, result.expanded_nodes);
  for (const auto coord : result.path) {
    EXPECT_TRUE(world.template field<PassableTag>(coord));
  }
}

TEST(TessPath, ReportsInvalidStartAndGoal) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  world.template field<PassableTag>(tess::Coord3{0, 0, 0}) = false;

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);

  const auto invalid_start = tess::astar_path<decltype(world), PassableTag>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{3, 0, 0}},
      scratch);
  const auto invalid_goal = tess::astar_path<decltype(world), PassableTag>(
      world, tess::PathRequest{tess::Coord3{1, 0, 0}, tess::Coord3{8, 0, 0}},
      scratch);

  EXPECT_EQ(invalid_start.status, tess::PathStatus::InvalidStart);
  EXPECT_EQ(invalid_goal.status, tess::PathStatus::InvalidGoal);
}

TEST(TessPath, ReportsNoPathWhenGoalIsCutOff) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  world.template field<PassableTag>(tess::Coord3{1, 0, 0}) = false;
  world.template field<PassableTag>(tess::Coord3{0, 1, 0}) = false;

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);

  const auto result = tess::astar_path<decltype(world), PassableTag>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{3, 0, 0}},
      scratch);

  EXPECT_EQ(result.status, tess::PathStatus::NoPath);
  EXPECT_TRUE(result.path.empty());
}

TEST(TessPath, WeightedAStarAvoidsExpensiveDirectTiles) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);
  world.template field<CostTag>(tess::Coord3{1, 0, 0}) = 10;
  world.template field<CostTag>(tess::Coord3{2, 0, 0}) = 10;
  world.template field<CostTag>(tess::Coord3{3, 0, 0}) = 10;

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);

  const auto result =
      tess::weighted_astar_path<decltype(world), PassableTag, CostTag>(
          world,
          tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{4, 0, 0}},
          scratch);

  ASSERT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(result.cost, 6u);
  EXPECT_EQ(result.path.front(), (tess::Coord3{0, 0, 0}));
  EXPECT_EQ(result.path.back(), (tess::Coord3{4, 0, 0}));
  EXPECT_NE(result.path[1], (tess::Coord3{1, 0, 0}));
  for (const auto coord : result.path) {
    EXPECT_TRUE(world.template field<PassableTag>(coord));
    EXPECT_GT(world.template field<CostTag>(coord), 0u);
  }
}

TEST(TessPath, WeightedAStarTreatsZeroCostTilesAsBlocked) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);
  world.template field<CostTag>(tess::Coord3{1, 0, 0}) = 0;

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);

  const auto result =
      tess::weighted_astar_path<decltype(world), PassableTag, CostTag>(
          world,
          tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{3, 0, 0}},
          scratch);

  ASSERT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(result.cost, 5u);
  EXPECT_NE(result.path[1], (tess::Coord3{1, 0, 0}));
  for (const auto coord : result.path) {
    EXPECT_GT(world.template field<CostTag>(coord), 0u);
  }
}

TEST(TessPath, WeightedAStarReportsInvalidZeroCostEndpoints) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);

  world.template field<CostTag>(tess::Coord3{0, 0, 0}) = 0;
  const auto invalid_start =
      tess::weighted_astar_path<decltype(world), PassableTag, CostTag>(
          world,
          tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{3, 0, 0}},
          scratch);
  world.template field<CostTag>(tess::Coord3{0, 0, 0}) = 1;
  world.template field<CostTag>(tess::Coord3{3, 0, 0}) = 0;
  const auto invalid_goal =
      tess::weighted_astar_path<decltype(world), PassableTag, CostTag>(
          world,
          tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{3, 0, 0}},
          scratch);

  EXPECT_EQ(invalid_start.status, tess::PathStatus::InvalidStart);
  EXPECT_EQ(invalid_goal.status, tess::PathStatus::InvalidGoal);
}

TEST(TessPath, WeightedAStarUsesUnitCostDirectFastPath) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);

  const auto result =
      tess::weighted_astar_path<decltype(world), PassableTag, CostTag>(
          world,
          tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}},
          scratch);

  ASSERT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(result.cost, 14u);
  EXPECT_EQ(result.path.size(), 15u);
  EXPECT_EQ(result.expanded_nodes, result.path.size());
  EXPECT_EQ(result.reached_nodes, result.path.size());
}

TEST(TessPath, WeightedAStarUsesUnitCostAxisDetourFastPath) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);
  world.template field<PassableTag>(tess::Coord3{1, 0, 0}) = false;

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);

  const auto result =
      tess::weighted_astar_path<decltype(world), PassableTag, CostTag>(
          world,
          tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{3, 0, 0}},
          scratch);

  ASSERT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(result.cost, 5u);
  EXPECT_EQ(result.expanded_nodes, result.path.size());
  EXPECT_EQ(result.path.front(), (tess::Coord3{0, 0, 0}));
  EXPECT_EQ(result.path.back(), (tess::Coord3{3, 0, 0}));
  EXPECT_NE(result.path[1], (tess::Coord3{1, 0, 0}));
}

TEST(TessPath, RejectsFullSeparatingBarrierBeforeAStar) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  for (std::int64_t y = 0; y < 8; ++y) {
    world.template field<PassableTag>(tess::Coord3{4, y, 0}) = false;
  }

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);

  const auto result = tess::astar_path<decltype(world), PassableTag>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}},
      scratch);

  EXPECT_EQ(result.status, tess::PathStatus::NoPath);
  EXPECT_EQ(result.expanded_nodes, 0u);
  EXPECT_EQ(result.reached_nodes, 0u);
  EXPECT_TRUE(result.path.empty());
}

TEST(TessPath, FindsAlternateDirectAxisOrder) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  world.template field<PassableTag>(tess::Coord3{4, 0, 0}) = false;

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);

  const auto result = tess::astar_path<decltype(world), PassableTag>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}},
      scratch);

  ASSERT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(result.cost, 14u);
  EXPECT_EQ(result.expanded_nodes, result.path.size());
  EXPECT_EQ(result.reached_nodes, result.path.size());
  EXPECT_EQ(result.path.front(), (tess::Coord3{0, 0, 0}));
  EXPECT_EQ(result.path.back(), (tess::Coord3{7, 7, 0}));
  for (const auto coord : result.path) {
    EXPECT_TRUE(world.template field<PassableTag>(coord));
  }
}

TEST(TessPath, FindsAxisAlignedOneTileDetourBeforeAStar) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  world.template field<PassableTag>(tess::Coord3{4, 0, 0}) = false;

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);

  const auto result = tess::astar_path<decltype(world), PassableTag>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 0, 0}},
      scratch);

  ASSERT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(result.cost, 9u);
  EXPECT_EQ(result.expanded_nodes, result.path.size());
  EXPECT_EQ(result.reached_nodes, result.path.size());
  EXPECT_EQ(result.path.front(), (tess::Coord3{0, 0, 0}));
  EXPECT_EQ(result.path.back(), (tess::Coord3{7, 0, 0}));
  for (const auto coord : result.path) {
    EXPECT_TRUE(world.template field<PassableTag>(coord));
  }
}

TEST(TessPath, FindsSinglePlaneGapBeforeAStar) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  for (std::int64_t y = 0; y < 8; ++y) {
    if (y != 7) {
      world.template field<PassableTag>(tess::Coord3{4, y, 0}) = false;
    }
  }

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);

  const auto result = tess::astar_path<decltype(world), PassableTag>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 0, 0}},
      scratch);

  ASSERT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(result.cost, 21u);
  EXPECT_EQ(result.expanded_nodes, result.path.size());
  EXPECT_EQ(result.reached_nodes, result.path.size());
  EXPECT_EQ(result.path.front(), (tess::Coord3{0, 0, 0}));
  EXPECT_EQ(result.path.back(), (tess::Coord3{7, 0, 0}));
  for (const auto coord : result.path) {
    EXPECT_TRUE(world.template field<PassableTag>(coord));
  }
}

TEST(TessPath, FindsForcedPlaneGapsBeforeAStar) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  for (std::int64_t y = 0; y < 8; ++y) {
    if (y != 0) {
      world.template field<PassableTag>(tess::Coord3{2, y, 0}) = false;
    }
    if (y != 7) {
      world.template field<PassableTag>(tess::Coord3{4, y, 0}) = false;
    }
  }

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);

  const auto result = tess::astar_path<decltype(world), PassableTag>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}},
      scratch);

  ASSERT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(result.cost, 14u);
  EXPECT_EQ(result.expanded_nodes, result.path.size());
  EXPECT_EQ(result.reached_nodes, result.path.size());
  EXPECT_EQ(result.path.front(), (tess::Coord3{0, 0, 0}));
  EXPECT_EQ(result.path.back(), (tess::Coord3{7, 7, 0}));
  for (const auto coord : result.path) {
    EXPECT_TRUE(world.template field<PassableTag>(coord));
  }
}

TEST(TessPath, FindsForcedPlaneGapsOnYProgressAxisBeforeAStar) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  for (std::int64_t x = 0; x < 8; ++x) {
    if (x != 0) {
      world.template field<PassableTag>(tess::Coord3{x, 2, 0}) = false;
    }
    if (x != 7) {
      world.template field<PassableTag>(tess::Coord3{x, 4, 0}) = false;
    }
  }

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);

  const auto result = tess::astar_path<decltype(world), PassableTag>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}},
      scratch);

  ASSERT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(result.cost, 14u);
  EXPECT_EQ(result.expanded_nodes, result.path.size());
  EXPECT_EQ(result.reached_nodes, result.path.size());
  EXPECT_EQ(result.path.front(), (tess::Coord3{0, 0, 0}));
  EXPECT_EQ(result.path.back(), (tess::Coord3{7, 7, 0}));
  for (const auto coord : result.path) {
    EXPECT_TRUE(world.template field<PassableTag>(coord));
  }
}

TEST(TessPath, FindsForcedPlaneGapsInReverseBeforeAStar) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  for (std::int64_t y = 0; y < 8; ++y) {
    if (y != 0) {
      world.template field<PassableTag>(tess::Coord3{2, y, 0}) = false;
    }
    if (y != 7) {
      world.template field<PassableTag>(tess::Coord3{4, y, 0}) = false;
    }
  }

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);

  const auto result = tess::astar_path<decltype(world), PassableTag>(
      world, tess::PathRequest{tess::Coord3{7, 7, 0}, tess::Coord3{0, 0, 0}},
      scratch);

  ASSERT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(result.cost, 14u);
  EXPECT_EQ(result.expanded_nodes, result.path.size());
  EXPECT_EQ(result.reached_nodes, result.path.size());
  EXPECT_EQ(result.path.front(), (tess::Coord3{7, 7, 0}));
  EXPECT_EQ(result.path.back(), (tess::Coord3{0, 0, 0}));
  for (const auto coord : result.path) {
    EXPECT_TRUE(world.template field<PassableTag>(coord));
  }
}

TEST(TessPath, FindsVertical2DSinglePlaneGapBeforeAStar) {
  tess::AlwaysResidentWorld<Vertical2D, Schema> world;
  fill_passable(world, true);
  for (std::int64_t z = 0; z < 8; ++z) {
    if (z != 7) {
      world.template field<PassableTag>(tess::Coord3{0, 4, z}) = false;
    }
  }

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);

  const auto result = tess::astar_path<decltype(world), PassableTag>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{0, 7, 0}},
      scratch);

  ASSERT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(result.cost, 21u);
  EXPECT_EQ(result.expanded_nodes, result.path.size());
  EXPECT_EQ(result.reached_nodes, result.path.size());
  EXPECT_EQ(result.path.front(), (tess::Coord3{0, 0, 0}));
  EXPECT_EQ(result.path.back(), (tess::Coord3{0, 7, 0}));
  for (const auto coord : result.path) {
    EXPECT_TRUE(world.template field<PassableTag>(coord));
  }
}

TEST(TessPath, FindsVertical2DForcedPlaneGapsBeforeAStar) {
  tess::AlwaysResidentWorld<Vertical2D, Schema> world;
  fill_passable(world, true);
  for (std::int64_t z = 0; z < 8; ++z) {
    if (z != 0) {
      world.template field<PassableTag>(tess::Coord3{0, 2, z}) = false;
    }
    if (z != 7) {
      world.template field<PassableTag>(tess::Coord3{0, 4, z}) = false;
    }
  }

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);

  const auto result = tess::astar_path<decltype(world), PassableTag>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{0, 7, 7}},
      scratch);

  ASSERT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(result.cost, 14u);
  EXPECT_EQ(result.expanded_nodes, result.path.size());
  EXPECT_EQ(result.reached_nodes, result.path.size());
  EXPECT_EQ(result.path.front(), (tess::Coord3{0, 0, 0}));
  EXPECT_EQ(result.path.back(), (tess::Coord3{0, 7, 7}));
  for (const auto coord : result.path) {
    EXPECT_TRUE(world.template field<PassableTag>(coord));
  }
}

TEST(TessPath, SupportsVertical2DCoordinates) {
  tess::AlwaysResidentWorld<Vertical2D, Schema> world;
  fill_passable(world, true);
  tess::PathScratch scratch;
  scratch.reserve_nodes(64);

  const auto result = tess::astar_path<decltype(world), PassableTag>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{0, 3, 3}},
      scratch);

  ASSERT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(result.path.front(), (tess::Coord3{0, 0, 0}));
  EXPECT_EQ(result.path.back(), (tess::Coord3{0, 3, 3}));
  for (const auto coord : result.path) {
    EXPECT_EQ(coord.x, 0);
  }
}

TEST(TessPath, SupportsChunked3DCoordinates) {
  tess::AlwaysResidentWorld<Chunked3D, Schema> world;
  fill_passable(world, true);
  tess::PathScratch scratch;
  scratch.reserve_nodes(64);

  const auto result = tess::astar_path<decltype(world), PassableTag>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{2, 2, 2}},
      scratch);

  ASSERT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(result.path.front(), (tess::Coord3{0, 0, 0}));
  EXPECT_EQ(result.path.back(), (tess::Coord3{2, 2, 2}));
  EXPECT_EQ(result.cost, 6u);
  EXPECT_EQ(result.expanded_nodes, result.path.size());
  EXPECT_GE(result.reached_nodes, result.expanded_nodes);
}

TEST(TessPath, Finds3DPlaneGapBeforeAStar) {
  tess::AlwaysResidentWorld<Chunked3D, Schema> world;
  fill_passable(world, true);
  for (std::int64_t z = 0; z < 4; ++z) {
    for (std::int64_t y = 0; y < 4; ++y) {
      if (y != 3 || z != 3) {
        world.template field<PassableTag>(tess::Coord3{2, y, z}) = false;
      }
    }
  }

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);

  const auto result = tess::astar_path<decltype(world), PassableTag>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{3, 0, 0}},
      scratch);

  ASSERT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(result.cost, 15u);
  EXPECT_EQ(result.expanded_nodes, result.path.size());
  EXPECT_EQ(result.reached_nodes, result.path.size());
  EXPECT_EQ(result.path.front(), (tess::Coord3{0, 0, 0}));
  EXPECT_EQ(result.path.back(), (tess::Coord3{3, 0, 0}));
  for (const auto coord : result.path) {
    EXPECT_TRUE(world.template field<PassableTag>(coord));
  }
}

TEST(TessPath, Rejects3DSlabWithoutGapBeforeAStar) {
  tess::AlwaysResidentWorld<Chunked3D, Schema> world;
  fill_passable(world, true);
  for (std::int64_t z = 0; z < 4; ++z) {
    for (std::int64_t y = 0; y < 4; ++y) {
      world.template field<PassableTag>(tess::Coord3{2, y, z}) = false;
    }
  }

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);

  const auto result = tess::astar_path<decltype(world), PassableTag>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{3, 3, 3}},
      scratch);

  EXPECT_EQ(result.status, tess::PathStatus::NoPath);
  EXPECT_EQ(result.expanded_nodes, 0u);
  EXPECT_EQ(result.reached_nodes, 0u);
  EXPECT_TRUE(result.path.empty());
}

TEST(TessPath, Finds3DPlaneGapWithMultipleOpenings) {
  tess::AlwaysResidentWorld<Chunked3D, Schema> world;
  fill_passable(world, true);
  for (std::int64_t z = 0; z < 4; ++z) {
    for (std::int64_t y = 0; y < 4; ++y) {
      if ((y != 1 || z != 0) && (y != 3 || z != 3)) {
        world.template field<PassableTag>(tess::Coord3{2, y, z}) = false;
      }
    }
  }

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);

  const auto result = tess::astar_path<decltype(world), PassableTag>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{3, 0, 0}},
      scratch);

  ASSERT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(result.cost, 5u);
  EXPECT_EQ(result.expanded_nodes, result.path.size());
  EXPECT_EQ(result.reached_nodes, result.path.size());
  EXPECT_EQ(result.path.front(), (tess::Coord3{0, 0, 0}));
  EXPECT_EQ(result.path.back(), (tess::Coord3{3, 0, 0}));
  for (const auto coord : result.path) {
    EXPECT_TRUE(world.template field<PassableTag>(coord));
  }
}

TEST(TessPath, WarmScratchPathQueryDoesNotAllocate) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  tess::PathScratch scratch;
  scratch.reserve_nodes(64);

  (void)tess::astar_path<decltype(world), PassableTag>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}},
      scratch);

  tess_test::reset_allocation_count();
  tess_test::set_allocation_counting(true);

  const auto result = tess::astar_path<decltype(world), PassableTag>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}},
      scratch);

  tess_test::set_allocation_counting(false);

  EXPECT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(tess_test::allocation_count(), 0);
}

TEST(TessPath, CachedAStarReusesRepeatedRoute) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  world.template field<PassableTag>(tess::Coord3{3, 0, 0}) = false;

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::RouteCacheScratch cache;
  cache.reserve_routes(2);
  cache.reserve_path_nodes(128);

  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}};
  const auto first = tess::cached_astar_path<decltype(world), PassableTag>(
      world, request, scratch, cache);
  const auto second = tess::cached_astar_path<decltype(world), PassableTag>(
      world, request, scratch, cache);
  const auto stats = cache.stats();

  ASSERT_EQ(first.status, tess::PathStatus::Found);
  ASSERT_EQ(second.status, tess::PathStatus::Found);
  EXPECT_EQ(second.cost, first.cost);
  EXPECT_EQ(second.path.front(), request.start);
  EXPECT_EQ(second.path.back(), request.goal);
  EXPECT_EQ(second.expanded_nodes, 0u);
  EXPECT_EQ(second.reached_nodes, 0u);
  EXPECT_EQ(stats.entries, 1u);
  EXPECT_EQ(stats.hits, 1u);
  EXPECT_EQ(stats.misses, 1u);
  EXPECT_EQ(stats.path_nodes, first.path.size());
}

TEST(TessPath, CachedAStarClearForcesRecompute) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  world.template field<PassableTag>(tess::Coord3{3, 0, 0}) = false;

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::RouteCacheScratch cache;
  cache.reserve_routes(2);
  cache.reserve_path_nodes(128);

  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}};
  const auto first = tess::cached_astar_path<decltype(world), PassableTag>(
      world, request, scratch, cache);
  cache.clear();
  const auto second = tess::cached_astar_path<decltype(world), PassableTag>(
      world, request, scratch, cache);
  const auto stats = cache.stats();

  ASSERT_EQ(first.status, tess::PathStatus::Found);
  ASSERT_EQ(second.status, tess::PathStatus::Found);
  EXPECT_EQ(second.cost, first.cost);
  EXPECT_GT(second.expanded_nodes, 0u);
  EXPECT_GT(second.reached_nodes, 0u);
  EXPECT_EQ(stats.entries, 1u);
  EXPECT_EQ(stats.hits, 0u);
  EXPECT_EQ(stats.misses, 1u);
}

TEST(TessPath, CachedAStarInvalidationForcesRecomputeAndKeepsStats) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  world.template field<PassableTag>(tess::Coord3{3, 0, 0}) = false;

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::RouteCacheScratch cache;
  cache.reserve_routes(2);
  cache.reserve_path_nodes(128);

  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}};
  const auto first = tess::cached_astar_path<decltype(world), PassableTag>(
      world, request, scratch, cache);
  const auto hit = tess::cached_astar_path<decltype(world), PassableTag>(
      world, request, scratch, cache);
  cache.invalidate();
  const auto recomputed = tess::cached_astar_path<decltype(world), PassableTag>(
      world, request, scratch, cache);
  const auto stats = cache.stats();

  ASSERT_EQ(first.status, tess::PathStatus::Found);
  ASSERT_EQ(hit.status, tess::PathStatus::Found);
  ASSERT_EQ(recomputed.status, tess::PathStatus::Found);
  EXPECT_EQ(hit.expanded_nodes, 0u);
  EXPECT_GT(recomputed.expanded_nodes, 0u);
  EXPECT_EQ(stats.entries, 1u);
  EXPECT_EQ(stats.hits, 1u);
  EXPECT_EQ(stats.misses, 2u);
}

TEST(TessPath, RouteCacheInvalidatesWhenCapturedWorldVersionsChange) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::RouteCacheScratch cache;
  cache.reserve_routes(4);
  cache.reserve_path_nodes(64);

  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}};
  const auto first = tess::cached_astar_path<decltype(world), PassableTag>(
      world, request, scratch, cache);
  cache.capture_world_versions(world);
  const auto hit = tess::cached_astar_path<decltype(world), PassableTag>(
      world, request, scratch, cache);

  ASSERT_EQ(first.status, tess::PathStatus::Found);
  ASSERT_EQ(hit.status, tess::PathStatus::Found);
  EXPECT_EQ(cache.stats().entries, 1u);
  EXPECT_EQ(cache.stats().hits, 1u);
  EXPECT_FALSE(cache.invalidate_if_world_changed(world));

  world.mark_dirty(tess::ChunkKey{0}, 1u,
                   tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}});

  EXPECT_TRUE(cache.invalidate_if_world_changed(world));
  EXPECT_EQ(cache.stats().entries, 0u);
  EXPECT_EQ(cache.stats().hits, 1u);
  EXPECT_EQ(cache.stats().misses, 1u);

  const auto recomputed = tess::cached_astar_path<decltype(world), PassableTag>(
      world, request, scratch, cache);
  EXPECT_EQ(recomputed.status, tess::PathStatus::Found);
  EXPECT_EQ(recomputed.expanded_nodes, first.expanded_nodes);
  EXPECT_EQ(cache.stats().misses, 2u);
}

TEST(TessPath, ChunkVersionDependenciesTrackExplicitChunks) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  tess::ChunkVersionDependencies dependencies;
  dependencies.reserve(2);

  dependencies.add_chunk(world, tess::ChunkKey{0});
  dependencies.add_chunk(world, tess::ChunkKey{1});

  ASSERT_EQ(dependencies.size(), 2u);
  EXPECT_TRUE(dependencies.is_valid(world));

  world.mark_dirty(tess::ChunkKey{2}, 1u,
                   tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}});

  EXPECT_TRUE(dependencies.is_valid(world));

  world.mark_dirty(tess::ChunkKey{1}, 1u,
                   tess::Box3{tess::Coord3{4, 0, 0}, tess::Extent3{1, 1, 1}});

  EXPECT_FALSE(dependencies.is_valid(world));
}

TEST(TessPath, ChunkVersionDependenciesCanCaptureAllChunks) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  tess::ChunkVersionDependencies dependencies;

  dependencies.capture_all(world);

  ASSERT_EQ(dependencies.size(), decltype(world)::chunk_count);
  EXPECT_TRUE(dependencies.is_valid(world));

  world.mark_dirty(tess::ChunkKey{3}, 1u,
                   tess::Box3{tess::Coord3{4, 4, 0}, tess::Extent3{1, 1, 1}});

  EXPECT_FALSE(dependencies.is_valid(world));
}

TEST(TessPath, WeightedRouteProductReplaysWhileDependenciesAreValid) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::WeightedRouteProduct product;
  product.reserve_path_nodes(8);
  product.reserve_dependencies(2);
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{3, 0, 0}};

  const auto built =
      tess::build_weighted_route_product<decltype(world), PassableTag, CostTag>(
          world, request, scratch, product);
  const auto replay = tess::weighted_route_product_path(world, product);

  ASSERT_EQ(built.status, tess::PathStatus::Found);
  EXPECT_EQ(replay.status, tess::PathStatus::Found);
  EXPECT_EQ(replay.cost, built.cost);
  EXPECT_EQ(replay.path.size(), built.path.size());
  EXPECT_EQ(product.request().start, request.start);
  EXPECT_FALSE(product.dependencies().empty());
}

TEST(TessPath, WeightedRouteProductInvalidatesCapturedChunksOnly) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::WeightedRouteProduct product;
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{3, 0, 0}};

  const auto built =
      tess::build_weighted_route_product<decltype(world), PassableTag, CostTag>(
          world, request, scratch, product);
  ASSERT_EQ(built.status, tess::PathStatus::Found);

  world.mark_dirty(tess::ChunkKey{3}, 1u,
                   tess::Box3{tess::Coord3{4, 4, 0}, tess::Extent3{1, 1, 1}});
  EXPECT_EQ(tess::weighted_route_product_path(world, product).status,
            tess::PathStatus::Found);

  world.mark_dirty(tess::ChunkKey{0}, 1u,
                   tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}});
  EXPECT_EQ(tess::weighted_route_product_path(world, product).status,
            tess::PathStatus::NoPath);
}

TEST(TessPath, WeightedPortalRouteProductStitchesWaypointSegments) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);
  for (std::int64_t y = 0; y < 8; ++y) {
    world.template field<PassableTag>(tess::Coord3{4, y, 0}) = false;
  }
  world.template field<PassableTag>(tess::Coord3{4, 3, 0}) = true;
  world.template field<CostTag>(tess::Coord3{4, 3, 0}) = 5;

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::WeightedPortalRouteProduct product;
  product.reserve_waypoints(1);
  product.reserve_path_nodes(16);
  product.reserve_dependencies(4);
  const auto request =
      tess::PathRequest{tess::Coord3{0, 3, 0}, tess::Coord3{7, 3, 0}};
  const auto waypoints = std::array{tess::Coord3{4, 3, 0}};

  const auto product_path =
      tess::build_weighted_portal_route_product<decltype(world), PassableTag,
                                                CostTag>(
          world, request, waypoints, scratch, product);
  const auto astar =
      tess::weighted_astar_path<decltype(world), PassableTag, CostTag>(
          world, request, scratch);

  ASSERT_EQ(product_path.status, tess::PathStatus::Found);
  ASSERT_EQ(astar.status, tess::PathStatus::Found);
  EXPECT_EQ(product_path.cost, astar.cost);
  EXPECT_EQ(product_path.path.front(), request.start);
  EXPECT_EQ(product_path.path.back(), request.goal);
  EXPECT_EQ(product.waypoints().size(), 1u);
  EXPECT_EQ(tess::weighted_portal_route_product_path(world, product).status,
            tess::PathStatus::Found);
}

TEST(TessPath, WeightedChunkPortalRouteProductFindsChunkBoundaryPortal) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);
  for (std::int64_t y = 0; y < 8; ++y) {
    world.template field<PassableTag>(tess::Coord3{4, y, 0}) = false;
  }
  world.template field<PassableTag>(tess::Coord3{4, 3, 0}) = true;
  world.template field<CostTag>(tess::Coord3{4, 3, 0}) = 5;

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::WeightedPortalRouteProduct product;
  product.reserve_waypoints(1);
  product.reserve_path_nodes(16);
  product.reserve_dependencies(4);
  const auto request =
      tess::PathRequest{tess::Coord3{0, 3, 0}, tess::Coord3{7, 3, 0}};

  const auto product_path =
      tess::build_weighted_chunk_portal_route_product<decltype(world),
                                                      PassableTag, CostTag>(
          world, request, scratch, product);
  const auto astar =
      tess::weighted_astar_path<decltype(world), PassableTag, CostTag>(
          world, request, scratch);

  ASSERT_EQ(product_path.status, tess::PathStatus::Found);
  ASSERT_EQ(astar.status, tess::PathStatus::Found);
  EXPECT_EQ(product_path.cost, astar.cost);
  EXPECT_EQ(product_path.path.front(), request.start);
  EXPECT_EQ(product_path.path.back(), request.goal);
  ASSERT_EQ(product.waypoints().size(), 1u);
  EXPECT_EQ(product.waypoints().front(), (tess::Coord3{4, 3, 0}));
  EXPECT_EQ(product.route_candidates(), 7u);
  EXPECT_GT(product.portal_scan_tiles(), 0u);
}

TEST(TessPath, WeightedPortalSegmentCacheReusesVerifiedSegments) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::WeightedPortalSegmentCache cache;
  cache.reserve_segments(2);
  cache.reserve_path_nodes(16);
  tess::WeightedPortalRouteProduct product;
  product.reserve_waypoints(1);
  product.reserve_path_nodes(16);
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 0, 0}};
  const auto waypoints = std::array{tess::Coord3{3, 0, 0}};

  const auto first =
      tess::build_weighted_portal_route_product<decltype(world), PassableTag,
                                                CostTag>(
          world, request, waypoints, scratch, cache, product);
  const auto first_expanded = first.expanded_nodes;
  const auto second =
      tess::build_weighted_portal_route_product<decltype(world), PassableTag,
                                                CostTag>(
          world, request, waypoints, scratch, cache, product);

  ASSERT_EQ(first.status, tess::PathStatus::Found);
  ASSERT_EQ(second.status, tess::PathStatus::Found);
  EXPECT_EQ(first.cost, second.cost);
  EXPECT_EQ(first.path.size(), second.path.size());
  EXPECT_EQ(second.path.front(), request.start);
  EXPECT_EQ(second.path.back(), request.goal);
  EXPECT_GT(first_expanded, 0u);
  EXPECT_EQ(second.expanded_nodes, 0u);
  EXPECT_EQ(second.reached_nodes, 0u);
  EXPECT_EQ(cache.size(), 2u);
}

TEST(TessPath, WeightedPortalSegmentCacheRejectsStaleSegments) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::WeightedPortalSegmentCache cache;
  cache.reserve_segments(2);
  cache.reserve_path_nodes(16);
  tess::WeightedPortalRouteProduct product;
  product.reserve_waypoints(1);
  product.reserve_path_nodes(16);
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 0, 0}};
  const auto waypoints = std::array{tess::Coord3{3, 0, 0}};

  const auto first =
      tess::build_weighted_portal_route_product<decltype(world), PassableTag,
                                                CostTag>(
          world, request, waypoints, scratch, cache, product);
  ASSERT_EQ(first.status, tess::PathStatus::Found);
  EXPECT_EQ(cache.size(), 2u);

  world.template field<CostTag>(tess::Coord3{3, 0, 0}) = 9;
  world.mark_dirty(tess::ChunkKey{0}, 1u,
                   tess::Box3{tess::Coord3{3, 0, 0}, tess::Extent3{1, 1, 1}});

  const auto second =
      tess::build_weighted_portal_route_product<decltype(world), PassableTag,
                                                CostTag>(
          world, request, waypoints, scratch, cache, product);

  ASSERT_EQ(second.status, tess::PathStatus::Found);
  EXPECT_GT(second.expanded_nodes, 0u);
  EXPECT_EQ(second.cost, 15u);
  EXPECT_EQ(second.path.front(), request.start);
  EXPECT_EQ(second.path.back(), request.goal);
  EXPECT_EQ(cache.size(), 4u);

  cache.clear();
  EXPECT_EQ(cache.size(), 0u);
}

TEST(TessPath, WeightedPortalSegmentCacheDoesNotStoreFailedSegments) {
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
  product.reserve_path_nodes(16);
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 0, 0}};
  const auto waypoints = std::array<tess::Coord3, 0>{};

  const auto first =
      tess::build_weighted_portal_route_product<decltype(world), PassableTag,
                                                CostTag>(
          world, request, waypoints, scratch, cache, product);
  const auto second =
      tess::build_weighted_portal_route_product<decltype(world), PassableTag,
                                                CostTag>(
          world, request, waypoints, scratch, cache, product);

  EXPECT_EQ(first.status, tess::PathStatus::NoPath);
  EXPECT_EQ(second.status, tess::PathStatus::NoPath);
  EXPECT_GT(first.expanded_nodes, 0u);
  EXPECT_GT(second.expanded_nodes, 0u);
  EXPECT_EQ(cache.size(), 0u);
}

TEST(TessPath, WeightedPortalRouteProductInvalidatesTouchedChunks) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::WeightedPortalRouteProduct product;
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{3, 0, 0}};

  const auto built =
      tess::build_weighted_portal_route_product<decltype(world), PassableTag,
                                                CostTag>(
          world, request, std::span<const tess::Coord3>{}, scratch, product);
  ASSERT_EQ(built.status, tess::PathStatus::Found);

  world.mark_dirty(tess::ChunkKey{3}, 1u,
                   tess::Box3{tess::Coord3{4, 4, 0}, tess::Extent3{1, 1, 1}});
  EXPECT_EQ(tess::weighted_portal_route_product_path(world, product).status,
            tess::PathStatus::Found);

  world.mark_dirty(tess::ChunkKey{0}, 1u,
                   tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}});
  EXPECT_EQ(tess::weighted_portal_route_product_path(world, product).status,
            tess::PathStatus::NoPath);
}

TEST(TessPath, CachedAStarReusesSuffixForSameGoal) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::RouteCacheScratch cache;
  cache.reserve_routes(2);
  cache.reserve_path_nodes(64);

  const auto first_request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 0, 0}};
  const auto suffix_request =
      tess::PathRequest{tess::Coord3{3, 0, 0}, tess::Coord3{7, 0, 0}};

  const auto first = tess::cached_astar_path<decltype(world), PassableTag>(
      world, first_request, scratch, cache);
  const auto suffix = tess::cached_astar_path<decltype(world), PassableTag>(
      world, suffix_request, scratch, cache);
  const auto stats = cache.stats();

  ASSERT_EQ(first.status, tess::PathStatus::Found);
  ASSERT_EQ(suffix.status, tess::PathStatus::Found);
  EXPECT_EQ(suffix.path.front(), suffix_request.start);
  EXPECT_EQ(suffix.path.back(), suffix_request.goal);
  EXPECT_EQ(suffix.cost, 4u);
  EXPECT_EQ(suffix.expanded_nodes, 0u);
  EXPECT_EQ(suffix.reached_nodes, 0u);
  EXPECT_EQ(stats.entries, 1u);
  EXPECT_EQ(stats.hits, 0u);
  EXPECT_EQ(stats.suffix_hits, 1u);
  EXPECT_EQ(stats.misses, 1u);
}

TEST(TessPath, WarmCachedAStarHitDoesNotAllocate) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::RouteCacheScratch cache;
  cache.reserve_routes(1);
  cache.reserve_path_nodes(64);

  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}};
  (void)tess::cached_astar_path<decltype(world), PassableTag>(world, request,
                                                              scratch, cache);

  tess_test::reset_allocation_count();
  tess_test::set_allocation_counting(true);

  const auto result = tess::cached_astar_path<decltype(world), PassableTag>(
      world, request, scratch, cache);

  tess_test::set_allocation_counting(false);

  EXPECT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(result.expanded_nodes, 0u);
  EXPECT_EQ(tess_test::allocation_count(), 0);
}

TEST(TessPath, BuildsSharedGoalDistanceFieldForMultipleStarts) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  world.template field<PassableTag>(tess::Coord3{3, 0, 0}) = false;
  world.template field<PassableTag>(tess::Coord3{3, 1, 0}) = false;
  world.template field<PassableTag>(tess::Coord3{3, 2, 0}) = false;

  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(64);

  const auto field = tess::build_distance_field<decltype(world), PassableTag>(
      world, tess::Coord3{7, 7, 0}, scratch);

  ASSERT_EQ(field.status, tess::PathStatus::Found);
  EXPECT_GT(field.expanded_nodes, 0u);
  EXPECT_GE(field.reached_nodes, field.expanded_nodes);

  const auto first = tess::distance_field_path<decltype(world), PassableTag>(
      world, tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}, scratch);

  ASSERT_EQ(first.status, tess::PathStatus::Found);
  EXPECT_EQ(first.path.front(), (tess::Coord3{0, 0, 0}));
  EXPECT_EQ(first.path.back(), (tess::Coord3{7, 7, 0}));
  EXPECT_EQ(first.cost, first.path.size() - 1u);
  for (const auto coord : first.path) {
    EXPECT_TRUE(world.template field<PassableTag>(coord));
  }

  const auto second = tess::distance_field_path<decltype(world), PassableTag>(
      world, tess::Coord3{0, 7, 0}, tess::Coord3{7, 7, 0}, scratch);

  ASSERT_EQ(second.status, tess::PathStatus::Found);
  EXPECT_EQ(second.path.front(), (tess::Coord3{0, 7, 0}));
  EXPECT_EQ(second.path.back(), (tess::Coord3{7, 7, 0}));
  EXPECT_EQ(second.cost, second.path.size() - 1u);
  for (const auto coord : second.path) {
    EXPECT_TRUE(world.template field<PassableTag>(coord));
  }
}

TEST(TessPath, DistanceFieldReportsNoPathForUnreachableStart) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  for (std::int64_t y = 0; y < 8; ++y) {
    world.template field<PassableTag>(tess::Coord3{4, y, 0}) = false;
  }

  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(64);

  const auto field = tess::build_distance_field<decltype(world), PassableTag>(
      world, tess::Coord3{7, 7, 0}, scratch);
  const auto result = tess::distance_field_path<decltype(world), PassableTag>(
      world, tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}, scratch);

  ASSERT_EQ(field.status, tess::PathStatus::Found);
  EXPECT_EQ(result.status, tess::PathStatus::NoPath);
  EXPECT_TRUE(result.path.empty());
}

TEST(TessPath, DistanceFieldRejectsMismatchedGoal) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);

  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(64);

  const auto field = tess::build_distance_field<decltype(world), PassableTag>(
      world, tess::Coord3{7, 7, 0}, scratch);
  const auto result = tess::distance_field_path<decltype(world), PassableTag>(
      world, tess::Coord3{0, 0, 0}, tess::Coord3{7, 6, 0}, scratch);

  ASSERT_EQ(field.status, tess::PathStatus::Found);
  EXPECT_EQ(result.status, tess::PathStatus::NoPath);
  EXPECT_TRUE(result.path.empty());
}

TEST(TessPath, WarmDistanceFieldQueriesDoNotAllocate) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(64);

  (void)tess::build_distance_field<decltype(world), PassableTag>(
      world, tess::Coord3{7, 7, 0}, scratch);
  (void)tess::distance_field_path<decltype(world), PassableTag>(
      world, tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}, scratch);

  tess_test::reset_allocation_count();
  tess_test::set_allocation_counting(true);

  const auto field = tess::build_distance_field<decltype(world), PassableTag>(
      world, tess::Coord3{7, 7, 0}, scratch);
  const auto result = tess::distance_field_path<decltype(world), PassableTag>(
      world, tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}, scratch);

  tess_test::set_allocation_counting(false);

  EXPECT_EQ(field.status, tess::PathStatus::Found);
  EXPECT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(tess_test::allocation_count(), 0);
}

TEST(TessPath, WeightedDistanceFieldMatchesWeightedAStarForSharedGoal) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);
  for (std::int64_t x = 1; x < 7; ++x) {
    world.template field<CostTag>(tess::Coord3{x, 0, 0}) = 10;
  }
  world.template field<PassableTag>(tess::Coord3{3, 2, 0}) = false;
  world.template field<CostTag>(tess::Coord3{4, 1, 0}) = 5;

  tess::DistanceFieldScratch field_scratch;
  field_scratch.reserve_nodes(64);
  tess::PathScratch astar_scratch;
  astar_scratch.reserve_nodes(64);
  const auto goal = tess::Coord3{7, 0, 0};

  const auto field =
      tess::build_weighted_distance_field<decltype(world), PassableTag,
                                          CostTag>(world, goal, field_scratch);

  ASSERT_EQ(field.status, tess::PathStatus::Found);
  EXPECT_GT(field.expanded_nodes, 0u);

  for (const auto start : {tess::Coord3{0, 0, 0}, tess::Coord3{0, 7, 0}}) {
    const auto field_path =
        tess::weighted_distance_field_path<decltype(world), PassableTag,
                                           CostTag>(world, start, goal,
                                                    field_scratch);
    const auto astar_path =
        tess::weighted_astar_path<decltype(world), PassableTag, CostTag>(
            world, tess::PathRequest{start, goal}, astar_scratch);

    ASSERT_EQ(field_path.status, tess::PathStatus::Found);
    ASSERT_EQ(astar_path.status, tess::PathStatus::Found);
    EXPECT_EQ(field_path.cost, astar_path.cost);
    EXPECT_EQ(field_path.path.front(), start);
    EXPECT_EQ(field_path.path.back(), goal);
    for (const auto coord : field_path.path) {
      EXPECT_TRUE(world.template field<PassableTag>(coord));
      EXPECT_GT(world.template field<CostTag>(coord), 0u);
    }
  }
}

TEST(TessPath, WeightedDistanceFieldInBoxRestrictsDomain) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);
  world.template field<CostTag>(tess::Coord3{2, 1, 0}) = 5;

  tess::DistanceFieldScratch boxed_scratch;
  boxed_scratch.reserve_nodes(64);
  tess::DistanceFieldScratch general_scratch;
  general_scratch.reserve_nodes(64);
  const auto domain = tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{4, 4, 1}};
  const auto goal = tess::Coord3{3, 3, 0};

  const auto boxed =
      tess::build_weighted_distance_field_in_box<decltype(world), PassableTag,
                                                 CostTag>(world, goal, domain,
                                                          boxed_scratch);
  const auto general =
      tess::build_weighted_distance_field<decltype(world), PassableTag,
                                          CostTag>(world, goal,
                                                   general_scratch);
  const auto boxed_path =
      tess::weighted_distance_field_path<decltype(world), PassableTag, CostTag>(
          world, tess::Coord3{0, 0, 0}, goal, boxed_scratch);
  const auto general_path =
      tess::weighted_distance_field_path<decltype(world), PassableTag, CostTag>(
          world, tess::Coord3{0, 0, 0}, goal, general_scratch);
  const auto outside_path =
      tess::weighted_distance_field_path<decltype(world), PassableTag, CostTag>(
          world, tess::Coord3{5, 5, 0}, goal, boxed_scratch);

  ASSERT_EQ(boxed.status, tess::PathStatus::Found);
  ASSERT_EQ(general.status, tess::PathStatus::Found);
  ASSERT_EQ(boxed_path.status, tess::PathStatus::Found);
  ASSERT_EQ(general_path.status, tess::PathStatus::Found);
  EXPECT_EQ(boxed_path.cost, general_path.cost);
  EXPECT_LT(boxed.expanded_nodes, general.expanded_nodes);
  EXPECT_EQ(outside_path.status, tess::PathStatus::NoPath);
}

TEST(TessPath, BoundedWeightedDistanceFieldMatchesGeneralWeightedField) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);
  world.template field<CostTag>(tess::Coord3{1, 0, 0}) = 7;
  world.template field<CostTag>(tess::Coord3{2, 0, 0}) = 7;
  world.template field<PassableTag>(tess::Coord3{3, 2, 0}) = false;

  tess::DistanceFieldScratch bounded_scratch;
  bounded_scratch.reserve_nodes(64);
  tess::DistanceFieldScratch general_scratch;
  general_scratch.reserve_nodes(64);
  const auto goal = tess::Coord3{7, 0, 0};
  const auto start = tess::Coord3{0, 0, 0};

  const auto bounded =
      tess::build_bounded_weighted_distance_field<decltype(world), PassableTag,
                                                  CostTag, 7>(world, goal,
                                                              bounded_scratch);
  const auto general =
      tess::build_weighted_distance_field<decltype(world), PassableTag,
                                          CostTag>(world, goal,
                                                   general_scratch);
  const auto bounded_path =
      tess::weighted_distance_field_path<decltype(world), PassableTag, CostTag>(
          world, start, goal, bounded_scratch);
  const auto general_path =
      tess::weighted_distance_field_path<decltype(world), PassableTag, CostTag>(
          world, start, goal, general_scratch);

  ASSERT_EQ(bounded.status, tess::PathStatus::Found);
  ASSERT_EQ(general.status, tess::PathStatus::Found);
  ASSERT_EQ(bounded_path.status, tess::PathStatus::Found);
  ASSERT_EQ(general_path.status, tess::PathStatus::Found);
  EXPECT_EQ(bounded_path.cost, general_path.cost);
}

TEST(TessPath, BoundedWeightedDistanceFieldFallsBackAboveBound) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);
  world.template field<CostTag>(tess::Coord3{7, 0, 0}) = 9;

  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(64);
  const auto goal = tess::Coord3{7, 0, 0};
  const auto start = tess::Coord3{0, 0, 0};

  const auto field =
      tess::build_bounded_weighted_distance_field<decltype(world), PassableTag,
                                                  CostTag, 7>(world, goal,
                                                              scratch);
  const auto path =
      tess::weighted_distance_field_path<decltype(world), PassableTag, CostTag>(
          world, start, goal, scratch);

  ASSERT_EQ(field.status, tess::PathStatus::Found);
  ASSERT_EQ(path.status, tess::PathStatus::Found);
  EXPECT_EQ(path.cost, 15u);
}

TEST(TessPath, WeightedPathBatchGroupsRepeatedGoalsAndKeepsResultSpans) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);
  world.template field<CostTag>(tess::Coord3{1, 0, 0}) = 7;
  world.template field<PassableTag>(tess::Coord3{2, 2, 0}) = false;

  const auto requests = std::array{
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}},
      tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{7, 7, 0}},
      tess::PathRequest{tess::Coord3{0, 7, 0}, tess::Coord3{7, 0, 0}},
  };

  tess::WeightedPathBatchScratch batch;
  batch.reserve_search_nodes(64);
  batch.reserve_requests(requests.size());
  batch.reserve_path_nodes(64);
  const auto results =
      tess::weighted_path_batch<decltype(world), PassableTag, CostTag, 7>(
          world, requests, batch);

  ASSERT_EQ(results.size(), requests.size());
  EXPECT_EQ(batch.stats().requests, requests.size());
  EXPECT_EQ(batch.stats().unique_goals, 2u);
  EXPECT_EQ(batch.stats().field_builds, 1u);
  EXPECT_EQ(batch.stats().astar_fallbacks, 1u);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  for (std::size_t i = 0; i < requests.size(); ++i) {
    const auto expected =
        tess::weighted_astar_path<decltype(world), PassableTag, CostTag>(
            world, requests[i], scratch);
    ASSERT_EQ(results[i].status, tess::PathStatus::Found);
    EXPECT_EQ(results[i].cost, expected.cost);
    EXPECT_EQ(results[i].path.front(), requests[i].start);
    EXPECT_EQ(results[i].path.back(), requests[i].goal);
  }
}

TEST(TessPath, WeightedDistanceFieldRejectsMismatchedGoal) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);

  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(64);

  const auto field = tess::build_weighted_distance_field<decltype(world),
                                                         PassableTag, CostTag>(
      world, tess::Coord3{7, 7, 0}, scratch);
  const auto result =
      tess::weighted_distance_field_path<decltype(world), PassableTag, CostTag>(
          world, tess::Coord3{0, 0, 0}, tess::Coord3{7, 6, 0}, scratch);

  ASSERT_EQ(field.status, tess::PathStatus::Found);
  EXPECT_EQ(result.status, tess::PathStatus::NoPath);
  EXPECT_TRUE(result.path.empty());
}

TEST(TessPath, WeightedDistanceFieldReportsNoPathForUnreachableStart) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);
  for (std::int64_t y = 0; y < 8; ++y) {
    world.template field<PassableTag>(tess::Coord3{4, y, 0}) = false;
  }

  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(64);

  const auto field = tess::build_weighted_distance_field<decltype(world),
                                                         PassableTag, CostTag>(
      world, tess::Coord3{7, 7, 0}, scratch);
  const auto result =
      tess::weighted_distance_field_path<decltype(world), PassableTag, CostTag>(
          world, tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}, scratch);

  ASSERT_EQ(field.status, tess::PathStatus::Found);
  EXPECT_EQ(result.status, tess::PathStatus::NoPath);
  EXPECT_TRUE(result.path.empty());
}

TEST(TessPath, WarmWeightedDistanceFieldQueriesDoNotAllocate) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);
  world.template field<CostTag>(tess::Coord3{1, 0, 0}) = 7;

  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(64);
  const auto goal = tess::Coord3{7, 7, 0};
  const auto start = tess::Coord3{0, 0, 0};

  (void)tess::build_weighted_distance_field<decltype(world), PassableTag,
                                            CostTag>(world, goal, scratch);
  (void)
      tess::weighted_distance_field_path<decltype(world), PassableTag, CostTag>(
          world, start, goal, scratch);

  tess_test::reset_allocation_count();
  tess_test::set_allocation_counting(true);

  const auto field =
      tess::build_weighted_distance_field<decltype(world), PassableTag,
                                          CostTag>(world, goal, scratch);
  const auto result =
      tess::weighted_distance_field_path<decltype(world), PassableTag, CostTag>(
          world, start, goal, scratch);

  tess_test::set_allocation_counting(false);

  EXPECT_EQ(field.status, tess::PathStatus::Found);
  EXPECT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(tess_test::allocation_count(), 0);
}

TEST(TessPath, WarmBoundedWeightedDistanceFieldQueriesDoNotAllocate) {
  tess::AlwaysResidentWorld<TopDown2D, Schema> world;
  fill_passable(world, true);
  fill_cost(world, 1);
  world.template field<CostTag>(tess::Coord3{1, 0, 0}) = 7;

  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(64);
  const auto goal = tess::Coord3{7, 7, 0};
  const auto start = tess::Coord3{0, 0, 0};

  (void)tess::build_bounded_weighted_distance_field<decltype(world),
                                                    PassableTag, CostTag, 7>(
      world, goal, scratch);
  (void)
      tess::weighted_distance_field_path<decltype(world), PassableTag, CostTag>(
          world, start, goal, scratch);

  tess_test::reset_allocation_count();
  tess_test::set_allocation_counting(true);

  const auto field =
      tess::build_bounded_weighted_distance_field<decltype(world), PassableTag,
                                                  CostTag, 7>(world, goal,
                                                              scratch);
  const auto result =
      tess::weighted_distance_field_path<decltype(world), PassableTag, CostTag>(
          world, start, goal, scratch);

  tess_test::set_allocation_counting(false);

  EXPECT_EQ(field.status, tess::PathStatus::Found);
  EXPECT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(tess_test::allocation_count(), 0);
}

}  // namespace
