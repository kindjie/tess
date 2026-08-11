#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

#include "allocation_counter.h"
#include "path_test_util.h"

// Pins for the packed open-list node that carries every weighted search's
// heap ordering. The packed key must be order-isomorphic to the legacy
// three-field comparator (f asc, g desc, index asc): given that strict
// total order over live nodes, any correct heap pops the identical
// sequence, so the goldens below pin expansions/paths/costs across the
// representation change.

namespace {

using tess_test::SerpCostTag;
using tess_test::SerpPassableTag;
using tess_test::SerpSchema;
using tess_test::SerpTopDown2D;

template <typename Shape>
using SerpWorld = tess::AlwaysResidentWorld<Shape, SerpSchema>;

constexpr auto kU32Max = std::numeric_limits<std::uint32_t>::max();

using Legacy = tess::PathScratch::OpenNode;
using Packed = tess::detail::PackedOpenNode;

[[nodiscard]] auto pack(Legacy node) -> Packed {
  return Packed::make(node.index, node.g, node.f);
}

TEST(TessPackedOpenNode, ComparatorIsomorphicOnCornerTriples) {
  const std::uint32_t words[] = {0, 1, kU32Max - 1, kU32Max};
  const std::uint64_t indexes[] = {0, 1,
                                   std::numeric_limits<std::uint64_t>::max()};
  std::vector<Legacy> nodes;
  nodes.reserve(48);
  for (const auto f : words) {
    for (const auto g : words) {
      for (const auto index : indexes) {
        nodes.push_back(Legacy{index, g, f});
      }
    }
  }
  for (const auto& lhs : nodes) {
    for (const auto& rhs : nodes) {
      EXPECT_EQ(tess::detail::open_node_less(lhs, rhs),
                tess::detail::packed_open_node_less(pack(lhs), pack(rhs)))
          << "lhs f=" << lhs.f << " g=" << lhs.g << " i=" << lhs.index
          << " rhs f=" << rhs.f << " g=" << rhs.g << " i=" << rhs.index;
    }
  }
}

TEST(TessPackedOpenNode, ComparatorIsomorphicOnSeededRandomTriples) {
  auto state = std::uint64_t{0x9E3779B97F4A7C15ull};
  const auto next = [&state] {
    state ^= state << 13u;
    state ^= state >> 7u;
    state ^= state << 17u;
    return state;
  };
  std::vector<Legacy> nodes;
  nodes.reserve(512);
  for (int i = 0; i < 512; ++i) {
    // Narrow ranges force frequent f/g collisions so every tie level of
    // the comparator is exercised, not just the first compare.
    nodes.push_back(Legacy{next() % 8, static_cast<std::uint32_t>(next() % 8),
                           static_cast<std::uint32_t>(next() % 8)});
  }
  for (const auto& lhs : nodes) {
    for (const auto& rhs : nodes) {
      ASSERT_EQ(tess::detail::open_node_less(lhs, rhs),
                tess::detail::packed_open_node_less(pack(lhs), pack(rhs)));
    }
  }
}

TEST(TessPackedOpenNode, PackRoundTripsAllCornerValues) {
  const std::uint32_t words[] = {0, 1, kU32Max - 1, kU32Max};
  for (const auto f : words) {
    for (const auto g : words) {
      const auto node = Packed::make(42, g, f);
      EXPECT_EQ(node.g(), g);
      EXPECT_EQ(node.f(), f);
      EXPECT_EQ(node.index, 42u);
    }
  }
}

TEST(TessPackedOpenNode, DifferentialHeapsPopIdenticalSequences) {
  // Adversarial insertion sequence: same-index stale entries with
  // decreasing g (the relax pattern), equal-(f,g) ties broken by index,
  // and boundary words. Both heaps must pop the same node at every step.
  std::vector<Legacy> sequence;
  sequence.reserve(218);
  for (std::uint32_t g = 8; g > 0; --g) {
    sequence.push_back(Legacy{7, g, 10});  // stale chain on one index
  }
  for (std::uint64_t index = 0; index < 6; ++index) {
    sequence.push_back(Legacy{index, 3, 5});  // equal (f, g) tie block
  }
  sequence.push_back(Legacy{100, kU32Max - 1, kU32Max});
  sequence.push_back(Legacy{101, 0, 0});
  sequence.push_back(Legacy{102, kU32Max - 1, 0});
  sequence.push_back(Legacy{103, 0, kU32Max});
  auto state = std::uint64_t{0xD6E8FEB86659FD93ull};
  const auto next = [&state] {
    state ^= state << 13u;
    state ^= state >> 7u;
    state ^= state << 17u;
    return state;
  };
  for (int i = 0; i < 200; ++i) {
    sequence.push_back(Legacy{next() % 16,
                              static_cast<std::uint32_t>(next() % 4),
                              static_cast<std::uint32_t>(next() % 4)});
  }

  std::vector<Legacy> legacy_heap;
  std::vector<Packed> packed_heap;
  const auto pop_both_and_compare = [&] {
    std::pop_heap(legacy_heap.begin(), legacy_heap.end(),
                  tess::detail::open_node_less);
    const auto legacy = legacy_heap.back();
    legacy_heap.pop_back();
    std::pop_heap(packed_heap.begin(), packed_heap.end(),
                  tess::detail::packed_open_node_less);
    const auto packed = packed_heap.back();
    packed_heap.pop_back();
    ASSERT_EQ(packed.index, legacy.index);
    ASSERT_EQ(packed.g(), legacy.g);
    ASSERT_EQ(packed.f(), legacy.f);
  };

  // Interleave pushes with occasional pops, then drain.
  std::size_t pushed = 0;
  for (const auto& node : sequence) {
    legacy_heap.push_back(node);
    std::push_heap(legacy_heap.begin(), legacy_heap.end(),
                   tess::detail::open_node_less);
    packed_heap.push_back(pack(node));
    std::push_heap(packed_heap.begin(), packed_heap.end(),
                   tess::detail::packed_open_node_less);
    if (++pushed % 5 == 0) {
      pop_both_and_compare();
    }
  }
  while (!legacy_heap.empty()) {
    pop_both_and_compare();
  }
  EXPECT_TRUE(packed_heap.empty());
}

// --- Behavior goldens across the representation change -----------------
//
// Exact literals captured from the pre-change implementation on the
// serpentine fixture (which defeats every pre-search fast path, so these
// values come from the real heap loops). Any pop-order deviation moves
// them.

struct SerpFixture {
  SerpWorld<SerpTopDown2D> world;
  tess::Coord3 start;
  tess::Coord3 goal;
};

[[nodiscard]] auto make_fixture() -> SerpFixture {
  SerpFixture fx;
  const auto endpoints = tess_test::build_serpentine_topdown(fx.world);
  // Uniform costs are maximally tie-heavy: every equal-f decision falls
  // through to the g and index tie-breaks the goldens pin.
  tess_test::fill_cost(fx.world, 1);
  fx.start = endpoints.start;
  fx.goal = endpoints.goal;
  return fx;
}

TEST(TessPackedOpenNode, WeightedAStarSerpentineGolden) {
  auto fx = make_fixture();
  tess::PathScratch scratch;
  const auto result = tess::weighted_astar_path<decltype(fx.world),
                                                SerpPassableTag, SerpCostTag>(
      fx.world, tess::PathRequest{fx.start, fx.goal}, scratch);
  ASSERT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_TRUE(
      tess_test::valid_path_walk(fx.world, result.path, fx.start, fx.goal));
  EXPECT_EQ(result.cost, 24u);
  EXPECT_EQ(result.path.size(), 25u);
  EXPECT_EQ(result.expanded_nodes, 40u);
  EXPECT_EQ(result.reached_nodes, 50u);
}

TEST(TessPackedOpenNode, WeightedFloodSerpentineGolden) {
  auto fx = make_fixture();
  tess::DistanceFieldScratch scratch;
  const auto result =
      tess::build_weighted_distance_field<decltype(fx.world), SerpPassableTag,
                                          SerpCostTag>(fx.world, fx.goal,
                                                       scratch);
  ASSERT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(result.expanded_nodes, 52u);
  EXPECT_EQ(result.reached_nodes, 52u);
  const auto replay =
      tess::weighted_distance_field_path<decltype(fx.world), SerpPassableTag,
                                         SerpCostTag>(
          fx.world, {fx.start, fx.goal}, scratch);
  ASSERT_EQ(replay.status, tess::PathStatus::Found);
  EXPECT_EQ(replay.cost, 24u);
}

TEST(TessPackedOpenNode, BoxedFloodSerpentineGolden) {
  auto fx = make_fixture();
  tess::DistanceFieldScratch scratch;
  const auto result =
      tess::build_weighted_distance_field_in_box<decltype(fx.world),
                                                 SerpPassableTag, SerpCostTag>(
          fx.world, fx.goal,
          tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{8, 8, 1}}, scratch,
          tess::MissingChunkPolicy::TreatAsBlocked);
  ASSERT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(result.expanded_nodes, 52u);
  EXPECT_EQ(result.reached_nodes, 52u);
}

