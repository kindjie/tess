#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

#include "allocation_counter.h"

namespace {

struct TerrainTag {};
struct CostTag {};
struct RegionTag {};

constexpr std::uint32_t DirtyTerrain = 1u << 0u;
constexpr std::uint32_t DirtyCost = 1u << 1u;
constexpr std::uint32_t DirtyTopology = 1u << 2u;
constexpr std::uint32_t ActiveFluid = 1u << 0u;
constexpr std::uint32_t ActiveFire = 1u << 1u;

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

  tess_test::ScopedAllocationCounter counter;
  for (std::uint64_t i = 0; i < Page<TopDown2D>::local_tile_count; ++i) {
    auto id = tess::LocalTileId{i};
    page.field<TerrainTag>(id) = static_cast<std::uint16_t>(i);
    auto terrain = page.field_span<TerrainTag>();
    EXPECT_EQ(terrain[id.value], static_cast<std::uint16_t>(i));
  }

  EXPECT_EQ(counter.count(), 0u);
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
  ASSERT_TRUE(checked);
  EXPECT_EQ(checked, resolved);

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

  using Pages = decltype(const_world.chunks());
  auto terrain = const_world.field_span<TerrainTag>(tess::ChunkKey{0});
  decltype(auto) tile = const_world.field<TerrainTag>(tess::Coord3{3, 4, 0});
  const auto* checked =
      const_world.try_field<TerrainTag>(tess::Coord3{3, 4, 0});

  static_assert(std::is_same_v<Pages, std::span<const Page<TopDown2D>>>);
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
  static_assert(noexcept(world.meta(key)));
  static_assert(noexcept(const_world.meta(key)));
  static_assert(noexcept(world.meta(chunk_coord)));
  static_assert(noexcept(const_world.meta(chunk_coord)));
  static_assert(noexcept(world.try_meta(key)));
  static_assert(noexcept(const_world.try_meta(key)));
  static_assert(noexcept(world.try_meta(chunk_coord)));
  static_assert(noexcept(const_world.try_meta(chunk_coord)));
  static_assert(noexcept(world.chunk_state(key)));
  static_assert(noexcept(const_world.chunk_state(key)));
  static_assert(noexcept(world.chunk_state(chunk_coord)));
  static_assert(noexcept(const_world.chunk_state(chunk_coord)));
  static_assert(
      noexcept(world.set_chunk_state(key, tess::ChunkState::ResidentActive)));
  static_assert(noexcept(world.mark_dirty(
      key, DirtyTerrain,
      tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}})));
  static_assert(noexcept(world.clear_dirty(key, DirtyTerrain)));
  static_assert(noexcept(world.mark_active(key, ActiveFluid)));
  static_assert(noexcept(world.clear_active(key, ActiveFluid)));
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

TEST(TessStorage, FreshWorldFieldValuesAreZeroInitializedWithoutWrites) {
  const World<Chunked3D> world;
  std::size_t nonzero_values = 0;

  for (const auto& page : world.chunks()) {
    for (const auto value : page.field_span<TerrainTag>()) {
      nonzero_values += value != 0 ? 1u : 0u;
    }
    for (const auto value : page.field_span<CostTag>()) {
      nonzero_values += value == 0.0F ? 0u : 1u;
    }
    for (const auto value : page.field_span<RegionTag>()) {
      nonzero_values += value != 0 ? 1u : 0u;
    }
  }

  EXPECT_EQ(nonzero_values, 0u);
}

