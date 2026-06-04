#include <benchmark/benchmark.h>
#include <tess/tess.h>

#include <array>
#include <cstdint>
#include <vector>

namespace {

using TopDown2D =
    tess::Shape<tess::Extent3{4096, 4096, 1}, tess::Extent3{64, 64, 1}>;
using Chunked3D =
    tess::Shape<tess::Extent3{1024, 1024, 128}, tess::Extent3{32, 32, 8}>;
using SingleChunk =
    tess::Shape<tess::Extent3{1024, 1024, 1}, tess::Extent3{1024, 1024, 1}>;
using HugeBounded = tess::Shape<tess::Extent3{1ull << 34, 1ull << 33, 256},
                                tess::Extent3{32, 32, 4}>;
using StorageChunk =
    tess::Shape<tess::Extent3{4096, 4096, 1}, tess::Extent3{64, 64, 1}>;
using StorageWorldShape =
    tess::Shape<tess::Extent3{1024, 1024, 1}, tess::Extent3{64, 64, 1}>;
using StorageSingleChunk =
    tess::Shape<tess::Extent3{256, 256, 1}, tess::Extent3{256, 256, 1}>;
using Block3DShape =
    tess::Shape<tess::Extent3{128, 128, 32}, tess::Extent3{16, 16, 8}>;

struct TerrainTag {};
struct CostTag {};

constexpr std::uint32_t DirtyTerrain = 1u << 0u;
constexpr std::uint32_t DirtyCost = 1u << 1u;

using StorageSchema = tess::FieldSchema<tess::Field<TerrainTag, std::uint16_t>,
                                        tess::Field<CostTag, float>>;
using StoragePage = tess::ChunkPage<StorageChunk, StorageSchema>;
using StorageWorld =
    tess::AlwaysResidentWorld<StorageWorldShape, StorageSchema>;
using StorageSingleChunkPage =
    tess::ChunkPage<StorageSingleChunk, StorageSchema>;
using Block2DWorld = tess::AlwaysResidentWorld<TopDown2D, StorageSchema>;
using Block3DWorld = tess::AlwaysResidentWorld<Block3DShape, StorageSchema>;

template <typename Shape>
void BM_chunk_coord(benchmark::State& state) {
  auto coord = tess::Coord3{
      47,
      33,
      tess::ShapeTraits<Shape>::size.z == 1 ? 0 : 17,
  };
  for (auto _ : state) {
    auto result = tess::chunk_coord<Shape>(coord);
    benchmark::DoNotOptimize(result);
    coord.x = (coord.x + 17) %
              static_cast<std::int64_t>(tess::ShapeTraits<Shape>::size.x);
    coord.y = (coord.y + 31) %
              static_cast<std::int64_t>(tess::ShapeTraits<Shape>::size.y);
    coord.z = (coord.z + 7) %
              static_cast<std::int64_t>(tess::ShapeTraits<Shape>::size.z);
  }
}

template <typename Shape>
void BM_local_coord(benchmark::State& state) {
  auto coord = tess::Coord3{
      47,
      33,
      tess::ShapeTraits<Shape>::size.z == 1 ? 0 : 17,
  };
  for (auto _ : state) {
    auto result = tess::local_coord<Shape>(coord);
    benchmark::DoNotOptimize(result);
    coord.x = (coord.x + 17) %
              static_cast<std::int64_t>(tess::ShapeTraits<Shape>::size.x);
    coord.y = (coord.y + 31) %
              static_cast<std::int64_t>(tess::ShapeTraits<Shape>::size.y);
    coord.z = (coord.z + 7) %
              static_cast<std::int64_t>(tess::ShapeTraits<Shape>::size.z);
  }
}

template <typename Shape>
void BM_local_tile_id(benchmark::State& state) {
  auto coord = tess::LocalCoord3{
      31,
      17,
      tess::ShapeTraits<Shape>::chunk.z == 1 ? 0ull : 3ull,
  };
  for (auto _ : state) {
    auto result = tess::local_tile_id<Shape>(coord);
    benchmark::DoNotOptimize(result);
    coord.x = (coord.x + 1) % tess::ShapeTraits<Shape>::chunk.x;
    coord.y = (coord.y + 3) % tess::ShapeTraits<Shape>::chunk.y;
    coord.z = (coord.z + 1) % tess::ShapeTraits<Shape>::chunk.z;
  }
}

template <typename Shape>
void BM_tile_key(benchmark::State& state) {
  auto coord = tess::Coord3{
      47,
      33,
      tess::ShapeTraits<Shape>::size.z == 1 ? 0 : 17,
  };
  for (auto _ : state) {
    auto result = tess::tile_key<Shape>(coord);
    benchmark::DoNotOptimize(result);
    coord.x = (coord.x + 17) %
              static_cast<std::int64_t>(tess::ShapeTraits<Shape>::size.x);
    coord.y = (coord.y + 31) %
              static_cast<std::int64_t>(tess::ShapeTraits<Shape>::size.y);
    coord.z = (coord.z + 7) %
              static_cast<std::int64_t>(tess::ShapeTraits<Shape>::size.z);
  }
}

template <typename Shape>
void BM_coord_from_tile_key(benchmark::State& state) {
  auto coord = tess::Coord3{
      47,
      33,
      tess::ShapeTraits<Shape>::size.z == 1 ? 0 : 17,
  };
  auto key = tess::tile_key<Shape>(coord);
  for (auto _ : state) {
    auto result = tess::coord<Shape>(key);
    benchmark::DoNotOptimize(result);
    coord.x = (coord.x + 17) %
              static_cast<std::int64_t>(tess::ShapeTraits<Shape>::size.x);
    coord.y = (coord.y + 31) %
              static_cast<std::int64_t>(tess::ShapeTraits<Shape>::size.y);
    coord.z = (coord.z + 7) %
              static_cast<std::int64_t>(tess::ShapeTraits<Shape>::size.z);
    key = tess::tile_key<Shape>(coord);
  }
}

void BM_field_span_acquisition(benchmark::State& state) {
  StoragePage page{tess::ChunkKey{0}, tess::ChunkCoord3{0, 0, 0}};
  for (auto _ : state) {
    auto terrain = page.field_span<TerrainTag>();
    auto costs = page.field_span<CostTag>();
    benchmark::DoNotOptimize(terrain.data());
    benchmark::DoNotOptimize(costs.data());
  }
}

void BM_chunk_field_write_read_iteration(benchmark::State& state) {
  StoragePage page{tess::ChunkKey{0}, tess::ChunkCoord3{0, 0, 0}};
  for (auto _ : state) {
    auto terrain = page.field_span<TerrainTag>();
    std::uint64_t sum = 0;
    for (std::uint64_t i = 0; i < StoragePage::local_tile_count; ++i) {
      terrain[i] = static_cast<std::uint16_t>(i);
      sum += terrain[i];
    }
    benchmark::DoNotOptimize(sum);
  }
}

void BM_single_chunk_page_iteration(benchmark::State& state) {
  StorageSingleChunkPage page{tess::ChunkKey{0}, tess::ChunkCoord3{0, 0, 0}};
  for (auto _ : state) {
    auto terrain = page.field_span<TerrainTag>();
    std::uint64_t sum = 0;
    for (std::uint64_t i = 0; i < StorageSingleChunkPage::local_tile_count;
         ++i) {
      terrain[i] = static_cast<std::uint16_t>(i);
      sum += terrain[i];
    }
    benchmark::DoNotOptimize(sum);
  }
}

void BM_flat_array_iteration(benchmark::State& state) {
  std::array<std::uint16_t, StorageSingleChunkPage::local_tile_count> terrain{};
  for (auto _ : state) {
    std::uint64_t sum = 0;
    for (std::uint64_t i = 0; i < terrain.size(); ++i) {
      terrain[i] = static_cast<std::uint16_t>(i);
      sum += terrain[i];
    }
    benchmark::DoNotOptimize(sum);
  }
}

void BM_world_chunk_lookup_by_key(benchmark::State& state) {
  StorageWorld world;
  auto key = tess::ChunkKey{0};
  for (auto _ : state) {
    auto& page = world.chunk(key);
    benchmark::DoNotOptimize(page.chunk_key());
    key.value = (key.value + 17) % StorageWorld::chunk_count;
  }
}

void BM_world_chunk_lookup_by_coord(benchmark::State& state) {
  StorageWorld world;
  auto coord = tess::ChunkCoord3{0, 0, 0};
  for (auto _ : state) {
    auto& page = world.chunk(coord);
    benchmark::DoNotOptimize(page.chunk_coord());
    const auto next = (tess::chunk_key<StorageWorldShape>(coord).value + 17) %
                      StorageWorld::chunk_count;
    coord = tess::chunk_coord<StorageWorldShape>(tess::ChunkKey{next});
  }
}

void BM_world_chunks_iteration(benchmark::State& state) {
  StorageWorld world;
  for (auto _ : state) {
    std::uint64_t sum = 0;
    for (const auto& page : world.chunks()) {
      sum += page.chunk_key().value;
    }
    benchmark::DoNotOptimize(sum);
  }
}

void BM_world_metadata_lookup_by_key(benchmark::State& state) {
  StorageWorld world;
  auto key = tess::ChunkKey{0};
  for (auto _ : state) {
    auto& meta = world.meta(key);
    benchmark::DoNotOptimize(meta.version);
    key.value = (key.value + 17) % StorageWorld::chunk_count;
  }
}

void BM_world_dirty_mark_clear(benchmark::State& state) {
  StorageWorld world;
  const auto bounds = tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}};
  auto key = tess::ChunkKey{0};
  for (auto _ : state) {
    world.mark_dirty(key, DirtyTerrain, bounds);
    world.clear_dirty(key, DirtyTerrain);
    key.value = (key.value + 17) % StorageWorld::chunk_count;
  }
}

