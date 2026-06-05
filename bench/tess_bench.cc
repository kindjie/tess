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
using PathShape =
    tess::Shape<tess::Extent3{16, 16, 1}, tess::Extent3{16, 16, 1}>;
using PathRealisticShape =
    tess::Shape<tess::Extent3{64, 64, 1}, tess::Extent3{16, 16, 1}>;
using PathScaleShape =
    tess::Shape<tess::Extent3{512, 512, 1}, tess::Extent3{32, 32, 1}>;
using PathLargeShape =
    tess::Shape<tess::Extent3{1024, 1024, 1}, tess::Extent3{32, 32, 1}>;
using PathVerticalScaleShape =
    tess::Shape<tess::Extent3{1, 512, 512}, tess::Extent3{1, 32, 32}>;
using Path3DShape =
    tess::Shape<tess::Extent3{64, 64, 16}, tess::Extent3{16, 16, 4}>;

struct TerrainTag {};
struct CostTag {};
struct PassableTag {};

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
using PathSchema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>>;
using PathWorld = tess::AlwaysResidentWorld<PathShape, PathSchema>;
using PathRealisticWorld =
    tess::AlwaysResidentWorld<PathRealisticShape, PathSchema>;
using PathScaleWorld = tess::AlwaysResidentWorld<PathScaleShape, PathSchema>;
using PathLargeWorld = tess::AlwaysResidentWorld<PathLargeShape, PathSchema>;
using PathVerticalScaleWorld =
    tess::AlwaysResidentWorld<PathVerticalScaleShape, PathSchema>;
using Path3DWorld = tess::AlwaysResidentWorld<Path3DShape, PathSchema>;

void record_path_counters(benchmark::State& state,
                          const tess::PathResult& result) {
  state.counters["cost"] = static_cast<double>(result.cost);
  state.counters["path_nodes"] = static_cast<double>(result.path.size());
  state.counters["expanded_nodes"] = static_cast<double>(result.expanded_nodes);
  state.counters["reached_nodes"] = static_cast<double>(result.reached_nodes);
}

#if TESS_DIAGNOSTICS_ENABLED
void record_path_diagnostic_counters(
    benchmark::State& state,
    const tess::diagnostics::PathCounters& diagnostics) {
  state.counters["diag.scratch_clear_calls"] =
      static_cast<double>(diagnostics.scratch_clear_calls);
  state.counters["diag.scratch_clear_nodes"] =
      static_cast<double>(diagnostics.scratch_clear_nodes);
  state.counters["diag.initializations"] =
      static_cast<double>(diagnostics.initializations);
  state.counters["diag.start_passability_checks"] =
      static_cast<double>(diagnostics.start_passability_checks);
  state.counters["diag.goal_passability_checks"] =
      static_cast<double>(diagnostics.goal_passability_checks);
  state.counters["diag.heap_pushes"] =
      static_cast<double>(diagnostics.heap_pushes);
  state.counters["diag.heap_pops"] = static_cast<double>(diagnostics.heap_pops);
  state.counters["diag.stale_pops"] =
      static_cast<double>(diagnostics.stale_pops);
  state.counters["diag.closed_pops"] =
      static_cast<double>(diagnostics.closed_pops);
  state.counters["diag.neighbor_candidates"] =
      static_cast<double>(diagnostics.neighbor_candidates);
  state.counters["diag.passability_checks"] =
      static_cast<double>(diagnostics.passability_checks);
  state.counters["diag.blocked_neighbors"] =
      static_cast<double>(diagnostics.blocked_neighbors);
  state.counters["diag.closed_neighbors"] =
      static_cast<double>(diagnostics.closed_neighbors);
  state.counters["diag.relax_attempts"] =
      static_cast<double>(diagnostics.relax_attempts);
  state.counters["diag.relax_successes"] =
      static_cast<double>(diagnostics.relax_successes);
  state.counters["diag.touched_nodes"] =
      static_cast<double>(diagnostics.touched_nodes);
  state.counters["diag.heuristic_calls"] =
      static_cast<double>(diagnostics.heuristic_calls);
  state.counters["diag.reconstructed_nodes"] =
      static_cast<double>(diagnostics.reconstructed_nodes);
}

void record_allocation_diagnostic_counters(
    benchmark::State& state,
    const tess::diagnostics::AllocationCounters& diagnostics) {
  state.counters["diag.allocations"] =
      static_cast<double>(diagnostics.allocations);
  state.counters["diag.allocation_bytes"] =
      static_cast<double>(diagnostics.allocation_bytes);
  state.counters["diag.deallocations"] =
      static_cast<double>(diagnostics.deallocations);
  state.counters["diag.deallocation_bytes"] =
      static_cast<double>(diagnostics.deallocation_bytes);
}

#define TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch) \
  tess::diagnostics::PathCounters path_counters;  \
  tess::diagnostics::AllocationCounters allocation_counters;
