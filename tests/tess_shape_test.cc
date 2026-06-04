#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstdint>
#include <limits>
#include <type_traits>

namespace {

using TopDown2D =
    tess::Shape<tess::Extent3{128, 64, 1}, tess::Extent3{32, 16, 1}>;
using Vertical2D =
    tess::Shape<tess::Extent3{1, 64, 32}, tess::Extent3{1, 16, 8}>;
using Chunked3D =
    tess::Shape<tess::Extent3{64, 64, 32}, tess::Extent3{16, 16, 8}>;
using SingleChunk = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{8, 8, 1}>;
using HugeBounded = tess::Shape<tess::Extent3{1ull << 34, 1ull << 33, 256},
                                tess::Extent3{32, 32, 4}>;

TEST(TessShape, DerivesTopDown2DTraits) {
  using Traits = tess::ShapeTraits<TopDown2D>;

  static_assert(Traits::size == tess::Extent3{128, 64, 1});
  static_assert(Traits::chunk == tess::Extent3{32, 16, 1});
  static_assert(Traits::chunk_count_x == 4);
  static_assert(Traits::chunk_count_y == 4);
  static_assert(Traits::chunk_count_z == 1);
  static_assert(Traits::chunk_count == 16);
  static_assert(Traits::local_tile_count == 512);
  static_assert(Traits::local_bits == 9);
  static_assert(Traits::chunk_bits == 4);
  static_assert(Traits::tile_key_bits == 13);
  static_assert(std::is_same_v<Traits::TileKeyStorage, std::uint64_t>);
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
  static_assert(Traits::local_bits == 11);
  static_assert(Traits::chunk_bits == 6);
  static_assert(Traits::tile_key_bits == 17);
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

TEST(TessShape, InfersHugeBoundedShapeKeyWidth) {
  using Traits = tess::ShapeTraits<HugeBounded>;

  static_assert(Traits::local_tile_count == 4096);
  static_assert(Traits::local_bits == 12);
  static_assert(Traits::chunk_bits == 63);
  static_assert(Traits::tile_key_bits == 75);
  static_assert(std::is_same_v<Traits::TileKeyStorage, unsigned __int128>);

  EXPECT_EQ(Traits::local_bits, 12);
  EXPECT_EQ(Traits::tile_key_bits, 75);
}

TEST(TessShape, ConvertsTopDownCoordinatesToChunkAndLocalCoordinates) {
  constexpr auto coord = tess::Coord3{63, 17, 0};

  static_assert(tess::chunk_coord<TopDown2D>(coord) ==
                tess::ChunkCoord3{1, 1, 0});
  static_assert(tess::local_coord<TopDown2D>(coord) ==
                tess::LocalCoord3{31, 1, 0});
  static_assert(tess::local_tile_id<TopDown2D>(tess::LocalCoord3{31, 1, 0}) ==
                tess::LocalTileId{63});

  EXPECT_EQ(tess::chunk_coord<TopDown2D>(coord), (tess::ChunkCoord3{1, 1, 0}));
  EXPECT_EQ(tess::local_coord<TopDown2D>(coord), (tess::LocalCoord3{31, 1, 0}));
}

TEST(TessShape, ConvertsVerticalCoordinatesToChunkAndLocalCoordinates) {
  constexpr auto coord = tess::Coord3{0, 31, 17};

  static_assert(tess::chunk_coord<Vertical2D>(coord) ==
                tess::ChunkCoord3{0, 1, 2});
  static_assert(tess::local_coord<Vertical2D>(coord) ==
                tess::LocalCoord3{0, 15, 1});
  static_assert(tess::coord<Vertical2D>(tess::ChunkCoord3{0, 1, 2},
                                        tess::LocalTileId{31}) == coord);

  EXPECT_EQ(tess::local_coord<Vertical2D>(coord),
            (tess::LocalCoord3{0, 15, 1}));
}

TEST(TessShape, RoundTripsChunkKeys) {
  constexpr auto chunk_coord = tess::ChunkCoord3{3, 2, 1};
  constexpr auto key = tess::chunk_key<Chunked3D>(chunk_coord);

  static_assert(key == tess::ChunkKey{27});
  static_assert(tess::chunk_coord<Chunked3D>(key) == chunk_coord);

  EXPECT_EQ(key, tess::ChunkKey{27});
  EXPECT_EQ(tess::chunk_coord<Chunked3D>(key), chunk_coord);
}

TEST(TessShape, RoundTripsTileKeys) {
  constexpr auto coord = tess::Coord3{47, 33, 17};
  constexpr auto tile = tess::tile_key<Chunked3D>(coord);

  static_assert(tess::chunk_key<Chunked3D>(tile) == tess::ChunkKey{42});
  static_assert(tess::local_tile_id<Chunked3D>(tile) == tess::LocalTileId{287});
  static_assert(tess::coord<Chunked3D>(tile) == coord);

  EXPECT_EQ(tess::coord<Chunked3D>(tile), coord);
}

TEST(TessShape, RepresentsSingleChunkTilesWithZeroChunkKey) {
  constexpr auto coord = tess::Coord3{7, 7, 0};
  constexpr auto tile = tess::tile_key<SingleChunk>(coord);

  static_assert(tess::chunk_key<SingleChunk>(tile) == tess::ChunkKey{0});
  static_assert(tess::local_tile_id<SingleChunk>(tile) ==
                tess::LocalTileId{63});
  static_assert(tess::coord<SingleChunk>(tile) == coord);

  EXPECT_EQ(tess::coord<SingleChunk>(tile), coord);
}

}  // namespace