TEST(TessStorage, WorldDefaultMetadataIsSleepingAndClean) {
  World<TopDown2D> top_down;
  World<Vertical2D> vertical;
  World<Chunked3D> chunked;

  for (const auto key :
       {tess::ChunkKey{0}, tess::ChunkKey{5}, tess::ChunkKey{15}}) {
    const auto& meta = top_down.meta(key);
    EXPECT_EQ(meta.state, tess::ChunkState::ResidentSleeping);
    EXPECT_EQ(meta.version, 0u);
    EXPECT_EQ(meta.topology_version, 0u);
    EXPECT_EQ(meta.field_dirty_flags, 0u);
    EXPECT_EQ(meta.active_flags, 0u);
    EXPECT_EQ(meta.dirty_bounds, (tess::Box3{}));
    EXPECT_EQ(meta.active_count, 0u);
    EXPECT_EQ(meta.entity_count, 0u);
  }

  EXPECT_EQ(vertical.meta(tess::ChunkKey{6}).state,
            tess::ChunkState::ResidentSleeping);
  EXPECT_EQ(chunked.meta(tess::ChunkKey{42}).state,
            tess::ChunkState::ResidentSleeping);
}

TEST(TessStorage, WorldLooksUpMetadataByKeyAndCoord) {
  World<Vertical2D> world;

  auto& by_key = world.meta(tess::ChunkKey{6});
  auto& by_coord = world.meta(tess::ChunkCoord3{0, 2, 1});

  by_key.entity_count = 12;

  EXPECT_EQ(&by_key, &by_coord);
  EXPECT_EQ(by_coord.entity_count, 12u);
  EXPECT_EQ(world.try_meta(tess::ChunkKey{16}), nullptr);
  EXPECT_EQ(world.try_meta(tess::ChunkCoord3{1, 0, 0}), nullptr);
  EXPECT_EQ(world.try_meta(tess::ChunkCoord3{0, 4, 0}), nullptr);
  EXPECT_EQ(world.try_meta(tess::ChunkCoord3{0, 0, 4}), nullptr);
}

TEST(TessStorage, WorldConstMetadataAccessReturnsConstReferences) {
  World<TopDown2D> world;
  world.meta(tess::ChunkKey{1}).entity_count = 3;
  const auto& const_world = world;

  decltype(auto) by_key = const_world.meta(tess::ChunkKey{1});
  decltype(auto) by_coord = const_world.meta(tess::ChunkCoord3{1, 0, 0});
  const auto* checked = const_world.try_meta(tess::ChunkKey{1});

  static_assert(std::is_same_v<decltype(by_key), const tess::ChunkMeta&>);
  static_assert(std::is_same_v<decltype(by_coord), const tess::ChunkMeta&>);

  ASSERT_NE(checked, nullptr);
  EXPECT_EQ(&by_key, &by_coord);
  EXPECT_EQ(checked->entity_count, 3u);
}

TEST(TessStorage, WorldTransitionsChunkStateExplicitly) {
  World<TopDown2D> world;
  constexpr auto key = tess::ChunkKey{2};

  EXPECT_EQ(world.chunk_state(key), tess::ChunkState::ResidentSleeping);
  world.set_chunk_state(key, tess::ChunkState::ResidentActive);
  EXPECT_EQ(world.chunk_state(key), tess::ChunkState::ResidentActive);
  EXPECT_EQ(world.chunk_state(tess::ChunkCoord3{2, 0, 0}),
            tess::ChunkState::ResidentActive);
  world.set_chunk_state(key, tess::ChunkState::ResidentSleeping);
  EXPECT_EQ(world.chunk_state(key), tess::ChunkState::ResidentSleeping);
}

