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

struct RevisionRestartProvider {
  bool enabled = false;
  std::uint64_t revision = 0;

  [[nodiscard]] auto transition_revision() const noexcept -> std::uint64_t {
    return revision;
  }

  template <typename WorldType, typename Sink>
  void for_each_forward(const WorldType&, tess::Coord3 from,
                        Sink&& sink) const {
    if (enabled && from == tess::Coord3{0, 0, 0}) {
      sink(tess::SpecialTransitionCandidate{.to = tess::Coord3{7, 7, 0}});
    }
  }

  template <typename WorldType, typename Sink>
  void for_each_reverse(const WorldType&, tess::Coord3 to, Sink&& sink) const {
    if (enabled && to == tess::Coord3{7, 7, 0}) {
      sink(tess::SpecialTransitionCandidate{.to = tess::Coord3{0, 0, 0}});
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
      world, goals, product, scratch);
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
                 world, goals, product, scratch)
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
                 world, goals, product, scratch)
                 .status),
            tess::PathStatus::Found);
  tess::FieldProductCache cache;
  cache.reserve_entries(1);

  ASSERT_TRUE((cache.store_weighted<World, Walker>(std::move(product))));
  EXPECT_NE((cache.lookup_weighted<World, Walker>(world, goals)), nullptr);
  EXPECT_EQ((cache.lookup<World, PassableTag>(world, goals)), nullptr);
}

TEST(TessWeightedFieldProduct,
     CacheSeparatesProviderInstancesWithEqualRevisions) {
  World world;
  fill_open(world);
  tess::GoalSet goals;
  goals.add({7, 7, 0});
  const auto shortcut = RevisionRestartProvider{.enabled = true, .revision = 0};
  const auto regular = RevisionRestartProvider{.enabled = false, .revision = 0};
  tess::DistanceFieldScratch scratch;
  tess::DistanceFieldProduct product;
  ASSERT_EQ((tess::build_weighted_distance_field_product<World, Walker>(
                 world, goals, product, scratch, shortcut))
                .status,
            tess::PathStatus::Found);
  tess::FieldProductCache cache;
  ASSERT_TRUE(
      (cache.store_weighted<World, Walker>(std::move(product), shortcut)));

  EXPECT_EQ((cache.lookup_weighted<World, Walker>(world, goals, regular)),
            nullptr);
  EXPECT_EQ(cache.stats().hits, 0u);
  EXPECT_EQ(cache.stats().misses, 1u);
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
      world, goals, product, scratch, provider);
  ASSERT_EQ(built.status, tess::PathStatus::Found);
  const auto path = tess::weighted_distance_field_product_path<World, Walker>(
      world, {0, 0, 0}, product, scratch, provider);

  ASSERT_EQ(path.status, tess::PathStatus::Found);
  EXPECT_EQ(path.cost, 3u);
  ASSERT_EQ(path.path.size(), 2u);
  EXPECT_EQ(path.path.back(), (tess::Coord3{7, 7, 0}));
}

TEST(TessWeightedFieldProduct, UnitProductHonorsSpecialTransitionCost) {
  World world;
  fill_open(world);
  tess::GoalSet goals;
  goals.add({7, 7, 0});
  tess::DistanceFieldScratch scratch;
  tess::DistanceFieldProduct product;
  const auto provider = ShortcutProvider{};

  const auto built = tess::build_distance_field_product<World, PassableTag>(
      world, goals, product, scratch, provider);
  ASSERT_EQ(built.status, tess::PathStatus::Found);
  const auto path = tess::distance_field_product_path<World, PassableTag>(
      world, {0, 0, 0}, product, scratch, provider);

  ASSERT_EQ(path.status, tess::PathStatus::Found);
  EXPECT_EQ(path.cost, 3u);
  ASSERT_EQ(path.path.size(), 2u);
  EXPECT_EQ(path.path.back(), (tess::Coord3{7, 7, 0}));
}

