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

template <typename Shape>
using World = tess::AlwaysResidentWorld<Shape, Schema>;

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

TEST(TessStorage, TopDown2DWorldOwnsResidentPagesInChunkKeyOrder) {
  World<TopDown2D> world;
  auto pages = world.chunks();

  static_assert(World<TopDown2D>::chunk_count == 16);
  static_assert(World<TopDown2D>::local_tile_count == 512);
  static_assert(World<TopDown2D>::field_count == 3);
  static_assert(World<TopDown2D>::page_byte_size == Page<TopDown2D>::byte_size);
  static_assert(World<TopDown2D>::storage_byte_size ==
                16 * Page<TopDown2D>::byte_size);

  ASSERT_EQ(pages.size(), World<TopDown2D>::chunk_count);
  EXPECT_EQ(pages[0].chunk_key(), (tess::ChunkKey{0}));
  EXPECT_EQ(pages[0].chunk_coord(), (tess::ChunkCoord3{0, 0, 0}));
  EXPECT_EQ(pages[5].chunk_key(), (tess::ChunkKey{5}));
  EXPECT_EQ(pages[5].chunk_coord(), (tess::ChunkCoord3{1, 1, 0}));
  EXPECT_EQ(pages[15].chunk_key(), (tess::ChunkKey{15}));
  EXPECT_EQ(pages[15].chunk_coord(), (tess::ChunkCoord3{3, 3, 0}));
}

TEST(TessStorage, Vertical2DWorldLooksUpChunksByKeyAndCoord) {
  World<Vertical2D> world;

  auto& by_key = world.chunk(tess::ChunkKey{6});
  auto& by_coord = world.chunk(tess::ChunkCoord3{0, 2, 1});

  static_assert(World<Vertical2D>::chunk_count == 16);
  static_assert(World<Vertical2D>::local_tile_count == 128);

  EXPECT_EQ(&by_key, &by_coord);
  EXPECT_EQ(by_key.chunk_key(), (tess::ChunkKey{6}));
  EXPECT_EQ(by_key.chunk_coord(), (tess::ChunkCoord3{0, 2, 1}));
  EXPECT_EQ(world.try_chunk(tess::ChunkKey{16}), nullptr);
  EXPECT_EQ(world.try_chunk(tess::ChunkCoord3{1, 0, 0}), nullptr);
  EXPECT_EQ(world.try_chunk(tess::ChunkCoord3{0, 4, 0}), nullptr);
  EXPECT_EQ(world.try_chunk(tess::ChunkCoord3{0, 0, 4}), nullptr);
}

TEST(TessStorage, Chunked3DWorldResolvesCoordinatesAndWritesFields) {
  World<Chunked3D> world;
  constexpr auto coord = tess::Coord3{47, 33, 17};
  const auto resolved = world.resolve(coord);

  static_assert(World<Chunked3D>::chunk_count == 64);
  static_assert(World<Chunked3D>::local_tile_count == 2048);

  EXPECT_EQ(resolved.chunk_key, (tess::ChunkKey{42}));
  EXPECT_EQ(resolved.local_tile_id, (tess::LocalTileId{287}));

  auto checked = world.try_resolve(coord);
  ASSERT_TRUE(checked.has_value());
  EXPECT_EQ(*checked, resolved);

  world.field<TerrainTag>(coord) = 11;
  world.field<CostTag>(coord) = 1.25F;
  world.field<RegionTag>(coord) = 99;

  auto& page = world.chunk(resolved.chunk_key);
  EXPECT_EQ(page.field<TerrainTag>(resolved.local_tile_id), 11);
  EXPECT_FLOAT_EQ(page.field<CostTag>(resolved.local_tile_id), 1.25F);
  EXPECT_EQ(page.field<RegionTag>(resolved.local_tile_id), 99u);
}

TEST(TessStorage, WorldTryResolveAndTryFieldRejectInvalidCoordinates) {
  World<Chunked3D> world;

  EXPECT_FALSE(world.try_resolve(tess::Coord3{-1, 0, 0}).has_value());
  EXPECT_FALSE(world.try_resolve(tess::Coord3{64, 0, 0}).has_value());
  EXPECT_FALSE(world.try_resolve(tess::Coord3{0, 64, 0}).has_value());
  EXPECT_FALSE(world.try_resolve(tess::Coord3{0, 0, 32}).has_value());
  EXPECT_EQ(world.try_field<TerrainTag>(tess::Coord3{-1, 0, 0}), nullptr);
  EXPECT_EQ(world.try_field<TerrainTag>(tess::Coord3{0, 0, 32}), nullptr);

  auto* terrain = world.try_field<TerrainTag>(tess::Coord3{1, 2, 3});
  ASSERT_NE(terrain, nullptr);
  *terrain = 17;
  EXPECT_EQ(world.field<TerrainTag>(tess::Coord3{1, 2, 3}), 17);
}

