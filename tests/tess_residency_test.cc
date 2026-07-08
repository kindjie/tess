#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "allocation_counter.h"

namespace {

struct TerrainTag {};
struct CostTag {};

constexpr std::uint32_t DirtyTerrain = 1u << 0u;
constexpr std::uint32_t DirtyCost = 1u << 1u;
constexpr std::uint32_t ActiveFire = 1u << 0u;

using TerrainField = tess::Field<TerrainTag, std::uint16_t>;
using CostField = tess::Field<CostTag, float>;
using Schema = tess::FieldSchema<TerrainField, CostField>;

// A bounded world far too large to ever materialize densely: 1,000,000 x
// 1,000,000 x 256 tiles in 32 x 32 x 8 chunks is ~3.05e13 chunks. An
// AlwaysResident world of this shape cannot be constructed; a sparse one
// with a small byte budget must construct instantly and allocate nothing
// until chunks are made resident.
using HugeSparse = tess::Shape<tess::Extent3{1'000'000, 1'000'000, 256},
                               tess::Extent3{32, 32, 8}>;
using Small = tess::Shape<tess::Extent3{128, 128, 1}, tess::Extent3{32, 32, 1}>;

template <typename Shape>
using Sparse = tess::SparseResidentWorld<Shape, Schema>;

template <typename Shape>
[[nodiscard]] auto page_bytes() -> std::size_t {
  return Sparse<Shape>::page_byte_size;
}

template <typename Shape>
[[nodiscard]] auto sorted_resident(const Sparse<Shape>& world)
    -> std::vector<std::uint64_t> {
  std::vector<std::uint64_t> keys;
  for (const auto key : world.resident_chunk_keys()) {
    keys.push_back(key.value);
  }
  std::sort(keys.begin(), keys.end());
  return keys;
}

TEST(TessResidency, HugeSparseWorldConstructsAndStaysEmptyUntilResident) {
  Sparse<HugeSparse> world{tess::ResidencyConfig{4 * page_bytes<HugeSparse>()}};

  static_assert(Sparse<HugeSparse>::chunk_count > 1'000'000'000ull,
                "HugeSparse must be far too large to materialize densely.");

  EXPECT_EQ(world.capacity(), 4u);
  EXPECT_EQ(world.resident_count(), 0u);
  EXPECT_EQ(world.resident_byte_size(), 0u);
  EXPECT_EQ(world.byte_budget(), 4 * page_bytes<HugeSparse>());
  EXPECT_TRUE(world.resident_chunk_keys().empty());

  // In-bounds vs out-of-bounds is distinct from residency.
  EXPECT_TRUE(world.contains(tess::ChunkKey{0}));
  EXPECT_TRUE(
      world.contains(tess::ChunkKey{Sparse<HugeSparse>::chunk_count - 1}));
  EXPECT_FALSE(world.contains(tess::ChunkKey{Sparse<HugeSparse>::chunk_count}));
  EXPECT_FALSE(world.is_resident(tess::ChunkKey{0}));
}

TEST(TessResidency, EnsureResidentMaterializesZeroedChunkAndReportsResidency) {
  Sparse<Small> world{tess::ResidencyConfig{8 * page_bytes<Small>()}};
  constexpr auto key = tess::ChunkKey{5};

  EXPECT_FALSE(world.is_resident(key));
  EXPECT_EQ(world.try_chunk(key), nullptr);
  EXPECT_EQ(world.try_meta(key), nullptr);

  const auto handle = world.ensure_resident(key);
  EXPECT_EQ(handle.key, key);
  EXPECT_TRUE(world.is_resident(key));
  EXPECT_TRUE(world.valid(handle));
  EXPECT_EQ(world.resident_count(), 1u);
  EXPECT_EQ(world.resident_byte_size(), page_bytes<Small>());

  auto* page = world.try_chunk(key);
  ASSERT_NE(page, nullptr);
  EXPECT_EQ(page->chunk_key(), key);
  EXPECT_EQ(page->chunk_coord(), tess::chunk_coord<Small>(key));

  // Freshly resident fields are zero-initialized.
  for (const auto value : page->field_span<TerrainTag>()) {
    EXPECT_EQ(value, 0u);
  }
  page->field<TerrainTag>(tess::LocalTileId{3}) = 42;
  EXPECT_EQ(world.chunk(key).field<TerrainTag>(tess::LocalTileId{3}), 42);
}

TEST(TessResidency, OutOfBoundsKeysAreNeverResidentAndYieldNullptr) {
  Sparse<Small> world{tess::ResidencyConfig{2 * page_bytes<Small>()}};
  const auto oob = tess::ChunkKey{Sparse<Small>::chunk_count};

  EXPECT_FALSE(world.contains(oob));
  EXPECT_FALSE(world.is_resident(oob));
  EXPECT_EQ(world.try_chunk(oob), nullptr);
  EXPECT_EQ(world.try_meta(oob), nullptr);
  EXPECT_EQ(world.residency_generation(oob), 0u);
}

TEST(TessResidency, EnsureResidentIsIdempotentAndPreservesData) {
  Sparse<Small> world{tess::ResidencyConfig{4 * page_bytes<Small>()}};
  constexpr auto key = tess::ChunkKey{2};

  const auto first = world.ensure_resident(key);
  world.chunk(key).field<CostTag>(tess::LocalTileId{7}) = 3.5F;

  const auto second = world.ensure_resident(key);
  EXPECT_EQ(first.generation, second.generation);
  EXPECT_EQ(world.resident_count(), 1u);
  EXPECT_FLOAT_EQ(world.chunk(key).field<CostTag>(tess::LocalTileId{7}), 3.5F);
}

TEST(TessResidency, ByteBudgetCapsResidentBytesUnderLruEviction) {
  Sparse<Small> world{tess::ResidencyConfig{2 * page_bytes<Small>()}};
  ASSERT_EQ(world.capacity(), 2u);

  world.ensure_resident(tess::ChunkKey{0});
  world.ensure_resident(tess::ChunkKey{1});
  EXPECT_EQ(world.resident_count(), 2u);
  EXPECT_LE(world.resident_byte_size(), world.byte_budget());

  // Touch 0 so 1 is the least-recently-used victim.
  EXPECT_TRUE(world.touch(tess::ChunkKey{0}));
  world.ensure_resident(tess::ChunkKey{2});

  EXPECT_EQ(world.resident_count(), 2u);
  EXPECT_LE(world.resident_byte_size(), world.byte_budget());
  EXPECT_TRUE(world.is_resident(tess::ChunkKey{0}));
  EXPECT_FALSE(world.is_resident(tess::ChunkKey{1}));
  EXPECT_TRUE(world.is_resident(tess::ChunkKey{2}));
}

TEST(TessResidency, EvictedChunkReloadsWithFreshGenerationAndData) {
  Sparse<Small> world{tess::ResidencyConfig{1 * page_bytes<Small>()}};
  ASSERT_EQ(world.capacity(), 1u);
  constexpr auto key = tess::ChunkKey{4};

  const auto original = world.ensure_resident(key);
  world.chunk(key).field<TerrainTag>(tess::LocalTileId{1}) = 99;

  // Force eviction of `key` by making a different chunk resident.
  world.ensure_resident(tess::ChunkKey{5});
  EXPECT_FALSE(world.is_resident(key));
  EXPECT_FALSE(world.valid(original));
  EXPECT_EQ(world.residency_generation(key), 0u);

  const auto reloaded = world.ensure_resident(tess::ChunkKey{4});
  EXPECT_GT(reloaded.generation, original.generation);
  EXPECT_FALSE(world.valid(original));
  EXPECT_TRUE(world.valid(reloaded));
  // Reloaded chunk is a fresh, zeroed page; the old write is gone.
  EXPECT_EQ(world.chunk(key).field<TerrainTag>(tess::LocalTileId{1}), 0u);
}

TEST(TessResidency, ExplicitEvictReleasesResidencyAndBytes) {
  Sparse<Small> world{tess::ResidencyConfig{4 * page_bytes<Small>()}};

  world.ensure_resident(tess::ChunkKey{1});
  world.ensure_resident(tess::ChunkKey{3});
  EXPECT_EQ(world.resident_count(), 2u);

  EXPECT_TRUE(world.evict(tess::ChunkKey{1}));
  EXPECT_FALSE(world.evict(tess::ChunkKey{1}));  // already gone
  EXPECT_FALSE(world.is_resident(tess::ChunkKey{1}));
  EXPECT_TRUE(world.is_resident(tess::ChunkKey{3}));
  EXPECT_EQ(world.resident_count(), 1u);
  EXPECT_EQ(world.resident_byte_size(), page_bytes<Small>());
}

TEST(TessResidency, ResidentChunkKeysEnumerateExactlyTheResidentSet) {
  Sparse<Small> world{tess::ResidencyConfig{4 * page_bytes<Small>()}};

  world.ensure_resident(tess::ChunkKey{7});
  world.ensure_resident(tess::ChunkKey{2});
  world.ensure_resident(tess::ChunkKey{9});
  world.evict(tess::ChunkKey{2});
  world.ensure_resident(tess::ChunkKey{5});

  EXPECT_EQ(sorted_resident(world), (std::vector<std::uint64_t>{5, 7, 9}));
}

TEST(TessResidency, DirtyEnumerationVisitsOnlyResidentChunks) {
  Sparse<Small> world{tess::ResidencyConfig{4 * page_bytes<Small>()}};
  const auto one = tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}};

  world.ensure_resident(tess::ChunkKey{1});
  world.ensure_resident(tess::ChunkKey{6});
  world.ensure_resident(tess::ChunkKey{3});

  world.mark_dirty(tess::ChunkKey{6}, DirtyTerrain, one);
  world.mark_dirty(tess::ChunkKey{1}, DirtyTerrain | DirtyCost, one);

  auto dirty = world.dirty_chunks(DirtyTerrain);
  std::sort(dirty.begin(), dirty.end(), [](tess::ChunkKey a, tess::ChunkKey b) {
    return a.value < b.value;
  });
  EXPECT_EQ(dirty, (std::vector<tess::ChunkKey>{tess::ChunkKey{1},
                                                tess::ChunkKey{6}}));

  world.mark_active(tess::ChunkKey{3}, ActiveFire);
  EXPECT_EQ(world.active_chunks(ActiveFire),
            (std::vector<tess::ChunkKey>{tess::ChunkKey{3}}));
  EXPECT_EQ(world.chunk_state(tess::ChunkKey{3}),
            tess::ChunkState::ResidentActive);
}