TEST(TessStorage, WorldDirtyFlagsUnionClearAndIncrementVersion) {
  World<TopDown2D> world;
  constexpr auto key = tess::ChunkKey{5};
  const auto first =
      tess::Box3{tess::Coord3{32, 16, 0}, tess::Extent3{2, 3, 1}};
  const auto second =
      tess::Box3{tess::Coord3{40, 18, 0}, tess::Extent3{4, 2, 1}};

  world.mark_dirty(key, DirtyTerrain, first);
  EXPECT_EQ(world.meta(key).field_dirty_flags, DirtyTerrain);
  EXPECT_EQ(world.meta(key).version, 1u);
  EXPECT_EQ(world.meta(key).dirty_bounds, first);

  world.mark_dirty(key, DirtyCost | DirtyTopology, second);
  EXPECT_EQ(world.meta(key).field_dirty_flags,
            DirtyTerrain | DirtyCost | DirtyTopology);
  EXPECT_EQ(world.meta(key).version, 2u);
  EXPECT_EQ(world.meta(key).dirty_bounds,
            (tess::Box3{tess::Coord3{32, 16, 0}, tess::Extent3{12, 4, 1}}));

  world.clear_dirty(key, DirtyCost);
  EXPECT_EQ(world.meta(key).field_dirty_flags, DirtyTerrain | DirtyTopology);
  EXPECT_EQ(world.meta(key).dirty_bounds,
            (tess::Box3{tess::Coord3{32, 16, 0}, tess::Extent3{12, 4, 1}}));
  world.clear_dirty(key, DirtyTerrain | DirtyTopology);
  EXPECT_EQ(world.meta(key).field_dirty_flags, 0u);
  EXPECT_EQ(world.meta(key).dirty_bounds, (tess::Box3{}));
  EXPECT_EQ(world.meta(key).version, 2u);
}

TEST(TessStorage, WorldDirtyBoundsUnionSaturatesHugeExtent) {
  // An extent >= 2^63 would flip negative under box_axis_end's int64 cast
  // and corrupt the union; the axis end saturates at the int64 maximum.
  World<TopDown2D> world;
  constexpr auto key = tess::ChunkKey{5};
  const auto small =
      tess::Box3{tess::Coord3{32, 16, 0}, tess::Extent3{2, 3, 1}};
  const auto huge = tess::Box3{
      tess::Coord3{0, 16, 0},
      tess::Extent3{std::uint64_t{1} << 63u, 3, 1},
  };

  world.mark_dirty(key, DirtyTerrain, small);
  world.mark_dirty(key, DirtyCost, huge);

  const auto bounds = world.meta(key).dirty_bounds;
  EXPECT_EQ(bounds.origin, (tess::Coord3{0, 16, 0}));
  constexpr auto max_end = std::numeric_limits<std::int64_t>::max();
  EXPECT_EQ(bounds.extent.x, static_cast<std::uint64_t>(max_end));
  EXPECT_EQ(bounds.extent.y, 3u);
  EXPECT_EQ(bounds.extent.z, 1u);
}

TEST(TessStorage, WorldObserveDirtyReturnsRequestedSubsetBoundsAndVersion) {
  World<TopDown2D> world;
  constexpr auto key = tess::ChunkKey{3};
  const auto bounds =
      tess::Box3{tess::Coord3{32, 16, 0}, tess::Extent3{4, 2, 1}};

  world.mark_dirty(key, DirtyTerrain | DirtyCost, bounds);

  const auto observed = world.observe_dirty(key, DirtyTerrain | DirtyTopology);
  EXPECT_EQ(observed.flags, DirtyTerrain);
  EXPECT_EQ(observed.bounds, bounds);
  EXPECT_EQ(observed.version, world.meta(key).version);

  const auto clean = world.observe_dirty(tess::ChunkKey{4}, DirtyTerrain);
  EXPECT_EQ(clean.flags, 0u);
  EXPECT_EQ(clean.bounds, (tess::Box3{}));
  EXPECT_EQ(clean.version, 0u);
}

TEST(TessStorage, WorldClearDirtyObservedClearsExactlyObservedGeneration) {
  World<TopDown2D> world;
  constexpr auto key = tess::ChunkKey{5};
  const auto bounds =
      tess::Box3{tess::Coord3{40, 18, 0}, tess::Extent3{2, 2, 1}};

  world.mark_dirty(key, DirtyTerrain | DirtyCost, bounds);
  const auto observed = world.observe_dirty(key, DirtyTerrain);
  const auto version_before = world.meta(key).version;

  EXPECT_TRUE(world.clear_dirty_observed(key, observed));
  EXPECT_EQ(world.meta(key).field_dirty_flags, DirtyCost);
  EXPECT_EQ(world.meta(key).dirty_bounds, bounds);
  EXPECT_EQ(world.meta(key).version, version_before);
}

