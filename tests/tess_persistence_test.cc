#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

struct TerrainTag {};
struct CostTag {};

using Shape = tess::Shape<tess::Extent3{8, 4, 1}, tess::Extent3{4, 2, 1}>;
using OtherShape = tess::Shape<tess::Extent3{16, 4, 1}, tess::Extent3{4, 2, 1}>;
using HexShape = tess::Shape<tess::Extent3{8, 4, 1}, tess::Extent3{4, 2, 1},
                             tess::lattice::HexAxial>;
using Fields = tess::FieldSchema<tess::Field<TerrainTag, std::uint16_t>,
                                 tess::Field<CostTag, float>>;
using Archive = tess::PersistenceSchema<
    0x746573742d776f72ULL, 3,
    tess::PersistedField<TerrainTag, 0x7465727261696eULL, 2>,
    tess::PersistedField<CostTag, 0x636f7374ULL, 1>>;
using ArchiveV4 = tess::PersistenceSchema<
    0x746573742d776f72ULL, 4,
    tess::PersistedField<TerrainTag, 0x7465727261696eULL, 2>,
    tess::PersistedField<CostTag, 0x636f7374ULL, 1>>;
using OtherArchive = tess::PersistenceSchema<
    0x6f746865722d776fULL, 3,
    tess::PersistedField<TerrainTag, 0x7465727261696eULL, 2>,
    tess::PersistedField<CostTag, 0x636f7374ULL, 1>>;
using ChangedFields = tess::PersistenceSchema<
    0x746573742d776f72ULL, 3,
    tess::PersistedField<TerrainTag, 0x7465727261696eULL, 3>,
    tess::PersistedField<CostTag, 0x636f7374ULL, 1>>;

using DenseWorld = tess::AlwaysResidentWorld<Shape, Fields>;
using SparseWorld = tess::SparseResidentWorld<Shape, Fields>;

constexpr std::uint32_t kLoadDirty = 1U << 6U;

void fill_chunk(auto& world, tess::ChunkKey key, std::uint16_t base) {
  auto terrain = world.template field_span<TerrainTag>(key);
  auto costs = world.template field_span<CostTag>(key);
  for (std::size_t i = 0; i < terrain.size(); ++i) {
    terrain[i] = static_cast<std::uint16_t>(base + i);
    costs[i] = static_cast<float>(base) + static_cast<float>(i) * 0.25F;
  }
}

void expect_chunk(const auto& world, tess::ChunkKey key, std::uint16_t base) {
  const auto terrain = world.template field_span<TerrainTag>(key);
  const auto costs = world.template field_span<CostTag>(key);
  for (std::size_t i = 0; i < terrain.size(); ++i) {
    EXPECT_EQ(terrain[i], static_cast<std::uint16_t>(base + i));
    EXPECT_FLOAT_EQ(costs[i],
                    static_cast<float>(base) + static_cast<float>(i) * 0.25F);
  }
}

TEST(TessPersistence, DenseArchiveRoundTripsAuthoritativeFieldsAndMetadata) {
  DenseWorld source;
  for (std::uint64_t key = 0; key < DenseWorld::chunk_count; ++key) {
    fill_chunk(source, tess::ChunkKey{key},
               static_cast<std::uint16_t>(key * 20));
  }
  source.mark_active(tess::ChunkKey{2}, 0x5U);
  source.meta(tess::ChunkKey{2}).entity_count = 7;

  std::vector<std::byte> bytes;
  const auto saved = tess::save_world_archive<Archive>(source, bytes);
  ASSERT_EQ(saved.status, tess::WorldArchiveStatus::Ok);
  EXPECT_EQ(saved.info.schema_id, Archive::id);
  EXPECT_EQ(saved.info.schema_version, Archive::version);
  EXPECT_EQ(saved.info.chunk_count, DenseWorld::chunk_count);
  EXPECT_EQ(saved.bytes_processed, bytes.size());

  const auto inspected = tess::inspect_world_archive(bytes);
  ASSERT_EQ(inspected.status, tess::WorldArchiveStatus::Ok);
  EXPECT_EQ(inspected.info.size, Shape::size);
  EXPECT_EQ(inspected.info.chunk, Shape::chunk);
  EXPECT_EQ(inspected.info.lattice_identity,
            tess::lattice::Identity::Orthogonal);

  DenseWorld restored;
  restored.field<TerrainTag>({0, 0, 0}) = 999;
  const auto loaded =
      tess::load_world_archive<Archive>(restored, bytes, kLoadDirty);
  ASSERT_EQ(loaded.status, tess::WorldArchiveStatus::Ok);
  for (std::uint64_t key = 0; key < DenseWorld::chunk_count; ++key) {
    expect_chunk(restored, tess::ChunkKey{key},
                 static_cast<std::uint16_t>(key * 20));
    EXPECT_EQ(restored.dirty_flags(tess::ChunkKey{key}), kLoadDirty);
    EXPECT_EQ(restored.dirty_bounds(tess::ChunkKey{key}),
              (tess::Box3{tess::coord<Shape>(
                              tess::chunk_coord<Shape>(tess::ChunkKey{key}),
                              tess::LocalTileId{}),
                          Shape::chunk}));
  }
  EXPECT_EQ(restored.active_flags(tess::ChunkKey{2}), 0x5U);
  EXPECT_EQ(restored.meta(tess::ChunkKey{2}).entity_count, 7U);
  EXPECT_EQ(restored.meta(tess::ChunkKey{2}).topology_version, 1U);
}