#define TESS_BENCH_PATH_DIAGNOSTICS_RESET() \
  do {                                      \
    path_counters.reset();                  \
    allocation_counters.reset();            \
  } while (false)
#define TESS_BENCH_PATH_DIAGNOSTICS_RUN(...)                                 \
  do {                                                                       \
    tess::diagnostics::ScopedPathCounters path_counter_scope{path_counters}; \
    tess::diagnostics::ScopedAllocationCounters allocation_scope{            \
        allocation_counters};                                                \
    __VA_ARGS__;                                                             \
  } while (false)
#define TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state)                        \
  do {                                                                   \
    record_path_diagnostic_counters((state), path_counters);             \
    record_allocation_diagnostic_counters((state), allocation_counters); \
  } while (false)
#else
#define TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch) \
  do {                                            \
  } while (false)
#define TESS_BENCH_PATH_DIAGNOSTICS_RESET() \
  do {                                      \
  } while (false)
#define TESS_BENCH_PATH_DIAGNOSTICS_RUN(...) \
  do {                                       \
    __VA_ARGS__;                             \
  } while (false)
#define TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state) \
  do {                                            \
  } while (false)
#endif

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

void BM_block_scratch_allocate_u32(benchmark::State& state) {
  tess::BlockScratch scratch;
  scratch.reserve_bytes(sizeof(std::uint32_t) * StorageWorldShape::chunk.x *
                        StorageWorldShape::chunk.y *
                        StorageWorldShape::chunk.z);

  for (auto _ : state) {
    scratch.reset();
    const auto values = scratch.allocate<std::uint32_t>(
        StorageWorldShape::chunk.x * StorageWorldShape::chunk.y *
        StorageWorldShape::chunk.z);
    benchmark::DoNotOptimize(values.data());
    benchmark::DoNotOptimize(values.size());
  }
}