TEST(TessStorage, WorldClearDirtyObservedPreservesMarksAfterObservation) {
  World<TopDown2D> world;
  constexpr auto key = tess::ChunkKey{7};
  const auto first =
      tess::Box3{tess::Coord3{32, 16, 0}, tess::Extent3{2, 2, 1}};
  const auto second =
      tess::Box3{tess::Coord3{44, 20, 0}, tess::Extent3{2, 2, 1}};

  world.mark_dirty(key, DirtyTerrain, first);
  const auto observed = world.observe_dirty(key, DirtyTerrain);

  // A mark that lands after observation advances the generation, even for
  // the same category. The stale clear must preserve every flag and bound.
  world.mark_dirty(key, DirtyTerrain, second);
  EXPECT_FALSE(world.clear_dirty_observed(key, observed));
  EXPECT_EQ(world.meta(key).field_dirty_flags, DirtyTerrain);
  EXPECT_EQ(world.meta(key).dirty_bounds,
            (tess::Box3{tess::Coord3{32, 16, 0}, tess::Extent3{14, 6, 1}}));

  // Re-observing the current generation makes the clear valid again.
  const auto refreshed = world.observe_dirty(key, DirtyTerrain);
  EXPECT_TRUE(world.clear_dirty_observed(key, refreshed));
  EXPECT_EQ(world.meta(key).field_dirty_flags, 0u);
  EXPECT_EQ(world.meta(key).dirty_bounds, (tess::Box3{}));
}

TEST(TessStorage, WorldClearDirtyObservedIgnoresEmptyObservations) {
  World<TopDown2D> world;
  constexpr auto key = tess::ChunkKey{9};
  const auto bounds =
      tess::Box3{tess::Coord3{32, 16, 0}, tess::Extent3{2, 2, 1}};

  const auto clean = world.observe_dirty(key, DirtyTerrain);
  EXPECT_TRUE(world.clear_dirty_observed(key, clean));

  world.mark_dirty(key, DirtyCost, bounds);
  const auto unrelated = world.observe_dirty(key, DirtyTerrain);
  EXPECT_EQ(unrelated.flags, 0u);
  EXPECT_TRUE(world.clear_dirty_observed(key, unrelated));
  EXPECT_EQ(world.meta(key).field_dirty_flags, DirtyCost);
  EXPECT_EQ(world.meta(key).dirty_bounds, bounds);
}

TEST(TessStorage, WorldObserveDirtyAccessorsAreNoexcept) {
  World<TopDown2D> world;
  constexpr auto key = tess::ChunkKey{0};
  static_assert(noexcept(world.observe_dirty(key, DirtyTerrain)));
  static_assert(
      noexcept(world.clear_dirty_observed(key, tess::DirtyObservation{})));
}

