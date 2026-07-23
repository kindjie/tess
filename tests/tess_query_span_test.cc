#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstdint>
#include <limits>
#include <vector>

#include "allocation_counter.h"

namespace {

using TopDown2D = tess::Shape<tess::Extent3{16, 16, 1}, tess::Extent3{8, 8, 1}>;
using Vertical2D = tess::Shape<tess::Extent3{1, 16, 8}, tess::Extent3{1, 8, 4}>;
using Chunked3D = tess::Shape<tess::Extent3{16, 16, 8}, tess::Extent3{8, 8, 4}>;
using Wide =
    tess::Shape<tess::Extent3{1ull << 33, 1, 1}, tess::Extent3{32, 1, 1}>;

auto expand(tess::TileSpan span, std::vector<tess::Coord3>& output) -> void {
  for (std::uint32_t x = 0; x < span.x_count; ++x) {
    output.push_back(
        tess::Coord3{span.origin.x + x, span.origin.y, span.origin.z});
  }
}

struct QueryDigest {
  std::uint64_t count = 0;
  std::uint64_t hash = 1469598103934665603ull;

  void add(tess::Coord3 coord) {
    ++count;
    hash ^= static_cast<std::uint64_t>(coord.x);
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(coord.y);
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(coord.z);
    hash *= 1099511628211ull;
  }
};

void digest(tess::TileSpan span, QueryDigest& output) {
  for (std::uint32_t x = 0; x < span.x_count; ++x) {
    output.add({span.origin.x + x, span.origin.y, span.origin.z});
  }
}

template <typename Shape, typename Predicate>
auto reference_digest(Predicate&& predicate) -> QueryDigest {
  QueryDigest result;
  const auto size = tess::ShapeTraits<Shape>::size;
  for (std::uint64_t z = 0; z < size.z; ++z) {
    for (std::uint64_t y = 0; y < size.y; ++y) {
      for (std::uint64_t x = 0; x < size.x; ++x) {
        const auto coord = tess::Coord3{static_cast<std::int64_t>(x),
                                        static_cast<std::int64_t>(y),
                                        static_cast<std::int64_t>(z)};
        if (predicate(coord)) {
          result.add(coord);
        }
      }
    }
  }
  return result;
}

struct Random {
  std::uint64_t state = 0x9e3779b97f4a7c15ull;

  auto next() -> std::uint32_t {
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<std::uint32_t>(state >> 32u);
  }