void BM_block_context_scratch_tile_iteration_2d(benchmark::State& state) {
  StorageWorld world;
  const auto keys = std::vector<tess::ChunkKey>{tess::ChunkKey{0}};
  tess::BlockScratch scratch;
  scratch.reserve_bytes(sizeof(std::uint32_t) * StorageWorldShape::chunk.x *
                        StorageWorldShape::chunk.y *
                        StorageWorldShape::chunk.z);
  auto ctx = tess::block_ctx<tess::WritePolicy::UniquePerChunk>(
      world, tess::chunk_domain(keys), scratch);

  for (auto _ : state) {
    std::uint64_t sum = 0;
    ctx.for_each_chunk([&](auto view) {
      ctx.reset_scratch();
      auto values = ctx.scratch()->template allocate<std::uint32_t>(
          StorageWorldShape::chunk.x * StorageWorldShape::chunk.y *
          StorageWorldShape::chunk.z);
      view.for_each_tile([&](tess::LocalTileId id, tess::LocalCoord3 coord) {
        values[id.value] =
            static_cast<std::uint32_t>(id.value + coord.x + coord.y);
        sum += values[id.value];
      });
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

void BM_queued_execute_resident_update(benchmark::State& state) {
  StorageWorld world;
  tess::FrameOps ops;
  (void)ops.update_field(tess::DomainDesc::resident_chunks(),
                         tess::FieldAccessDesc{0, DirtyTerrain, 0},
                         tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);
  const auto& plan = report.plan();

  for (auto _ : state) {
    const auto result = tess::execute_plan<tess::WritePolicy::UniquePerChunk>(
        world, plan, [](auto view) {
          auto terrain = view.template field_span<TerrainTag>();
          terrain[0] = static_cast<std::uint16_t>(view.key().value);
        });
    auto chunk_count = result.chunk_count;
    benchmark::DoNotOptimize(chunk_count);
  }
}

template <typename World>
void fill_path_passable(World& world, std::uint8_t value) {
  for (auto& page : world.chunks()) {
    auto passable = page.template field_span<PassableTag>();
    for (auto& tile : passable) {
      tile = value;
    }
  }
}

template <typename Shape>
[[nodiscard]] constexpr auto path_node_count() noexcept -> std::uint64_t {
  return Shape::size.x * Shape::size.y * Shape::size.z;
}

template <typename World>
void block_vertical_wall(World& world, std::int64_t x, std::int64_t gap_y) {
  using Shape = typename World::shape_type;
  for (std::int64_t y = 0; y < static_cast<std::int64_t>(Shape::size.y); ++y) {
    if (y != gap_y) {
      world.template field<PassableTag>(tess::Coord3{x, y, 0}) = 0;
    }
  }
}

template <typename World>
void block_yz_wall(World& world, std::int64_t y, std::int64_t gap_z) {
  using Shape = typename World::shape_type;
  for (std::int64_t z = 0; z < static_cast<std::int64_t>(Shape::size.z); ++z) {
    if (z != gap_z) {
      world.template field<PassableTag>(tess::Coord3{0, y, z}) = 0;
    }
  }
}

template <typename World>
void block_3d_x_slab(World& world, std::int64_t x, tess::Coord3 gap) {
  using Shape = typename World::shape_type;
  for (std::int64_t z = 0; z < static_cast<std::int64_t>(Shape::size.z); ++z) {
    for (std::int64_t y = 0; y < static_cast<std::int64_t>(Shape::size.y);
         ++y) {
      if (y != gap.y || z != gap.z) {
        world.template field<PassableTag>(tess::Coord3{x, y, z}) = 0;
      }
    }
  }
}

template <typename World>
void block_3d_x_slab_with_two_gaps(World& world, std::int64_t x,
                                   tess::Coord3 first_gap,
                                   tess::Coord3 second_gap) {
  using Shape = typename World::shape_type;
  for (std::int64_t z = 0; z < static_cast<std::int64_t>(Shape::size.z); ++z) {
    for (std::int64_t y = 0; y < static_cast<std::int64_t>(Shape::size.y);
         ++y) {
      const auto first = y == first_gap.y && z == first_gap.z;
      const auto second = y == second_gap.y && z == second_gap.z;
      if (!first && !second) {
        world.template field<PassableTag>(tess::Coord3{x, y, z}) = 0;
      }
    }
  }
}

void carve_3d_corridor(Path3DWorld& world) {
  fill_path_passable(world, 0);
  auto carve = [&](tess::Coord3 coord) {
    world.template field<PassableTag>(coord) = 1;
  };
  for (std::int64_t x = 0; x <= 16; ++x) {
    carve(tess::Coord3{x, 0, 0});
  }
  for (std::int64_t y = 0; y <= 16; ++y) {
    carve(tess::Coord3{16, y, 0});
  }
  for (std::int64_t z = 0; z <= 4; ++z) {
    carve(tess::Coord3{16, 16, z});
  }
  for (std::int64_t x = 16; x <= 32; ++x) {
    carve(tess::Coord3{x, 16, 4});
  }
  for (std::int64_t y = 16; y <= 32; ++y) {
    carve(tess::Coord3{32, y, 4});
  }
  for (std::int64_t z = 4; z <= 8; ++z) {
    carve(tess::Coord3{32, 32, z});
  }
  for (std::int64_t x = 32; x <= 48; ++x) {
    carve(tess::Coord3{x, 32, 8});
  }
  for (std::int64_t y = 32; y <= 48; ++y) {
    carve(tess::Coord3{48, y, 8});
  }
  for (std::int64_t z = 8; z <= 12; ++z) {
    carve(tess::Coord3{48, 48, z});
  }
  for (std::int64_t x = 48; x <= 63; ++x) {
    carve(tess::Coord3{x, 48, 12});
  }
  for (std::int64_t y = 48; y <= 63; ++y) {
    carve(tess::Coord3{63, y, 12});
  }
  for (std::int64_t z = 12; z <= 15; ++z) {
    carve(tess::Coord3{63, 63, z});
  }
}

void carve_striped_maze(PathScaleWorld& world) {
  for (std::int64_t x = 8;
       x < static_cast<std::int64_t>(PathScaleShape::size.x); x += 8) {
    const auto gap_y =
        x % 16 == 0 ? PathScaleShape::size.y - 1 : std::uint64_t{0};
    block_vertical_wall(world, x, static_cast<std::int64_t>(gap_y));
  }
}

void carve_vertical_striped_maze(PathVerticalScaleWorld& world) {
  for (std::int64_t y = 8;
       y < static_cast<std::int64_t>(PathVerticalScaleShape::size.y); y += 8) {
    const auto gap_z =
        y % 16 == 0 ? PathVerticalScaleShape::size.z - 1 : std::uint64_t{0};
    block_yz_wall(world, y, static_cast<std::int64_t>(gap_z));
  }
}

void carve_striped_maze(PathLargeWorld& world) {
  for (std::int64_t x = 8;
       x < static_cast<std::int64_t>(PathLargeShape::size.x); x += 8) {
    const auto gap_y =
        x % 16 == 0 ? PathLargeShape::size.y - 1 : std::uint64_t{0};
    block_vertical_wall(world, x, static_cast<std::int64_t>(gap_y));
  }
}

[[nodiscard]] constexpr auto path_hash(std::uint64_t x,
                                       std::uint64_t y) noexcept
    -> std::uint64_t {
  auto value = x * 0x9e3779b97f4a7c15ull;
  value ^= y + 0xbf58476d1ce4e5b9ull + (value << 6u) + (value >> 2u);
  value ^= value >> 30u;
  value *= 0xbf58476d1ce4e5b9ull;
  value ^= value >> 27u;
  value *= 0x94d049bb133111ebull;
  value ^= value >> 31u;
  return value;
}

void carve_sparse_blockers(PathScaleWorld& world) {
  for (std::int64_t y = 1;
       y + 1 < static_cast<std::int64_t>(PathScaleShape::size.y); ++y) {
    for (std::int64_t x = 1;
         x + 1 < static_cast<std::int64_t>(PathScaleShape::size.x); ++x) {
      const auto hash = path_hash(static_cast<std::uint64_t>(x),
                                  static_cast<std::uint64_t>(y));
      if (hash % 100u < 18u) {
        world.template field<PassableTag>(tess::Coord3{x, y, 0}) = 0;
      }
    }
  }

  for (std::int64_t x = 32; x < 512; x += 64) {
    world.template field<PassableTag>(tess::Coord3{x, 1, 0}) = 0;
  }
  for (std::int64_t y = 64; y < 512; y += 64) {
    world.template field<PassableTag>(tess::Coord3{510, y, 0}) = 0;
  }
  world.template field<PassableTag>(tess::Coord3{1, 1, 0}) = 1;
  world.template field<PassableTag>(tess::Coord3{510, 510, 0}) = 1;
}

void carve_room_portals(PathScaleWorld& world) {
  constexpr auto room_size = std::int64_t{32};
  for (std::int64_t x = room_size;
       x < static_cast<std::int64_t>(PathScaleShape::size.x); x += room_size) {
    for (std::int64_t y = 0;
         y < static_cast<std::int64_t>(PathScaleShape::size.y); ++y) {
      const auto room = y / room_size;
      const auto portal = (room * 23 + x / room_size * 17) % room_size;
      if (y % room_size != portal) {
        world.template field<PassableTag>(tess::Coord3{x, y, 0}) = 0;
      }
    }
  }
  for (std::int64_t y = room_size;
       y < static_cast<std::int64_t>(PathScaleShape::size.y); y += room_size) {
    for (std::int64_t x = 0;
         x < static_cast<std::int64_t>(PathScaleShape::size.x); ++x) {
      const auto room = x / room_size;
      const auto portal = (room * 29 + y / room_size * 19) % room_size;
      if (x % room_size != portal) {
        world.template field<PassableTag>(tess::Coord3{x, y, 0}) = 0;
      }
    }
  }
}

void carve_branch_lattice(PathScaleWorld& world) {
  fill_path_passable(world, 0);
  for (std::int64_t y = 0;
       y < static_cast<std::int64_t>(PathScaleShape::size.y); y += 4) {
    for (std::int64_t x = 0;
         x < static_cast<std::int64_t>(PathScaleShape::size.x); ++x) {
      world.template field<PassableTag>(tess::Coord3{x, y, 0}) = 1;
    }
  }
  for (std::int64_t x = 0;
       x < static_cast<std::int64_t>(PathScaleShape::size.x); x += 4) {
    for (std::int64_t y = 0;
         y < static_cast<std::int64_t>(PathScaleShape::size.y); ++y) {
      world.template field<PassableTag>(tess::Coord3{x, y, 0}) = 1;
    }
  }

  for (std::int64_t x = 64; x < 512; x += 64) {
    world.template field<PassableTag>(tess::Coord3{x, 4, 0}) = 0;
  }
  for (std::int64_t y = 64; y < 512; y += 64) {
    world.template field<PassableTag>(tess::Coord3{508, y, 0}) = 0;
  }
}

void BM_path_astar_open_2d(benchmark::State& state) {
  PathWorld world;
  fill_path_passable(world, 1);
  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathShape>());
  TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_BENCH_PATH_DIAGNOSTICS_RESET();
    TESS_BENCH_PATH_DIAGNOSTICS_RUN(
        result = tess::astar_path<PathWorld, PassableTag>(
            world,
            tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{15, 15, 0}},
            scratch));
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  record_path_counters(state, result);
  TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state);
}

void BM_path_astar_open_2d_64x64(benchmark::State& state) {
  PathRealisticWorld world;
  fill_path_passable(world, 1);
  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathRealisticShape>());
  TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_BENCH_PATH_DIAGNOSTICS_RESET();
    TESS_BENCH_PATH_DIAGNOSTICS_RUN(
        result = tess::astar_path<PathRealisticWorld, PassableTag>(
            world,
            tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{63, 63, 0}},
            scratch));
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  record_path_counters(state, result);
  TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state);
}