TEST(TessStorage, WorldDirtyBoundsUnionCoversAllRelativeOrientations) {
  const auto union_via_mark = [](tess::Box3 lhs, tess::Box3 rhs) {
    World<TopDown2D> world;
    world.mark_dirty(tess::ChunkKey{0}, DirtyTerrain, lhs);
    world.mark_dirty(tess::ChunkKey{0}, DirtyTerrain, rhs);
    return world.meta(tess::ChunkKey{0}).dirty_bounds;
  };

  const auto base = tess::Box3{tess::Coord3{4, 4, 0}, tess::Extent3{2, 2, 1}};

  // rhs fully left of lhs.
  EXPECT_EQ(union_via_mark(base, tess::Box3{tess::Coord3{0, 4, 0},
                                            tess::Extent3{2, 2, 1}}),
            (tess::Box3{tess::Coord3{0, 4, 0}, tess::Extent3{6, 2, 1}}));
  // rhs fully below lhs.
  EXPECT_EQ(union_via_mark(base, tess::Box3{tess::Coord3{4, 0, 0},
                                            tess::Extent3{2, 2, 1}}),
            (tess::Box3{tess::Coord3{4, 0, 0}, tess::Extent3{2, 6, 1}}));
  // rhs up-right of lhs.
  EXPECT_EQ(union_via_mark(base, tess::Box3{tess::Coord3{7, 7, 0},
                                            tess::Extent3{2, 2, 1}}),
            (tess::Box3{tess::Coord3{4, 4, 0}, tess::Extent3{5, 5, 1}}));
  // Overlapping boxes.
  EXPECT_EQ(
      union_via_mark(tess::Box3{tess::Coord3{2, 2, 0}, tess::Extent3{3, 3, 1}},
                     tess::Box3{tess::Coord3{4, 4, 0}, tess::Extent3{3, 3, 1}}),
      (tess::Box3{tess::Coord3{2, 2, 0}, tess::Extent3{5, 5, 1}}));
  // rhs contained in lhs.
  EXPECT_EQ(
      union_via_mark(tess::Box3{tess::Coord3{1, 1, 0}, tess::Extent3{6, 6, 1}},
                     tess::Box3{tess::Coord3{3, 3, 0}, tess::Extent3{2, 2, 1}}),
      (tess::Box3{tess::Coord3{1, 1, 0}, tess::Extent3{6, 6, 1}}));
  // Identical boxes.
  EXPECT_EQ(union_via_mark(base, base), base);
  // rhs above lhs along z.
  EXPECT_EQ(
      union_via_mark(tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}},
                     tess::Box3{tess::Coord3{0, 0, 2}, tess::Extent3{1, 1, 1}}),
      (tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 3}}));
  // rhs below lhs along z.
  EXPECT_EQ(
      union_via_mark(tess::Box3{tess::Coord3{0, 0, 2}, tess::Extent3{1, 1, 1}},
                     tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}}),
      (tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 3}}));
  // Overlapping z spans.
  EXPECT_EQ(
      union_via_mark(tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 2}},
                     tess::Box3{tess::Coord3{0, 0, 1}, tess::Extent3{1, 1, 2}}),
      (tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 3}}));
}

TEST(TessStorage, WorldTopologyDirtyAdvancesTopologyVersion) {
  World<TopDown2D> world;
  constexpr auto key = tess::ChunkKey{5};
  const auto bounds =
      tess::Box3{tess::Coord3{32, 16, 0}, tess::Extent3{2, 3, 1}};

  world.mark_topology_dirty(key, DirtyTopology, bounds);
  EXPECT_EQ(world.meta(key).field_dirty_flags, DirtyTopology);
  EXPECT_EQ(world.meta(key).version, 1u);
  EXPECT_EQ(world.meta(key).topology_version, 1u);
  EXPECT_EQ(world.meta(key).dirty_bounds, bounds);

  world.mark_topology_rebuilt(key);
  EXPECT_EQ(world.meta(key).topology_version, 2u);
}

TEST(TessStorage, WorldZeroDirtyMaskDoesNotChangeMetadata) {
  World<TopDown2D> world;
  constexpr auto key = tess::ChunkKey{5};
  const auto bounds =
      tess::Box3{tess::Coord3{32, 16, 0}, tess::Extent3{2, 3, 1}};

  world.mark_dirty(key, 0, bounds);
  EXPECT_EQ(world.meta(key).field_dirty_flags, 0u);
  EXPECT_EQ(world.meta(key).dirty_bounds, (tess::Box3{}));
  EXPECT_EQ(world.meta(key).version, 0u);

  world.mark_dirty(key, DirtyTerrain, bounds);
  world.clear_dirty(key, 0);
  EXPECT_EQ(world.meta(key).field_dirty_flags, DirtyTerrain);
  EXPECT_EQ(world.meta(key).dirty_bounds, bounds);
  EXPECT_EQ(world.meta(key).version, 1u);
}

