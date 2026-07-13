#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

#include "allocation_counter.h"

namespace {

struct TerrainTag {};
struct CostTag {};

struct alignas(alignof(std::max_align_t)) MaxAligned {
  unsigned char data[alignof(std::max_align_t)];
};

constexpr std::uint32_t DirtyTerrain = 1u << 0u;
constexpr std::uint32_t DirtyCost = 1u << 1u;
constexpr std::uint32_t ActiveFluid = 1u << 0u;
constexpr std::uint32_t ActiveFire = 1u << 1u;

using TopDown2D =
    tess::Shape<tess::Extent3{128, 64, 1}, tess::Extent3{32, 16, 1}>;
using Vertical2D =
    tess::Shape<tess::Extent3{1, 64, 32}, tess::Extent3{1, 16, 8}>;
using Chunked3D =
    tess::Shape<tess::Extent3{64, 64, 32}, tess::Extent3{16, 16, 8}>;

using Schema = tess::FieldSchema<tess::Field<TerrainTag, std::uint16_t>,
                                 tess::Field<CostTag, float>>;

template <typename Shape>
using World = tess::AlwaysResidentWorld<Shape, Schema>;

template <typename WorldType>
auto visited_keys(WorldType& world, tess::ChunkDomain domain)
    -> std::vector<tess::ChunkKey> {
  std::vector<tess::ChunkKey> visited;
  tess::for_each_chunk(world, domain, tess::WritePolicy::ReadOnly,
                       [&](auto view) { visited.push_back(view.key()); });
  return visited;
}

TEST(TessBlock, ExplicitChunkDomainVisitsRequestedChunksInKeyOrder) {
  World<TopDown2D> world;
  const std::vector<tess::ChunkKey> requested{
      tess::ChunkKey{7},
      tess::ChunkKey{2},
      tess::ChunkKey{5},
  };
  const auto keys = tess::explicit_chunk_domain(requested);

  EXPECT_EQ(visited_keys(world, tess::chunk_domain(keys)),
            (std::vector<tess::ChunkKey>{
                tess::ChunkKey{2},
                tess::ChunkKey{5},
                tess::ChunkKey{7},
            }));
}

TEST(TessBlock, OwnedChunkDomainKeepsReturnedKeysAlive) {
  World<TopDown2D> world;
  const auto bounds = tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}};

  world.mark_dirty(tess::ChunkKey{7}, DirtyTerrain, bounds);
  world.mark_dirty(tess::ChunkKey{2}, DirtyTerrain, bounds);

  const auto keys = tess::dirty_chunk_domain(world, DirtyTerrain);
  EXPECT_EQ(visited_keys(world, tess::chunk_domain(keys)),
            (std::vector<tess::ChunkKey>{
                tess::ChunkKey{2},
                tess::ChunkKey{7},
            }));
}

TEST(TessBlock, DirtyChunkDomainVisitsChunksMatchingMask) {
  World<TopDown2D> world;
  const auto bounds = tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}};

  world.mark_dirty(tess::ChunkKey{7}, DirtyTerrain, bounds);
  world.mark_dirty(tess::ChunkKey{2}, DirtyCost, bounds);
  world.mark_dirty(tess::ChunkKey{5}, DirtyTerrain | DirtyCost, bounds);

  const auto keys = tess::dirty_chunk_domain(world, DirtyTerrain);

  EXPECT_EQ(visited_keys(world, tess::chunk_domain(keys)),
            (std::vector<tess::ChunkKey>{
                tess::ChunkKey{5},
                tess::ChunkKey{7},
            }));
}

TEST(TessBlock, ActiveChunkDomainVisitsChunksMatchingMask) {
  World<TopDown2D> world;

  world.mark_active(tess::ChunkKey{9}, ActiveFire);
  world.mark_active(tess::ChunkKey{1}, ActiveFluid);
  world.mark_active(tess::ChunkKey{4}, ActiveFluid | ActiveFire);

  const auto keys = tess::active_chunk_domain(world, ActiveFire);

  EXPECT_EQ(visited_keys(world, tess::chunk_domain(keys)),
            (std::vector<tess::ChunkKey>{
                tess::ChunkKey{4},
                tess::ChunkKey{9},
            }));
}

TEST(TessBlock, ChunkViewExposesIdentityBoundsPageMetadataAndFields) {
  World<TopDown2D> world;
  constexpr auto key = tess::ChunkKey{5};
  const auto keys = std::vector<tess::ChunkKey>{key};

  world.meta(key).entity_count = 12;
  world.chunk(key).template field<TerrainTag>(tess::LocalTileId{3}) = 44;

  tess::for_each_chunk<tess::WritePolicy::UniquePerChunk>(
      world, tess::chunk_domain(keys), [&](auto view) {
        auto terrain = view.template field_span<TerrainTag>();
        static_assert(
            std::is_same_v<decltype(terrain), std::span<std::uint16_t>>);

        EXPECT_EQ(view.key(), key);
        EXPECT_EQ(view.coord(), (tess::ChunkCoord3{1, 1, 0}));
        EXPECT_EQ(view.bounds(), (tess::Box3{
                                     tess::Coord3{32, 16, 0},
                                     tess::Extent3{32, 16, 1},
                                 }));
        EXPECT_EQ(&view.page(), &world.chunk(key));
        EXPECT_EQ(&view.meta(), &world.meta(key));
        EXPECT_EQ(view.meta().entity_count, 12u);
        EXPECT_EQ(terrain[3], 44);
      });
}

TEST(TessBlock, BlockCtxExposesWorldDomainPolicySizeAndEmptyState) {
  World<TopDown2D> world;
  const auto keys = std::vector<tess::ChunkKey>{
      tess::ChunkKey{1},
      tess::ChunkKey{3},
  };
  const auto domain = tess::chunk_domain(keys);
  const auto ctx =
      tess::block_ctx<tess::WritePolicy::UniquePerTile>(world, domain);

  EXPECT_EQ(&ctx.world(), &world);
  EXPECT_EQ(ctx.domain().keys().data(), domain.keys().data());
  EXPECT_EQ(ctx.domain().size(), domain.size());
  EXPECT_EQ(ctx.policy(), tess::WritePolicy::UniquePerTile);
  EXPECT_EQ(ctx.size(), keys.size());
  EXPECT_FALSE(ctx.empty());

  const auto empty_ctx =
      tess::block_ctx<tess::WritePolicy::ReadOnly>(world, tess::ChunkDomain{});
  EXPECT_EQ(empty_ctx.size(), 0u);
  EXPECT_TRUE(empty_ctx.empty());
}