void BM_path_astar_open_2d_512x512(benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);
  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_BENCH_PATH_DIAGNOSTICS_RESET();
    TESS_BENCH_PATH_DIAGNOSTICS_RUN(
        result = tess::astar_path<PathScaleWorld, PassableTag>(
            world,
            tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{511, 511, 0}},
            scratch));
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  record_path_counters(state, result);
  TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state);
}

void BM_path_astar_open_2d_1024x1024(benchmark::State& state) {
  PathLargeWorld world;
  fill_path_passable(world, 1);
  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathLargeShape>());
  TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_BENCH_PATH_DIAGNOSTICS_RESET();
    TESS_BENCH_PATH_DIAGNOSTICS_RUN(
        result = tess::astar_path<PathLargeWorld, PassableTag>(
            world,
            tess::PathRequest{tess::Coord3{0, 0, 0},
                              tess::Coord3{1023, 1023, 0}},
            scratch));
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  record_path_counters(state, result);
  TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state);
}

void BM_path_astar_wall_gap_512x512(benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);
  block_vertical_wall(world, 256, 511);

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_BENCH_PATH_DIAGNOSTICS_RESET();
    TESS_BENCH_PATH_DIAGNOSTICS_RUN(
        result = tess::astar_path<PathScaleWorld, PassableTag>(
            world,
            tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{511, 0, 0}},
            scratch));
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  record_path_counters(state, result);
  TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state);
}