// A store that displaces an entry hands its storage back to the caller, so
// a caller rebuilding into the same product reuses that capacity instead
// of allocating a world-sized distance array every build.
//
// Before this, `store` took the product by move and left the member empty,
// so the next `build_distance_field_product` ran `distance_.assign(...)`
// against a zero-capacity vector. The header comment argued only that the
// moved-from state was never observed, which was true and beside the
// point: the capacity was gone.
TEST(TessWeightedFieldProduct, StoreHandsBackStorageSoRebuildsDoNotAllocate) {
  World world;
  fill_open(world);
  tess::DistanceFieldScratch scratch;
  tess::DistanceFieldProduct product;

  tess::GoalSet goals_a;
  goals_a.add({7, 7, 0});
  tess::GoalSet goals_b;
  goals_b.add({0, 7, 0});

  // Size the budget from a MEASURED product: a product carries goals,
  // dependencies and metadata beyond its distance array, so a computed
  // budget silently rejects the store instead of holding one entry.
  const auto product_bytes = [&] {
    tess::FieldProductCache probe(std::size_t{1} << 40U);
    tess::DistanceFieldProduct one;
    tess::DistanceFieldScratch probe_scratch;
    (void)tess::build_distance_field_product<World, PassableTag>(
        world, goals_a, one, probe_scratch);
    (void)probe.store_reusing<World, PassableTag>(one);
    return probe.stats().bytes;
  }();
  ASSERT_GT(product_bytes, 0u);

  // Holds one product, so the second store evicts the first and the caller
  // receives the evicted storage.
  tess::FieldProductCache cache(product_bytes);
  cache.reserve_entries(2);

  ASSERT_EQ((tess::build_distance_field_product<World, PassableTag>(
                 world, goals_a, product, scratch))
                .status,
            tess::PathStatus::Found);
  ASSERT_NE((cache.store_reusing<World, PassableTag>(product)), nullptr);

  ASSERT_EQ((tess::build_distance_field_product<World, PassableTag>(
                 world, goals_b, product, scratch))
                .status,
            tess::PathStatus::Found);
  ASSERT_NE((cache.store_reusing<World, PassableTag>(product)), nullptr);
  ASSERT_GT(cache.stats().evictions, 0u)
      << "the second store must evict for storage to be handed back";

  // The product now holds the evicted entry's buffers. Rebuilding into it
  // must not reach the allocator.
  {
    tess_test::ScopedAllocationCounter counter;
    const auto rebuilt = tess::build_distance_field_product<World, PassableTag>(
        world, goals_a, product, scratch);
    EXPECT_EQ(rebuilt.status, tess::PathStatus::Found);
    EXPECT_EQ(counter.count(), 0u);
  }
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
                 world, goals, product, scratch))
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
                 world, goals, product, scratch)
                 .status),
            tess::PathStatus::Found);
  tess::FieldProductCache cache;
  ASSERT_TRUE((cache.store_weighted<World, Walker>(std::move(product))));

  world.template field<CostTag>({1, 0, 0}) = 5;
  world.mark_dirty(tess::ChunkKey{0}, tess::DirtyMask{1u},
                   tess::Box3{{1, 0, 0}, {1, 1, 1}});

  EXPECT_EQ((cache.lookup_weighted<World, Walker>(world, goals)), nullptr);
  EXPECT_EQ(cache.stats().stale_rejections, 1u);
}

TEST(TessWeightedFieldProduct,
     BlockedFrontierCapturesOnlyReachedAndFaceNeighborChunks) {
  using WideShape =
      tess::Shape<tess::Extent3{16, 4, 1}, tess::Extent3{4, 4, 1}>;
  using WideWorld = tess::AlwaysResidentWorld<WideShape, Schema>;
  WideWorld world;
  fill_open(world);
  for (std::int64_t y = 0; y < 4; ++y) {
    for (std::int64_t x = 4; x < 8; ++x) {
      world.template field<PassableTag>({x, y, 0}) = false;
    }
  }
  tess::GoalSet goals;
  goals.add({0, 0, 0});
  tess::DistanceFieldScratch scratch;
  tess::DistanceFieldProduct product;

  ASSERT_EQ((tess::build_weighted_distance_field_product<WideWorld, Walker>(
                 world, goals, product, scratch)
                 .status),
            tess::PathStatus::Found);
  ASSERT_EQ(product.dependencies().size(), 2u);

  world.mark_dirty(tess::ChunkKey{2}, tess::DirtyMask{1u},
                   tess::Box3{{8, 0, 0}, {1, 1, 1}});
  EXPECT_TRUE(product.is_valid(world));

  world.mark_dirty(tess::ChunkKey{1}, tess::DirtyMask{1u},
                   tess::Box3{{4, 0, 0}, {1, 1, 1}});
  EXPECT_FALSE(product.is_valid(world));
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
      world, goals, product, scratch);

  {
    tess_test::ScopedAllocationCounter counter;
    for (int i = 0; i < 10; ++i) {
      const auto result =
          tess::build_weighted_distance_field_product<World, Walker>(
              world, goals, product, scratch);
      EXPECT_EQ(result.status, tess::PathStatus::Found);
    }
    EXPECT_EQ(counter.count(), 0u);
  }
}