TEST(TessBlock, BlockScratchReportsCapacityUsedAndRemainingBytes) {
  tess::BlockScratch scratch;

  EXPECT_EQ(scratch.capacity_bytes(), 0u);
  EXPECT_EQ(scratch.used_bytes(), 0u);
  EXPECT_EQ(scratch.remaining_bytes(), 0u);

  scratch.reserve_bytes(17);

  EXPECT_GE(scratch.capacity_bytes(), 17u);
  EXPECT_EQ(scratch.used_bytes(), 0u);
  EXPECT_EQ(scratch.remaining_bytes(), scratch.capacity_bytes());

  const auto values = scratch.allocate<std::uint32_t>(3);

  ASSERT_EQ(values.size(), 3u);
  EXPECT_GE(scratch.used_bytes(), sizeof(std::uint32_t) * values.size());
  EXPECT_EQ(scratch.remaining_bytes(),
            scratch.capacity_bytes() - scratch.used_bytes());
}

TEST(TessBlock, BlockScratchTypedAllocationsAreSizedAlignedAndWritable) {
  tess::BlockScratch scratch;
  scratch.reserve_bytes(64);

  const auto bytes = scratch.allocate<std::uint8_t>(1);
  auto values = scratch.allocate<std::uint32_t>(3);

  ASSERT_EQ(bytes.size(), 1u);
  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ(
      reinterpret_cast<std::uintptr_t>(values.data()) % alignof(std::uint32_t),
      0u);

  values[0] = 11;
  values[1] = 22;
  values[2] = 33;

  EXPECT_EQ(values[0], 11u);
  EXPECT_EQ(values[1], 22u);
  EXPECT_EQ(values[2], 33u);
}

TEST(TessBlock, BlockScratchResetReusesStorageWithoutClearingCapacity) {
  tess::BlockScratch scratch;
  scratch.reserve_bytes(64);

  const auto capacity = scratch.capacity_bytes();
  auto first = scratch.allocate<std::uint32_t>(4);
  ASSERT_EQ(first.size(), 4u);
  first[0] = 99;
  EXPECT_GT(scratch.used_bytes(), 0u);

  scratch.reset();

  EXPECT_EQ(scratch.capacity_bytes(), capacity);
  EXPECT_EQ(scratch.used_bytes(), 0u);
  EXPECT_EQ(scratch.remaining_bytes(), capacity);

  auto second = scratch.allocate<std::uint32_t>(4);
  ASSERT_EQ(second.size(), 4u);
  EXPECT_EQ(second.data(), first.data());
}

TEST(TessBlock, BlockScratchExhaustionReturnsEmptySpanWithoutAdvancing) {
  tess::BlockScratch scratch;
  scratch.reserve_bytes(sizeof(std::max_align_t));

  const auto first = scratch.allocate<std::max_align_t>(1);
  ASSERT_EQ(first.size(), 1u);
  const auto used = scratch.used_bytes();

  const auto exhausted = scratch.allocate<std::uint8_t>(1);

  EXPECT_TRUE(exhausted.empty());
  EXPECT_EQ(exhausted.data(), nullptr);
  EXPECT_EQ(scratch.used_bytes(), used);
}

TEST(TessBlock, BlockScratchZeroCountAllocationReturnsEmptyWithoutAdvancing) {
  tess::BlockScratch scratch;

  const auto unreserved = scratch.allocate<std::uint32_t>(0);
  EXPECT_TRUE(unreserved.empty());
  EXPECT_EQ(unreserved.data(), nullptr);
  EXPECT_EQ(scratch.used_bytes(), 0u);

  scratch.reserve_bytes(64);
  const auto first = scratch.allocate<std::uint32_t>(2);
  ASSERT_EQ(first.size(), 2u);
  const auto used = scratch.used_bytes();

  const auto empty = scratch.allocate<std::uint32_t>(0);

  EXPECT_TRUE(empty.empty());
  EXPECT_EQ(empty.data(), nullptr);
  EXPECT_EQ(scratch.used_bytes(), used);
}

TEST(TessBlock, BlockScratchOverflowingByteCountReturnsEmptyWithoutWrapping) {
  tess::BlockScratch scratch;
  tess::BlockDiagnostics diagnostics;
  scratch.reserve_bytes(64);

  const auto used = scratch.used_bytes();
  // count * sizeof(std::uint32_t) wraps to 0 in std::size_t arithmetic; a
  // missing guard would report success with a tiny (empty) allocation.
  constexpr auto overflow_count =
      std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t) + 1;

  const auto values = scratch.allocate<std::uint32_t>(overflow_count);

  EXPECT_TRUE(values.empty());
  EXPECT_EQ(values.data(), nullptr);
  EXPECT_EQ(scratch.used_bytes(), used);

  if (values.empty()) {
    diagnostics.record_scratch_allocation_failure();
  }
  EXPECT_EQ(diagnostics.scratch_allocation_failures(), 1u);
}

TEST(TessBlock, BlockScratchMixedAlignmentsProduceAlignedDisjointSpans) {
  tess::BlockScratch scratch;
  scratch.reserve_bytes(256);

  const auto chars = scratch.allocate<char>(3);
  const auto words = scratch.allocate<std::uint64_t>(2);
  const auto maxes = scratch.allocate<MaxAligned>(1);

  ASSERT_EQ(chars.size(), 3u);
  ASSERT_EQ(words.size(), 2u);
  ASSERT_EQ(maxes.size(), 1u);

  const auto char_begin = reinterpret_cast<std::uintptr_t>(chars.data());
  const auto word_begin = reinterpret_cast<std::uintptr_t>(words.data());
  const auto max_begin = reinterpret_cast<std::uintptr_t>(maxes.data());

  EXPECT_EQ(word_begin % alignof(std::uint64_t), 0u);
  EXPECT_EQ(max_begin % alignof(MaxAligned), 0u);

  EXPECT_LE(char_begin + chars.size_bytes(), word_begin);
  EXPECT_LE(word_begin + words.size_bytes(), max_begin);
}

TEST(TessBlock, BlockScratchGrowthKeepsUsedBytesAndServesNewAllocations) {
  tess::BlockScratch scratch;
  scratch.reserve_bytes(sizeof(std::uint32_t) * 4);

  auto first = scratch.allocate<std::uint32_t>(4);
  ASSERT_EQ(first.size(), 4u);
  const auto used = scratch.used_bytes();
  const auto old_capacity = scratch.capacity_bytes();

  // Growth invalidates previously returned spans and does not preserve
  // scratch contents; only the byte accounting carries over.
  scratch.reserve_bytes(old_capacity * 4);

  EXPECT_GE(scratch.capacity_bytes(), old_capacity * 4);
  EXPECT_EQ(scratch.used_bytes(), used);

  const auto second = scratch.allocate<std::uint64_t>(2);
  ASSERT_EQ(second.size(), 2u);
  EXPECT_EQ(
      reinterpret_cast<std::uintptr_t>(second.data()) % alignof(std::uint64_t),
      0u);
  EXPECT_GT(scratch.used_bytes(), used);
}