TEST(TessStorage, WorldActiveFlagsDriveStateAndActiveCount) {
  World<TopDown2D> world;
  constexpr auto key = tess::ChunkKey{4};

  world.mark_active(key, ActiveFluid);
  EXPECT_EQ(world.meta(key).active_flags, ActiveFluid);
  EXPECT_EQ(world.meta(key).active_count, 1u);
  EXPECT_EQ(world.chunk_state(key), tess::ChunkState::ResidentActive);

  world.mark_active(key, ActiveFluid | ActiveFire);
  EXPECT_EQ(world.meta(key).active_flags, ActiveFluid | ActiveFire);
  EXPECT_EQ(world.meta(key).active_count, 2u);
  EXPECT_EQ(world.chunk_state(key), tess::ChunkState::ResidentActive);

  world.clear_active(key, ActiveFluid);
  EXPECT_EQ(world.meta(key).active_flags, ActiveFire);
  EXPECT_EQ(world.meta(key).active_count, 1u);
  EXPECT_EQ(world.chunk_state(key), tess::ChunkState::ResidentActive);

  world.clear_active(key, ActiveFire);
  EXPECT_EQ(world.meta(key).active_flags, 0u);
  EXPECT_EQ(world.meta(key).active_count, 0u);
  EXPECT_EQ(world.chunk_state(key), tess::ChunkState::ResidentSleeping);
}

TEST(TessStorage, WorldZeroActiveMaskDoesNotChangeMetadata) {
  World<TopDown2D> world;
  constexpr auto key = tess::ChunkKey{4};

  world.mark_active(key, 0);
  EXPECT_EQ(world.meta(key).active_flags, 0u);
  EXPECT_EQ(world.meta(key).active_count, 0u);
  EXPECT_EQ(world.chunk_state(key), tess::ChunkState::ResidentSleeping);

  world.mark_active(key, ActiveFluid);
  world.clear_active(key, 0);
  EXPECT_EQ(world.meta(key).active_flags, ActiveFluid);
  EXPECT_EQ(world.meta(key).active_count, 1u);
  EXPECT_EQ(world.chunk_state(key), tess::ChunkState::ResidentActive);
}

TEST(TessStorage, WorldDirtyAndActiveChunkQueriesReturnKeyOrder) {
  World<TopDown2D> world;
  const auto one = tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}};

  world.mark_dirty(tess::ChunkKey{7}, DirtyTerrain, one);
  world.mark_dirty(tess::ChunkKey{2}, DirtyCost, one);
  world.mark_dirty(tess::ChunkKey{5}, DirtyTerrain | DirtyCost, one);
  world.mark_active(tess::ChunkKey{9}, ActiveFire);
  world.mark_active(tess::ChunkKey{1}, ActiveFluid);
  world.mark_active(tess::ChunkKey{4}, ActiveFluid | ActiveFire);

  EXPECT_EQ(world.dirty_chunks(DirtyTerrain), (std::vector<tess::ChunkKey>{
                                                  tess::ChunkKey{5},
                                                  tess::ChunkKey{7},
                                              }));
  EXPECT_EQ(world.dirty_chunks(DirtyCost), (std::vector<tess::ChunkKey>{
                                               tess::ChunkKey{2},
                                               tess::ChunkKey{5},
                                           }));
  EXPECT_EQ(world.active_chunks(ActiveFluid), (std::vector<tess::ChunkKey>{
                                                  tess::ChunkKey{1},
                                                  tess::ChunkKey{4},
                                              }));
  EXPECT_EQ(world.active_chunks(ActiveFire), (std::vector<tess::ChunkKey>{
                                                 tess::ChunkKey{4},
                                                 tess::ChunkKey{9},
                                             }));
}