TEST(TessWeightedFieldProduct, ReplaceStoreThatEvictsReturnsTheStoredEntry) {
  World world;
  fill_open(world);
  // Wall off everything outside chunk (0,0) so the first build for goal_b
  // captures only 3 dependency chunks (reached + face neighbors).
  for (std::int64_t y = 0; y < 8; ++y) {
    for (std::int64_t x = 0; x < 8; ++x) {
      if (x >= 4 || y >= 4) {
        world.template field<PassableTag>({x, y, 0}) = false;
      }
    }
  }

  const auto goal_a = tess::Coord3{0, 0, 0};
  const auto goal_b = tess::Coord3{1, 1, 0};
  const auto goal_c = tess::Coord3{2, 2, 0};
  tess::DistanceFieldScratch scratch;

  const auto measure = [&](tess::Coord3 goal) {
    tess::FieldProductCache probe(std::size_t{1} << 40U);
    tess::GoalSet goals;
    goals.add(goal);
    tess::DistanceFieldProduct one;
    tess::DistanceFieldScratch probe_scratch;
    EXPECT_EQ((tess::build_distance_field_product<World, PassableTag>(
                   world, goals, one, probe_scratch))
                  .status,
              tess::PathStatus::Found);
    EXPECT_NE((probe.store_reusing<World, PassableTag>(one)), nullptr);
    return probe.stats().bytes;
  };
  const auto size_a = measure(goal_a);
  const auto size_b_small = measure(goal_b);
  const auto size_c = measure(goal_c);

  // Exactly enough for the three walled products; the larger open-world
  // rebuild of goal_b must evict the LRU entry (goal_a, at index 0).
  tess::FieldProductCache cache(size_a + size_b_small + size_c);
  cache.reserve_entries(4);
  const auto build_and_store = [&](tess::Coord3 goal) {
    tess::GoalSet goals;
    goals.add(goal);
    tess::DistanceFieldProduct built;
    EXPECT_EQ((tess::build_distance_field_product<World, PassableTag>(
                   world, goals, built, scratch))
                  .status,
              tess::PathStatus::Found);
    return cache.store_reusing<World, PassableTag>(built);
  };
  ASSERT_NE(build_and_store(goal_a), nullptr);
  ASSERT_NE(build_and_store(goal_b), nullptr);
  ASSERT_NE(build_and_store(goal_c), nullptr);
  ASSERT_EQ(cache.stats().entries, 3u);
  ASSERT_EQ(cache.stats().evictions, 0u);

  // Open the world: the rebuild for goal_b now reaches all 4 chunks, so
  // its dependency list (and byte size) grows and the replace store must
  // evict goal_a's entry.
  fill_open(world);
  tess::GoalSet goals_b;
  goals_b.add(goal_b);
  tess::DistanceFieldProduct rebuilt;
  ASSERT_EQ((tess::build_distance_field_product<World, PassableTag>(
                 world, goals_b, rebuilt, scratch))
                .status,
            tess::PathStatus::Found);
  const auto* stored = cache.store_reusing<World, PassableTag>(rebuilt);

  ASSERT_EQ(cache.stats().evictions, 1u);
  ASSERT_EQ(cache.stats().entries, 2u);
  ASSERT_NE(stored, nullptr);
  ASSERT_EQ(stored->goals().size(), 1u);
  // The store must return the entry it stored (goal_b), not whichever
  // entry the post-eviction shift left at the stale index (goal_c).
  EXPECT_EQ(stored->goals()[0], goal_b);
}

