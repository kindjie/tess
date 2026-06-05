#include <gtest/gtest.h>
#include <tess/tess.h>

#include <atomic>
#include <cstddef>
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

using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>>;
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
  EXPECT_GT(result.reached_nodes, result.expanded_nodes);
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

}  // namespace