TEST(TessStorage, WorldConstAccessReturnsConstPagesFieldsAndSpans) {
  World<TopDown2D> world;
  world.field<TerrainTag>(tess::Coord3{3, 4, 0}) = 23;
  const auto& const_world = world;

  auto pages = const_world.chunks();
  auto terrain = const_world.field_span<TerrainTag>(tess::ChunkKey{0});
  decltype(auto) tile = const_world.field<TerrainTag>(tess::Coord3{3, 4, 0});
  const auto* checked =
      const_world.try_field<TerrainTag>(tess::Coord3{3, 4, 0});

  static_assert(
      std::is_same_v<decltype(pages), std::span<const Page<TopDown2D>>>);
  static_assert(
      std::is_same_v<decltype(terrain), std::span<const std::uint16_t>>);
  static_assert(std::is_same_v<decltype(tile), const std::uint16_t&>);

  ASSERT_NE(checked, nullptr);
  EXPECT_EQ(terrain[tess::local_tile_id<TopDown2D>(
                        tess::local_coord<TopDown2D>(tess::Coord3{3, 4, 0}))
                        .value],
            23);
  EXPECT_EQ(tile, 23);
  EXPECT_EQ(*checked, 23);
}

TEST(TessStorage, WorldFieldSpansKeepStructOfArraysIndependent) {
  World<TopDown2D> world;
  auto terrain = world.field_span<TerrainTag>(tess::ChunkKey{0});
  auto regions = world.field_span<RegionTag>(tess::ChunkKey{0});

  terrain[10] = 5;
  regions[10] = 700;

  EXPECT_NE(static_cast<const void*>(terrain.data()),
            static_cast<const void*>(regions.data()));
  EXPECT_EQ(
      world.chunk(tess::ChunkKey{0}).field<TerrainTag>(tess::LocalTileId{10}),
      5);
  EXPECT_EQ(
      world.chunk(tess::ChunkKey{0}).field<RegionTag>(tess::LocalTileId{10}),
      700u);
}

TEST(TessStorage, WorldHotAccessorsAreNoexcept) {
  World<TopDown2D> world;
  const auto& const_world = world;
  constexpr auto key = tess::ChunkKey{0};
  constexpr auto chunk_coord = tess::ChunkCoord3{0, 0, 0};
  constexpr auto coord = tess::Coord3{0, 0, 0};

  static_assert(!noexcept(World<TopDown2D>{}));
  static_assert(noexcept(world.chunks()));
  static_assert(noexcept(const_world.chunks()));
  static_assert(noexcept(world.chunk(key)));
  static_assert(noexcept(const_world.chunk(key)));
  static_assert(noexcept(world.chunk(chunk_coord)));
  static_assert(noexcept(const_world.chunk(chunk_coord)));
  static_assert(noexcept(world.try_chunk(key)));
  static_assert(noexcept(const_world.try_chunk(key)));
  static_assert(noexcept(world.try_chunk(chunk_coord)));
  static_assert(noexcept(const_world.try_chunk(chunk_coord)));
  static_assert(noexcept(world.resolve(coord)));
  static_assert(noexcept(world.try_resolve(coord)));
  static_assert(noexcept(world.field<TerrainTag>(coord)));
  static_assert(noexcept(const_world.field<TerrainTag>(coord)));
  static_assert(noexcept(world.try_field<TerrainTag>(coord)));
  static_assert(noexcept(const_world.try_field<TerrainTag>(coord)));
  static_assert(noexcept(world.field_span<TerrainTag>(key)));
  static_assert(noexcept(const_world.field_span<TerrainTag>(key)));

  EXPECT_EQ(world.chunks().size(), World<TopDown2D>::chunk_count);
}

TEST(TessStorage, RepeatedWorldHotAccessDoesNotAllocateAfterConstruction) {
  World<TopDown2D> world;
  std::uint64_t observed = 0;

  allocation_count.store(0, std::memory_order_relaxed);
  count_allocations.store(true, std::memory_order_relaxed);
  for (std::uint64_t i = 0; i < 1024; ++i) {
    const auto key = tess::ChunkKey{i % World<TopDown2D>::chunk_count};
    const auto chunk = tess::chunk_coord<TopDown2D>(key);
    const auto coord = tess::coord<TopDown2D>(chunk, tess::LocalTileId{0});
    auto* page = world.try_chunk(key);
    auto* terrain = world.try_field<TerrainTag>(coord);
    auto regions = world.field_span<RegionTag>(key);

    ASSERT_NE(page, nullptr);
    ASSERT_NE(terrain, nullptr);
    *terrain = static_cast<std::uint16_t>(i);
    regions[0] = static_cast<std::uint32_t>(i + 1);
    observed += page->chunk_key().value + *terrain + regions[0];
  }
  count_allocations.store(false, std::memory_order_relaxed);

  EXPECT_GT(observed, 0u);
  EXPECT_EQ(allocation_count.load(std::memory_order_relaxed), 0);
}

}  // namespace
