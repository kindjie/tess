#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstdint>
#include <limits>

namespace {

using TopDown2D =
    tess::Shape<tess::Extent3{128, 64, 1}, tess::Extent3{32, 16, 1}>;
using Vertical2D =
    tess::Shape<tess::Extent3{1, 64, 32}, tess::Extent3{1, 16, 8}>;
using Chunked3D =
    tess::Shape<tess::Extent3{64, 64, 32}, tess::Extent3{16, 16, 8}>;
using SingleChunk = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{8, 8, 1}>;

TEST(TessShape, DerivesTopDown2DTraits) {
  using Traits = tess::ShapeTraits<TopDown2D>;

  static_assert(Traits::size == tess::Extent3{128, 64, 1});
  static_assert(Traits::chunk == tess::Extent3{32, 16, 1});
  static_assert(Traits::chunk_count_x == 4);
  static_assert(Traits::chunk_count_y == 4);
  static_assert(Traits::chunk_count_z == 1);
  static_assert(Traits::chunk_count == 16);
  static_assert(Traits::local_tile_count == 512);
  static_assert(!Traits::single_chunk);
  static_assert(!Traits::degenerate_x);
  static_assert(!Traits::degenerate_y);
  static_assert(Traits::degenerate_z);

  EXPECT_EQ(Traits::chunk_count, 16);
  EXPECT_EQ(Traits::local_tile_count, 512);
}

TEST(TessShape, DerivesVertical2DTraits) {
  using Traits = tess::ShapeTraits<Vertical2D>;

  static_assert(Traits::chunk_count_x == 1);
  static_assert(Traits::chunk_count_y == 4);
  static_assert(Traits::chunk_count_z == 4);
  static_assert(Traits::chunk_count == 16);
  static_assert(Traits::local_tile_count == 128);
  static_assert(Traits::degenerate_x);
  static_assert(!Traits::degenerate_y);
  static_assert(!Traits::degenerate_z);

  EXPECT_EQ(Traits::chunk_count, 16);
}

TEST(TessShape, DerivesChunked3DTraits) {
  using Traits = tess::ShapeTraits<Chunked3D>;

  static_assert(Traits::chunk_count_x == 4);
  static_assert(Traits::chunk_count_y == 4);
  static_assert(Traits::chunk_count_z == 4);
  static_assert(Traits::chunk_count == 64);
  static_assert(Traits::local_tile_count == 2048);
  static_assert(!Traits::degenerate_x);
  static_assert(!Traits::degenerate_y);
  static_assert(!Traits::degenerate_z);

  EXPECT_EQ(Traits::chunk_count, 64);
}

TEST(TessShape, DetectsSingleChunkWorlds) {
  using Traits = tess::ShapeTraits<SingleChunk>;

  static_assert(Traits::single_chunk);
  static_assert(Traits::chunk_count == 1);

  EXPECT_TRUE(Traits::single_chunk);
}

TEST(TessShape, LowersCoord2ToCoord3) {
  static_assert(tess::to_coord3(tess::Coord2{2, 3}) == tess::Coord3{2, 3, 0});

  EXPECT_EQ(tess::to_coord3(tess::Coord2{2, 3}), (tess::Coord3{2, 3, 0}));
}

TEST(TessShape, ChecksBoxContainment) {
  constexpr tess::Box3 box{
      .origin = tess::Coord3{10, 20, 2},
      .extent = tess::Extent3{4, 5, 3},
  };

  static_assert(tess::contains(box, tess::Coord3{10, 20, 2}));
  static_assert(tess::contains(box, tess::Coord3{13, 24, 4}));
  static_assert(!tess::contains(box, tess::Coord3{14, 24, 4}));
  static_assert(!tess::contains(box, tess::Coord3{13, 25, 4}));
  static_assert(!tess::contains(box, tess::Coord3{13, 24, 5}));

  EXPECT_TRUE(tess::contains(box, tess::Coord3{10, 20, 2}));
  EXPECT_FALSE(tess::contains(box, tess::Coord3{14, 24, 4}));
}

TEST(TessShape, ChecksBoxContainmentAtSignedCoordinateLimits) {
  constexpr auto min = std::numeric_limits<std::int64_t>::min();
  constexpr auto max = std::numeric_limits<std::int64_t>::max();
  constexpr tess::Box3 box{
      .origin = tess::Coord3{min, 0, 0},
      .extent = tess::Extent3{std::numeric_limits<std::uint64_t>::max(), 1, 1},
  };

  static_assert(tess::contains(box, tess::Coord3{min, 0, 0}));
  static_assert(tess::contains(box, tess::Coord3{0, 0, 0}));
  static_assert(tess::contains(box, tess::Coord3{max - 1, 0, 0}));
  static_assert(!tess::contains(box, tess::Coord3{max, 0, 0}));

  EXPECT_TRUE(tess::contains(box, tess::Coord3{0, 0, 0}));
  EXPECT_FALSE(tess::contains(box, tess::Coord3{max, 0, 0}));
}

TEST(TessShape, ChecksShapeContainment) {
  static_assert(tess::contains<Chunked3D>(tess::Coord3{0, 0, 0}));
  static_assert(tess::contains<Chunked3D>(tess::Coord3{63, 63, 31}));
  static_assert(!tess::contains<Chunked3D>(tess::Coord3{-1, 0, 0}));
  static_assert(!tess::contains<Chunked3D>(tess::Coord3{64, 0, 0}));
  static_assert(!tess::contains<Chunked3D>(tess::Coord3{0, 64, 0}));
  static_assert(!tess::contains<Chunked3D>(tess::Coord3{0, 0, 32}));

  EXPECT_TRUE(tess::contains<Chunked3D>(tess::Coord3{63, 63, 31}));
  EXPECT_FALSE(tess::contains<Chunked3D>(tess::Coord3{64, 0, 0}));
}

}  // namespace