TEST(TessPersistence, DenseLoadInvalidatesWarmDerivedProducts) {
  DenseWorld target;
  for (std::uint64_t key = 0; key < DenseWorld::chunk_count; ++key) {
    auto terrain = target.template field_span<TerrainTag>(tess::ChunkKey{key});
    std::fill(terrain.begin(), terrain.end(), 1);
    target.mark_dirty(
        tess::ChunkKey{key}, 1U,
        {tess::coord<Shape>(tess::chunk_coord<Shape>(tess::ChunkKey{key}),
                            tess::LocalTileId{}),
         Shape::chunk});
  }
  tess::GoalSet goals;
  goals.add({7, 3, 0});
  tess::DistanceFieldScratch scratch;
  tess::DistanceFieldProduct product;
  ASSERT_EQ((tess::build_distance_field_product<DenseWorld, TerrainTag>(
                 target, goals, scratch, product))
                .status,
            tess::PathStatus::Found);
  ASSERT_TRUE(product.is_valid(target));

  DenseWorld source;
  for (std::uint64_t key = 0; key < DenseWorld::chunk_count; ++key) {
    auto terrain = source.template field_span<TerrainTag>(tess::ChunkKey{key});
    std::fill(terrain.begin(), terrain.end(),
              static_cast<std::uint16_t>(key + 2));
  }
  std::vector<std::byte> bytes;
  ASSERT_EQ(tess::save_world_archive<Archive>(source, bytes).status,
            tess::WorldArchiveStatus::Ok);
  ASSERT_EQ(tess::load_world_archive<Archive>(target, bytes, kLoadDirty).status,
            tess::WorldArchiveStatus::Ok);

  EXPECT_FALSE(product.is_valid(target));
}

TEST(TessPersistence, CanonicalBytesDoNotDependOnMutationOrder) {
  DenseWorld first;
  DenseWorld second;
  for (std::uint64_t key = 0; key < DenseWorld::chunk_count; ++key) {
    fill_chunk(first, tess::ChunkKey{key},
               static_cast<std::uint16_t>(key * 10));
  }
  for (std::uint64_t key = DenseWorld::chunk_count; key-- > 0;) {
    fill_chunk(second, tess::ChunkKey{key},
               static_cast<std::uint16_t>(key * 10));
  }
  first.mark_dirty(tess::ChunkKey{0}, 1U, {{0, 0, 0}, {1, 1, 1}});
  second.mark_dirty(tess::ChunkKey{3}, 2U, {{6, 2, 0}, {1, 1, 1}});

  std::vector<std::byte> first_bytes;
  std::vector<std::byte> second_bytes;
  ASSERT_EQ(tess::save_world_archive<Archive>(first, first_bytes).status,
            tess::WorldArchiveStatus::Ok);
  ASSERT_EQ(tess::save_world_archive<Archive>(second, second_bytes).status,
            tess::WorldArchiveStatus::Ok);
  EXPECT_EQ(first_bytes, second_bytes);
}