TEST(TessBlock, BlockDiagnosticsCountsAndResetsScratchAllocationFailures) {
  tess::BlockDiagnostics diagnostics;

  EXPECT_EQ(diagnostics.scratch_allocation_failures(), 0u);

  diagnostics.record_scratch_allocation_failure();
  diagnostics.record_scratch_allocation_failure();

  EXPECT_EQ(diagnostics.scratch_allocation_failures(), 2u);

  diagnostics.reset();

  EXPECT_EQ(diagnostics.scratch_allocation_failures(), 0u);
}

TEST(TessBlock, BlockCtxExposesOptionalScratchAndDiagnostics) {
  World<TopDown2D> world;
  const auto keys = std::vector<tess::ChunkKey>{tess::ChunkKey{1}};
  const auto domain = tess::chunk_domain(keys);

  auto no_scratch_ctx =
      tess::block_ctx<tess::WritePolicy::ReadOnly>(world, domain);
  const auto& const_no_scratch_ctx = no_scratch_ctx;

  EXPECT_EQ(no_scratch_ctx.scratch(), nullptr);
  EXPECT_EQ(const_no_scratch_ctx.scratch(), nullptr);
  EXPECT_EQ(no_scratch_ctx.diagnostics(), nullptr);
  EXPECT_EQ(const_no_scratch_ctx.diagnostics(), nullptr);
  no_scratch_ctx.reset_scratch();
  no_scratch_ctx.reset_diagnostics();

  tess::BlockScratch scratch;
  tess::BlockDiagnostics diagnostics;
  scratch.reserve_bytes(32);
  auto scratch_ctx = tess::block_ctx<tess::WritePolicy::UniquePerChunk>(
      world, domain, scratch, diagnostics);
  const auto& const_scratch_ctx = scratch_ctx;

  EXPECT_EQ(scratch_ctx.scratch(), &scratch);
  EXPECT_EQ(const_scratch_ctx.scratch(), &scratch);
  EXPECT_EQ(scratch_ctx.diagnostics(), &diagnostics);
  EXPECT_EQ(const_scratch_ctx.diagnostics(), &diagnostics);

  const auto values = scratch_ctx.scratch()->allocate<std::uint32_t>(2);
  ASSERT_EQ(values.size(), 2u);
  EXPECT_GT(scratch.used_bytes(), 0u);
  scratch_ctx.diagnostics()->record_scratch_allocation_failure();
  EXPECT_EQ(diagnostics.scratch_allocation_failures(), 1u);

  scratch_ctx.reset_scratch();
  scratch_ctx.reset_diagnostics();
  EXPECT_EQ(scratch.used_bytes(), 0u);
  EXPECT_EQ(diagnostics.scratch_allocation_failures(), 0u);

  auto diagnostics_ctx =
      tess::block_ctx<tess::WritePolicy::ReadOnly>(world, domain, diagnostics);
  EXPECT_EQ(diagnostics_ctx.scratch(), nullptr);
  EXPECT_EQ(diagnostics_ctx.diagnostics(), &diagnostics);
}

TEST(TessBlock, BlockCtxChunkViewMatchesDirectChunkViewBehavior) {
  World<TopDown2D> world;
  constexpr auto key = tess::ChunkKey{5};
  const auto keys = std::vector<tess::ChunkKey>{key};

  world.meta(key).entity_count = 12;
  world.chunk(key).template field<TerrainTag>(tess::LocalTileId{3}) = 44;

  const auto ctx = tess::block_ctx<tess::WritePolicy::UniquePerChunk>(
      world, tess::chunk_domain(keys));
  const auto view = ctx.chunk_view(key);
  const auto direct = tess::ChunkView<World<TopDown2D>>{world, key};
  auto terrain = view.template field_span<TerrainTag>();

  EXPECT_EQ(view.key(), direct.key());
  EXPECT_EQ(view.coord(), direct.coord());
  EXPECT_EQ(view.bounds(), direct.bounds());
  EXPECT_EQ(&view.page(), &direct.page());
  EXPECT_EQ(&view.meta(), &direct.meta());
  EXPECT_EQ(view.meta().entity_count, 12u);
  EXPECT_EQ(terrain[3], 44);
}

TEST(TessBlock, BlockCtxForEachChunkMatchesFreeFunctionDomainOrder) {
  World<TopDown2D> world;
  const auto keys = std::vector<tess::ChunkKey>{
      tess::ChunkKey{7},
      tess::ChunkKey{2},
      tess::ChunkKey{5},
  };
  const auto domain = tess::chunk_domain(keys);
  const auto ctx = tess::block_ctx<tess::WritePolicy::ReadOnly>(world, domain);
  std::vector<tess::ChunkKey> ctx_visited;
  std::vector<tess::ChunkKey> free_visited;

  ctx.for_each_chunk([&](auto view) { ctx_visited.push_back(view.key()); });
  tess::for_each_chunk(world, domain, tess::WritePolicy::ReadOnly,
                       [&](auto view) { free_visited.push_back(view.key()); });

  EXPECT_EQ(ctx_visited, keys);
  EXPECT_EQ(ctx_visited, free_visited);
}

TEST(TessBlock, ConstWorldIterationReturnsConstPageMetadataAndFields) {
  World<TopDown2D> world;
  constexpr auto key = tess::ChunkKey{1};
  const auto keys = std::vector<tess::ChunkKey>{key};
  const auto& const_world = world;

  tess::for_each_chunk(
      const_world, tess::chunk_domain(keys), tess::WritePolicy::ReadOnly,
      [&](auto view) {
        auto terrain = view.template field_span<TerrainTag>();
        decltype(auto) page = view.page();
        decltype(auto) meta = view.meta();

        static_assert(
            std::is_same_v<decltype(terrain), std::span<const std::uint16_t>>);
        static_assert(
            std::is_same_v<decltype(page),
                           const typename World<TopDown2D>::page_type&>);
        static_assert(std::is_same_v<decltype(meta), const tess::ChunkMeta&>);

        EXPECT_EQ(view.key(), key);
        EXPECT_EQ(&page, &const_world.chunk(key));
        EXPECT_EQ(&meta, &const_world.meta(key));
      });
}

