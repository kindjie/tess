#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace {

struct TerrainTag {};
struct CostTag {};
struct TinyTag {};
struct ModeTag {};
struct EnabledTag {};

enum class ScopedMode : std::uint8_t {
  Known = 1,
};

enum LegacyMode : std::uint8_t {
  LegacyKnown = 1,
};

static_assert(tess::detail::archive_scalar_supported_v<ScopedMode>);
static_assert(!tess::detail::archive_scalar_supported_v<LegacyMode>);

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
using TinyShape = tess::Shape<tess::Extent3{1, 1, 1}, tess::Extent3{1, 1, 1}>;
using TinyFields = tess::FieldSchema<tess::Field<TinyTag, std::uint8_t>>;
using TinyArchive = tess::PersistenceSchema<
    0x0102030405060708ULL, 9,
    tess::PersistedField<TinyTag, 0x1112131415161718ULL, 3>>;
using TinyWorld = tess::AlwaysResidentWorld<TinyShape, TinyFields>;
using EnumFields = tess::FieldSchema<tess::Field<ModeTag, ScopedMode>,
                                     tess::Field<EnabledTag, bool>>;
using EnumArchive = tess::PersistenceSchema<
    0x2122232425262728ULL, 1,
    tess::PersistedField<ModeTag, 0x3132333435363738ULL>,
    tess::PersistedField<EnabledTag, 0x4142434445464748ULL>>;
using EnumWorld = tess::AlwaysResidentWorld<TinyShape, EnumFields>;

constexpr std::uint32_t kLoadDirty = 1U << 6U;
constexpr std::size_t kBodySizeOffset = 12;
constexpr std::size_t kChecksumOffset = 20;
constexpr std::size_t kKeyLayoutOffset = 80;
constexpr std::size_t kResidencyOffset = 108;
constexpr std::size_t kFieldCountOffset = 109;
constexpr std::size_t kChunkCountOffset = 113;
constexpr std::size_t kHeaderSize = 121;
constexpr std::size_t kFieldDescriptorSize = 17;

template <typename UInt>
void write_unsigned_le(std::vector<std::byte>& bytes, std::size_t offset,
                       UInt value) {
  for (std::size_t i = 0; i < sizeof(UInt); ++i) {
    bytes[offset + i] =
        static_cast<std::byte>((value >> (i * 8U)) & static_cast<UInt>(0xff));
  }
}

auto test_crc32(std::span<const std::byte> bytes) -> std::uint32_t {
  auto crc = std::uint32_t{0xffffffffU};
  for (const auto byte : bytes) {
    crc ^= std::to_integer<std::uint8_t>(byte);
    for (int bit = 0; bit < 8; ++bit) {
      const auto mask =
          static_cast<std::uint32_t>(-static_cast<std::int32_t>(crc & 1U));
      crc = (crc >> 1U) ^ (0xedb88320U & mask);
    }
  }
  return ~crc;
}

void refresh_body_checksum(std::vector<std::byte>& bytes) {
  write_unsigned_le(bytes, kChecksumOffset,
                    test_crc32(std::span{bytes}.subspan(kHeaderSize)));
}

auto decode_hex(std::string_view hex) -> std::vector<std::byte> {
  const auto nibble = [](char value) {
    if (value >= '0' && value <= '9') {
      return static_cast<unsigned int>(value - '0');
    }
    return static_cast<unsigned int>(value - 'a' + 10);
  };
  auto bytes = std::vector<std::byte>{};
  bytes.reserve(hex.size() / 2);
  for (std::size_t i = 0; i < hex.size(); i += 2) {
    bytes.push_back(
        static_cast<std::byte>((nibble(hex[i]) << 4U) | nibble(hex[i + 1])));
  }
  return bytes;
}

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