void BM_path_astar_alternate_direct_512x512(benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);
  world.template field<PassableTag>(tess::Coord3{256, 0, 0}) = 0;

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_BENCH_PATH_DIAGNOSTICS_RESET();
    TESS_BENCH_PATH_DIAGNOSTICS_RUN(
        result = tess::astar_path<PathScaleWorld, PassableTag>(
            world,
            tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{511, 511, 0}},
            scratch));
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  record_path_counters(state, result);
  TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state);
}

void BM_path_astar_axis_detour_512x512(benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);
  world.template field<PassableTag>(tess::Coord3{256, 0, 0}) = 0;

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_BENCH_PATH_DIAGNOSTICS_RESET();
    TESS_BENCH_PATH_DIAGNOSTICS_RUN(
        result = tess::astar_path<PathScaleWorld, PassableTag>(
            world,
            tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{511, 0, 0}},
            scratch));
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  record_path_counters(state, result);
  TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state);
}

void BM_path_astar_no_path_512x512(benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);
  block_vertical_wall(world, 256, -1);

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_BENCH_PATH_DIAGNOSTICS_RESET();
    TESS_BENCH_PATH_DIAGNOSTICS_RUN(
        result = tess::astar_path<PathScaleWorld, PassableTag>(
            world,
            tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{511, 511, 0}},
            scratch));
    auto expanded = result.expanded_nodes;
    benchmark::DoNotOptimize(expanded);
  }
  record_path_counters(state, result);
  TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state);
}

void BM_path_astar_striped_maze_512x512(benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);
  carve_striped_maze(world);

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_BENCH_PATH_DIAGNOSTICS_RESET();
    TESS_BENCH_PATH_DIAGNOSTICS_RUN(
        result = tess::astar_path<PathScaleWorld, PassableTag>(
            world,
            tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{511, 511, 0}},
            scratch));
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  record_path_counters(state, result);
  TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state);
}

void BM_path_astar_short_open_512x512(benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);
  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_BENCH_PATH_DIAGNOSTICS_RESET();
    TESS_BENCH_PATH_DIAGNOSTICS_RUN(
        result = tess::astar_path<PathScaleWorld, PassableTag>(
            world,
            tess::PathRequest{tess::Coord3{32, 32, 0}, tess::Coord3{63, 63, 0}},
            scratch));
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  record_path_counters(state, result);
  TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state);
}

void BM_path_astar_medium_open_512x512(benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);
  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_BENCH_PATH_DIAGNOSTICS_RESET();
    TESS_BENCH_PATH_DIAGNOSTICS_RUN(
        result = tess::astar_path<PathScaleWorld, PassableTag>(
            world,
            tess::PathRequest{tess::Coord3{64, 64, 0},
                              tess::Coord3{255, 255, 0}},
            scratch));
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  record_path_counters(state, result);
  TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state);
}

void BM_path_astar_long_open_512x512(benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);
  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_BENCH_PATH_DIAGNOSTICS_RESET();
    TESS_BENCH_PATH_DIAGNOSTICS_RUN(
        result = tess::astar_path<PathScaleWorld, PassableTag>(
            world,
            tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{511, 511, 0}},
            scratch));
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  record_path_counters(state, result);
  TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state);
}

void BM_path_astar_no_path_1024x1024(benchmark::State& state) {
  PathLargeWorld world;
  fill_path_passable(world, 1);
  block_vertical_wall(world, 512, -1);

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathLargeShape>());
  TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_BENCH_PATH_DIAGNOSTICS_RESET();
    TESS_BENCH_PATH_DIAGNOSTICS_RUN(
        result = tess::astar_path<PathLargeWorld, PassableTag>(
            world,
            tess::PathRequest{tess::Coord3{0, 0, 0},
                              tess::Coord3{1023, 1023, 0}},
            scratch));
    auto expanded = result.expanded_nodes;
    benchmark::DoNotOptimize(expanded);
  }
  record_path_counters(state, result);
  TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state);
}