TEST(TessBlock, ConstWorldBlockCtxReturnsConstPageMetadataAndFields) {
  World<TopDown2D> world;
  constexpr auto key = tess::ChunkKey{1};
  const auto keys = std::vector<tess::ChunkKey>{key};
  const auto& const_world = world;
  const auto ctx = tess::block_ctx<tess::WritePolicy::ReadOnly>(
      const_world, tess::chunk_domain(keys));

  ctx.for_each_chunk([&](auto view) {
    auto terrain = view.template field_span<TerrainTag>();
    decltype(auto) page = view.page();
    decltype(auto) meta = view.meta();

    static_assert(
        std::is_same_v<decltype(terrain), std::span<const std::uint16_t>>);
    static_assert(std::is_same_v<decltype(page),
                                 const typename World<TopDown2D>::page_type&>);
    static_assert(std::is_same_v<decltype(meta), const tess::ChunkMeta&>);

    EXPECT_EQ(view.key(), key);
    EXPECT_EQ(&page, &const_world.chunk(key));
    EXPECT_EQ(&meta, &const_world.meta(key));
  });
}

TEST(TessBlock, ReadOnlyPolicyReturnsConstViewsForMutableWorld) {
  World<TopDown2D> world;
  constexpr auto key = tess::ChunkKey{1};
  const auto keys = std::vector<tess::ChunkKey>{key};

  tess::for_each_chunk<tess::WritePolicy::ReadOnly>(
      world, tess::chunk_domain(keys), [&](auto view) {
        auto terrain = view.template field_span<TerrainTag>();
        decltype(auto) page = view.page();
        decltype(auto) meta = view.meta();

        static_assert(
            std::is_same_v<decltype(terrain), std::span<const std::uint16_t>>);
        static_assert(
            std::is_same_v<decltype(page),
                           const typename World<TopDown2D>::page_type&>);
        static_assert(std::is_same_v<decltype(meta), const tess::ChunkMeta&>);

        EXPECT_EQ(view.key(), key);
        EXPECT_EQ(&page, &world.chunk(key));
        EXPECT_EQ(&meta, &world.meta(key));
      });
}

TEST(TessBlock, RuntimeReadOnlyPolicyReturnsConstViewsForMutableWorld) {
  World<TopDown2D> world;
  constexpr auto key = tess::ChunkKey{1};
  const auto keys = std::vector<tess::ChunkKey>{key};

  tess::for_each_chunk(
      world, tess::chunk_domain(keys), tess::WritePolicy::ReadOnly,
      [&](tess::ChunkView<const World<TopDown2D>> view) {
        auto terrain = view.template field_span<TerrainTag>();
        decltype(auto) page = view.page();
        decltype(auto) meta = view.meta();

        static_assert(
            std::is_same_v<decltype(terrain), std::span<const std::uint16_t>>);
        static_assert(
            std::is_same_v<decltype(page),
                           const typename World<TopDown2D>::page_type&>);
        static_assert(std::is_same_v<decltype(meta), const tess::ChunkMeta&>);

        EXPECT_EQ(view.key(), key);
        EXPECT_EQ(&page, &world.chunk(key));
        EXPECT_EQ(&meta, &world.meta(key));
      });
}

TEST(TessBlock, RuntimeInvalidWritePolicyFailsFast) {
  EXPECT_DEATH(
      {
        World<TopDown2D> world;
        const auto keys = std::vector<tess::ChunkKey>{tess::ChunkKey{0}};
        tess::for_each_chunk(
            world, tess::chunk_domain(keys),
            // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
            static_cast<tess::WritePolicy>(255), [](auto) {});
      },
      ".*");
}

TEST(TessBlock, RuntimeUniqueWritePoliciesDispatchMutableViews) {
  const auto policies = std::vector<tess::WritePolicy>{
      tess::WritePolicy::UniquePerTile,
      tess::WritePolicy::UniquePerChunk,
  };

  std::uint16_t expected = 10;
  for (const auto policy : policies) {
    World<TopDown2D> world;
    constexpr auto key = tess::ChunkKey{1};
    const auto keys = std::vector<tess::ChunkKey>{key};
    std::size_t visited = 0;

    tess::for_each_chunk(
        world, tess::chunk_domain(keys), policy,
        [&](tess::ChunkView<World<TopDown2D>> view) {
          auto terrain = view.template field_span<TerrainTag>();
          static_assert(
              std::is_same_v<decltype(terrain), std::span<std::uint16_t>>);

          terrain[0] = expected;
          view.meta().entity_count = expected;
          ++visited;
        });

    EXPECT_EQ(visited, 1u);
    EXPECT_EQ(world.chunk(key).template field<TerrainTag>(tess::LocalTileId{0}),
              expected);
    EXPECT_EQ(world.meta(key).entity_count, expected);
    ++expected;
  }
}

TEST(TessBlock, RuntimeUnsafePolicyDispatchesMutableViews) {
  World<TopDown2D> world;
  constexpr auto key = tess::ChunkKey{3};
  const auto keys = std::vector<tess::ChunkKey>{key};
  std::size_t visited = 0;

  ASSERT_TRUE(tess::is_valid_write_policy(tess::WritePolicy::Unsafe));

  tess::for_each_chunk(
      world, tess::chunk_domain(keys), tess::WritePolicy::Unsafe,
      [&](tess::ChunkView<World<TopDown2D>> view) {
        auto terrain = view.template field_span<TerrainTag>();
        static_assert(
            std::is_same_v<decltype(terrain), std::span<std::uint16_t>>);

        terrain[5] = 77;
        view.meta().entity_count = 4;
        ++visited;
      });

  EXPECT_EQ(visited, 1u);
  EXPECT_EQ(world.chunk(key).template field<TerrainTag>(tess::LocalTileId{5}),
            77);
  EXPECT_EQ(world.meta(key).entity_count, 4u);
}

TEST(TessBlock, ReadOnlyBlockCtxChunkViewsAreConstForMutableWorld) {
  World<TopDown2D> world;
  constexpr auto key = tess::ChunkKey{1};
  const auto keys = std::vector<tess::ChunkKey>{key};
  const auto ctx = tess::block_ctx<tess::WritePolicy::ReadOnly>(
      world, tess::chunk_domain(keys));
  const auto view = ctx.chunk_view(key);
  using TerrainSpan = decltype(view.template field_span<TerrainTag>());
  decltype(auto) page = view.page();
  decltype(auto) meta = view.meta();

  static_assert(std::is_same_v<TerrainSpan, std::span<const std::uint16_t>>);
  static_assert(std::is_same_v<decltype(page),
                               const typename World<TopDown2D>::page_type&>);
  static_assert(std::is_same_v<decltype(meta), const tess::ChunkMeta&>);

  EXPECT_EQ(&page, &world.chunk(key));
  EXPECT_EQ(&meta, &world.meta(key));
}