void BM_world_dirty_chunks_iteration(benchmark::State& state) {
  StorageWorld world;
  const auto bounds = tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}};
  for (std::uint64_t key = 0; key < StorageWorld::chunk_count; key += 3) {
    world.mark_dirty(tess::ChunkKey{key}, DirtyTerrain, bounds);
  }
  for (std::uint64_t key = 1; key < StorageWorld::chunk_count; key += 5) {
    world.mark_dirty(tess::ChunkKey{key}, DirtyCost, bounds);
  }

  for (auto _ : state) {
    auto chunks = world.dirty_chunks(DirtyTerrain);
    benchmark::DoNotOptimize(chunks.data());
    benchmark::DoNotOptimize(chunks.size());
  }
}

void BM_block_explicit_domain_iteration(benchmark::State& state) {
  StorageWorld world;
  std::vector<tess::ChunkKey> keys;
  keys.reserve(StorageWorld::chunk_count);
  for (std::uint64_t key = 0; key < StorageWorld::chunk_count; key += 2) {
    keys.push_back(tess::ChunkKey{key});
  }
  const auto domain = tess::chunk_domain(keys);

  for (auto _ : state) {
    std::uint64_t sum = 0;
    tess::for_each_chunk(
        world, domain, tess::WritePolicy::ReadOnly,
        [&](auto view) { sum += view.key().value + view.bounds().extent.x; });
    benchmark::DoNotOptimize(sum);
  }
}