void BM_path_astar_striped_maze_1024x1024(benchmark::State& state) {
  PathLargeWorld world;
  fill_path_passable(world, 1);
  carve_striped_maze(world);

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathLargeShape>());
  TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_BENCH_PATH_DIAGNOSTICS_RESET();
    TESS_BENCH_PATH_DIAGNOSTICS_RUN(
        result = tess::astar_path<PathLargeWorld, PassableTag>(
            world,
            tess::PathRequest{tess::Coord3{0, 0, 0},
                              tess::Coord3{1023, 1023, 0}},
            scratch));
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  record_path_counters(state, result);
  TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state);
}

void BM_path_astar_vertical_open_512x512(benchmark::State& state) {
  PathVerticalScaleWorld world;
  fill_path_passable(world, 1);
  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathVerticalScaleShape>());
  TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_BENCH_PATH_DIAGNOSTICS_RESET();
    TESS_BENCH_PATH_DIAGNOSTICS_RUN(
        result = tess::astar_path<PathVerticalScaleWorld, PassableTag>(
            world,
            tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{0, 511, 511}},
            scratch));
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  record_path_counters(state, result);
  TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state);
}

void BM_path_astar_vertical_wall_gap_512x512(benchmark::State& state) {
  PathVerticalScaleWorld world;
  fill_path_passable(world, 1);
  block_yz_wall(world, 256, 511);

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathVerticalScaleShape>());
  TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_BENCH_PATH_DIAGNOSTICS_RESET();
    TESS_BENCH_PATH_DIAGNOSTICS_RUN(
        result = tess::astar_path<PathVerticalScaleWorld, PassableTag>(
            world,
            tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{0, 511, 0}},
            scratch));
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  record_path_counters(state, result);
  TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state);
}

void BM_path_astar_vertical_striped_maze_512x512(benchmark::State& state) {
  PathVerticalScaleWorld world;
  fill_path_passable(world, 1);
  carve_vertical_striped_maze(world);

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathVerticalScaleShape>());
  TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_BENCH_PATH_DIAGNOSTICS_RESET();
    TESS_BENCH_PATH_DIAGNOSTICS_RUN(
        result = tess::astar_path<PathVerticalScaleWorld, PassableTag>(
            world,
            tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{0, 511, 511}},
            scratch));
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  record_path_counters(state, result);
  TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state);
}

void BM_path_astar_open_3d_64x64x16(benchmark::State& state) {
  Path3DWorld world;
  fill_path_passable(world, 1);
  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<Path3DShape>());
  TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_BENCH_PATH_DIAGNOSTICS_RESET();
    TESS_BENCH_PATH_DIAGNOSTICS_RUN(
        result = tess::astar_path<Path3DWorld, PassableTag>(
            world,
            tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{63, 63, 15}},
            scratch));
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  record_path_counters(state, result);
  TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state);
}

void BM_path_astar_slab_gap_3d_64x64x16(benchmark::State& state) {
  Path3DWorld world;
  fill_path_passable(world, 1);
  block_3d_x_slab(world, 32, tess::Coord3{32, 63, 15});

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<Path3DShape>());
  TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_BENCH_PATH_DIAGNOSTICS_RESET();
    TESS_BENCH_PATH_DIAGNOSTICS_RUN(
        result = tess::astar_path<Path3DWorld, PassableTag>(
            world,
            tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{63, 0, 0}},
            scratch));
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  record_path_counters(state, result);
  TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state);
}

void BM_path_astar_slab_no_gap_3d_64x64x16(benchmark::State& state) {
  Path3DWorld world;
  fill_path_passable(world, 1);
  block_3d_x_slab(world, 32, tess::Coord3{32, -1, -1});

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<Path3DShape>());
  TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_BENCH_PATH_DIAGNOSTICS_RESET();
    TESS_BENCH_PATH_DIAGNOSTICS_RUN(
        result = tess::astar_path<Path3DWorld, PassableTag>(
            world,
            tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{63, 63, 15}},
            scratch));
    auto expanded = result.expanded_nodes;
    benchmark::DoNotOptimize(expanded);
  }
  record_path_counters(state, result);
  TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state);
}

void BM_path_astar_slab_multi_gap_3d_64x64x16(benchmark::State& state) {
  Path3DWorld world;
  fill_path_passable(world, 1);
  block_3d_x_slab_with_two_gaps(world, 32, tess::Coord3{32, 8, 0},
                                tess::Coord3{32, 63, 15});

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<Path3DShape>());
  TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_BENCH_PATH_DIAGNOSTICS_RESET();
    TESS_BENCH_PATH_DIAGNOSTICS_RUN(
        result = tess::astar_path<Path3DWorld, PassableTag>(
            world,
            tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{63, 0, 0}},
            scratch));
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  record_path_counters(state, result);
  TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state);
}

void BM_path_astar_corridor_3d_64x64x16(benchmark::State& state) {
  Path3DWorld world;
  carve_3d_corridor(world);

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<Path3DShape>());
  TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_BENCH_PATH_DIAGNOSTICS_RESET();
    TESS_BENCH_PATH_DIAGNOSTICS_RUN(
        result = tess::astar_path<Path3DWorld, PassableTag>(
            world,
            tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{63, 63, 15}},
            scratch));
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  record_path_counters(state, result);
  TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state);
}