TEST(TessResidency, ResidencySurvivesProbeChainDeletionUnderKeyCollisions) {
  // Exercise the directory's backward-shift deletion by churning many keys
  // through a small residency window; every surviving key must remain
  // findable and every evicted key must report non-resident.
  Sparse<Small> world{tess::ResidencyConfig{3 * page_bytes<Small>()}};
  ASSERT_EQ(world.capacity(), 3u);

  for (std::uint64_t round = 0; round < 64; ++round) {
    const auto key = tess::ChunkKey{round % Sparse<Small>::chunk_count};
    world.ensure_resident(key);
    ASSERT_TRUE(world.is_resident(key));
    ASSERT_LE(world.resident_count(), world.capacity());
    for (const auto resident : world.resident_chunk_keys()) {
      ASSERT_NE(world.try_chunk(resident), nullptr);
      ASSERT_TRUE(world.is_resident(resident));
    }
  }
}

TEST(TessResidency, WarmResidentSetAccessDoesNotAllocate) {
  Sparse<Small> world{tess::ResidencyConfig{4 * page_bytes<Small>()}};
  const std::vector<tess::ChunkKey> warm = {
      tess::ChunkKey{0}, tess::ChunkKey{1}, tess::ChunkKey{2},
      tess::ChunkKey{3}};

  // Warm up: materialize the full resident window once (allocations allowed).
  for (const auto key : warm) {
    world.ensure_resident(key);
  }

  std::uint64_t observed = 0;
  {
    tess_test::ScopedAllocationCounter counter;
    for (std::uint64_t i = 0; i < 1024; ++i) {
      const auto key = warm[i % warm.size()];
      const auto handle = world.ensure_resident(key);
      EXPECT_TRUE(world.valid(handle));
      world.touch(key);
      auto& page = world.chunk(key);
      page.field<TerrainTag>(tess::LocalTileId{0}) =
          static_cast<std::uint16_t>(i);
      observed += page.field<TerrainTag>(tess::LocalTileId{0});
    }
    EXPECT_EQ(counter.count(), 0u);
  }
  EXPECT_GT(observed, 0u);
}

TEST(TessResidency, EvictionReusingSlotsStaysWithinCapacityAllocations) {
  // After the slot pool is warm, evict-and-reload churn must not allocate:
  // slots and the directory are fixed-capacity.
  Sparse<Small> world{tess::ResidencyConfig{2 * page_bytes<Small>()}};

  world.ensure_resident(tess::ChunkKey{0});
  world.ensure_resident(tess::ChunkKey{1});

  {
    tess_test::ScopedAllocationCounter counter;
    for (std::uint64_t i = 0; i < 512; ++i) {
      world.ensure_resident(tess::ChunkKey{i % Sparse<Small>::chunk_count});
    }
    EXPECT_EQ(counter.count(), 0u);
  }
  EXPECT_LE(world.resident_count(), world.capacity());
}

}  // namespace