TEST(TessPersistence, CanonicalFormatMatchesGoldenBytes) {
  TinyWorld source;
  source.field<TinyTag>({0, 0, 0}) = 0xab;
  source.mark_active(tess::ChunkKey{0}, 0x01020304U);
  source.set_chunk_state(tess::ChunkKey{0}, tess::ChunkState::ResidentActive);
  source.meta(tess::ChunkKey{0}).entity_count = 0x05060708U;

  std::vector<std::byte> bytes;
  ASSERT_EQ(tess::save_world_archive<TinyArchive>(source, bytes).status,
            tess::WorldArchiveStatus::Ok);
  // This literal is intentionally independent of the archive writer: it locks
  // the complete v1 envelope, metadata, scalar encoding, and CRC byte order.
  const auto golden = decode_hex(
      "54455353574c440001000000230000000000000002f5e6d50100000000000000"
      "0100000000000000010000000000000001000000000000000100000000000000"
      "01000000000000004854524f0100000001000000080706050403020109000000"
      "000000000c000000000000000101000000010000000000000018171615141312"
      "110300000001010000000000000000000000010403020108070605ab");
  EXPECT_EQ(bytes, golden);

  TinyWorld restored;
  ASSERT_EQ(tess::load_world_archive<TinyArchive>(restored, golden).status,
            tess::WorldArchiveStatus::Ok);
  EXPECT_EQ(restored.field<TinyTag>({0, 0, 0}), 0xab);
  EXPECT_EQ(restored.active_flags(tess::ChunkKey{0}), 0x01020304U);
  EXPECT_EQ(restored.meta(tess::ChunkKey{0}).entity_count, 0x05060708U);
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

TEST(TessPersistence,
     ScopedEnumUnknownValuesLoadOnlyAfterCompleteScalarPreflight) {
  EnumWorld source;
  source.field<ModeTag>({0, 0, 0}) = ScopedMode::Known;
  source.field<EnabledTag>({0, 0, 0}) = true;
  std::vector<std::byte> bytes;
  ASSERT_EQ(tess::save_world_archive<EnumArchive>(source, bytes).status,
            tess::WorldArchiveStatus::Ok);

  constexpr auto kFirstFieldOffset =
      kHeaderSize + EnumArchive::field_count * kFieldDescriptorSize +
      sizeof(std::uint64_t) + sizeof(std::uint8_t) + sizeof(std::uint32_t) * 2;
  bytes[kFirstFieldOffset] = std::byte{0xfe};
  bytes[kFirstFieldOffset + 1] = std::byte{2};
  refresh_body_checksum(bytes);

  EnumWorld target;
  target.field<ModeTag>({0, 0, 0}) = ScopedMode::Known;
  target.field<EnabledTag>({0, 0, 0}) = false;
  EXPECT_EQ(tess::load_world_archive<EnumArchive>(target, bytes).status,
            tess::WorldArchiveStatus::Corrupt);
  EXPECT_EQ(target.field<ModeTag>({0, 0, 0}), ScopedMode::Known);
  EXPECT_FALSE(target.field<EnabledTag>({0, 0, 0}));

  bytes[kFirstFieldOffset + 1] = std::byte{1};
  refresh_body_checksum(bytes);
  ASSERT_EQ(tess::load_world_archive<EnumArchive>(target, bytes).status,
            tess::WorldArchiveStatus::Ok);
  EXPECT_EQ(static_cast<std::uint8_t>(target.field<ModeTag>({0, 0, 0})), 0xfe);
  EXPECT_TRUE(target.field<EnabledTag>({0, 0, 0}));
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

TEST(TessPersistence, ClassifiesEnvelopeAndChunkCompatibilityStatuses) {
  DenseWorld source;
  std::vector<std::byte> bytes;
  ASSERT_EQ(tess::save_world_archive<Archive>(source, bytes).status,
            tess::WorldArchiveStatus::Ok);
  DenseWorld target;

  auto changed = bytes;
  changed[0] ^= std::byte{1};
  EXPECT_EQ(tess::inspect_world_archive(changed).status,
            tess::WorldArchiveStatus::InvalidMagic);

  changed = bytes;
  write_unsigned_le(changed, 8, std::uint32_t{2});
  EXPECT_EQ(tess::inspect_world_archive(changed).status,
            tess::WorldArchiveStatus::UnsupportedFormat);

  changed = bytes;
  write_unsigned_le(changed, kKeyLayoutOffset, std::uint32_t{2});
  EXPECT_EQ(tess::load_world_archive<Archive>(target, changed).status,
            tess::WorldArchiveStatus::KeyLayoutMismatch);

  changed = bytes;
  changed[kResidencyOffset] =
      static_cast<std::byte>(tess::WorldArchiveResidency::SparseResident);
  EXPECT_EQ(tess::load_world_archive<Archive>(target, changed).status,
            tess::WorldArchiveStatus::ResidencyMismatch);

  changed = bytes;
  const auto first_chunk_state = kHeaderSize +
                                 Archive::field_count * kFieldDescriptorSize +
                                 sizeof(std::uint64_t);
  changed[first_chunk_state] = std::byte{2};
  refresh_body_checksum(changed);
  EXPECT_EQ(tess::inspect_world_archive(changed).status,
            tess::WorldArchiveStatus::InvalidChunk);
}

TEST(TessPersistence, CompleteEnvelopeWithImpossibleFieldTableIsCorrupt) {
  DenseWorld source;
  std::vector<std::byte> bytes;
  ASSERT_EQ(tess::save_world_archive<Archive>(source, bytes).status,
            tess::WorldArchiveStatus::Ok);

  const auto descriptor_bytes = Archive::field_count * kFieldDescriptorSize;
  bytes.resize(kHeaderSize + descriptor_bytes);
  write_unsigned_le(bytes, kBodySizeOffset,
                    static_cast<std::uint64_t>(descriptor_bytes));
  write_unsigned_le(bytes, kFieldCountOffset, std::uint32_t{3});
  write_unsigned_le(bytes, kChunkCountOffset, std::uint64_t{0});
  refresh_body_checksum(bytes);
  EXPECT_EQ(tess::inspect_world_archive(bytes).status,
            tess::WorldArchiveStatus::Corrupt);
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

TEST(TessPersistence, SparseLoadRematerializesPagesWithNewGenerations) {
  const tess::ResidencyConfig two_chunks{SparseWorld::page_byte_size * 2};
  SparseWorld source(two_chunks);
  SparseWorld target(two_chunks);
  for (const auto key : {tess::ChunkKey{1}, tess::ChunkKey{3}}) {
    source.ensure_resident(key);
    target.ensure_resident(key);
    fill_chunk(source, key, static_cast<std::uint16_t>(key.value * 10));
    fill_chunk(target, key, static_cast<std::uint16_t>(key.value * 20));
    target.mark_topology_dirty(
        key, 1U,
        {tess::coord<Shape>(tess::chunk_coord<Shape>(key), tess::LocalTileId{}),
         Shape::chunk});
  }
  const auto old_handle = target.ensure_resident(tess::ChunkKey{1});
  const auto old_generation = target.residency_generation(tess::ChunkKey{1});
  tess::LocalTopologyScratch topology_scratch;
  tess::SparseRegionGraph graph;
  ASSERT_EQ((tess::build_region_graph<SparseWorld, TerrainTag>(
                 target, topology_scratch, graph))
                .status,
            tess::TopologyStatus::Built);
  ASSERT_TRUE(tess::is_region_graph_fresh(target, graph));

  std::vector<std::byte> bytes;
  ASSERT_EQ(tess::save_world_archive<Archive>(source, bytes).status,
            tess::WorldArchiveStatus::Ok);
  ASSERT_EQ(tess::load_world_archive<Archive>(target, bytes, kLoadDirty).status,
            tess::WorldArchiveStatus::Ok);

  EXPECT_FALSE(target.valid(old_handle));
  EXPECT_GT(target.residency_generation(tess::ChunkKey{1}), old_generation);
  EXPECT_EQ(target.meta(tess::ChunkKey{1}).version, 1U);
  EXPECT_EQ(target.meta(tess::ChunkKey{1}).topology_version, 1U);
  EXPECT_FALSE(tess::is_region_graph_fresh(target, graph));
}

}  // namespace