TEST(TessPersistence, RejectsDamageWithoutMutatingTarget) {
  DenseWorld source;
  fill_chunk(source, tess::ChunkKey{0}, 20);
  std::vector<std::byte> bytes;
  ASSERT_EQ(tess::save_world_archive<Archive>(source, bytes).status,
            tess::WorldArchiveStatus::Ok);

  DenseWorld target;
  target.field<TerrainTag>({0, 0, 0}) = 777;
  auto corrupt = bytes;
  corrupt.back() ^= std::byte{0x40};
  EXPECT_EQ(tess::load_world_archive<Archive>(target, corrupt).status,
            tess::WorldArchiveStatus::Corrupt);
  EXPECT_EQ(target.field<TerrainTag>({0, 0, 0}), 777);

  const auto truncated =
      std::span<const std::byte>{bytes}.first(bytes.size() - 1);
  EXPECT_EQ(tess::load_world_archive<Archive>(target, truncated).status,
            tess::WorldArchiveStatus::Truncated);
  EXPECT_EQ(target.field<TerrainTag>({0, 0, 0}), 777);
}

TEST(TessPersistence, ClassifiesCompatibilityBeforeMutation) {
  DenseWorld source;
  std::vector<std::byte> bytes;
  ASSERT_EQ(tess::save_world_archive<Archive>(source, bytes).status,
            tess::WorldArchiveStatus::Ok);

  tess::AlwaysResidentWorld<OtherShape, Fields> other_shape;
  EXPECT_EQ(tess::load_world_archive<Archive>(other_shape, bytes).status,
            tess::WorldArchiveStatus::ShapeMismatch);

  tess::AlwaysResidentWorld<HexShape, Fields> other_lattice;
  EXPECT_EQ(tess::load_world_archive<Archive>(other_lattice, bytes).status,
            tess::WorldArchiveStatus::LatticeMismatch);

  DenseWorld target;
  EXPECT_EQ(tess::load_world_archive<ArchiveV4>(target, bytes).status,
            tess::WorldArchiveStatus::MigrationRequired);
  EXPECT_EQ(tess::load_world_archive<OtherArchive>(target, bytes).status,
            tess::WorldArchiveStatus::SchemaMismatch);
  EXPECT_EQ(tess::load_world_archive<ChangedFields>(target, bytes).status,
            tess::WorldArchiveStatus::FieldMismatch);
}

TEST(TessPersistence, SparseArchiveIsCanonicalAndCapacityChecked) {
  const tess::ResidencyConfig two_chunks{SparseWorld::page_byte_size * 2};
  SparseWorld first(two_chunks);
  SparseWorld second(two_chunks);
  for (const auto key : {tess::ChunkKey{3}, tess::ChunkKey{1}}) {
    first.ensure_resident(key);
    fill_chunk(first, key, static_cast<std::uint16_t>(key.value * 10));
  }
  for (const auto key : {tess::ChunkKey{1}, tess::ChunkKey{3}}) {
    second.ensure_resident(key);
    fill_chunk(second, key, static_cast<std::uint16_t>(key.value * 10));
  }

  std::vector<std::byte> first_bytes;
  std::vector<std::byte> second_bytes;
  ASSERT_EQ(tess::save_world_archive<Archive>(first, first_bytes).status,
            tess::WorldArchiveStatus::Ok);
  ASSERT_EQ(tess::save_world_archive<Archive>(second, second_bytes).status,
            tess::WorldArchiveStatus::Ok);
  EXPECT_EQ(first_bytes, second_bytes);

  SparseWorld too_small({SparseWorld::page_byte_size});
  too_small.ensure_resident(tess::ChunkKey{0});
  too_small.field<TerrainTag>({0, 0, 0}) = 888;
  EXPECT_EQ(tess::load_world_archive<Archive>(too_small, first_bytes).status,
            tess::WorldArchiveStatus::ResidencyCapacityExceeded);
  EXPECT_TRUE(too_small.is_resident(tess::ChunkKey{0}));
  EXPECT_EQ(too_small.field<TerrainTag>({0, 0, 0}), 888);

  SparseWorld restored(two_chunks);
  restored.ensure_resident(tess::ChunkKey{0});
  ASSERT_EQ(tess::load_world_archive<Archive>(restored, first_bytes, kLoadDirty)
                .status,
            tess::WorldArchiveStatus::Ok);
  EXPECT_FALSE(restored.is_resident(tess::ChunkKey{0}));
  EXPECT_TRUE(restored.is_resident(tess::ChunkKey{1}));
  EXPECT_TRUE(restored.is_resident(tess::ChunkKey{3}));
  expect_chunk(restored, tess::ChunkKey{1}, 10);
  expect_chunk(restored, tess::ChunkKey{3}, 30);
}

}  // namespace
