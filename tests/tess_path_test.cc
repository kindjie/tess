#include <gtest/gtest.h>
#include <tess/tess.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

namespace {

std::atomic<bool> count_allocations{false};
std::atomic<int> allocation_count{0};

}  // namespace

void* operator new(std::size_t size) {
  if (count_allocations.load(std::memory_order_relaxed)) {
    allocation_count.fetch_add(1, std::memory_order_relaxed);
  }
  if (void* ptr = std::malloc(size)) {
    return ptr;
  }
  throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
  if (count_allocations.load(std::memory_order_relaxed)) {
    allocation_count.fetch_add(1, std::memory_order_relaxed);
  }
  if (void* ptr = std::malloc(size)) {
    return ptr;
  }
  throw std::bad_alloc();
}

void operator delete(void* ptr) noexcept { std::free(ptr); }

void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }

void operator delete[](void* ptr) noexcept { std::free(ptr); }

void operator delete[](void* ptr, std::size_t) noexcept { std::free(ptr); }

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

  allocation_count.store(0, std::memory_order_relaxed);
  count_allocations.store(true, std::memory_order_relaxed);

  const auto result = tess::astar_path<decltype(world), PassableTag>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}},
      scratch);

  count_allocations.store(false, std::memory_order_relaxed);

  EXPECT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(allocation_count.load(std::memory_order_relaxed), 0);
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

  allocation_count.store(0, std::memory_order_relaxed);
  count_allocations.store(true, std::memory_order_relaxed);

  const auto result = tess::cached_astar_path<decltype(world), PassableTag>(
      world, request, scratch, cache);

  count_allocations.store(false, std::memory_order_relaxed);

  EXPECT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(result.expanded_nodes, 0u);
  EXPECT_EQ(allocation_count.load(std::memory_order_relaxed), 0);
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

  allocation_count.store(0, std::memory_order_relaxed);
  count_allocations.store(true, std::memory_order_relaxed);

  const auto field = tess::build_distance_field<decltype(world), PassableTag>(
      world, tess::Coord3{7, 7, 0}, scratch);
  const auto result = tess::distance_field_path<decltype(world), PassableTag>(
      world, tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}, scratch);

  count_allocations.store(false, std::memory_order_relaxed);

  EXPECT_EQ(field.status, tess::PathStatus::Found);
  EXPECT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(allocation_count.load(std::memory_order_relaxed), 0);
}

}  // namespace