TEST(TessPackedOpenNode, WeightedGoalSetProductGolden) {
  auto fx = make_fixture();
  tess::GoalSet goals;
  goals.reserve(2);
  goals.add(fx.goal);
  goals.add(tess::Coord3{0, 7, 0});
  tess::DistanceFieldScratch scratch;
  tess::DistanceFieldProduct product;
  const auto built = tess::build_weighted_distance_field_product<
      decltype(fx.world),
      tess::movement::LegacyWeighted<SerpPassableTag, SerpCostTag>>(
      fx.world, goals, product, scratch);
  ASSERT_EQ(built.status, tess::PathStatus::Found);
  EXPECT_EQ(built.expanded_nodes, 52u);
  EXPECT_EQ(built.reached_nodes, 52u);
}

TEST(TessPackedOpenNode, StairProviderProductGolden) {
  // A special-transitions provider forces build_distance_field_product's
  // weighted-heap branch even under a unit movement class — the one
  // packed consumer no plain fixture reaches.
  struct StairTag {};
  using StairSchema = tess::FieldSchema<tess::Field<SerpPassableTag, bool>,
                                        tess::Field<SerpCostTag, std::uint32_t>,
                                        tess::Field<StairTag, std::uint8_t>>;
  using StairShape =
      tess::Shape<tess::Extent3{4, 4, 2}, tess::Extent3{4, 4, 2}>;
  using StairWorld = tess::AlwaysResidentWorld<StairShape, StairSchema>;
  StairWorld world;
  for (auto& page : world.chunks()) {
    std::fill(page.field_span<SerpPassableTag>().begin(),
              page.field_span<SerpPassableTag>().end(), true);
    std::fill(page.field_span<SerpCostTag>().begin(),
              page.field_span<SerpCostTag>().end(), 1u);
  }
  world.field<StairTag>(tess::Coord3{1, 1, 0}) =
      static_cast<std::uint8_t>(tess::StairDirection::PositiveX);
  const auto provider = tess::StairTransitions<StairTag>{};

  tess::GoalSet goals;
  goals.reserve(1);
  goals.add(tess::Coord3{2, 1, 1});
  tess::DistanceFieldScratch scratch;
  tess::DistanceFieldProduct product;
  const auto built =
      tess::build_distance_field_product<StairWorld, SerpPassableTag>(
          world, goals, product, scratch, provider);
  ASSERT_EQ(built.status, tess::PathStatus::Found);
  EXPECT_EQ(built.expanded_nodes, 32u);
  EXPECT_EQ(built.reached_nodes, 32u);
}

