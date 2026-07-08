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
// 1,000,000 x 256 tiles in 32 x 32 x 8 chunks is ~3e10 chunks. An
// AlwaysResident world of this shape cannot be constructed; a sparse one
// with a small byte budget constructs instantly. Its footprint is the fixed
// slot pool (capacity pages sized to the budget), allocated once up front —
// resident_byte_size() reports occupancy of that pool, not lazy growth.
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

TEST(TessResidency, TinyBudgetClampsCapacityToOneWithoutUb) {
  // A budget smaller than one page must still yield a usable world (capacity
  // 1), never a zero-capacity world that would evict from an empty slot set.
  // This path is unguarded in release (asserts compile out), so it must be a
  // runtime clamp.
  Sparse<Small> world{tess::ResidencyConfig{page_bytes<Small>() / 2}};
  EXPECT_EQ(world.capacity(), 1u);

  world.ensure_resident(tess::ChunkKey{0});
  world.ensure_resident(tess::ChunkKey{1});  // evicts 0
  EXPECT_EQ(world.resident_count(), 1u);
  EXPECT_TRUE(world.is_resident(tess::ChunkKey{1}));
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

TEST(TessResidency, CoordinateAccessorsResolveThroughResidentChunks) {
  Sparse<Small> world{tess::ResidencyConfig{4 * page_bytes<Small>()}};
  constexpr auto coord = tess::Coord3{40, 20, 0};
  const auto resolved = world.resolve(coord);

  // try_field is residency-tolerant: null before the chunk is resident, even
  // though the coordinate is in bounds.
  EXPECT_EQ(world.try_field<TerrainTag>(coord), nullptr);
  EXPECT_FALSE(world.is_resident(resolved.chunk_key));

  world.ensure_resident(resolved.chunk_key);
  world.field<TerrainTag>(coord) = 55;
  world.field<CostTag>(coord) = 2.25F;

  auto* terrain = world.try_field<TerrainTag>(coord);
  ASSERT_NE(terrain, nullptr);
  EXPECT_EQ(*terrain, 55);
  EXPECT_FLOAT_EQ(world.field<CostTag>(coord), 2.25F);
  EXPECT_EQ(world.field_span<TerrainTag>(
                resolved.chunk_key)[resolved.local_tile_id.value],
            55);

  // Out-of-bounds coordinates resolve to nullptr/nullopt regardless.
  EXPECT_FALSE(world.try_resolve(tess::Coord3{-1, 0, 0}).has_value());
  EXPECT_EQ(world.try_field<TerrainTag>(tess::Coord3{128, 0, 0}), nullptr);
}

TEST(TessResidency, ResidentMetadataProtocolMatchesDenseSemantics) {
  Sparse<Small> world{tess::ResidencyConfig{4 * page_bytes<Small>()}};
  constexpr auto key = tess::ChunkKey{2};
  const auto bounds =
      tess::Box3{tess::Coord3{64, 32, 0}, tess::Extent3{2, 2, 1}};
  world.ensure_resident(key);

  world.mark_topology_dirty(key, DirtyTerrain, bounds);
  EXPECT_EQ(world.meta(key).field_dirty_flags, DirtyTerrain);
  EXPECT_EQ(world.meta(key).topology_version, 1u);

  const auto observed = world.observe_dirty(key, DirtyTerrain);
  EXPECT_EQ(observed.flags, DirtyTerrain);
  world.mark_dirty(key, DirtyCost, bounds);  // advances generation
  EXPECT_FALSE(world.clear_dirty_observed(key, observed));
  EXPECT_EQ(world.meta(key).field_dirty_flags, DirtyTerrain | DirtyCost);

  const auto refreshed = world.observe_dirty(key, DirtyTerrain | DirtyCost);
  EXPECT_TRUE(world.clear_dirty_observed(key, refreshed));
  EXPECT_EQ(world.meta(key).field_dirty_flags, 0u);

  world.set_chunk_state(key, tess::ChunkState::ResidentActive);
  EXPECT_EQ(world.chunk_state(key), tess::ChunkState::ResidentActive);
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

using DenseSmall = tess::AlwaysResidentWorld<Small, Schema>;
using SparseSmall = Sparse<Small>;

TEST(TessNodeIndexSpace, DenseSpaceIsIdentityOverTheWholeTileCount) {
  DenseSmall world;
  const tess::detail::NodeIndexSpace<DenseSmall> space{world};

  static_assert(tess::detail::NodeIndexSpace<DenseSmall>::is_dense,
                "AlwaysResident node index space must be dense.");
  EXPECT_EQ(space.capacity_hint(),
            DenseSmall::chunk_count * DenseSmall::local_tile_count);

  // Every tile is resident and maps to itself, so dense A* indexes node
  // arrays exactly as it did before the trait existed.
  for (const std::uint64_t index : {0ull, 1ull, 1023ull, 5000ull}) {
    EXPECT_TRUE(space.is_resident_index(index));
    EXPECT_EQ(space.offset(index), index);
  }
}

TEST(TessNodeIndexSpace, SparseSpaceMapsResidentTilesIntoBudgetedOffsets) {
  SparseSmall world{tess::ResidencyConfig{2 * page_bytes<Small>()}};
  const tess::detail::NodeIndexSpace<SparseSmall> space{world};

  static_assert(!tess::detail::NodeIndexSpace<SparseSmall>::is_dense,
                "SparseResident node index space must not be dense.");
  EXPECT_EQ(space.capacity_hint(),
            world.capacity() * SparseSmall::local_tile_count);

  // Chunk (0,0,0) is key 0; chunk (1,0,0) is key 1 for this 4-wide shape.
  const auto index0 = tess::tile_key<Small>(tess::Coord3{0, 0, 0}).value;
  const auto index1 = tess::tile_key<Small>(tess::Coord3{32, 0, 0}).value;

  // Nothing is resident yet, so neither tile has a node-array offset.
  EXPECT_FALSE(space.is_resident_index(index0));
  EXPECT_FALSE(space.is_resident_index(index1));

  world.ensure_resident(tess::ChunkKey{0});
  EXPECT_TRUE(space.is_resident_index(index0));
  EXPECT_FALSE(space.is_resident_index(index1));

  const auto slot0 = world.resident_slot(tess::ChunkKey{0});
  EXPECT_EQ(space.offset(index0), slot0 * SparseSmall::local_tile_count);
  EXPECT_LT(space.offset(index0), space.capacity_hint());

  // A second resident chunk occupies a disjoint offset block bounded by the
  // per-chunk tile count, so node arrays never alias across resident chunks.
  world.ensure_resident(tess::ChunkKey{1});
  const auto slot1 = world.resident_slot(tess::ChunkKey{1});
  EXPECT_NE(slot0, slot1);
  EXPECT_EQ(space.offset(index1), slot1 * SparseSmall::local_tile_count);
  EXPECT_LT(space.offset(index1), space.capacity_hint());

  const auto lo0 = slot0 * SparseSmall::local_tile_count;
  const auto lo1 = slot1 * SparseSmall::local_tile_count;
  EXPECT_TRUE(lo0 + SparseSmall::local_tile_count <= lo1 ||
              lo1 + SparseSmall::local_tile_count <= lo0);
}

TEST(TessNodeIndexSpace, OffsetUsesResidentSlotNotChunkKey) {
  // Materialize chunks out of key order so a chunk's resident slot differs from
  // its key. An offset() that indexed by chunk key instead of slot would stay
  // in bounds on this tiny world and silently corrupt; assert the slot mapping.
  SparseSmall world{tess::ResidencyConfig{2 * page_bytes<Small>()}};
  world.ensure_resident(tess::ChunkKey{1});  // takes the first slot
  world.ensure_resident(tess::ChunkKey{0});  // takes the second slot
  const tess::detail::NodeIndexSpace<SparseSmall> space{world};

  const auto slot_of_key0 = world.resident_slot(tess::ChunkKey{0});
  ASSERT_NE(slot_of_key0, static_cast<std::size_t>(0));  // slot != key value 0
  const auto index0 = tess::tile_key<Small>(tess::Coord3{0, 0, 0}).value;
  EXPECT_EQ(space.offset(index0), slot_of_key0 * SparseSmall::local_tile_count);
}

// Materializes `key` and marks every tile in it passable (a freshly resident
// page is zeroed, i.e. impassable, so the search needs tiles turned on).
void make_chunk_passable(SparseSmall& world, tess::ChunkKey key) {
  world.ensure_resident(key);
  auto& page = world.chunk(key);
  for (std::uint64_t i = 0; i < SparseSmall::local_tile_count; ++i) {
    page.field<TerrainTag>(tess::LocalTileId{i}) = 1;
  }
}

[[nodiscard]] auto sparse_astar(const SparseSmall& world, tess::Coord3 start,
                                tess::Coord3 goal, tess::PathScratch& scratch,
                                tess::MissingChunkPolicy policy) {
  return tess::astar_path<SparseSmall, TerrainTag>(
      world, tess::PathRequest{start, goal}, scratch, policy);
}

TEST(TessSparseAstar, FindsRouteWithinAndAcrossResidentChunks) {
  SparseSmall world{tess::ResidencyConfig{4 * page_bytes<Small>()}};
  make_chunk_passable(world, tess::ChunkKey{0});  // x in [0, 32)
  make_chunk_passable(world, tess::ChunkKey{1});  // x in [32, 64)
  tess::PathScratch scratch;

  const auto within = sparse_astar(world, {0, 0, 0}, {10, 0, 0}, scratch,
                                   tess::MissingChunkPolicy::TreatAsBlocked);
  EXPECT_EQ(within.status, tess::PathStatus::Found);
  EXPECT_EQ(within.cost, 10u);

  // Crossing the chunk-0/chunk-1 boundary succeeds because both are resident.
  const auto across = sparse_astar(world, {0, 0, 0}, {40, 0, 0}, scratch,
                                   tess::MissingChunkPolicy::TreatAsBlocked);
  EXPECT_EQ(across.status, tess::PathStatus::Found);
  EXPECT_EQ(across.cost, 40u);
}

TEST(TessSparseAstar, MissingChunkOnRouteIsBlockedOrIndeterminateByPolicy) {
  // Start in chunk 0 and goal in chunk 2, with chunk 1 (the only bridge)
  // deliberately not resident. Both endpoints are resident so the search runs.
  SparseSmall world{tess::ResidencyConfig{4 * page_bytes<Small>()}};
  make_chunk_passable(world, tess::ChunkKey{0});  // x in [0, 32)
  make_chunk_passable(world, tess::ChunkKey{2});  // x in [64, 96)
  ASSERT_FALSE(world.is_resident(tess::ChunkKey{1}));
  tess::PathScratch scratch;

  const auto blocked = sparse_astar(world, {0, 0, 0}, {64, 0, 0}, scratch,
                                    tess::MissingChunkPolicy::TreatAsBlocked);
  EXPECT_EQ(blocked.status, tess::PathStatus::NoPath);

  const auto indeterminate =
      sparse_astar(world, {0, 0, 0}, {64, 0, 0}, scratch,
                   tess::MissingChunkPolicy::Indeterminate);
  EXPECT_EQ(indeterminate.status, tess::PathStatus::Indeterminate);
}

TEST(TessSparseAstar, RealWallStaysNoPathEvenUnderIndeterminate) {
  // A single-chunk world has no neighboring chunks, so the reachable region is
  // bounded only by the world edges and a real wall. The search never touches a
  // non-resident neighbor, so Indeterminate must not fire for a genuine wall.
  using Solo = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{32, 32, 1}>;
  using SparseSolo = tess::SparseResidentWorld<Solo, Schema>;
  static_assert(SparseSolo::chunk_count == 1);

  SparseSolo world{tess::ResidencyConfig{page_bytes<Solo>()}};
  world.ensure_resident(tess::ChunkKey{0});
  auto& page = world.chunk(tess::ChunkKey{0});
  for (std::uint64_t i = 0; i < SparseSolo::local_tile_count; ++i) {
    page.field<TerrainTag>(tess::LocalTileId{i}) = 1;
  }
  for (std::int32_t y = 0; y < 32; ++y) {
    world.field<TerrainTag>(tess::Coord3{5, y, 0}) = 0;  // full-height wall
  }
  tess::PathScratch scratch;

  const auto result = tess::astar_path<SparseSolo, TerrainTag>(
      world, tess::PathRequest{{0, 0, 0}, {10, 0, 0}}, scratch,
      tess::MissingChunkPolicy::Indeterminate);
  EXPECT_EQ(result.status, tess::PathStatus::NoPath);
}

TEST(TessSparseAstar, NonResidentEndpointsRespectPolicy) {
  SparseSmall world{tess::ResidencyConfig{2 * page_bytes<Small>()}};
  make_chunk_passable(world, tess::ChunkKey{0});
  // Goal (40,0,0) lives in chunk 1, which is not resident.
  ASSERT_FALSE(world.is_resident(tess::ChunkKey{1}));
  tess::PathScratch scratch;

  const auto blocked = sparse_astar(world, {0, 0, 0}, {40, 0, 0}, scratch,
                                    tess::MissingChunkPolicy::TreatAsBlocked);
  EXPECT_EQ(blocked.status, tess::PathStatus::InvalidGoal);

  const auto indeterminate =
      sparse_astar(world, {0, 0, 0}, {40, 0, 0}, scratch,
                   tess::MissingChunkPolicy::Indeterminate);
  EXPECT_EQ(indeterminate.status, tess::PathStatus::Indeterminate);

  // A non-resident start is symmetric: chunk 3 holds (100,0,0).
  const auto bad_start = sparse_astar(world, {100, 0, 0}, {0, 0, 0}, scratch,
                                      tess::MissingChunkPolicy::Indeterminate);
  EXPECT_EQ(bad_start.status, tess::PathStatus::Indeterminate);

  const auto bad_start_blocked =
      sparse_astar(world, {100, 0, 0}, {0, 0, 0}, scratch,
                   tess::MissingChunkPolicy::TreatAsBlocked);
  EXPECT_EQ(bad_start_blocked.status, tess::PathStatus::InvalidStart);
}

TEST(TessSparseAstar, FindsRouteWhenResidentSlotsDifferFromChunkKeys) {
  // Resident slots assigned out of key order, so the search must route through
  // the resident-slot mapping, not the chunk keys.
  SparseSmall world{tess::ResidencyConfig{2 * page_bytes<Small>()}};
  make_chunk_passable(world, tess::ChunkKey{1});  // slot 0
  make_chunk_passable(world, tess::ChunkKey{0});  // slot 1
  ASSERT_NE(world.resident_slot(tess::ChunkKey{0}),
            static_cast<std::size_t>(0));
  tess::PathScratch scratch;

  const auto across = sparse_astar(world, {0, 0, 0}, {40, 0, 0}, scratch,
                                   tess::MissingChunkPolicy::TreatAsBlocked);
  EXPECT_EQ(across.status, tess::PathStatus::Found);
  EXPECT_EQ(across.cost, 40u);
}

// weighted_astar_path shares astar_path's sparse machinery (dense-only scan,
// resident-slot node arrays, missing-chunk policy) but needs an integral entry
// cost, so it gets its own schema and helper. The scenarios mirror the
// unweighted sparse tests one-to-one to prove the ported search behaves
// identically at every non-resident boundary.
struct WeightCostTag {};
using WeightedSchema =
    tess::FieldSchema<TerrainField, tess::Field<WeightCostTag, std::uint32_t>>;
using SparseWeighted = tess::SparseResidentWorld<Small, WeightedSchema>;

// A freshly resident page is zeroed (impassable, zero entry cost). Weighted A*
// needs both a passable terrain tile and a positive integral entry cost.
void make_chunk_weighted_passable(SparseWeighted& world, tess::ChunkKey key) {
  world.ensure_resident(key);
  auto& page = world.chunk(key);
  for (std::uint64_t i = 0; i < SparseWeighted::local_tile_count; ++i) {
    page.field<TerrainTag>(tess::LocalTileId{i}) = 1;
    page.field<WeightCostTag>(tess::LocalTileId{i}) = 1;
  }
}

[[nodiscard]] auto sparse_weighted(const SparseWeighted& world,
                                   tess::Coord3 start, tess::Coord3 goal,
                                   tess::PathScratch& scratch,
                                   tess::MissingChunkPolicy policy) {
  return tess::weighted_astar_path<SparseWeighted, TerrainTag, WeightCostTag>(
      world, tess::PathRequest{start, goal}, scratch, policy);
}

TEST(TessSparseWeightedAstar, FindsRouteWithinAndAcrossResidentChunks) {
  SparseWeighted world{
      tess::ResidencyConfig{4 * SparseWeighted::page_byte_size}};
  make_chunk_weighted_passable(world, tess::ChunkKey{0});  // x in [0, 32)
  make_chunk_weighted_passable(world, tess::ChunkKey{1});  // x in [32, 64)
  tess::PathScratch scratch;

  // Sparse worlds skip the dense-only fast-path scan, so both of these route
  // through the full weighted A* over resident-slot node arrays.
  const auto within = sparse_weighted(world, {0, 0, 0}, {10, 0, 0}, scratch,
                                      tess::MissingChunkPolicy::TreatAsBlocked);
  EXPECT_EQ(within.status, tess::PathStatus::Found);
  EXPECT_EQ(within.cost, 10u);

  const auto across = sparse_weighted(world, {0, 0, 0}, {40, 0, 0}, scratch,
                                      tess::MissingChunkPolicy::TreatAsBlocked);
  EXPECT_EQ(across.status, tess::PathStatus::Found);
  EXPECT_EQ(across.cost, 40u);
}

TEST(TessSparseWeightedAstar,
     MissingChunkOnRouteIsBlockedOrIndeterminateByPolicy) {
  SparseWeighted world{
      tess::ResidencyConfig{4 * SparseWeighted::page_byte_size}};
  make_chunk_weighted_passable(world, tess::ChunkKey{0});  // x in [0, 32)
  make_chunk_weighted_passable(world, tess::ChunkKey{2});  // x in [64, 96)
  ASSERT_FALSE(world.is_resident(tess::ChunkKey{1}));
  tess::PathScratch scratch;

  const auto blocked =
      sparse_weighted(world, {0, 0, 0}, {64, 0, 0}, scratch,
                      tess::MissingChunkPolicy::TreatAsBlocked);
  EXPECT_EQ(blocked.status, tess::PathStatus::NoPath);

  const auto indeterminate =
      sparse_weighted(world, {0, 0, 0}, {64, 0, 0}, scratch,
                      tess::MissingChunkPolicy::Indeterminate);
  EXPECT_EQ(indeterminate.status, tess::PathStatus::Indeterminate);
}

TEST(TessSparseWeightedAstar, RealWallStaysNoPathEvenUnderIndeterminate) {
  using Solo = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{32, 32, 1}>;
  using SparseWeightedSolo = tess::SparseResidentWorld<Solo, WeightedSchema>;
  static_assert(SparseWeightedSolo::chunk_count == 1);

  SparseWeightedSolo world{
      tess::ResidencyConfig{SparseWeightedSolo::page_byte_size}};
  world.ensure_resident(tess::ChunkKey{0});
  auto& page = world.chunk(tess::ChunkKey{0});
  for (std::uint64_t i = 0; i < SparseWeightedSolo::local_tile_count; ++i) {
    page.field<TerrainTag>(tess::LocalTileId{i}) = 1;
    page.field<WeightCostTag>(tess::LocalTileId{i}) = 1;
  }
  for (std::int32_t y = 0; y < 32; ++y) {
    world.field<TerrainTag>(tess::Coord3{5, y, 0}) = 0;  // full-height wall
  }
  tess::PathScratch scratch;

  const auto result =
      tess::weighted_astar_path<SparseWeightedSolo, TerrainTag, WeightCostTag>(
          world, tess::PathRequest{{0, 0, 0}, {10, 0, 0}}, scratch,
          tess::MissingChunkPolicy::Indeterminate);
  EXPECT_EQ(result.status, tess::PathStatus::NoPath);
}

TEST(TessSparseWeightedAstar, NonResidentEndpointsRespectPolicy) {
  SparseWeighted world{
      tess::ResidencyConfig{2 * SparseWeighted::page_byte_size}};
  make_chunk_weighted_passable(world, tess::ChunkKey{0});
  ASSERT_FALSE(world.is_resident(tess::ChunkKey{1}));  // goal (40,0,0)'s chunk
  tess::PathScratch scratch;

  const auto blocked =
      sparse_weighted(world, {0, 0, 0}, {40, 0, 0}, scratch,
                      tess::MissingChunkPolicy::TreatAsBlocked);
  EXPECT_EQ(blocked.status, tess::PathStatus::InvalidGoal);

  const auto indeterminate =
      sparse_weighted(world, {0, 0, 0}, {40, 0, 0}, scratch,
                      tess::MissingChunkPolicy::Indeterminate);
  EXPECT_EQ(indeterminate.status, tess::PathStatus::Indeterminate);

  const auto bad_start =
      sparse_weighted(world, {100, 0, 0}, {0, 0, 0}, scratch,
                      tess::MissingChunkPolicy::Indeterminate);
  EXPECT_EQ(bad_start.status, tess::PathStatus::Indeterminate);

  const auto bad_start_blocked =
      sparse_weighted(world, {100, 0, 0}, {0, 0, 0}, scratch,
                      tess::MissingChunkPolicy::TreatAsBlocked);
  EXPECT_EQ(bad_start_blocked.status, tess::PathStatus::InvalidStart);
}

TEST(TessSparseWeightedAstar, FindsRouteWhenResidentSlotsDifferFromChunkKeys) {
  SparseWeighted world{
      tess::ResidencyConfig{2 * SparseWeighted::page_byte_size}};
  make_chunk_weighted_passable(world, tess::ChunkKey{1});  // slot 0
  make_chunk_weighted_passable(world, tess::ChunkKey{0});  // slot 1
  ASSERT_NE(world.resident_slot(tess::ChunkKey{0}),
            static_cast<std::size_t>(0));
  tess::PathScratch scratch;

  const auto across = sparse_weighted(world, {0, 0, 0}, {40, 0, 0}, scratch,
                                      tess::MissingChunkPolicy::TreatAsBlocked);
  EXPECT_EQ(across.status, tess::PathStatus::Found);
  EXPECT_EQ(across.cost, 40u);
}

// The unweighted distance-field pair reuses the astar passability harness
// (build_distance_field floods over is_passable<TerrainTag>; distance_field_
// path is a pure gradient reader). build_distance_field mirrors astar_path's
// missing-chunk policy; distance_field_path only needs offset safety.
[[nodiscard]] auto sparse_build_field(const SparseSmall& world,
                                      tess::Coord3 goal,
                                      tess::DistanceFieldScratch& scratch,
                                      tess::MissingChunkPolicy policy) {
  return tess::build_distance_field<SparseSmall, TerrainTag>(world, goal,
                                                             scratch, policy);
}

[[nodiscard]] auto sparse_field_path(const SparseSmall& world,
                                     tess::Coord3 start, tess::Coord3 goal,
                                     tess::DistanceFieldScratch& scratch) {
  return tess::distance_field_path<SparseSmall, TerrainTag>(world, start, goal,
                                                            scratch);
}

TEST(TessSparseDistanceField, BuildsFieldAndExtractsPathAcrossResidentChunks) {
  SparseSmall world{tess::ResidencyConfig{4 * page_bytes<Small>()}};
  make_chunk_passable(world, tess::ChunkKey{0});  // x in [0, 32)
  make_chunk_passable(world, tess::ChunkKey{1});  // x in [32, 64)
  tess::DistanceFieldScratch scratch;

  const auto field = sparse_build_field(
      world, {0, 0, 0}, scratch, tess::MissingChunkPolicy::TreatAsBlocked);
  EXPECT_EQ(field.status, tess::PathStatus::Found);

  const auto within = sparse_field_path(world, {10, 0, 0}, {0, 0, 0}, scratch);
  EXPECT_EQ(within.status, tess::PathStatus::Found);
  EXPECT_EQ(within.cost, 10u);

  // The flood covers both resident chunks, so a start in chunk 1 descends home.
  const auto across = sparse_field_path(world, {40, 0, 0}, {0, 0, 0}, scratch);
  EXPECT_EQ(across.status, tess::PathStatus::Found);
  EXPECT_EQ(across.cost, 40u);
}

TEST(TessSparseDistanceField,
     MissingChunkTruncatesFieldBlockedOrIndeterminateByPolicy) {
  SparseSmall world{tess::ResidencyConfig{4 * page_bytes<Small>()}};
  make_chunk_passable(world, tess::ChunkKey{0});  // x in [0, 32)
  make_chunk_passable(world, tess::ChunkKey{2});  // x in [64, 96)
  ASSERT_FALSE(world.is_resident(tess::ChunkKey{1}));
  tess::DistanceFieldScratch scratch;

  const auto blocked = sparse_build_field(
      world, {0, 0, 0}, scratch, tess::MissingChunkPolicy::TreatAsBlocked);
  EXPECT_EQ(blocked.status, tess::PathStatus::Found);

  // The field stops at chunk 0's edge; chunk 2 is disconnected by the missing
  // chunk 1, so a start there is unreached and yields no path.
  const auto unreached =
      sparse_field_path(world, {64, 0, 0}, {0, 0, 0}, scratch);
  EXPECT_EQ(unreached.status, tess::PathStatus::NoPath);

  const auto indeterminate = sparse_build_field(
      world, {0, 0, 0}, scratch, tess::MissingChunkPolicy::Indeterminate);
  EXPECT_EQ(indeterminate.status, tess::PathStatus::Indeterminate);
}

TEST(TessSparseDistanceField,
     RealWallTruncatesFieldButStaysFoundUnderIndeterminate) {
  using Solo = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{32, 32, 1}>;
  using SparseSolo = tess::SparseResidentWorld<Solo, Schema>;
  static_assert(SparseSolo::chunk_count == 1);

  SparseSolo world{tess::ResidencyConfig{page_bytes<Solo>()}};
  world.ensure_resident(tess::ChunkKey{0});
  auto& page = world.chunk(tess::ChunkKey{0});
  for (std::uint64_t i = 0; i < SparseSolo::local_tile_count; ++i) {
    page.field<TerrainTag>(tess::LocalTileId{i}) = 1;
  }
  for (std::int32_t y = 0; y < 32; ++y) {
    world.field<TerrainTag>(tess::Coord3{5, y, 0}) = 0;  // full-height wall
  }
  tess::DistanceFieldScratch scratch;

  // No neighboring chunks exist, so the flood never skips a missing chunk; a
  // genuine wall must not raise Indeterminate.
  const auto field = tess::build_distance_field<SparseSolo, TerrainTag>(
      world, {0, 0, 0}, scratch, tess::MissingChunkPolicy::Indeterminate);
  EXPECT_EQ(field.status, tess::PathStatus::Found);

  // The wall genuinely blocks the far side, so no path can be extracted there.
  const auto blocked = tess::distance_field_path<SparseSolo, TerrainTag>(
      world, {10, 0, 0}, {0, 0, 0}, scratch);
  EXPECT_EQ(blocked.status, tess::PathStatus::NoPath);
}

TEST(TessSparseDistanceField, NonResidentGoalRespectsPolicy) {
  SparseSmall world{tess::ResidencyConfig{2 * page_bytes<Small>()}};
  make_chunk_passable(world, tess::ChunkKey{0});
  ASSERT_FALSE(world.is_resident(tess::ChunkKey{1}));  // (40,0,0)'s chunk
  tess::DistanceFieldScratch scratch;

  const auto blocked = sparse_build_field(
      world, {40, 0, 0}, scratch, tess::MissingChunkPolicy::TreatAsBlocked);
  EXPECT_EQ(blocked.status, tess::PathStatus::InvalidGoal);

  const auto indeterminate = sparse_build_field(
      world, {40, 0, 0}, scratch, tess::MissingChunkPolicy::Indeterminate);
  EXPECT_EQ(indeterminate.status, tess::PathStatus::Indeterminate);
}

TEST(TessSparseDistanceField, NonResidentStartPathIsInvalidStart) {
  SparseSmall world{tess::ResidencyConfig{2 * page_bytes<Small>()}};
  make_chunk_passable(world, tess::ChunkKey{0});
  ASSERT_FALSE(world.is_resident(tess::ChunkKey{1}));
  tess::DistanceFieldScratch scratch;

  const auto field = sparse_build_field(
      world, {0, 0, 0}, scratch, tess::MissingChunkPolicy::TreatAsBlocked);
  ASSERT_EQ(field.status, tess::PathStatus::Found);

  // (40,0,0) lives in the non-resident chunk 1; it is not in the field.
  const auto path = sparse_field_path(world, {40, 0, 0}, {0, 0, 0}, scratch);
  EXPECT_EQ(path.status, tess::PathStatus::InvalidStart);
}

TEST(TessSparseDistanceField, FieldWorksWhenResidentSlotsDifferFromChunkKeys) {
  SparseSmall world{tess::ResidencyConfig{2 * page_bytes<Small>()}};
  make_chunk_passable(world, tess::ChunkKey{1});  // slot 0
  make_chunk_passable(world, tess::ChunkKey{0});  // slot 1
  ASSERT_NE(world.resident_slot(tess::ChunkKey{0}),
            static_cast<std::size_t>(0));
  tess::DistanceFieldScratch scratch;

  const auto field = sparse_build_field(
      world, {0, 0, 0}, scratch, tess::MissingChunkPolicy::TreatAsBlocked);
  ASSERT_EQ(field.status, tess::PathStatus::Found);

  const auto across = sparse_field_path(world, {40, 0, 0}, {0, 0, 0}, scratch);
  EXPECT_EQ(across.status, tess::PathStatus::Found);
  EXPECT_EQ(across.cost, 40u);
}

}  // namespace