void BM_path_astar_sparse_blockers_512x512(benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);
  carve_sparse_blockers(world);

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_BENCH_PATH_DIAGNOSTICS_RESET();
    TESS_BENCH_PATH_DIAGNOSTICS_RUN(
        result = tess::astar_path<PathScaleWorld, PassableTag>(
            world,
            tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{510, 510, 0}},
            scratch));
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  record_path_counters(state, result);
  TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state);
}

void BM_path_astar_room_portals_512x512(benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);
  carve_room_portals(world);

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_BENCH_PATH_DIAGNOSTICS_RESET();
    TESS_BENCH_PATH_DIAGNOSTICS_RUN(
        result = tess::astar_path<PathScaleWorld, PassableTag>(
            world,
            tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{510, 510, 0}},
            scratch));
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  record_path_counters(state, result);
  TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state);
}

void BM_path_astar_branch_lattice_512x512(benchmark::State& state) {
  PathScaleWorld world;
  carve_branch_lattice(world);

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_BENCH_PATH_DIAGNOSTICS_RESET();
    TESS_BENCH_PATH_DIAGNOSTICS_RUN(
        result = tess::astar_path<PathScaleWorld, PassableTag>(
            world,
            tess::PathRequest{tess::Coord3{4, 4, 0}, tess::Coord3{508, 508, 0}},
            scratch));
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  record_path_counters(state, result);
  TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state);
}

void BM_path_astar_batch_100_shared_room_portals_512x512(
    benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);
  carve_room_portals(world);

  std::array<tess::PathRequest, 100> requests{};
  for (std::size_t i = 0; i < requests.size(); ++i) {
    const auto offset = static_cast<std::int64_t>(i);
    requests[i] = tess::PathRequest{
        tess::Coord3{1 + offset % 16, 1 + offset / 16, 0},
        tess::Coord3{510, 510, 0},
    };
  }

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_BENCH_PATH_DIAGNOSTICS_RESET();
    std::uint64_t total_cost = 0;
    std::uint64_t total_expanded = 0;
    TESS_BENCH_PATH_DIAGNOSTICS_RUN(for (const auto request : requests) {
      result = tess::astar_path<PathScaleWorld, PassableTag>(world, request,
                                                             scratch);
      total_cost += result.cost;
      total_expanded += result.expanded_nodes;
    });
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_expanded);
  }
  state.counters["agents"] = static_cast<double>(requests.size());
  record_path_counters(state, result);
  TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state);
}

void BM_path_astar_batch_100_open_512x512(benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);

  std::array<tess::PathRequest, 100> requests{};
  for (std::size_t i = 0; i < requests.size(); ++i) {
    const auto offset = static_cast<std::int64_t>(i);
    requests[i] = tess::PathRequest{
        tess::Coord3{offset % 64, offset / 64, 0},
        tess::Coord3{511 - (offset % 64), 511 - (offset / 64), 0},
    };
  }

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_BENCH_PATH_DIAGNOSTICS_RESET();
    std::uint64_t total_cost = 0;
    std::uint64_t total_expanded = 0;
    TESS_BENCH_PATH_DIAGNOSTICS_RUN(for (const auto request : requests) {
      result = tess::astar_path<PathScaleWorld, PassableTag>(world, request,
                                                             scratch);
      total_cost += result.cost;
      total_expanded += result.expanded_nodes;
    });
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_expanded);
  }
  state.counters["agents"] = static_cast<double>(requests.size());
  record_path_counters(state, result);
  TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state);
}

void BM_path_astar_batch_100_mixed_512x512(benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);
  block_vertical_wall(world, 256, 511);

  std::array<tess::PathRequest, 100> requests{};
  for (std::size_t i = 0; i < requests.size(); ++i) {
    const auto offset = static_cast<std::int64_t>(i);
    if (i % 3 == 0) {
      requests[i] = tess::PathRequest{
          tess::Coord3{offset % 128, offset / 128, 0},
          tess::Coord3{255 - (offset % 128), 255 - (offset / 128), 0},
      };
    } else if (i % 3 == 1) {
      requests[i] = tess::PathRequest{
          tess::Coord3{offset % 128, offset / 128, 0},
          tess::Coord3{511, offset / 128, 0},
      };
    } else {
      requests[i] = tess::PathRequest{
          tess::Coord3{offset % 128, offset / 128, 0},
          tess::Coord3{511, 511, 0},
      };
    }
  }

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  TESS_BENCH_PATH_DIAGNOSTICS_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_BENCH_PATH_DIAGNOSTICS_RESET();
    std::uint64_t total_cost = 0;
    std::uint64_t total_expanded = 0;
    TESS_BENCH_PATH_DIAGNOSTICS_RUN(for (const auto request : requests) {
      result = tess::astar_path<PathScaleWorld, PassableTag>(world, request,
                                                             scratch);
      total_cost += result.cost;
      total_expanded += result.expanded_nodes;
    });
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_expanded);
  }
  state.counters["agents"] = static_cast<double>(requests.size());
  record_path_counters(state, result);
  TESS_BENCH_PATH_DIAGNOSTICS_RECORD(state);
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
BENCHMARK(BM_block_scratch_allocate_u32)->Name("block/scratch_allocate_u32");
BENCHMARK(BM_block_context_scratch_tile_iteration_2d)
    ->Name("block/context_scratch_tile_iteration_2d");