TEST(TessPackedOpenNode, WeightedEarlyExitLeavesScratchReusable) {
  // The goal return abandons live heap entries; the next search on the
  // same scratch must match a fresh-scratch run exactly.
  auto fx = make_fixture();
  tess::PathScratch reused;
  const auto first = tess::weighted_astar_path<decltype(fx.world),
                                               SerpPassableTag, SerpCostTag>(
      fx.world, tess::PathRequest{fx.start, fx.goal}, reused);
  ASSERT_EQ(first.status, tess::PathStatus::Found);

  // Interleave a unit search on the same scratch (shared open_ vector).
  const auto unit = tess::astar_path<decltype(fx.world), SerpPassableTag>(
      fx.world, tess::PathRequest{fx.start, fx.goal}, reused);
  ASSERT_EQ(unit.status, tess::PathStatus::Found);

  tess::PathScratch fresh;
  const auto expected = tess::weighted_astar_path<decltype(fx.world),
                                                  SerpPassableTag, SerpCostTag>(
      fx.world, tess::PathRequest{fx.goal, fx.start}, fresh);
  const auto second = tess::weighted_astar_path<decltype(fx.world),
                                                SerpPassableTag, SerpCostTag>(
      fx.world, tess::PathRequest{fx.goal, fx.start}, reused);
  ASSERT_EQ(second.status, expected.status);
  EXPECT_EQ(second.cost, expected.cost);
  EXPECT_EQ(second.expanded_nodes, expected.expanded_nodes);
  EXPECT_EQ(second.reached_nodes, expected.reached_nodes);
  ASSERT_EQ(second.path.size(), expected.path.size());
  for (std::size_t i = 0; i < second.path.size(); ++i) {
    EXPECT_EQ(second.path[i], expected.path[i]);
  }
}

TEST(TessPackedOpenNode, WarmWeightedSearchIsAllocationFree) {
  auto fx = make_fixture();
  tess::PathScratch scratch;
  scratch.reserve_nodes(tess::ShapeTraits<SerpTopDown2D>::chunk_count *
                        tess::ShapeTraits<SerpTopDown2D>::local_tile_count);
  auto warm = tess::weighted_astar_path<decltype(fx.world), SerpPassableTag,
                                        SerpCostTag>(
      fx.world, tess::PathRequest{fx.start, fx.goal}, scratch);
  ASSERT_EQ(warm.status, tess::PathStatus::Found);
  tess_test::ScopedAllocationCounter counter;
  const auto again = tess::weighted_astar_path<decltype(fx.world),
                                               SerpPassableTag, SerpCostTag>(
      fx.world, tess::PathRequest{fx.start, fx.goal}, scratch);
  ASSERT_EQ(again.status, tess::PathStatus::Found);
  EXPECT_EQ(counter.count(), 0u);
}

}  // namespace
