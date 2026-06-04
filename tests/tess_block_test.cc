#include <gtest/gtest.h>
#include <tess/tess.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <span>
#include <type_traits>
#include <vector>

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

  tess::for_each_chunk(
      world, tess::chunk_domain(keys), tess::WritePolicy::UniquePerChunk,
      [&](auto view) {
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

  allocation_count.store(0, std::memory_order_relaxed);
  count_allocations.store(true, std::memory_order_relaxed);
  tess::for_each_chunk(world, tess::chunk_domain(keys),
                       tess::WritePolicy::UniquePerChunk, [&](auto view) {
                         auto terrain = view.template field_span<TerrainTag>();
                         terrain[0] =
                             static_cast<std::uint16_t>(view.key().value);
                         sum += terrain[0] + view.bounds().extent.x;
                       });
  count_allocations.store(false, std::memory_order_relaxed);

  EXPECT_GT(sum, 0u);
  EXPECT_EQ(allocation_count.load(std::memory_order_relaxed), 0);
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

  allocation_count.store(0, std::memory_order_relaxed);
  count_allocations.store(true, std::memory_order_relaxed);
  tess::for_each_chunk(
      world, tess::chunk_domain(keys), tess::WritePolicy::UniquePerChunk,
      [&](auto view) {
        auto terrain = view.template field_span<TerrainTag>();
        view.for_each_tile([&](tess::LocalTileId id, tess::LocalCoord3 coord) {
          terrain[id.value] =
              static_cast<std::uint16_t>(id.value + view.key().value);
          sum += terrain[id.value] + coord.x + coord.y + coord.z;
        });
      });
  count_allocations.store(false, std::memory_order_relaxed);

  EXPECT_GT(sum, 0u);
  EXPECT_EQ(allocation_count.load(std::memory_order_relaxed), 0);
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