void BM_block_dirty_domain_iteration(benchmark::State& state) {
  StorageWorld world;
  const auto bounds = tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}};
  for (std::uint64_t key = 0; key < StorageWorld::chunk_count; key += 3) {
    world.mark_dirty(tess::ChunkKey{key}, DirtyTerrain, bounds);
  }
  const auto keys = tess::dirty_chunk_domain(world, DirtyTerrain);
  const auto domain = tess::chunk_domain(keys);

  for (auto _ : state) {
    std::uint64_t sum = 0;
    tess::for_each_chunk(
        world, domain, tess::WritePolicy::ReadOnly,
        [&](auto view) { sum += view.key().value + view.meta().version; });
    benchmark::DoNotOptimize(sum);
  }
}

void BM_block_context_iteration_2d(benchmark::State& state) {
  StorageWorld world;
  std::vector<tess::ChunkKey> keys;
  keys.reserve(StorageWorld::chunk_count);
  for (std::uint64_t key = 0; key < StorageWorld::chunk_count; ++key) {
    keys.push_back(tess::ChunkKey{key});
  }
  const auto ctx = tess::block_ctx<tess::WritePolicy::UniquePerChunk>(
      world, tess::chunk_domain(keys));

  for (auto _ : state) {
    std::uint64_t sum = 0;
    ctx.for_each_chunk([&](auto view) {
      auto terrain = view.template field_span<TerrainTag>();
      terrain[0] = static_cast<std::uint16_t>(view.key().value);
      sum += terrain[0] + view.meta().version + view.bounds().extent.x;
    });
    benchmark::DoNotOptimize(sum);
  }
}

template <typename WorldType>
void BM_block_chunk_tile_iteration(benchmark::State& state) {
  WorldType world;
  const auto keys = std::vector<tess::ChunkKey>{tess::ChunkKey{0}};
  const auto domain = tess::chunk_domain(keys);

  for (auto _ : state) {
    std::uint64_t sum = 0;
    tess::for_each_chunk(
        world, domain, tess::WritePolicy::UniquePerChunk, [&](auto view) {
          auto terrain = view.template field_span<TerrainTag>();
          view.for_each_tile(
              [&](tess::LocalTileId id, tess::LocalCoord3 coord) {
                terrain[id.value] =
                    static_cast<std::uint16_t>(id.value + coord.x + coord.y);
                sum += terrain[id.value];
              });
        });
    benchmark::DoNotOptimize(sum);
  }
}

