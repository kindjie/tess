#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstdint>
#include <limits>
#include <type_traits>

#include "allocation_counter.h"

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
// Largest legal single-chunk shape: chunk dims must be powers of two and the
// local tile count must fit std::uint64_t, so the maximum local tile count is
// 2^63 (local_bits == 63, chunk_bits == 0, tile keys in std::uint64_t). A
// 64-bit local tile count (for example 2^64 - 1 via 4294967295 x 4294967297)
// is unrepresentable because such chunk dims are not powers of two.
using MaxSingleChunk = tess::Shape<tess::Extent3{1ull << 31, 1ull << 31, 2},
                                   tess::Extent3{1ull << 31, 1ull << 31, 2}>;

TEST(TessShape, PublicValueTypesDefaultInitialize) {
  static_assert(tess::Extent3{} == tess::Extent3{0, 0, 1});
  static_assert(tess::Coord2{} == tess::Coord2{0, 0});
  static_assert(tess::Coord3{} == tess::Coord3{0, 0, 0});
  static_assert(tess::ChunkCoord3{} == tess::ChunkCoord3{0, 0, 0});
  static_assert(tess::LocalCoord3{} == tess::LocalCoord3{0, 0, 0});
  static_assert(tess::LocalTileId{} == tess::LocalTileId{0});
  static_assert(tess::ChunkKey{} == tess::ChunkKey{0});
  static_assert(tess::Box3{} ==
                tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{0, 0, 1}});
  static_assert(
      tess::ResolvedTile<TopDown2D>{} ==
      tess::ResolvedTile<TopDown2D>{tess::ChunkKey{0}, tess::LocalTileId{0}});
  static_assert(tess::TileKey<TopDown2D>{} == tess::TileKey<TopDown2D>{0});

  EXPECT_EQ(tess::Extent3{}, (tess::Extent3{0, 0, 1}));
}

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
  static_assert(std::is_same_v<Traits::TileKeyStorage, tess::detail::UInt128>);

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

TEST(TessShape, KeyConversionsAreNoexcept) {
  constexpr auto coord = tess::Coord3{47, 33, 17};
  constexpr auto chunk_coord = tess::ChunkCoord3{2, 2, 2};
  constexpr auto local_coord = tess::LocalCoord3{15, 1, 1};
  constexpr auto chunk_key = tess::ChunkKey{42};
  constexpr auto local_tile_id = tess::LocalTileId{287};
  constexpr auto tile = tess::tile_key<Chunked3D>(coord);

  static_assert(noexcept(tess::chunk_coord<Chunked3D>(coord)));
  static_assert(noexcept(tess::local_coord<Chunked3D>(coord)));
  static_assert(noexcept(tess::local_tile_id<Chunked3D>(local_coord)));
  static_assert(noexcept(tess::coord<Chunked3D>(chunk_coord, local_tile_id)));
  static_assert(noexcept(tess::chunk_key<Chunked3D>(chunk_coord)));
  static_assert(noexcept(tess::chunk_coord<Chunked3D>(chunk_key)));
  static_assert(noexcept(tess::tile_key<Chunked3D>(coord)));
  static_assert(noexcept(tess::chunk_key<Chunked3D>(tile)));
  static_assert(noexcept(tess::local_tile_id<Chunked3D>(tile)));
  static_assert(noexcept(tess::coord<Chunked3D>(tile)));
}