TEST(TessBlock, ReadOnlyBlockCtxWorldAccessIsConstForMutableWorld) {
  World<TopDown2D> world;
  const auto keys = std::vector<tess::ChunkKey>{tess::ChunkKey{1}};
  const auto ctx = tess::block_ctx<tess::WritePolicy::ReadOnly>(
      world, tess::chunk_domain(keys));
  decltype(auto) exposed_world = ctx.world();

  static_assert(
      std::is_same_v<decltype(exposed_world), const World<TopDown2D>&>);
  EXPECT_EQ(&exposed_world, &world);
}

TEST(TessBlock, MutableWritePoliciesReturnMutableViews) {
  World<TopDown2D> world;
  constexpr auto key = tess::ChunkKey{1};
  const auto keys = std::vector<tess::ChunkKey>{key};

  const auto tile_ctx = tess::block_ctx<tess::WritePolicy::UniquePerTile>(
      world, tess::chunk_domain(keys));

  tile_ctx.for_each_chunk([](auto view) {
    auto terrain = view.template field_span<TerrainTag>();
    decltype(auto) page = view.page();
    decltype(auto) meta = view.meta();

    static_assert(std::is_same_v<decltype(terrain), std::span<std::uint16_t>>);
    static_assert(std::is_same_v<decltype(page), World<TopDown2D>::page_type&>);
    static_assert(std::is_same_v<decltype(meta), tess::ChunkMeta&>);
  });

  const auto ctx = tess::block_ctx<tess::WritePolicy::UniquePerChunk>(
      world, tess::chunk_domain(keys));
  const auto view = ctx.chunk_view(key);
  auto terrain = view.template field_span<TerrainTag>();
  decltype(auto) page = view.page();
  decltype(auto) meta = view.meta();

  static_assert(std::is_same_v<decltype(terrain), std::span<std::uint16_t>>);
  static_assert(std::is_same_v<decltype(page), World<TopDown2D>::page_type&>);
  static_assert(std::is_same_v<decltype(meta), tess::ChunkMeta&>);

  terrain[0] = 7;
  meta.entity_count = 3;
  EXPECT_EQ(page.template field<TerrainTag>(tess::LocalTileId{0}), 7);
  EXPECT_EQ(world.meta(key).entity_count, 3u);
}

TEST(TessBlock, TopDown2DTileIterationVisitsLocalIdsInOrder) {
  World<TopDown2D> world;
  const auto keys = std::vector<tess::ChunkKey>{tess::ChunkKey{0}};
  std::uint64_t expected_id = 0;

  tess::for_each_chunk(
      world, tess::chunk_domain(keys), tess::WritePolicy::ReadOnly,
      [&](auto view) {
        view.for_each_tile([&](tess::LocalTileId id, tess::LocalCoord3 coord) {
          EXPECT_EQ(id, (tess::LocalTileId{expected_id}));
          EXPECT_EQ(coord, view.local_coord(id));
          ++expected_id;
        });
      });

  EXPECT_EQ(expected_id, tess::ShapeTraits<TopDown2D>::local_tile_count);
}

TEST(TessBlock, TileIterationSupportsVertical2DWorldCoordinates) {
  World<Vertical2D> world;
  const auto keys = std::vector<tess::ChunkKey>{tess::ChunkKey{6}};
  bool saw_target = false;

  tess::for_each_chunk(
      world, tess::chunk_domain(keys), tess::WritePolicy::ReadOnly,
      [&](auto view) {
        view.for_each_tile([&](tess::LocalTileId id, tess::LocalCoord3 coord) {
          if (id == tess::LocalTileId{31}) {
            saw_target = true;
            EXPECT_EQ(coord, (tess::LocalCoord3{0, 15, 1}));
            EXPECT_EQ(view.world_coord(coord), (tess::Coord3{0, 47, 9}));
            EXPECT_EQ(view.world_coord(id), (tess::Coord3{0, 47, 9}));
          }
        });
      });

  EXPECT_TRUE(saw_target);
}

TEST(TessBlock, TileIterationSupportsChunked3DWorldCoordinates) {
  World<Chunked3D> world;
  const auto keys = std::vector<tess::ChunkKey>{tess::ChunkKey{42}};
  bool saw_target = false;

  tess::for_each_chunk(
      world, tess::chunk_domain(keys), tess::WritePolicy::ReadOnly,
      [&](auto view) {
        view.for_each_tile([&](tess::LocalTileId id, tess::LocalCoord3 coord) {
          if (id == tess::LocalTileId{287}) {
            saw_target = true;
            EXPECT_EQ(coord, (tess::LocalCoord3{15, 1, 1}));
            EXPECT_EQ(view.world_coord(coord), (tess::Coord3{47, 33, 17}));
            EXPECT_EQ(view.world_coord(id), (tess::Coord3{47, 33, 17}));
          }
        });
      });

  EXPECT_TRUE(saw_target);
}

TEST(TessBlock, LocalCoordinateHelpersRoundTripRepresentativeTiles) {
  World<Chunked3D> world;
  const auto keys = std::vector<tess::ChunkKey>{tess::ChunkKey{42}};

  tess::for_each_chunk(
      world, tess::chunk_domain(keys), tess::WritePolicy::ReadOnly,
      [](auto view) {
        constexpr auto final_id =
            tess::ShapeTraits<Chunked3D>::local_tile_count - 1;
        const auto ids = std::vector<tess::LocalTileId>{
            tess::LocalTileId{0},
            tess::LocalTileId{287},
            tess::LocalTileId{255},
            tess::LocalTileId{final_id},
        };

        for (const auto id : ids) {
          const auto coord = view.local_coord(id);
          EXPECT_EQ(view.local_tile_id(coord), id);
          EXPECT_EQ(view.world_coord(id), view.world_coord(coord));
        }

        EXPECT_EQ(view.local_coord(tess::LocalTileId{0}),
                  (tess::LocalCoord3{0, 0, 0}));
        EXPECT_EQ(view.local_coord(tess::LocalTileId{255}),
                  (tess::LocalCoord3{15, 15, 0}));
        EXPECT_EQ(view.local_coord(tess::LocalTileId{final_id}),
                  (tess::LocalCoord3{15, 15, 7}));
      });
}