  auto signed_between(std::int32_t low, std::int32_t high) -> std::int64_t {
    return low + static_cast<std::int64_t>(
                     next() % static_cast<std::uint32_t>(high - low + 1));
  }
};

template <typename Shape>
void check_random_query(Random& random, bool radius_query) {
  QueryDigest actual;
  if (radius_query) {
    const auto center = tess::Coord3{random.signed_between(-6, 21),
                                     random.signed_between(-6, 21),
                                     random.signed_between(-3, 10)};
    const auto radius = random.next() % 7u;
    (void)tess::for_each_radius_span<Shape>(
        center, radius, [&](tess::TileSpan span) { digest(span, actual); });
    const auto squared = static_cast<std::uint64_t>(radius) * radius;
    const auto expected = reference_digest<Shape>([&](tess::Coord3 coord) {
      const auto dx = tess::detail::abs_delta(coord.x, center.x);
      const auto dy = tess::detail::abs_delta(coord.y, center.y);
      const auto dz = tess::detail::abs_delta(coord.z, center.z);
      return dx * dx + dy * dy + dz * dz <= squared;
    });
    EXPECT_EQ(actual.count, expected.count);
    EXPECT_EQ(actual.hash, expected.hash);
    return;
  }

  const auto box = tess::Box3{
      {random.signed_between(-6, 21), random.signed_between(-6, 21),
       random.signed_between(-3, 10)},
      {random.next() % 12u, random.next() % 12u, random.next() % 8u},
  };
  (void)tess::for_each_box_span<Shape>(
      box, [&](tess::TileSpan span) { digest(span, actual); });
  const auto expected = reference_digest<Shape>(
      [&](tess::Coord3 coord) { return tess::contains(box, coord); });
  EXPECT_EQ(actual.count, expected.count);
  EXPECT_EQ(actual.hash, expected.hash);
}

TEST(TessQuerySpan, BoxClipsToShapeAndEmitsDeterministicRows) {
  std::vector<tess::TileSpan> spans;
  const auto stats = tess::for_each_box_span<TopDown2D>(
      tess::Box3{{-2, 14, -1}, {6, 4, 3}},
      [&](tess::TileSpan span) { spans.push_back(span); });

  ASSERT_EQ(spans.size(), 2u);
  EXPECT_EQ(spans[0], (tess::TileSpan{{0, 14, 0}, 4}));
  EXPECT_EQ(spans[1], (tess::TileSpan{{0, 15, 0}, 4}));
  EXPECT_EQ(stats.spans, 2u);
  EXPECT_EQ(stats.tiles, 8u);
}

TEST(TessQuerySpan, BoxUsesZThenYThenXOrderInThreeDimensions) {
  std::vector<tess::TileSpan> spans;
  (void)tess::for_each_box_span<Chunked3D>(
      tess::Box3{{2, 3, 4}, {3, 2, 2}},
      [&](tess::TileSpan span) { spans.push_back(span); });

  ASSERT_EQ(spans.size(), 4u);
  EXPECT_EQ(spans[0], (tess::TileSpan{{2, 3, 4}, 3}));
  EXPECT_EQ(spans[1], (tess::TileSpan{{2, 4, 4}, 3}));
  EXPECT_EQ(spans[2], (tess::TileSpan{{2, 3, 5}, 3}));
  EXPECT_EQ(spans[3], (tess::TileSpan{{2, 4, 5}, 3}));
}

TEST(TessQuerySpan, VerticalShapeClipsDegenerateWorldAxis) {
  std::vector<tess::TileSpan> spans;
  (void)tess::for_each_box_span<Vertical2D>(
      tess::Box3{{-5, 6, 2}, {12, 2, 3}},
      [&](tess::TileSpan span) { spans.push_back(span); });

  ASSERT_EQ(spans.size(), 6u);
  for (const auto span : spans) {
    EXPECT_EQ(span.origin.x, 0);
    EXPECT_EQ(span.x_count, 1u);
  }
  EXPECT_EQ(spans.front().origin, (tess::Coord3{0, 6, 2}));
  EXPECT_EQ(spans.back().origin, (tess::Coord3{0, 7, 4}));
}

TEST(TessQuerySpan, RadiusMatchesEuclideanReferenceTileSet) {
  std::vector<tess::Coord3> actual;
  const auto stats = tess::for_each_radius_span<TopDown2D>(
      {4, 4, 0}, 2, [&](tess::TileSpan span) { expand(span, actual); });

  std::vector<tess::Coord3> expected;
  for (std::int64_t y = 0; y < 16; ++y) {
    for (std::int64_t x = 0; x < 16; ++x) {
      const auto dx = x - 4;
      const auto dy = y - 4;
      if (dx * dx + dy * dy <= 4) {
        expected.push_back({x, y, 0});
      }
    }
  }
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(stats.tiles, 13u);
}

TEST(TessQuerySpan, RadiusClipsAtWorldEdges) {
  std::vector<tess::Coord3> actual;
  (void)tess::for_each_radius_span<TopDown2D>(
      {0, 0, 0}, 2, [&](tess::TileSpan span) { expand(span, actual); });
  EXPECT_EQ(actual.size(), 6u);
  for (const auto coord : actual) {
    EXPECT_TRUE(tess::contains(tess::shape_bounds<TopDown2D>(), coord));
  }
}

TEST(TessQuerySpan, ChunkSpanClipsLocalBoxAndReturnsWorldCoordinates) {
  std::vector<tess::TileSpan> spans;
  const auto stats = tess::for_each_chunk_span<TopDown2D>(
      tess::ChunkKey{3}, tess::Box3{{-2, 6, 0}, {6, 5, 1}},
      [&](tess::TileSpan span) { spans.push_back(span); });

  ASSERT_EQ(spans.size(), 2u);
  EXPECT_EQ(spans[0], (tess::TileSpan{{8, 14, 0}, 4}));
  EXPECT_EQ(spans[1], (tess::TileSpan{{8, 15, 0}, 4}));
  EXPECT_EQ(stats.tiles, 8u);
}

TEST(TessQuerySpan, EmptyBoxEmitsNothing) {
  std::size_t calls = 0;
  const auto stats = tess::for_each_box_span<TopDown2D>(
      tess::Box3{{0, 0, 0}, {0, 4, 1}}, [&](tess::TileSpan) { ++calls; });
  EXPECT_EQ(calls, 0u);
  EXPECT_EQ(stats.spans, 0u);
  EXPECT_EQ(stats.tiles, 0u);
}

TEST(TessQuerySpan, WiderThanUint32RunSplitsWithoutDroppingTiles) {
  std::vector<tess::TileSpan> spans;
  constexpr auto max = std::numeric_limits<std::uint32_t>::max();
  const auto stats = tess::for_each_box_span<Wide>(
      tess::Box3{{0, 0, 0}, {static_cast<std::uint64_t>(max) + 2, 1, 1}},
      [&](tess::TileSpan span) { spans.push_back(span); });

  ASSERT_EQ(spans.size(), 2u);
  EXPECT_EQ(spans[0], (tess::TileSpan{{0, 0, 0}, max}));
  EXPECT_EQ(spans[1],
            (tess::TileSpan{{static_cast<std::int64_t>(max), 0, 0}, 2}));
  EXPECT_EQ(stats.tiles, static_cast<std::uint64_t>(max) + 2);
}

TEST(TessQuerySpan, WarmEmissionDoesNotAllocate) {
  std::uint64_t checksum = 0;
  {
    tess_test::ScopedAllocationCounter counter;
    for (int i = 0; i < 32; ++i) {
      const auto stats = tess::for_each_radius_span<Chunked3D>(
          {8, 8, 4}, 4, [&](tess::TileSpan span) {
            checksum +=
                static_cast<std::uint64_t>(span.origin.x) + span.x_count;
          });
      checksum += stats.tiles;
    }
    EXPECT_EQ(counter.count(), 0u);
  }
  EXPECT_GT(checksum, 0u);
}

TEST(TessQuerySpan, RandomizedQueriesMatchReferenceAcrossAllMvpLayouts) {
  Random random;
  for (std::uint32_t i = 0; i < 100000; ++i) {
    const auto radius_query = (i & 1u) != 0;
    switch (i % 3u) {
      case 0:
        check_random_query<TopDown2D>(random, radius_query);
        break;
      case 1:
        check_random_query<Vertical2D>(random, radius_query);
        break;
      case 2:
        check_random_query<Chunked3D>(random, radius_query);
        break;
    }
  }
}

}  // namespace
