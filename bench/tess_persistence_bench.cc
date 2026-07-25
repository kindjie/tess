#include <benchmark/benchmark.h>
#include <tess/tess.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

struct TerrainTag {};
struct CostTag {};

using Shape = tess::Shape<tess::Extent3{512, 512, 1}, tess::Extent3{64, 64, 1}>;
using Fields = tess::FieldSchema<tess::Field<TerrainTag, std::uint8_t>,
                                 tess::Field<CostTag, std::uint32_t>>;
using World = tess::AlwaysResidentWorld<Shape, Fields>;
using Archive = tess::PersistenceSchema<
    0x62656e63682d776fULL, 1,
    tess::PersistedField<TerrainTag, 0x7465727261696eULL>,
    tess::PersistedField<CostTag, 0x636f7374ULL>>;

void populate(World& world) {
  for (std::uint64_t key = 0; key < World::chunk_count; ++key) {
    auto terrain = world.field_span<TerrainTag>(tess::ChunkKey{key});
    auto costs = world.field_span<CostTag>(tess::ChunkKey{key});
    for (std::size_t i = 0; i < terrain.size(); ++i) {
      terrain[i] = static_cast<std::uint8_t>((key + i) % 5);
      costs[i] = static_cast<std::uint32_t>(1 + (key * 17 + i) % 100);
    }
  }
}

void BM_persistence_save_dense_512x512(benchmark::State& state) {
  World world;
  populate(world);
  std::vector<std::byte> bytes;
  for (auto _ : state) {
    auto result = tess::save_world_archive<Archive>(world, bytes);
    benchmark::DoNotOptimize(result.bytes_processed);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(bytes.size()));
}

void BM_persistence_load_dense_512x512(benchmark::State& state) {
  World source;
  populate(source);
  std::vector<std::byte> bytes;
  const auto saved = tess::save_world_archive<Archive>(source, bytes);
  if (saved.status != tess::WorldArchiveStatus::Ok) {
    state.SkipWithError("failed to prepare persistence archive");
    return;
  }

  World target;
  for (auto _ : state) {
    auto result = tess::load_world_archive<Archive>(target, bytes, 1U);
    benchmark::DoNotOptimize(result.bytes_processed);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(bytes.size()));
}

BENCHMARK(BM_persistence_save_dense_512x512)
    ->Name("persistence/save_dense_512x512_2_fields");
BENCHMARK(BM_persistence_load_dense_512x512)
    ->Name("persistence/load_dense_512x512_2_fields");

}  // namespace