TEST(TessBlock, TopDown2DBoundariesIgnoreDegenerateZAxis) {
  World<TopDown2D> world;
  const auto keys = std::vector<tess::ChunkKey>{tess::ChunkKey{0}};

  tess::for_each_chunk(
      world, tess::chunk_domain(keys), tess::WritePolicy::ReadOnly,
      [](auto view) {
        EXPECT_EQ(view.local_bounds(), (tess::Box3{
                                           tess::Coord3{0, 0, 0},
                                           tess::Extent3{32, 16, 1},
                                       }));

        EXPECT_TRUE(view.is_boundary(tess::LocalCoord3{0, 0, 0}));
        EXPECT_TRUE(view.is_boundary(tess::LocalCoord3{31, 15, 0}));
        EXPECT_TRUE(view.is_boundary(tess::LocalCoord3{17, 0, 0}));
        EXPECT_TRUE(view.is_boundary(tess::LocalCoord3{0, 9, 0}));
        EXPECT_FALSE(view.is_boundary(tess::LocalCoord3{17, 9, 0}));
        EXPECT_TRUE(view.is_interior(tess::LocalCoord3{17, 9, 0}));

        std::uint64_t boundary_count = 0;
        std::uint64_t interior_count = 0;
        view.for_each_tile([&](tess::LocalTileId, tess::LocalCoord3 coord) {
          if (view.is_boundary(coord)) {
            ++boundary_count;
          } else {
            ++interior_count;
          }
        });

        EXPECT_EQ(boundary_count, 92u);
        EXPECT_EQ(interior_count, 420u);
      });
}

TEST(TessBlock, Vertical2DBoundariesUseOnlyNonDegenerateAxes) {
  World<Vertical2D> world;
  const auto keys = std::vector<tess::ChunkKey>{tess::ChunkKey{0}};

  tess::for_each_chunk(
      world, tess::chunk_domain(keys), tess::WritePolicy::ReadOnly,
      [](auto view) {
        EXPECT_TRUE(view.is_boundary(tess::LocalCoord3{0, 0, 0}));
        EXPECT_TRUE(view.is_boundary(tess::LocalCoord3{0, 15, 7}));
        EXPECT_TRUE(view.is_boundary(tess::LocalCoord3{0, 8, 0}));
        EXPECT_TRUE(view.is_boundary(tess::LocalCoord3{0, 0, 4}));
        EXPECT_FALSE(view.is_boundary(tess::LocalCoord3{0, 8, 4}));
        EXPECT_TRUE(view.is_interior(tess::LocalCoord3{0, 8, 4}));

        std::uint64_t boundary_count = 0;
        std::uint64_t interior_count = 0;
        view.for_each_tile([&](tess::LocalTileId, tess::LocalCoord3 coord) {
          if (view.is_boundary(coord)) {
            ++boundary_count;
          } else {
            ++interior_count;
          }
        });

        EXPECT_EQ(boundary_count, 44u);
        EXPECT_EQ(interior_count, 84u);
      });
}

TEST(TessBlock, Chunked3DBoundariesUseAllAxes) {
  World<Chunked3D> world;
  const auto keys = std::vector<tess::ChunkKey>{tess::ChunkKey{0}};

  tess::for_each_chunk(
      world, tess::chunk_domain(keys), tess::WritePolicy::ReadOnly,
      [](auto view) {
        EXPECT_TRUE(view.is_boundary(tess::LocalCoord3{0, 0, 0}));
        EXPECT_TRUE(view.is_boundary(tess::LocalCoord3{15, 15, 7}));
        EXPECT_TRUE(view.is_boundary(tess::LocalCoord3{8, 7, 0}));
        EXPECT_FALSE(view.is_boundary(tess::LocalCoord3{8, 7, 4}));
        EXPECT_TRUE(view.is_interior(tess::LocalCoord3{8, 7, 4}));

        std::uint64_t boundary_count = 0;
        std::uint64_t interior_count = 0;
        view.for_each_tile([&](tess::LocalTileId, tess::LocalCoord3 coord) {
          if (view.is_boundary(coord)) {
            ++boundary_count;
          } else {
            ++interior_count;
          }
        });

        EXPECT_EQ(boundary_count, 872u);
        EXPECT_EQ(interior_count, 1176u);
      });
}

TEST(TessBlock, LocalCandidateHelpersAcceptAndRejectSignedCoordinates) {
  World<Chunked3D> world;
  const auto keys = std::vector<tess::ChunkKey>{tess::ChunkKey{42}};

  tess::for_each_chunk(
      world, tess::chunk_domain(keys), tess::WritePolicy::ReadOnly,
      [](auto view) {
        EXPECT_TRUE(view.contains_local(tess::Coord3{0, 0, 0}));
        EXPECT_TRUE(view.contains_local(tess::Coord3{8, 7, 4}));
        EXPECT_TRUE(view.contains_local(tess::Coord3{15, 15, 7}));

        EXPECT_EQ(view.try_local_coord(tess::Coord3{0, 0, 0}),
                  (tess::LocalCoord3{0, 0, 0}));
        EXPECT_EQ(view.try_local_coord(tess::Coord3{8, 7, 4}),
                  (tess::LocalCoord3{8, 7, 4}));
        EXPECT_EQ(view.try_local_coord(tess::Coord3{15, 15, 7}),
                  (tess::LocalCoord3{15, 15, 7}));

        EXPECT_FALSE(view.contains_local(tess::Coord3{-1, 0, 0}));
        EXPECT_FALSE(view.contains_local(tess::Coord3{16, 0, 0}));
        EXPECT_FALSE(view.contains_local(tess::Coord3{0, -1, 0}));
        EXPECT_FALSE(view.contains_local(tess::Coord3{0, 16, 0}));
        EXPECT_FALSE(view.contains_local(tess::Coord3{0, 0, -1}));
        EXPECT_FALSE(view.contains_local(tess::Coord3{0, 0, 8}));

        EXPECT_FALSE(view.try_local_coord(tess::Coord3{-1, 0, 0}));
        EXPECT_FALSE(view.try_local_coord(tess::Coord3{16, 0, 0}));
        EXPECT_FALSE(view.try_local_coord(tess::Coord3{0, -1, 0}));
        EXPECT_FALSE(view.try_local_coord(tess::Coord3{0, 16, 0}));
        EXPECT_FALSE(view.try_local_coord(tess::Coord3{0, 0, -1}));
        EXPECT_FALSE(view.try_local_coord(tess::Coord3{0, 0, 8}));
      });
}

TEST(TessBlock, LocalCandidateHelpersRejectDegenerateAxisOutsideChunk) {
  World<TopDown2D> world;
  const auto keys = std::vector<tess::ChunkKey>{tess::ChunkKey{0}};

  tess::for_each_chunk(
      world, tess::chunk_domain(keys), tess::WritePolicy::ReadOnly,
      [](auto view) {
        EXPECT_TRUE(view.contains_local(tess::Coord3{17, 9, 0}));
        EXPECT_FALSE(view.contains_local(tess::Coord3{17, 9, -1}));
        EXPECT_FALSE(view.contains_local(tess::Coord3{17, 9, 1}));

        EXPECT_EQ(view.try_local_coord(tess::Coord3{17, 9, 0}),
                  (tess::LocalCoord3{17, 9, 0}));
        EXPECT_FALSE(view.try_local_coord(tess::Coord3{17, 9, -1}));
        EXPECT_FALSE(view.try_local_coord(tess::Coord3{17, 9, 1}));
      });
}