template <typename WorldType>
void BM_block_chunk_boundary_scan(benchmark::State& state) {
  WorldType world;
  const auto keys = std::vector<tess::ChunkKey>{tess::ChunkKey{0}};
  const auto domain = tess::chunk_domain(keys);

  for (auto _ : state) {
    std::uint64_t boundary_count = 0;
    std::uint64_t interior_count = 0;
    std::uint64_t sum = 0;
    tess::for_each_chunk(
        world, domain, tess::WritePolicy::UniquePerChunk, [&](auto view) {
          auto terrain = view.template field_span<TerrainTag>();
          view.for_each_tile(
              [&](tess::LocalTileId id, tess::LocalCoord3 coord) {
                if (view.is_boundary(coord)) {
                  terrain[id.value] = 1;
                  ++boundary_count;
                } else {
                  terrain[id.value] = 2;
                  ++interior_count;
                }
                sum += terrain[id.value] + id.value;
              });
        });
    benchmark::DoNotOptimize(boundary_count);
    benchmark::DoNotOptimize(interior_count);
    benchmark::DoNotOptimize(sum);
  }
}

BENCHMARK(BM_chunk_coord<TopDown2D>)->Name("key/chunk_coord_2d_u64");
BENCHMARK(BM_local_coord<TopDown2D>)->Name("key/local_coord_2d_u64");
BENCHMARK(BM_local_tile_id<TopDown2D>)->Name("key/local_tile_id_2d_u64");
BENCHMARK(BM_tile_key<TopDown2D>)->Name("key/tile_key_2d_u64");
BENCHMARK(BM_coord_from_tile_key<TopDown2D>)
    ->Name("key/coord_from_tile_key_2d_u64");

BENCHMARK(BM_tile_key<Chunked3D>)->Name("key/tile_key_3d_u64");
BENCHMARK(BM_coord_from_tile_key<Chunked3D>)
    ->Name("key/coord_from_tile_key_3d_u64");

BENCHMARK(BM_tile_key<SingleChunk>)->Name("key/tile_key_single_chunk_u64");
BENCHMARK(BM_tile_key<HugeBounded>)->Name("key/tile_key_huge_u128");
BENCHMARK(BM_coord_from_tile_key<HugeBounded>)
    ->Name("key/coord_from_tile_key_huge_u128");

BENCHMARK(BM_field_span_acquisition)->Name("storage/field_span_acquisition");
BENCHMARK(BM_chunk_field_write_read_iteration)
    ->Name("storage/chunk_field_write_read_iteration");
BENCHMARK(BM_single_chunk_page_iteration)
    ->Name("storage/single_chunk_page_iteration");
BENCHMARK(BM_flat_array_iteration)->Name("storage/flat_array_iteration");
BENCHMARK(BM_world_chunk_lookup_by_key)
    ->Name("storage/world_chunk_lookup_by_key");
BENCHMARK(BM_world_chunk_lookup_by_coord)
    ->Name("storage/world_chunk_lookup_by_coord");
BENCHMARK(BM_world_chunks_iteration)->Name("storage/world_chunks_iteration");
BENCHMARK(BM_world_metadata_lookup_by_key)
    ->Name("storage/world_metadata_lookup_by_key");
BENCHMARK(BM_world_dirty_mark_clear)->Name("storage/world_dirty_mark_clear");
BENCHMARK(BM_world_dirty_chunks_iteration)
    ->Name("storage/world_dirty_chunks_iteration");
BENCHMARK(BM_block_explicit_domain_iteration)
    ->Name("block/explicit_domain_iteration");
BENCHMARK(BM_block_dirty_domain_iteration)
    ->Name("block/dirty_domain_iteration");
BENCHMARK(BM_block_context_iteration_2d)->Name("block/context_iteration_2d");
BENCHMARK(BM_block_chunk_tile_iteration<StorageWorld>)
    ->Name("block/chunk_tile_iteration_2d");
BENCHMARK(BM_block_chunk_tile_iteration<Block3DWorld>)
    ->Name("block/chunk_tile_iteration_3d");
BENCHMARK(BM_block_chunk_boundary_scan<Block2DWorld>)
    ->Name("block/chunk_boundary_scan_2d");
BENCHMARK(BM_block_chunk_boundary_scan<Block3DWorld>)
    ->Name("block/chunk_boundary_scan_3d");

}  // namespace