TEST(TessStorage, CollectChunkQueriesAppendAndMatchByValueQueries) {
  World<TopDown2D> world;
  const auto one = tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}};

  world.mark_dirty(tess::ChunkKey{7}, DirtyTerrain, one);
  world.mark_dirty(tess::ChunkKey{2}, DirtyCost, one);
  world.mark_dirty(tess::ChunkKey{5}, DirtyTerrain | DirtyCost, one);
  world.mark_active(tess::ChunkKey{9}, ActiveFire);
  world.mark_active(tess::ChunkKey{1}, ActiveFluid);
  world.mark_active(tess::ChunkKey{4}, ActiveFluid | ActiveFire);

  std::vector<tess::ChunkKey> dirty;
  world.collect_dirty_chunks(DirtyTerrain, dirty);
  EXPECT_EQ(dirty, world.dirty_chunks(DirtyTerrain));

  std::vector<tess::ChunkKey> active;
  world.collect_active_chunks(ActiveFluid, active);
  EXPECT_EQ(active, world.active_chunks(ActiveFluid));

  // The out-parameter variants append to the caller-owned vector.
  world.collect_dirty_chunks(DirtyCost, dirty);
  auto expected = world.dirty_chunks(DirtyTerrain);
  const auto cost_chunks = world.dirty_chunks(DirtyCost);
  expected.insert(expected.end(), cost_chunks.begin(), cost_chunks.end());
  EXPECT_EQ(dirty, expected);
}

TEST(TessStorage, CollectChunkQueriesDoNotAllocateWithReservedVectors) {
  World<TopDown2D> world;
  const auto one = tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}};

  world.mark_dirty(tess::ChunkKey{3}, DirtyTerrain, one);
  world.mark_dirty(tess::ChunkKey{8}, DirtyTerrain, one);
  world.mark_active(tess::ChunkKey{6}, ActiveFluid);

  std::vector<tess::ChunkKey> dirty;
  std::vector<tess::ChunkKey> active;
  dirty.reserve(World<TopDown2D>::chunk_count);
  active.reserve(World<TopDown2D>::chunk_count);

  {
    tess_test::ScopedAllocationCounter counter;
    world.collect_dirty_chunks(DirtyTerrain, dirty);
    world.collect_active_chunks(ActiveFluid, active);
    EXPECT_EQ(counter.count(), 0u);
  }
  EXPECT_EQ(dirty, (std::vector<tess::ChunkKey>{
                       tess::ChunkKey{3},
                       tess::ChunkKey{8},
                   }));
  EXPECT_EQ(active, (std::vector<tess::ChunkKey>{tess::ChunkKey{6}}));
}

TEST(TessStorage, RepeatedWorldHotAccessDoesNotAllocateAfterConstruction) {
  World<TopDown2D> world;
  std::uint64_t observed = 0;

  tess_test::ScopedAllocationCounter counter;
  for (std::uint64_t i = 0; i < 1024; ++i) {
    const auto key = tess::ChunkKey{i % World<TopDown2D>::chunk_count};
    const auto chunk = tess::chunk_coord<TopDown2D>(key);
    const auto coord = tess::coord<TopDown2D>(chunk, tess::LocalTileId{0});
    auto* page = world.try_chunk(key);
    auto* meta = world.try_meta(key);
    auto* terrain = world.try_field<TerrainTag>(coord);
    auto regions = world.field_span<RegionTag>(key);

    ASSERT_NE(page, nullptr);
    ASSERT_NE(meta, nullptr);
    ASSERT_NE(terrain, nullptr);
    meta->entity_count = static_cast<std::uint32_t>(i);
    *terrain = static_cast<std::uint16_t>(i);
    regions[0] = static_cast<std::uint32_t>(i + 1);
    observed +=
        page->chunk_key().value + meta->entity_count + *terrain + regions[0];
  }

  EXPECT_GT(observed, 0u);
  EXPECT_EQ(counter.count(), 0u);
}

}  // namespace
