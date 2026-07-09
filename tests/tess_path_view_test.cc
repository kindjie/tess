#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstdint>
#include <type_traits>
#include <vector>

#include "allocation_counter.h"

namespace {

// A PathView must never be constructible from a temporary vector: it would
// dangle at the end of the full expression. Lvalue vectors are fine.
static_assert(
    std::is_constructible_v<tess::PathView, const std::vector<tess::Coord3>&>);
static_assert(
    !std::is_constructible_v<tess::PathView, std::vector<tess::Coord3>&&>);
static_assert(!std::is_constructible_v<tess::PathView,
                                       const std::vector<tess::Coord3>&&>);

struct PassableTag {};
using Schema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>>;
using Line = tess::Shape<tess::Extent3{16, 1, 1}, tess::Extent3{8, 1, 1}>;
using World = tess::AlwaysResidentWorld<Line, Schema>;

void fill_passable(World& world) {
  for (auto& page : world.chunks()) {
    for (auto& tile : page.template field_span<PassableTag>()) {
      tile = 1;
    }
  }
}

}  // namespace

TEST(TessPathView, DefaultIsEmpty) {
  tess::PathView view;
  EXPECT_TRUE(view.empty());
  EXPECT_EQ(view.size(), 0u);
  EXPECT_TRUE(view.span().empty());
}

TEST(TessPathView, MirrorsUnderlyingNodes) {
  const std::vector<tess::Coord3> nodes{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
  const tess::PathView view{nodes};

  ASSERT_EQ(view.size(), 3u);
  EXPECT_FALSE(view.empty());
  EXPECT_EQ(view.front(), (tess::Coord3{0, 0, 0}));
  EXPECT_EQ(view.back(), (tess::Coord3{2, 0, 0}));
  EXPECT_EQ(view[1], (tess::Coord3{1, 0, 0}));
  EXPECT_EQ(view.data(), nodes.data());  // non-owning: same storage

  std::size_t count = 0;
  for (const auto coord : view) {
    EXPECT_EQ(coord, nodes[count]);
    ++count;
  }
  EXPECT_EQ(count, 3u);

  const auto span = view.span();
  EXPECT_EQ(span.data(), nodes.data());
  EXPECT_EQ(span.size(), nodes.size());
}

TEST(TessPathView, SuffixIsTheRemainingPathAndSharesStorage) {
  const std::vector<tess::Coord3> nodes{
      {0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}};
  const tess::PathView view{nodes};

  const auto whole = view.suffix(0);
  EXPECT_EQ(whole.size(), 4u);
  EXPECT_EQ(whole.data(), nodes.data());

  const auto rest = view.suffix(1);
  ASSERT_EQ(rest.size(), 3u);
  EXPECT_EQ(rest.front(), (tess::Coord3{1, 0, 0}));
  EXPECT_EQ(rest.data(), nodes.data() + 1);  // shares storage, no copy

  // Suffix of a suffix composes.
  const auto rest2 = rest.suffix(2);
  ASSERT_EQ(rest2.size(), 1u);
  EXPECT_EQ(rest2.front(), (tess::Coord3{3, 0, 0}));
}

TEST(TessPathView, SuffixClampsPastTheEnd) {
  const std::vector<tess::Coord3> nodes{{0, 0, 0}, {1, 0, 0}};
  const tess::PathView view{nodes};

  EXPECT_TRUE(view.suffix(2).empty());  // exactly at the end
  EXPECT_TRUE(view.suffix(9).empty());  // past the end, clamped
  EXPECT_EQ(view.suffix(9).size(), 0u);
}

TEST(TessPathView, ViewAndSuffixOperationsAreAllocationFree) {
  const std::vector<tess::Coord3> nodes{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
  {
    tess_test::ScopedAllocationCounter counter;
    const tess::PathView view{nodes};
    const auto rest = view.suffix(1);
    EXPECT_EQ(view.size() + rest.size() + rest.span().size(), 7u);
    EXPECT_EQ(counter.count(), 0u);
    EXPECT_EQ(counter.bytes(), 0u);
  }
}

TEST(TessPathView, PathResultExposesAWalkableSuffix) {
  World world;
  fill_passable(world);

  tess::PathScratch scratch;
  const auto result = tess::astar_path<World, PassableTag>(
      world, tess::PathRequest{{0, 0, 0}, {15, 0, 0}}, scratch);
  ASSERT_EQ(result.status, tess::PathStatus::Found);
  ASSERT_FALSE(result.path.empty());
  EXPECT_EQ(result.path.front(), (tess::Coord3{0, 0, 0}));
  EXPECT_EQ(result.path.back(), (tess::Coord3{15, 0, 0}));

  // Walking one step: the suffix from index 1 drops the start and still ends
  // at the goal, sharing the scratch storage the result already views.
  const auto remaining = result.path.suffix(1);
  ASSERT_EQ(remaining.size(), result.path.size() - 1);
  EXPECT_EQ(remaining.front(), (tess::Coord3{1, 0, 0}));
  EXPECT_EQ(remaining.back(), (tess::Coord3{15, 0, 0}));
  EXPECT_EQ(remaining.data(), result.path.data() + 1);
}