// after an evicting store, the caller's product is not
// "displaced storage" but the evicted entry's complete contents: status
// Found, the other key's goals, and dependencies that pass is_valid. The
// rvalue store() wrapper documents its argument as "left moved-from
// (empty but reusable)", which this violates.
TEST(TessWeightedFieldProduct, EvictingStoreLeavesCallerWithValidWrongProduct) {
  World world;
  fill_open(world);
  tess::DistanceFieldScratch scratch;
  tess::DistanceFieldProduct product;

  const auto goal_a = tess::Coord3{7, 7, 0};
  const auto goal_b = tess::Coord3{0, 7, 0};

  const auto product_bytes = [&] {
    tess::FieldProductCache probe(std::size_t{1} << 40U);
    tess::GoalSet goals;
    goals.add(goal_a);
    tess::DistanceFieldProduct one;
    tess::DistanceFieldScratch probe_scratch;
    (void)tess::build_distance_field_product<World, PassableTag>(
        world, goals, one, probe_scratch);
    (void)probe.store_reusing<World, PassableTag>(one);
    return probe.stats().bytes;
  }();
  tess::FieldProductCache cache(product_bytes);

  tess::GoalSet goals;
  goals.add(goal_a);
  ASSERT_EQ((tess::build_distance_field_product<World, PassableTag>(
                 world, goals, product, scratch))
                .status,
            tess::PathStatus::Found);
  ASSERT_NE((cache.store_reusing<World, PassableTag>(product)), nullptr);

  goals.clear();
  goals.add(goal_b);
  ASSERT_EQ((tess::build_distance_field_product<World, PassableTag>(
                 world, goals, product, scratch))
                .status,
            tess::PathStatus::Found);
  // The rvalue wrapper shares store_with_key with store_reusing; use it to
  // show existing callers observe the same state.
  ASSERT_TRUE((cache.store<World, PassableTag>(std::move(product))));
  ASSERT_GT(cache.stats().evictions, 0u);

  // Inspecting the argument after the move is the POINT of this test: the
  // store documents the state it leaves behind, so that state is part of
  // the contract and something a caller may rely on. clang-tidy cannot
  // know that, and flags the read generically.
  //
  // Documented: left empty. Before the fix: the evicted goal_a product,
  // fully populated and passing every validity gate.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_NE(product.status(), tess::PathStatus::Found)
      << "argument still claims Found after the store";
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_TRUE(product.goals().empty())
      << "argument still carries the evicted entry's goals";
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_FALSE(product.is_valid(world))
      << "argument passes is_valid with the WRONG goal set: distance_at "
      << "now serves goal_a distances to a caller that stored goal_b";
}

// REVIEW PROOF TEST (OOB variant): with only TWO entries, the evicting
// replace store leaves the replaced entry at index 0 of a size-1 vector,
// but store_with_key still reads entries_[1] -- past the end. ASan
// container annotations flag this; without them it is silent UB.
TEST(TessWeightedFieldProduct, ReplaceStoreThatEvictsIndexesOutOfBounds) {
  World world;
  fill_open(world);
  for (std::int64_t y = 0; y < 8; ++y) {
    for (std::int64_t x = 0; x < 8; ++x) {
      if (x >= 4 || y >= 4) {
        world.template field<PassableTag>({x, y, 0}) = false;
      }
    }
  }
  const auto goal_a = tess::Coord3{0, 0, 0};
  const auto goal_b = tess::Coord3{1, 1, 0};
  tess::DistanceFieldScratch scratch;

  const auto measure = [&](tess::Coord3 goal) {
    tess::FieldProductCache probe(std::size_t{1} << 40U);
    tess::GoalSet goals;
    goals.add(goal);
    tess::DistanceFieldProduct one;
    tess::DistanceFieldScratch probe_scratch;
    EXPECT_EQ((tess::build_distance_field_product<World, PassableTag>(
                   world, goals, one, probe_scratch))
                  .status,
              tess::PathStatus::Found);
    EXPECT_NE((probe.store_reusing<World, PassableTag>(one)), nullptr);
    return probe.stats().bytes;
  };
  tess::FieldProductCache cache(measure(goal_a) + measure(goal_b));
  cache.reserve_entries(4);
  const auto build_and_store = [&](tess::Coord3 goal) {
    tess::GoalSet goals;
    goals.add(goal);
    tess::DistanceFieldProduct built;
    EXPECT_EQ((tess::build_distance_field_product<World, PassableTag>(
                   world, goals, built, scratch))
                  .status,
              tess::PathStatus::Found);
    return cache.store_reusing<World, PassableTag>(built);
  };
  ASSERT_NE(build_and_store(goal_a), nullptr);
  ASSERT_NE(build_and_store(goal_b), nullptr);

  fill_open(world);
  tess::GoalSet goals_b;
  goals_b.add(goal_b);
  tess::DistanceFieldProduct rebuilt;
  ASSERT_EQ((tess::build_distance_field_product<World, PassableTag>(
                 world, goals_b, rebuilt, scratch))
                .status,
            tess::PathStatus::Found);
  // Evicts goal_a's entry (index 0) and then reads entries_[1] of a
  // vector whose size is now 1.
  const auto* stored = cache.store_reusing<World, PassableTag>(rebuilt);
  EXPECT_EQ(cache.stats().entries, 1u);
  (void)stored;
}

}  // namespace