TEST(TessBlock, WorldCoordConvertsSignedLocalCandidatesOutsideChunk) {
  World<Chunked3D> world;
  const auto keys = std::vector<tess::ChunkKey>{tess::ChunkKey{42}};

  tess::for_each_chunk(world, tess::chunk_domain(keys),
                       tess::WritePolicy::ReadOnly, [](auto view) {
                         EXPECT_EQ(view.coord(), (tess::ChunkCoord3{2, 2, 2}));
                         EXPECT_EQ(view.world_coord(tess::Coord3{0, 0, 0}),
                                   (tess::Coord3{32, 32, 16}));
                         EXPECT_EQ(view.world_coord(tess::Coord3{15, 15, 7}),
                                   (tess::Coord3{47, 47, 23}));
                         EXPECT_EQ(view.world_coord(tess::Coord3{-1, 8, 4}),
                                   (tess::Coord3{31, 40, 20}));
                         EXPECT_EQ(view.world_coord(tess::Coord3{16, 8, 4}),
                                   (tess::Coord3{48, 40, 20}));
                         EXPECT_EQ(view.world_coord(tess::Coord3{8, -1, 4}),
                                   (tess::Coord3{40, 31, 20}));
                         EXPECT_EQ(view.world_coord(tess::Coord3{8, 16, 4}),
                                   (tess::Coord3{40, 48, 20}));
                         EXPECT_EQ(view.world_coord(tess::Coord3{8, 7, -1}),
                                   (tess::Coord3{40, 39, 15}));
                         EXPECT_EQ(view.world_coord(tess::Coord3{8, 7, 8}),
                                   (tess::Coord3{40, 39, 24}));
                       });
}

TEST(TessBlock, ChunkViewBoundaryHelpersAreNoexcept) {
  World<Chunked3D> world;
  const auto view = tess::ChunkView<World<Chunked3D>>{world, tess::ChunkKey{0}};

  static_assert(noexcept(view.local_bounds()));
  static_assert(noexcept(view.contains_local(tess::Coord3{0, 0, 0})));
  static_assert(noexcept(view.try_local_coord(tess::Coord3{0, 0, 0})));
  static_assert(noexcept(view.is_boundary(tess::LocalCoord3{0, 0, 0})));
  static_assert(noexcept(view.is_interior(tess::LocalCoord3{1, 1, 1})));
  static_assert(noexcept(view.world_coord(tess::Coord3{0, 0, 0})));

  EXPECT_TRUE(view.contains_local(tess::Coord3{0, 0, 0}));
}

TEST(TessBlock, ConstWorldChunkViewExposesCoordinateHelpers) {
  World<Chunked3D> world;
  const auto keys = std::vector<tess::ChunkKey>{tess::ChunkKey{42}};
  const auto& const_world = world;

  tess::for_each_chunk(
      const_world, tess::chunk_domain(keys), tess::WritePolicy::ReadOnly,
      [](auto view) {
        EXPECT_EQ(view.local_coord(tess::LocalTileId{287}),
                  (tess::LocalCoord3{15, 1, 1}));
        EXPECT_EQ(view.local_tile_id(tess::LocalCoord3{15, 1, 1}),
                  (tess::LocalTileId{287}));
        EXPECT_EQ(view.world_coord(tess::LocalCoord3{15, 1, 1}),
                  (tess::Coord3{47, 33, 17}));
        EXPECT_EQ(view.world_coord(tess::LocalTileId{287}),
                  (tess::Coord3{47, 33, 17}));
        EXPECT_EQ(view.local_bounds(), (tess::Box3{
                                           tess::Coord3{0, 0, 0},
                                           tess::Extent3{16, 16, 8},
                                       }));
        EXPECT_TRUE(view.contains_local(tess::Coord3{8, 7, 4}));
        EXPECT_EQ(view.try_local_coord(tess::Coord3{8, 7, 4}),
                  (tess::LocalCoord3{8, 7, 4}));
        EXPECT_FALSE(view.try_local_coord(tess::Coord3{16, 7, 4}));
        EXPECT_TRUE(view.is_boundary(tess::LocalCoord3{15, 1, 1}));
        EXPECT_TRUE(view.is_interior(tess::LocalCoord3{8, 7, 4}));
        EXPECT_EQ(view.world_coord(tess::Coord3{-1, 8, 4}),
                  (tess::Coord3{31, 40, 20}));
      });
}

TEST(TessBlock, PrebuiltChunkDomainIterationDoesNotAllocate) {
  World<TopDown2D> world;
  const auto keys = std::vector<tess::ChunkKey>{
      tess::ChunkKey{0},
      tess::ChunkKey{4},
      tess::ChunkKey{8},
      tess::ChunkKey{12},
  };
  std::uint64_t sum = 0;

  tess_test::ScopedAllocationCounter counter;
  tess::for_each_chunk<tess::WritePolicy::UniquePerChunk>(
      world, tess::chunk_domain(keys), [&](auto view) {
        auto terrain = view.template field_span<TerrainTag>();
        terrain[0] = static_cast<std::uint16_t>(view.key().value);
        sum += terrain[0] + view.bounds().extent.x;
      });

  EXPECT_GT(sum, 0u);
  EXPECT_EQ(counter.count(), 0u);
}

TEST(TessBlock, NestedChunkAndTileIterationDoesNotAllocate) {
  World<TopDown2D> world;
  const auto keys = std::vector<tess::ChunkKey>{
      tess::ChunkKey{0},
      tess::ChunkKey{4},
      tess::ChunkKey{8},
      tess::ChunkKey{12},
  };
  std::uint64_t sum = 0;

  tess_test::ScopedAllocationCounter counter;
  tess::for_each_chunk<tess::WritePolicy::UniquePerChunk>(
      world, tess::chunk_domain(keys), [&](auto view) {
        auto terrain = view.template field_span<TerrainTag>();
        view.for_each_tile([&](tess::LocalTileId id, tess::LocalCoord3 coord) {
          terrain[id.value] =
              static_cast<std::uint16_t>(id.value + view.key().value);
          sum += terrain[id.value] + coord.x + coord.y + coord.z;
        });
      });

  EXPECT_GT(sum, 0u);
  EXPECT_EQ(counter.count(), 0u);
}