BENCHMARK(BM_block_chunk_tile_iteration<StorageWorld>)
    ->Name("block/chunk_tile_iteration_2d");
BENCHMARK(BM_block_chunk_tile_iteration<Block3DWorld>)
    ->Name("block/chunk_tile_iteration_3d");
BENCHMARK(BM_block_chunk_boundary_scan<Block2DWorld>)
    ->Name("block/chunk_boundary_scan_2d");
BENCHMARK(BM_block_chunk_boundary_scan<Block3DWorld>)
    ->Name("block/chunk_boundary_scan_3d");
BENCHMARK(BM_queued_execute_resident_update)
    ->Name("queued/execute_resident_update");
BENCHMARK(BM_path_astar_open_2d)->Name("path/astar_open_2d");
BENCHMARK(BM_path_astar_open_2d_64x64)->Name("path/astar_open_2d_64x64");
BENCHMARK(BM_path_astar_open_2d_512x512)->Name("path/astar_open_2d_512x512");
BENCHMARK(BM_path_astar_open_2d_1024x1024)
    ->Name("path/astar_open_2d_1024x1024");
BENCHMARK(BM_path_astar_wall_gap_512x512)->Name("path/astar_wall_gap_512x512");
BENCHMARK(BM_path_astar_alternate_direct_512x512)
    ->Name("path/astar_alternate_direct_512x512");
BENCHMARK(BM_path_astar_axis_detour_512x512)
    ->Name("path/astar_axis_detour_512x512");
BENCHMARK(BM_path_astar_no_path_512x512)->Name("path/astar_no_path_512x512");
BENCHMARK(BM_path_astar_striped_maze_512x512)
    ->Name("path/astar_striped_maze_512x512");
BENCHMARK(BM_path_astar_short_open_512x512)
    ->Name("path/astar_short_open_512x512");
BENCHMARK(BM_path_astar_medium_open_512x512)
    ->Name("path/astar_medium_open_512x512");
BENCHMARK(BM_path_astar_long_open_512x512)
    ->Name("path/astar_long_open_512x512");
BENCHMARK(BM_path_astar_no_path_1024x1024)
    ->Name("path/astar_no_path_1024x1024");
BENCHMARK(BM_path_astar_striped_maze_1024x1024)
    ->Name("path/astar_striped_maze_1024x1024");
BENCHMARK(BM_path_astar_vertical_open_512x512)
    ->Name("path/astar_vertical_open_512x512");
BENCHMARK(BM_path_astar_vertical_wall_gap_512x512)
    ->Name("path/astar_vertical_wall_gap_512x512");
BENCHMARK(BM_path_astar_vertical_striped_maze_512x512)
    ->Name("path/astar_vertical_striped_maze_512x512");
BENCHMARK(BM_path_astar_open_3d_64x64x16)->Name("path/astar_open_3d_64x64x16");
BENCHMARK(BM_path_astar_slab_gap_3d_64x64x16)
    ->Name("path/astar_slab_gap_3d_64x64x16");
BENCHMARK(BM_path_astar_slab_no_gap_3d_64x64x16)
    ->Name("path/astar_slab_no_gap_3d_64x64x16");
BENCHMARK(BM_path_astar_slab_multi_gap_3d_64x64x16)
    ->Name("path/astar_slab_multi_gap_3d_64x64x16");
BENCHMARK(BM_path_astar_corridor_3d_64x64x16)
    ->Name("path/astar_corridor_3d_64x64x16");
BENCHMARK(BM_path_astar_sparse_blockers_512x512)
    ->Name("path/astar_sparse_blockers_512x512");
BENCHMARK(BM_path_astar_room_portals_512x512)
    ->Name("path/astar_room_portals_512x512");
BENCHMARK(BM_path_astar_branch_lattice_512x512)
    ->Name("path/astar_branch_lattice_512x512");
BENCHMARK(BM_path_astar_batch_100_open_512x512)
    ->Name("path/astar_batch_100_open_512x512");
BENCHMARK(BM_path_astar_batch_100_mixed_512x512)
    ->Name("path/astar_batch_100_mixed_512x512");
BENCHMARK(BM_path_astar_batch_100_shared_room_portals_512x512)
    ->Name("path/astar_batch_100_shared_room_portals_512x512");

}  // namespace