TEST(TessShape, PublicValueHelpersAreNoexcept) {
  constexpr auto coord2 = tess::Coord2{2, 3};
  constexpr auto coord3 = tess::Coord3{2, 3, 0};
  constexpr auto box = tess::Box3{
      .origin = tess::Coord3{0, 0, 0},
      .extent = tess::Extent3{8, 8, 1},
  };

  static_assert(noexcept(tess::Extent3{1, 1, 1} == tess::Extent3{1, 1, 1}));
  static_assert(noexcept(tess::Coord2{1, 2} == tess::Coord2{1, 2}));
  static_assert(noexcept(tess::Coord3{1, 2, 3} == tess::Coord3{1, 2, 3}));
  static_assert(
      noexcept(tess::ChunkCoord3{1, 2, 3} == tess::ChunkCoord3{1, 2, 3}));
  static_assert(
      noexcept(tess::LocalCoord3{1, 2, 3} == tess::LocalCoord3{1, 2, 3}));
  static_assert(noexcept(tess::LocalTileId{1} == tess::LocalTileId{1}));
  static_assert(noexcept(tess::ChunkKey{1} == tess::ChunkKey{1}));
  static_assert(noexcept(box == box));
  static_assert(
      noexcept(tess::TileKey<Chunked3D>{1} == tess::TileKey<Chunked3D>{1}));
  static_assert(noexcept(tess::ResolvedTile<Chunked3D>{
                             tess::ChunkKey{1},
                             tess::LocalTileId{2},
                         } == tess::ResolvedTile<Chunked3D>{
                                  tess::ChunkKey{1},
                                  tess::LocalTileId{2},
                              }));
  static_assert(noexcept(tess::to_coord3(coord2)));
  static_assert(noexcept(tess::contains(box, coord3)));
  static_assert(noexcept(tess::contains<Chunked3D>(coord3)));

  EXPECT_EQ(tess::to_coord3(coord2), coord3);
  EXPECT_TRUE(tess::contains(box, coord3));
}

TEST(TessShape, KeyConversionsDoNotAllocate) {
  auto coord = tess::Coord3{47, 33, 17};
  auto observed = std::uint64_t{0};

  tess_test::ScopedAllocationCounter counter;
  for (auto i = 0; i < 1024; ++i) {
    const auto tile = tess::tile_key<Chunked3D>(coord);
    const auto decoded = tess::coord<Chunked3D>(tile);
    observed += tess::chunk_key<Chunked3D>(tile).value;
    observed += tess::local_tile_id<Chunked3D>(tile).value;
    coord.x = (decoded.x + 1) % 64;
    coord.y = (decoded.y + 3) % 64;
    coord.z = (decoded.z + 5) % 32;
  }

  EXPECT_GT(observed, 0u);
  EXPECT_EQ(counter.count(), 0u);
}

TEST(TessUInt128, MultipliesWithCarriesAcrossThe64BitBoundary) {
  using tess::detail::UInt128;
  constexpr auto max64 = std::numeric_limits<std::uint64_t>::max();

  static_assert(UInt128{max64} * UInt128{max64} ==
                UInt128::from_parts(0xfffffffffffffffeULL, 1));
  static_assert(UInt128{1ull << 32} * UInt128{1ull << 32} ==
                UInt128::from_parts(1, 0));
  static_assert(UInt128::from_parts(1, 2) * UInt128::from_parts(3, 4) ==
                UInt128::from_parts(10, 8));
  static_assert(UInt128{3} * UInt128{5} == UInt128{15});

  EXPECT_EQ(UInt128{max64} * UInt128{max64},
            UInt128::from_parts(0xfffffffffffffffeULL, 1));
  EXPECT_EQ(UInt128{1ull << 32} * UInt128{1ull << 32},
            UInt128::from_parts(1, 0));
}

TEST(TessUInt128, NonNegativeIntConstructionMatchesUnsigned) {
  using tess::detail::UInt128;
  static_assert(UInt128{0} == UInt128{0u});
  static_assert(UInt128{42} == UInt128{42u});
}

#if TESS_ENABLE_ASSERTS
TEST(TessUInt128DeathTest, NegativeIntConstructionAsserts) {
  // The implicit int constructor documents a non-negative precondition
  // (a negative count is always a caller bug, not a wrapped huge value).
  int value = -1;
  EXPECT_DEATH(static_cast<void>(tess::detail::UInt128{value}),
               "tess assertion failed");
}
#endif  // TESS_ENABLE_ASSERTS

