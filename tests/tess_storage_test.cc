#include <gtest/gtest.h>
#include <tess/tess.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <span>
#include <type_traits>

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

struct TerrainTag {};
struct CostTag {};
struct RegionTag {};

using TopDown2D =
    tess::Shape<tess::Extent3{128, 64, 1}, tess::Extent3{32, 16, 1}>;
using Vertical2D =
    tess::Shape<tess::Extent3{1, 64, 32}, tess::Extent3{1, 16, 8}>;
using Chunked3D =
    tess::Shape<tess::Extent3{64, 64, 32}, tess::Extent3{16, 16, 8}>;

using TerrainField = tess::Field<TerrainTag, std::uint16_t>;
using CostField = tess::Field<CostTag, float>;
using RegionField = tess::Field<RegionTag, std::uint32_t>;
using Schema = tess::FieldSchema<TerrainField, CostField, RegionField>;

template <typename Shape>
using Page = tess::ChunkPage<Shape, Schema>;

TEST(TessStorage, FieldSchemaAcceptsDistinctTagsAndFindsValueTypes) {
  static_assert(tess::is_valid_field_schema_v<TerrainField, CostField>);
  static_assert(
      !tess::is_valid_field_schema_v<TerrainField,
                                     tess::Field<TerrainTag, std::uint8_t>>);
  static_assert(Schema::field_count == 3);
  static_assert(Schema::contains<TerrainTag>);
  static_assert(std::is_same_v<Schema::value_type<TerrainTag>, std::uint16_t>);
  static_assert(std::is_same_v<Schema::value_type<CostTag>, float>);
  static_assert(std::is_same_v<Schema::value_type<RegionTag>, std::uint32_t>);

  EXPECT_EQ(Schema::field_count, 3u);
}

TEST(TessStorage, TopDown2DPageExposesContiguousTypedSpans) {
  Page<TopDown2D> page{tess::ChunkKey{5}, tess::ChunkCoord3{1, 1, 0}};
  auto terrain = page.field_span<TerrainTag>();

  static_assert(Page<TopDown2D>::local_tile_count == 512);
  static_assert(Page<TopDown2D>::field_count == 3);
  static_assert(std::is_same_v<decltype(terrain), std::span<std::uint16_t>>);

  ASSERT_EQ(terrain.size(), Page<TopDown2D>::local_tile_count);
  terrain[0] = 7;
  terrain[1] = 9;

  EXPECT_EQ(&terrain[1], &terrain[0] + 1);
  EXPECT_EQ(page.field<TerrainTag>(tess::LocalTileId{0}), 7);
  EXPECT_EQ(page.field<TerrainTag>(tess::LocalTileId{1}), 9);
}

TEST(TessStorage, Vertical2DPageWritesThroughFieldAccessAndReadsSpan) {
  Page<Vertical2D> page{tess::ChunkKey{6}, tess::ChunkCoord3{0, 2, 1}};

  static_assert(Page<Vertical2D>::local_tile_count == 128);

  page.field<CostTag>(tess::LocalTileId{31}) = 4.5F;
  auto costs = page.field_span<CostTag>();

  ASSERT_EQ(costs.size(), Page<Vertical2D>::local_tile_count);
  EXPECT_FLOAT_EQ(costs[31], 4.5F);
}

TEST(TessStorage, Chunked3DPageKeepsFieldsIndependent) {
  Page<Chunked3D> page{tess::ChunkKey{27}, tess::ChunkCoord3{3, 2, 1}};

  static_assert(Page<Chunked3D>::local_tile_count == 2048);

  page.field<TerrainTag>(tess::LocalTileId{10}) = 3;
  page.field<RegionTag>(tess::LocalTileId{10}) = 99;

  auto terrain = page.field_span<TerrainTag>();
  auto regions = page.field_span<RegionTag>();

  EXPECT_NE(static_cast<const void*>(terrain.data()),
            static_cast<const void*>(regions.data()));
  EXPECT_EQ(terrain[10], 3);
  EXPECT_EQ(regions[10], 99u);
}

TEST(TessStorage, ConstPagesReturnConstSpansAndReferences) {
  Page<TopDown2D> page{tess::ChunkKey{0}, tess::ChunkCoord3{0, 0, 0}};
  page.field<TerrainTag>(tess::LocalTileId{4}) = 42;
  const auto& const_page = page;

  auto terrain = const_page.field_span<TerrainTag>();
  decltype(auto) tile = const_page.field<TerrainTag>(tess::LocalTileId{4});

  static_assert(
      std::is_same_v<decltype(terrain), std::span<const std::uint16_t>>);
  static_assert(std::is_same_v<decltype(tile), const std::uint16_t&>);

  EXPECT_EQ(terrain[4], 42);
  EXPECT_EQ(tile, 42);
}

TEST(TessStorage, PageAccessorsAreNoexcept) {
  Page<TopDown2D> page{tess::ChunkKey{0}, tess::ChunkCoord3{0, 0, 0}};
  const auto& const_page = page;

  static_assert(
      noexcept(Page<TopDown2D>{tess::ChunkKey{0}, tess::ChunkCoord3{0, 0, 0}}));
  static_assert(noexcept(page.chunk_key()));
  static_assert(noexcept(page.chunk_coord()));
  static_assert(noexcept(page.field_span<TerrainTag>()));
  static_assert(noexcept(const_page.field_span<TerrainTag>()));
  static_assert(noexcept(page.field<TerrainTag>(tess::LocalTileId{0})));
  static_assert(noexcept(const_page.field<TerrainTag>(tess::LocalTileId{0})));

  EXPECT_EQ(page.chunk_key(), (tess::ChunkKey{0}));
}

TEST(TessStorage, PageMetadataReportsChunkIdentityAndByteSize) {
  Page<TopDown2D> page{tess::ChunkKey{9}, tess::ChunkCoord3{1, 2, 0}};
  constexpr auto expected_bytes =
      Page<TopDown2D>::local_tile_count *
      (sizeof(std::uint16_t) + sizeof(float) + sizeof(std::uint32_t));

  static_assert(Page<TopDown2D>::byte_size == expected_bytes);

  EXPECT_EQ(page.chunk_key(), (tess::ChunkKey{9}));
  EXPECT_EQ(page.chunk_coord(), (tess::ChunkCoord3{1, 2, 0}));
  EXPECT_EQ(Page<TopDown2D>::byte_size, expected_bytes);
}

TEST(TessStorage, RepeatedFieldAccessDoesNotAllocate) {
  Page<TopDown2D> page{tess::ChunkKey{0}, tess::ChunkCoord3{0, 0, 0}};

  allocation_count.store(0, std::memory_order_relaxed);
  count_allocations.store(true, std::memory_order_relaxed);
  for (std::uint64_t i = 0; i < Page<TopDown2D>::local_tile_count; ++i) {
    auto id = tess::LocalTileId{i};
    page.field<TerrainTag>(id) = static_cast<std::uint16_t>(i);
    auto terrain = page.field_span<TerrainTag>();
    EXPECT_EQ(terrain[id.value], static_cast<std::uint16_t>(i));
  }
  count_allocations.store(false, std::memory_order_relaxed);

  EXPECT_EQ(allocation_count.load(std::memory_order_relaxed), 0);
}

}  // namespace