TEST(TessBlock, PrebuiltBlockCtxNestedChunkAndTileIterationDoesNotAllocate) {
  World<TopDown2D> world;
  const auto keys = std::vector<tess::ChunkKey>{
      tess::ChunkKey{0},
      tess::ChunkKey{4},
      tess::ChunkKey{8},
      tess::ChunkKey{12},
  };
  const auto ctx = tess::block_ctx<tess::WritePolicy::UniquePerChunk>(
      world, tess::chunk_domain(keys));
  std::uint64_t sum = 0;

  tess_test::ScopedAllocationCounter counter;
  ctx.for_each_chunk([&](auto view) {
    auto terrain = view.template field_span<TerrainTag>();
    view.for_each_tile([&](tess::LocalTileId id, tess::LocalCoord3 coord) {
      terrain[id.value] =
          static_cast<std::uint16_t>(id.value + view.key().value);
      sum += terrain[id.value] + coord.x + coord.y + coord.z;
    });
  });

  EXPECT_GT(sum, 0u);
  EXPECT_EQ(counter.count(), 0u);
}

TEST(TessBlock, PreReservedScratchInChunkAndTileIterationDoesNotAllocate) {
  World<TopDown2D> world;
  const auto keys = std::vector<tess::ChunkKey>{
      tess::ChunkKey{0},
      tess::ChunkKey{4},
      tess::ChunkKey{8},
      tess::ChunkKey{12},
  };
  tess::BlockScratch scratch;
  scratch.reserve_bytes(sizeof(std::uint32_t) *
                        tess::ShapeTraits<TopDown2D>::local_tile_count);
  auto ctx = tess::block_ctx<tess::WritePolicy::UniquePerChunk>(
      world, tess::chunk_domain(keys), scratch);
  std::uint64_t sum = 0;

  tess_test::ScopedAllocationCounter counter;
  ctx.for_each_chunk([&](auto view) {
    ctx.reset_scratch();
    auto values = ctx.scratch()->allocate<std::uint32_t>(
        tess::ShapeTraits<TopDown2D>::local_tile_count);
    ASSERT_EQ(values.size(), tess::ShapeTraits<TopDown2D>::local_tile_count);
    view.for_each_tile([&](tess::LocalTileId id, tess::LocalCoord3 coord) {
      values[id.value] =
          static_cast<std::uint32_t>(id.value + view.key().value);
      sum += values[id.value] + coord.x + coord.y + coord.z;
    });
  });

  EXPECT_GT(sum, 0u);
  EXPECT_EQ(counter.count(), 0u);
}

TEST(TessBlock, ScratchExhaustionCanBeReportedDuringChunkIteration) {
  World<TopDown2D> world;
  const auto keys = std::vector<tess::ChunkKey>{
      tess::ChunkKey{0},
      tess::ChunkKey{4},
  };
  tess::BlockScratch scratch;
  tess::BlockDiagnostics diagnostics;
  scratch.reserve_bytes(sizeof(std::max_align_t));
  auto ctx = tess::block_ctx<tess::WritePolicy::UniquePerChunk>(
      world, tess::chunk_domain(keys), scratch, diagnostics);
  std::uint64_t visited = 0;

  ctx.for_each_chunk([&](auto view) {
    ctx.reset_scratch();
    const auto values = ctx.scratch()->allocate<std::uint32_t>(
        tess::ShapeTraits<TopDown2D>::local_tile_count);
    if (values.empty()) {
      ctx.diagnostics()->record_scratch_allocation_failure();
    }
    visited += view.key().value + 1;
  });

  EXPECT_GT(visited, 0u);
  EXPECT_EQ(diagnostics.scratch_allocation_failures(), keys.size());
}

TEST(TessBlock, NestedBoundaryPredicateIterationDoesNotAllocate) {
  World<TopDown2D> world;
  const auto keys = std::vector<tess::ChunkKey>{
      tess::ChunkKey{0},
      tess::ChunkKey{4},
      tess::ChunkKey{8},
      tess::ChunkKey{12},
  };
  std::uint64_t boundary_count = 0;
  std::uint64_t interior_count = 0;
  std::uint64_t sum = 0;

  tess_test::ScopedAllocationCounter counter;
  tess::for_each_chunk<tess::WritePolicy::UniquePerChunk>(
      world, tess::chunk_domain(keys), [&](auto view) {
        auto terrain = view.template field_span<TerrainTag>();
        view.for_each_tile([&](tess::LocalTileId id, tess::LocalCoord3 coord) {
          terrain[id.value] =
              static_cast<std::uint16_t>(id.value + view.key().value);
          if (view.is_boundary(coord)) {
            ++boundary_count;
          } else {
            ++interior_count;
          }
          const auto candidate = tess::Coord3{
              static_cast<std::int64_t>(coord.x),
              static_cast<std::int64_t>(coord.y),
              static_cast<std::int64_t>(coord.z),
          };
          sum += static_cast<std::uint64_t>(terrain[id.value]) +
                 static_cast<std::uint64_t>(view.contains_local(candidate));
        });
      });

  EXPECT_GT(sum, 0u);
  EXPECT_EQ(boundary_count, 92u * keys.size());
  EXPECT_EQ(interior_count, 420u * keys.size());
  EXPECT_EQ(counter.count(), 0u);
}

TEST(TessBlock, Vertical2DChunkBoundsUseWorldAxes) {
  World<Vertical2D> world;
  const auto keys = std::vector<tess::ChunkKey>{tess::ChunkKey{6}};

  tess::for_each_chunk(world, tess::chunk_domain(keys),
                       tess::WritePolicy::ReadOnly, [](auto view) {
                         EXPECT_EQ(view.coord(), (tess::ChunkCoord3{0, 2, 1}));
                         EXPECT_EQ(view.bounds(), (tess::Box3{
                                                      tess::Coord3{0, 32, 8},
                                                      tess::Extent3{1, 16, 8},
                                                  }));
                       });
}

TEST(TessBlock, Chunked3DChunkBoundsUseAllAxes) {
  World<Chunked3D> world;
  const auto keys = std::vector<tess::ChunkKey>{tess::ChunkKey{42}};

  tess::for_each_chunk(world, tess::chunk_domain(keys),
                       tess::WritePolicy::ReadOnly, [](auto view) {
                         EXPECT_EQ(view.coord(), (tess::ChunkCoord3{2, 2, 2}));
                         EXPECT_EQ(view.bounds(), (tess::Box3{
                                                      tess::Coord3{32, 32, 16},
                                                      tess::Extent3{16, 16, 8},
                                                  }));
                       });
}

}  // namespace