TEST(TessUInt128, ShiftsAcrossThe64BitBoundary) {
  using tess::detail::UInt128;

  static_assert((UInt128{1} << 0) == UInt128{1});
  static_assert((UInt128{1} << 63) == UInt128{1ull << 63});
  static_assert((UInt128{1} << 64) == UInt128::from_parts(1, 0));
  static_assert((UInt128{0xff} << 60) ==
                UInt128::from_parts(0xf, 0xf000000000000000ULL));
  static_assert((UInt128{1} << 127) == UInt128::from_parts(1ull << 63, 0));
  static_assert((UInt128{1} << 128) == UInt128{0});
  static_assert((UInt128{1} << 200) == UInt128{0});

  static_assert((UInt128::from_parts(1, 0) >> 0) == UInt128::from_parts(1, 0));
  static_assert((UInt128::from_parts(1, 0) >> 64) == UInt128{1});
  static_assert((UInt128::from_parts(0xf, 0xf000000000000000ULL) >> 60) ==
                UInt128{0xff});
  static_assert((UInt128::from_parts(1ull << 63, 0) >> 127) == UInt128{1});
  static_assert((UInt128::from_parts(1ull << 63, 0) >> 128) == UInt128{0});
  static_assert((UInt128::from_parts(1ull << 63, 0) >> 200) == UInt128{0});

  auto value = UInt128{1};
  value <<= 100;
  EXPECT_EQ(value, UInt128::from_parts(1ull << 36, 0));
  value >>= 100;
  EXPECT_EQ(value, UInt128{1});
}

TEST(TessUInt128, SubtractsBorrowsComparesAndNarrows) {
  using tess::detail::UInt128;
  constexpr auto max64 = std::numeric_limits<std::uint64_t>::max();

  static_assert(UInt128::from_parts(1, 0) - UInt128{1} ==
                UInt128::from_parts(0, max64));
  static_assert(UInt128::from_parts(5, 10) - UInt128::from_parts(2, 3) ==
                UInt128::from_parts(3, 7));

  static_assert(UInt128::from_parts(1, 0) > UInt128::from_parts(0, max64));
  static_assert(UInt128{2} < UInt128::from_parts(0, 3));
  static_assert(UInt128{7} == UInt128{7});
  static_assert(UInt128{7} <= 7);
  static_assert(UInt128::from_parts(0, 2) - 1 <= 1);

  static_assert(
      (UInt128::from_parts(0xf0, 0x0f) & UInt128::from_parts(0x10, 0x0e)) ==
      UInt128::from_parts(0x10, 0x0e));
  static_assert(
      (UInt128::from_parts(0xf0, 0x0f) | UInt128::from_parts(0x01, 0x30)) ==
      UInt128::from_parts(0xf1, 0x3f));

  static_assert(static_cast<std::uint64_t>(UInt128::from_parts(123, 456)) ==
                456);

  EXPECT_EQ(UInt128::from_parts(1, 0) - UInt128{1},
            UInt128::from_parts(0, max64));
  EXPECT_GT(UInt128::from_parts(1, 0), UInt128::from_parts(0, max64));
  EXPECT_EQ(static_cast<std::uint64_t>(UInt128::from_parts(123, 456)), 456u);
}

TEST(TessUInt128, MeasuresBitWidthAtThe64BitBoundary) {
  using tess::detail::UInt128;

  static_assert(tess::detail::bit_width(UInt128{0}) == 0);
  static_assert(tess::detail::bit_width(UInt128{1}) == 1);
  static_assert(tess::detail::bit_width(UInt128::from_parts(1, 0)) == 65);
  static_assert(tess::detail::bits_for_count(UInt128::from_parts(1, 0)) == 64);
  static_assert(tess::detail::bits_for_count(UInt128{1ull << 63}) == 63);
  static_assert(tess::detail::bits_for_count(
                    UInt128::from_parts(0, (1ull << 63) + 1)) == 64);

  EXPECT_EQ(tess::detail::bit_width(UInt128::from_parts(1, 0)), 65u);
  EXPECT_EQ(tess::detail::bits_for_count(UInt128::from_parts(1, 0)), 64u);
}

