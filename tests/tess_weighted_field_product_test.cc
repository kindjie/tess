#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <cstdint>
#include <utility>

#include "allocation_counter.h"

namespace {

namespace mv = tess::movement;

struct PassableTag {};
struct CostTag {};

using Shape = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{4, 4, 1}>;
using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                 tess::Field<CostTag, std::uint32_t>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;
using Walker =
    mv::MovementClass<mv::Field<PassableTag>, mv::FieldCost<CostTag>>;

struct ShortcutProvider {
  [[maybe_unused]] static constexpr std::uint32_t maximum_transition_cost = 3;

  template <typename WorldType, typename Sink>
  void for_each_forward(const WorldType&, tess::Coord3 from,
                        Sink&& sink) const {
    if (from == tess::Coord3{0, 0, 0}) {
      sink(tess::SpecialTransitionCandidate{.to = tess::Coord3{7, 7, 0},
                                            .cost = 3});
    }
  }

  template <typename WorldType, typename Sink>
  void for_each_reverse(const WorldType&, tess::Coord3 to, Sink&& sink) const {
    if (to == tess::Coord3{7, 7, 0}) {
      sink(tess::SpecialTransitionCandidate{.to = tess::Coord3{0, 0, 0},
                                            .cost = 3});
    }
  }
};

template <typename WorldType>
void fill_open(WorldType& world) {
  for (auto& chunk : world.chunks()) {
    auto passable = chunk.template field_span<PassableTag>();
    std::fill(passable.begin(), passable.end(), true);
    auto cost = chunk.template field_span<CostTag>();
    std::fill(cost.begin(), cost.end(), 1u);
  }
}

auto two_goals() -> tess::GoalSet {
  tess::GoalSet goals;
  goals.reserve(2);
  goals.add({7, 0, 0});
  goals.add({0, 7, 0});
  return goals;
}

TEST(TessWeightedFieldProduct, MultiGoalReplayChoosesLowestWeightedCost) {
  World world;
  fill_open(world);
  for (std::int64_t x = 1; x < 8; ++x) {
    world.template field<CostTag>({x, 0, 0}) = 10;
  }
  const auto goals = two_goals();
  tess::DistanceFieldScratch scratch;
  tess::DistanceFieldProduct product;

  const auto built = tess::build_weighted_distance_field_product<World, Walker>(
      world, goals, scratch, product);
  ASSERT_EQ(built.status, tess::PathStatus::Found);
  const auto path = tess::weighted_distance_field_product_path<World, Walker>(
      world, {0, 0, 0}, product, scratch);

  ASSERT_EQ(path.status, tess::PathStatus::Found);
  EXPECT_EQ(path.cost, 7u);
  ASSERT_FALSE(path.path.empty());
  EXPECT_EQ(path.path.back(), (tess::Coord3{0, 7, 0}));
}

TEST(TessWeightedFieldProduct, NearestTargetReportsChosenGoalAndPath) {
  World world;
  fill_open(world);
  const auto goals = two_goals();
  tess::DistanceFieldScratch scratch;
  tess::DistanceFieldProduct product;
  ASSERT_EQ((tess::build_weighted_distance_field_product<World, Walker>(
                 world, goals, scratch, product)
                 .status),
            tess::PathStatus::Found);

  const auto nearest = tess::weighted_nearest_target<World, Walker>(
      world, {6, 0, 0}, product, scratch);

  EXPECT_EQ(nearest.status, tess::PathStatus::Found);
  EXPECT_EQ(nearest.cost, 1u);
  EXPECT_EQ(nearest.target, (tess::Coord3{7, 0, 0}));
  ASSERT_EQ(nearest.path.size(), 2u);
}

TEST(TessWeightedFieldProduct, CacheSeparatesWeightedAndUnitModels) {
  World world;
  fill_open(world);
  const auto goals = two_goals();
  tess::DistanceFieldScratch scratch;
  tess::DistanceFieldProduct product;
  ASSERT_EQ((tess::build_weighted_distance_field_product<World, Walker>(
                 world, goals, scratch, product)
                 .status),
            tess::PathStatus::Found);
  tess::FieldProductCache cache;
  cache.reserve_entries(1);

  ASSERT_TRUE((cache.store_weighted<World, Walker>(std::move(product))));
  EXPECT_NE((cache.lookup_weighted<World, Walker>(world, goals)), nullptr);
  EXPECT_EQ((cache.lookup<World, PassableTag>(world, goals)), nullptr);
}

TEST(TessWeightedFieldProduct, ProviderReverseEdgeBuildsReplayableShortcut) {
  World world;
  fill_open(world);
  tess::GoalSet goals;
  goals.add({7, 7, 0});
  tess::DistanceFieldScratch scratch;
  tess::DistanceFieldProduct product;
  const auto provider = ShortcutProvider{};

  const auto built = tess::build_weighted_distance_field_product<World, Walker>(
      world, goals, scratch, product, provider);
  ASSERT_EQ(built.status, tess::PathStatus::Found);
  const auto path = tess::weighted_distance_field_product_path<World, Walker>(
      world, {0, 0, 0}, product, scratch, provider);

  ASSERT_EQ(path.status, tess::PathStatus::Found);
  EXPECT_EQ(path.cost, 3u);
  ASSERT_EQ(path.path.size(), 2u);
  EXPECT_EQ(path.path.back(), (tess::Coord3{7, 7, 0}));
}

TEST(TessWeightedFieldProduct, SupportsVerticalDegenerateLayout) {
  using VerticalShape =
      tess::Shape<tess::Extent3{1, 4, 4}, tess::Extent3{1, 2, 2}>;
  using VerticalWorld = tess::AlwaysResidentWorld<VerticalShape, Schema>;
  VerticalWorld world;
  fill_open(world);
  tess::GoalSet goals;
  goals.add({0, 3, 3});
  tess::DistanceFieldScratch scratch;
  tess::DistanceFieldProduct product;

  ASSERT_EQ((tess::build_weighted_distance_field_product<VerticalWorld, Walker>(
                 world, goals, scratch, product))
                .status,
            tess::PathStatus::Found);
  const auto path =
      tess::weighted_distance_field_product_path<VerticalWorld, Walker>(
          world, {0, 0, 0}, product, scratch);

  EXPECT_EQ(path.status, tess::PathStatus::Found);
  EXPECT_EQ(path.cost, 6u);
  EXPECT_EQ(path.path.back(), (tess::Coord3{0, 3, 3}));
}

TEST(TessWeightedFieldProduct, RelevantWorldEditInvalidatesCachedProduct) {
  World world;
  fill_open(world);
  const auto goals = two_goals();
  tess::DistanceFieldScratch scratch;
  tess::DistanceFieldProduct product;
  ASSERT_EQ((tess::build_weighted_distance_field_product<World, Walker>(
                 world, goals, scratch, product)
                 .status),
            tess::PathStatus::Found);
  tess::FieldProductCache cache;
  ASSERT_TRUE((cache.store_weighted<World, Walker>(std::move(product))));

  world.template field<CostTag>({1, 0, 0}) = 5;
  world.mark_dirty(tess::ChunkKey{0}, 1u, tess::Box3{{1, 0, 0}, {1, 1, 1}});

  EXPECT_EQ((cache.lookup_weighted<World, Walker>(world, goals)), nullptr);
  EXPECT_EQ(cache.stats().stale_rejections, 1u);
}

TEST(TessWeightedFieldProduct, ReservedWarmRebuildDoesNotAllocate) {
  World world;
  fill_open(world);
  const auto goals = two_goals();
  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(64);
  tess::DistanceFieldProduct product;
  product.reserve_goals(2);
  product.reserve_nodes(64);
  product.reserve_dependencies(World::chunk_count);
  (void)tess::build_weighted_distance_field_product<World, Walker>(
      world, goals, scratch, product);

  {
    tess_test::ScopedAllocationCounter counter;
    for (int i = 0; i < 10; ++i) {
      const auto result =
          tess::build_weighted_distance_field_product<World, Walker>(
              world, goals, scratch, product);
      EXPECT_EQ(result.status, tess::PathStatus::Found);
    }
    EXPECT_EQ(counter.count(), 0u);
  }
}

}  // namespace