TEST(TessShape, RoundTripsHugeBoundedTileKeys) {
  using Traits = tess::ShapeTraits<HugeBounded>;
  constexpr auto max = tess::Coord3{
      static_cast<std::int64_t>(Traits::size.x) - 1,
      static_cast<std::int64_t>(Traits::size.y) - 1,
      static_cast<std::int64_t>(Traits::size.z) - 1,
  };
  constexpr tess::Coord3 corners[] = {
      tess::Coord3{0, 0, 0},
      max,
      tess::Coord3{max.x, 0, 0},
      tess::Coord3{0, max.y, max.z},
      tess::Coord3{123456789, 87654321, 200},
  };

  for (const auto coord : corners) {
    const auto tile = tess::tile_key<HugeBounded>(coord);
    EXPECT_EQ(
        tess::chunk_key<HugeBounded>(tile),
        tess::chunk_key<HugeBounded>(tess::chunk_coord<HugeBounded>(coord)));
    EXPECT_EQ(tess::local_tile_id<HugeBounded>(tile),
              tess::local_tile_id<HugeBounded>(
                  tess::local_coord<HugeBounded>(coord)));
    EXPECT_EQ(tess::coord<HugeBounded>(tile), coord);
  }

  constexpr auto tile = tess::tile_key<HugeBounded>(max);
  static_assert(tess::coord<HugeBounded>(tile) == max);
  static_assert(
      tess::chunk_key<HugeBounded>(tile) ==
      tess::chunk_key<HugeBounded>(tess::chunk_coord<HugeBounded>(max)));
  static_assert(
      tess::local_tile_id<HugeBounded>(tile) ==
      tess::local_tile_id<HugeBounded>(tess::local_coord<HugeBounded>(max)));
}

TEST(TessShape, RoundTripsMaxSingleChunkTileKeysWithoutWideShifts) {
  using Traits = tess::ShapeTraits<MaxSingleChunk>;

  static_assert(Traits::single_chunk);
  static_assert(Traits::chunk_bits == 0);
  static_assert(Traits::local_bits == 63);
  static_assert(Traits::tile_key_bits == 63);
  static_assert(std::is_same_v<Traits::TileKeyStorage, std::uint64_t>);
  static_assert(Traits::precise_local_tile_count ==
                tess::detail::UInt128{1ull << 63});

  constexpr auto max = tess::Coord3{(1ll << 31) - 1, (1ll << 31) - 1, 1};
  constexpr tess::Coord3 corners[] = {
      tess::Coord3{0, 0, 0},         max,
      tess::Coord3{max.x, 0, 1},     tess::Coord3{0, max.y, 0},
      tess::Coord3{12345, 67890, 1},
  };

  for (const auto coord : corners) {
    const auto tile = tess::tile_key<MaxSingleChunk>(coord);
    EXPECT_EQ(tess::chunk_key<MaxSingleChunk>(tile), tess::ChunkKey{0});
    EXPECT_EQ(tess::local_tile_id<MaxSingleChunk>(tile),
              tess::local_tile_id<MaxSingleChunk>(
                  tess::local_coord<MaxSingleChunk>(coord)));
    EXPECT_EQ(tess::coord<MaxSingleChunk>(tile), coord);
  }

  constexpr auto tile = tess::tile_key<MaxSingleChunk>(max);
  static_assert(tess::chunk_key<MaxSingleChunk>(tile) == tess::ChunkKey{0});
  static_assert(tess::local_tile_id<MaxSingleChunk>(tile).value ==
                (1ull << 63) - 1);
  static_assert(tess::coord<MaxSingleChunk>(tile) == max);
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
