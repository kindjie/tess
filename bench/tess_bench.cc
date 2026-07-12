#include <benchmark/benchmark.h>
#include <tess/tess.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
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
using WeightedPathSchema =
    tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>,
                      tess::Field<CostTag, std::uint32_t>>;
using PathWorld = tess::AlwaysResidentWorld<PathShape, PathSchema>;
using PathRealisticWorld =
    tess::AlwaysResidentWorld<PathRealisticShape, PathSchema>;
using PathScaleWorld = tess::AlwaysResidentWorld<PathScaleShape, PathSchema>;
using WeightedPathScaleWorld =
    tess::AlwaysResidentWorld<PathScaleShape, WeightedPathSchema>;
using PathLargeWorld = tess::AlwaysResidentWorld<PathLargeShape, PathSchema>;
using PathVerticalScaleWorld =
    tess::AlwaysResidentWorld<PathVerticalScaleShape, PathSchema>;
using Path3DWorld = tess::AlwaysResidentWorld<Path3DShape, PathSchema>;

// Correctness checks mandated by docs/planning/benchmark-plan.md run outside
// the timed regions; a failed check aborts the benchmark binary so threshold
// runs cannot silently gate on wrong results.
void bench_check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "tess_bench correctness check failed: %s\n", message);
    std::abort();
  }
}

// Validates a Found path: endpoints match the request and every step is a
// unit move onto a passable tile.
template <typename World>
void check_found_path(World& world, const tess::PathResult& result,
                      tess::PathRequest request) {
  bench_check(result.status == tess::PathStatus::Found,
              "path status is not Found");
  bench_check(!result.path.empty(), "found path is empty");
  bench_check(result.path.front() == request.start,
              "path does not begin at the requested start");
  bench_check(result.path.back() == request.goal,
              "path does not end at the requested goal");
  for (std::size_t i = 1; i < result.path.size(); ++i) {
    bench_check(
        tess::manhattan_distance(result.path[i - 1], result.path[i]) == 1,
        "path contains a non-unit step");
    bench_check(world.template field<PassableTag>(result.path[i]) != 0,
                "path crosses an impassable tile");
  }
}

// Additionally pins the cost to an expected value captured from an untimed
// setup run of the same deterministic search.
template <typename World>
void check_found_path(World& world, const tess::PathResult& result,
                      tess::PathRequest request, std::uint64_t expected_cost) {
  check_found_path(world, result, request);
  bench_check(result.cost == expected_cost,
              "path cost differs from the expected setup-run cost");
}

void record_path_counters(benchmark::State& state,
                          const tess::PathResult& result) {
  state.counters["cost"] = static_cast<double>(result.cost);
  state.counters["path_nodes"] = static_cast<double>(result.path.size());
  state.counters["expanded_nodes"] = static_cast<double>(result.expanded_nodes);
  state.counters["reached_nodes"] = static_cast<double>(result.reached_nodes);
}

void record_route_cache_counters(benchmark::State& state,
                                 tess::RouteCacheStats stats) {
  state.counters["cache.entries"] = static_cast<double>(stats.entries);
  state.counters["cache.hits"] = static_cast<double>(stats.hits);
  state.counters["cache.suffix_hits"] = static_cast<double>(stats.suffix_hits);
  state.counters["cache.misses"] = static_cast<double>(stats.misses);
  state.counters["cache.path_nodes"] = static_cast<double>(stats.path_nodes);
}

template <typename World, std::size_t Count>
void record_path_batch_counters(
    benchmark::State& state,
    const std::array<tess::PathRequest, Count>& requests,
    std::uint64_t total_cost, std::uint64_t total_expanded) {
  using Shape = typename World::shape_type;
  std::array<tess::Coord3, Count> starts{};
  std::array<tess::Coord3, Count> goals{};
  std::array<std::uint64_t, Count> start_chunks{};
  std::array<std::uint64_t, Count> goal_chunks{};
  std::size_t unique_starts = 0;
  std::size_t unique_goals = 0;
  std::size_t unique_start_chunks = 0;
  std::size_t unique_goal_chunks = 0;

  const auto append_coord = [](auto& values, std::size_t& size,
                               tess::Coord3 coord) {
    for (std::size_t i = 0; i < size; ++i) {
      if (values[i] == coord) {
        return;
      }
    }
    values[size] = coord;
    ++size;
  };
  const auto append_key = [](auto& values, std::size_t& size,
                             std::uint64_t key) {
    for (std::size_t i = 0; i < size; ++i) {
      if (values[i] == key) {
        return;
      }
    }
    values[size] = key;
    ++size;
  };

  for (const auto request : requests) {
    append_coord(starts, unique_starts, request.start);
    append_coord(goals, unique_goals, request.goal);
    append_key(
        start_chunks, unique_start_chunks,
        static_cast<std::uint64_t>(
            tess::chunk_key<Shape>(tess::chunk_coord<Shape>(request.start))
                .value));
    append_key(
        goal_chunks, unique_goal_chunks,
        static_cast<std::uint64_t>(
            tess::chunk_key<Shape>(tess::chunk_coord<Shape>(request.goal))
                .value));
  }

  state.counters["agents"] = static_cast<double>(Count);
  state.counters["batch.unique_starts"] = static_cast<double>(unique_starts);
  state.counters["batch.unique_goals"] = static_cast<double>(unique_goals);
  state.counters["batch.unique_start_chunks"] =
      static_cast<double>(unique_start_chunks);
  state.counters["batch.unique_goal_chunks"] =
      static_cast<double>(unique_goal_chunks);
  // Whole-batch aggregates: the per-request counter names (cost,
  // expanded_nodes, ...) previously published the LAST request only, which
  // made per-node timing math ~100x off for 100-request batches.
  state.counters["batch.cost_total"] = static_cast<double>(total_cost);
  state.counters["batch.expanded_total"] = static_cast<double>(total_expanded);
  state.counters["batch.avg_expanded_nodes"] =
      static_cast<double>(total_expanded) / static_cast<double>(Count);
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
  state.counters["diag.cost_reads"] =
      static_cast<double>(diagnostics.cost_reads);
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

#define TESS_PATH_DIAG_DECL(scratch)             \
  tess::diagnostics::PathCounters path_counters; \
  tess::diagnostics::AllocationCounters allocation_counters;
#define TESS_PATH_DIAG_RESET()   \
  do {                           \
    path_counters.reset();       \
    allocation_counters.reset(); \
  } while (false)
#define TESS_PATH_DIAG_RUN(...)                                              \
  do {                                                                       \
    tess::diagnostics::ScopedPathCounters path_counter_scope{path_counters}; \
    tess::diagnostics::ScopedAllocationCounters allocation_scope{            \
        allocation_counters};                                                \
    __VA_ARGS__;                                                             \
  } while (false)
#define TESS_PATH_DIAG_RECORD(state)                                     \
  do {                                                                   \
    record_path_diagnostic_counters((state), path_counters);             \
    record_allocation_diagnostic_counters((state), allocation_counters); \
  } while (false)
#else
#define TESS_PATH_DIAG_DECL(scratch) \
  do {                               \
  } while (false)
#define TESS_PATH_DIAG_RESET() \
  do {                         \
  } while (false)
#define TESS_PATH_DIAG_RUN(...) \
  do {                          \
    __VA_ARGS__;                \
  } while (false)
#define TESS_PATH_DIAG_RECORD(state) \
  do {                               \
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
  // Precompute the encoded keys so the timed loop measures decode only;
  // calling tile_key() inside the loop double-counted the encode cost.
  constexpr auto key_count = std::size_t{1024};
  std::vector<decltype(tess::tile_key<Shape>(coord))> keys;
  keys.reserve(key_count);
  for (std::size_t i = 0; i < key_count; ++i) {
    keys.push_back(tess::tile_key<Shape>(coord));
    coord.x = (coord.x + 17) %
              static_cast<std::int64_t>(tess::ShapeTraits<Shape>::size.x);
    coord.y = (coord.y + 31) %
              static_cast<std::int64_t>(tess::ShapeTraits<Shape>::size.y);
    coord.z = (coord.z + 7) %
              static_cast<std::int64_t>(tess::ShapeTraits<Shape>::size.z);
  }

  std::size_t index = 0;
  for (auto _ : state) {
    auto result = tess::coord<Shape>(keys[index]);
    benchmark::DoNotOptimize(result);
    index = (index + 1) & (key_count - 1);
  }
}

// The escape-then-clobber pattern in the next four benchmarks is
// load-bearing: DoNotOptimize(&page) makes the page observable, and
// ClobberMemory() forces each iteration's stores to commit (and reloads
// after it). Without both, dead-store elimination and loop-invariant
// hoisting collapse these loops to nothing -- all four compiled to
// empty loops (~0.25 ns) until audit 2026-07-11 finding H1.
void BM_field_span_acquisition(benchmark::State& state) {
  StoragePage page{tess::ChunkKey{0}, tess::ChunkCoord3{0, 0, 0}};
  // Span acquisition is pure address arithmetic off the page base, so a
  // visible &page is not enough -- the offsets hoist as loop-invariant.
  // An opaque pointer forces the arithmetic to re-run each iteration.
  StoragePage* opaque_page = &page;
  for (auto _ : state) {
    benchmark::DoNotOptimize(opaque_page);
    auto terrain = opaque_page->field_span<TerrainTag>();
    auto costs = opaque_page->field_span<CostTag>();
    benchmark::DoNotOptimize(terrain.data());
    benchmark::DoNotOptimize(costs.data());
  }
}

void BM_chunk_field_write_read_iteration(benchmark::State& state) {
  StoragePage page{tess::ChunkKey{0}, tess::ChunkCoord3{0, 0, 0}};
  benchmark::DoNotOptimize(&page);
  for (auto _ : state) {
    auto terrain = page.field_span<TerrainTag>();
    for (std::uint64_t i = 0; i < StoragePage::local_tile_count; ++i) {
      terrain[i] = static_cast<std::uint16_t>(i);
    }
    // Commit the stores and forget their values, so the read loop below
    // must load instead of store-forwarding the known constants.
    benchmark::ClobberMemory();
    std::uint64_t sum = 0;
    for (std::uint64_t i = 0; i < StoragePage::local_tile_count; ++i) {
      sum += terrain[i];
    }
    benchmark::DoNotOptimize(sum);
  }
}

void BM_single_chunk_page_iteration(benchmark::State& state) {
  StorageSingleChunkPage page{tess::ChunkKey{0}, tess::ChunkCoord3{0, 0, 0}};
  benchmark::DoNotOptimize(&page);
  for (auto _ : state) {
    auto terrain = page.field_span<TerrainTag>();
    for (std::uint64_t i = 0; i < StorageSingleChunkPage::local_tile_count;
         ++i) {
      terrain[i] = static_cast<std::uint16_t>(i);
    }
    benchmark::ClobberMemory();
    std::uint64_t sum = 0;
    for (std::uint64_t i = 0; i < StorageSingleChunkPage::local_tile_count;
         ++i) {
      sum += terrain[i];
    }
    benchmark::DoNotOptimize(sum);
  }
}

void BM_flat_array_iteration(benchmark::State& state) {
  std::array<std::uint16_t, StorageSingleChunkPage::local_tile_count> terrain{};
  benchmark::DoNotOptimize(terrain.data());
  for (auto _ : state) {
    for (std::uint64_t i = 0; i < terrain.size(); ++i) {
      terrain[i] = static_cast<std::uint16_t>(i);
    }
    benchmark::ClobberMemory();
    std::uint64_t sum = 0;
    for (std::uint64_t i = 0; i < terrain.size(); ++i) {
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
  std::size_t expected_dirty = 0;
  for (std::uint64_t key = 0; key < StorageWorld::chunk_count; key += 3) {
    world.mark_dirty(tess::ChunkKey{key}, DirtyTerrain, bounds);
    ++expected_dirty;
  }
  for (std::uint64_t key = 1; key < StorageWorld::chunk_count; key += 5) {
    world.mark_dirty(tess::ChunkKey{key}, DirtyCost, bounds);
  }

  // Collect into a hoisted, reserved vector and iterate the collected keys;
  // the previous by-value dirty_chunks() call never iterated the result, so
  // the loop timed vector allocation instead of dirty-chunk iteration.
  std::vector<tess::ChunkKey> chunks;
  chunks.reserve(StorageWorld::chunk_count);
  for (auto _ : state) {
    chunks.clear();
    world.collect_dirty_chunks(DirtyTerrain, chunks);
    std::uint64_t sum = 0;
    for (const auto key : chunks) {
      sum += key.value;
    }
    benchmark::DoNotOptimize(sum);
  }
  bench_check(chunks.size() == expected_dirty,
              "dirty chunk collection count mismatch");
}

// Streaming-scale variant of the dirty scan: 16384 chunks with a minimal
// schema, so the scanned state plus the collected-key output (64 KB flag
// column + 128 KB output vector) exceeds L1 on every target and the scan
// pays the layout's memory cost, not just loop overhead. Before the M5 SoA
// split the scan streamed 80-byte ChunkMeta structs (1.3 MB) for the same
// work (audit 2026-07-11 M5; sized up on Codex review of the split). The
// world is static: 16k pages are constructed once, not per timing run.
using StorageScanShape =
    tess::Shape<tess::Extent3{8192, 8192, 1}, tess::Extent3{64, 64, 1}>;
using StorageScanSchema = tess::FieldSchema<tess::Field<TerrainTag, bool>>;
using StorageScanWorld =
    tess::AlwaysResidentWorld<StorageScanShape, StorageScanSchema>;

void BM_world_dirty_chunks_iteration_16k(benchmark::State& state) {
  static auto* world = [] {
    auto* w = new StorageScanWorld();
    const auto bounds =
        tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}};
    for (std::uint64_t key = 0; key < StorageScanWorld::chunk_count; key += 3) {
      w->mark_dirty(tess::ChunkKey{key}, DirtyTerrain, bounds);
    }
    for (std::uint64_t key = 1; key < StorageScanWorld::chunk_count; key += 5) {
      w->mark_dirty(tess::ChunkKey{key}, DirtyCost, bounds);
    }
    return w;
  }();
  constexpr auto expected_dirty =
      (StorageScanWorld::chunk_count + 2) / 3;  // keys 0,3,6,...

  std::vector<tess::ChunkKey> chunks;
  chunks.reserve(StorageScanWorld::chunk_count);
  for (auto _ : state) {
    chunks.clear();
    world->collect_dirty_chunks(DirtyTerrain, chunks);
    std::uint64_t sum = 0;
    for (const auto key : chunks) {
      sum += key.value;
    }
    benchmark::DoNotOptimize(sum);
  }
  bench_check(chunks.size() == expected_dirty,
              "dirty chunk collection count mismatch (16k)");
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

  // With a compile-time-constant size, reset+allocate folds to a single
  // constant store (audit 2026-07-11 H1: this loop compiled empty). An
  // opaque runtime size forces the alignment/bounds arithmetic to run.
  std::size_t count = StorageWorldShape::chunk.x * StorageWorldShape::chunk.y *
                      StorageWorldShape::chunk.z;
  benchmark::DoNotOptimize(&scratch);
  for (auto _ : state) {
    benchmark::DoNotOptimize(count);
    scratch.reset();
    const auto values = scratch.allocate<std::uint32_t>(count);
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
    tess::for_each_chunk<tess::WritePolicy::UniquePerChunk>(
        world, domain, [&](auto view) {
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
    tess::for_each_chunk<tess::WritePolicy::UniquePerChunk>(
        world, domain, [&](auto view) {
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

  std::uint64_t last_chunk_count = 0;
  for (auto _ : state) {
    const auto result = tess::execute_plan<tess::WritePolicy::UniquePerChunk>(
        world, plan, [](auto view) {
          auto terrain = view.template field_span<TerrainTag>();
          terrain[0] = static_cast<std::uint16_t>(view.key().value);
        });
    last_chunk_count = result.chunk_count;
    benchmark::DoNotOptimize(last_chunk_count);
  }
  bench_check(last_chunk_count == StorageWorld::chunk_count,
              "queued execution did not visit every resident chunk");
}

// Result-bearing counterpart (S6): same per-chunk write through
// execute_plan_deferred_dirty_with_results, plus the per-frame channel
// drain and clear a consumer performs. Gates the ack-delivery overhead
// against the resultless ceiling above.
void BM_queued_execute_resident_update_with_results(benchmark::State& state) {
  StorageWorld world;
  tess::FrameOps ops;
  (void)ops.update_field(tess::DomainDesc::resident_chunks(),
                         tess::FieldAccessDesc{0, DirtyTerrain, 0},
                         tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);
  const auto& plan = report.plan();

  struct Ack {
    std::uint64_t chunks = 0;
  };
  tess::ResultChannel<Ack> channel;
  channel.reserve_operations(1);
  tess::PlannedDirtyAccumulator dirty;

  std::uint64_t last_chunk_count = 0;
  std::uint64_t drained = 0;
  for (auto _ : state) {
    const auto result = tess::execute_plan_deferred_dirty_with_results<
        tess::WritePolicy::UniquePerChunk>(
        world, plan, dirty, channel, [](auto view, Ack& ack) {
          auto terrain = view.template field_span<TerrainTag>();
          terrain[0] = static_cast<std::uint16_t>(view.key().value);
          ++ack.chunks;
        });
    last_chunk_count = result.chunk_count;
    drained += channel.drain_results([](tess::OpHandle,
                                        const tess::OpCompletion& completion,
                                        const Ack* value) {
      benchmark::DoNotOptimize(completion.chunk_count);
      benchmark::DoNotOptimize(value);
    });
    channel.clear();
    dirty.clear();
    benchmark::DoNotOptimize(last_chunk_count);
  }
  bench_check(last_chunk_count == StorageWorld::chunk_count,
              "result-bearing execution did not visit every resident chunk");
  bench_check(drained == static_cast<std::uint64_t>(state.iterations()),
              "result-bearing execution did not deliver one ack per frame");
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

template <typename World>
void fill_path_cost(World& world, std::uint32_t value) {
  for (auto& page : world.chunks()) {
    auto costs = page.template field_span<CostTag>();
    for (auto& tile : costs) {
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

template <typename World, typename SetCost>
void carve_sparse_blockers_impl(World& world, SetCost set_cost) {
  for (std::int64_t y = 1;
       y + 1 < static_cast<std::int64_t>(PathScaleShape::size.y); ++y) {
    for (std::int64_t x = 1;
         x + 1 < static_cast<std::int64_t>(PathScaleShape::size.x); ++x) {
      const auto hash = path_hash(static_cast<std::uint64_t>(x),
                                  static_cast<std::uint64_t>(y));
      if (hash % 100u < 18u) {
        world.template field<PassableTag>(tess::Coord3{x, y, 0}) = 0;
      }
      if (hash % 100u >= 18u && hash % 100u < 32u) {
        set_cost(tess::Coord3{x, y, 0}, 7);
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
  set_cost(tess::Coord3{1, 1, 0}, 1);
  world.template field<PassableTag>(tess::Coord3{510, 510, 0}) = 1;
  set_cost(tess::Coord3{510, 510, 0}, 1);
}

void carve_sparse_blockers(PathScaleWorld& world) {
  carve_sparse_blockers_impl(world,
                             [](tess::Coord3, std::uint32_t) noexcept {});
}

void carve_sparse_blockers(WeightedPathScaleWorld& world) {
  carve_sparse_blockers_impl(world,
                             [&world](tess::Coord3 coord, std::uint32_t cost) {
                               world.template field<CostTag>(coord) = cost;
                             });
}

template <typename World, typename SetCost>
void carve_room_portals_impl(World& world, SetCost set_cost) {
  constexpr auto room_size = std::int64_t{32};
  for (std::int64_t x = room_size;
       x < static_cast<std::int64_t>(PathScaleShape::size.x); x += room_size) {
    for (std::int64_t y = 0;
         y < static_cast<std::int64_t>(PathScaleShape::size.y); ++y) {
      const auto room = y / room_size;
      const auto portal = (room * 23 + x / room_size * 17) % room_size;
      if (y % room_size != portal) {
        world.template field<PassableTag>(tess::Coord3{x, y, 0}) = 0;
      } else {
        set_cost(tess::Coord3{x, y, 0}, 5);
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
      } else {
        set_cost(tess::Coord3{x, y, 0}, 5);
      }
    }
  }
}

void carve_room_portals(PathScaleWorld& world) {
  carve_room_portals_impl(world, [](tess::Coord3, std::uint32_t) noexcept {});
}

void carve_room_portals(WeightedPathScaleWorld& world) {
  carve_room_portals_impl(world,
                          [&world](tess::Coord3 coord, std::uint32_t cost) {
                            world.template field<CostTag>(coord) = cost;
                          });
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

// Shared runner for single-request unit-cost A* benchmarks. The untimed
// setup run warms scratch storage and captures the expected result for the
// post-loop correctness check.
template <typename World, typename Setup>
void run_unit_astar_bench(benchmark::State& state, tess::PathRequest request,
                          bool expect_found, Setup setup,
                          bool check_alloc_free = false) {
  using Shape = typename World::shape_type;
  World world;
  fill_path_passable(world, 1);
  setup(world);

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<Shape>());
  const auto expected =
      tess::astar_path<World, PassableTag>(world, request, scratch);
  TESS_PATH_DIAG_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    TESS_PATH_DIAG_RUN(
        result = tess::astar_path<World, PassableTag>(world, request, scratch));
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  if (expect_found) {
    check_found_path(world, result, request, expected.cost);
  } else {
    bench_check(result.status == tess::PathStatus::NoPath,
                "expected an unreachable goal");
    bench_check(result.path.empty(), "no-path result carries path nodes");
  }
#if TESS_DIAGNOSTICS_ENABLED
  if (check_alloc_free) {
    // Representative warm-path allocation assertion (benchmark-plan.md
    // section 14): the last timed iteration ran against warmed scratch, so
    // the diagnostics allocation counters must report zero allocations.
    bench_check(allocation_counters.allocations == 0,
                "warm path iteration allocated");
  }
#else
  (void)check_alloc_free;
#endif
  record_path_counters(state, result);
  TESS_PATH_DIAG_RECORD(state);
}

void BM_path_astar_open_2d(benchmark::State& state) {
  run_unit_astar_bench<PathWorld>(
      state, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{15, 15, 0}},
      true, [](PathWorld&) noexcept {}, true);
}

void BM_path_astar_open_2d_64x64(benchmark::State& state) {
  run_unit_astar_bench<PathRealisticWorld>(
      state, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{63, 63, 0}},
      true, [](PathRealisticWorld&) noexcept {});
}

void BM_path_astar_open_2d_512x512(benchmark::State& state) {
  run_unit_astar_bench<PathScaleWorld>(
      state,
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{511, 511, 0}}, true,
      [](PathScaleWorld&) noexcept {});
}

void BM_path_astar_open_2d_1024x1024(benchmark::State& state) {
  run_unit_astar_bench<PathLargeWorld>(
      state,
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{1023, 1023, 0}},
      true, [](PathLargeWorld&) noexcept {});
}

template <typename Setup>
void run_weighted_astar_512x512(benchmark::State& state,
                                tess::PathRequest request, Setup setup) {
  WeightedPathScaleWorld world;
  fill_path_passable(world, 1);
  fill_path_cost(world, 1);
  setup(world);

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  // Untimed setup run captures the expected cost for the post-loop check.
  const auto expected =
      tess::weighted_astar_path<WeightedPathScaleWorld, PassableTag, CostTag>(
          world, request, scratch);
  TESS_PATH_DIAG_DECL(scratch);
  tess::PathResult result;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    TESS_PATH_DIAG_RUN(
        result = tess::weighted_astar_path<WeightedPathScaleWorld, PassableTag,
                                           CostTag>(world, request, scratch));
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  check_found_path(world, result, request, expected.cost);
  record_path_counters(state, result);
  TESS_PATH_DIAG_RECORD(state);
}

void BM_path_weighted_astar_open_512x512(benchmark::State& state) {
  run_weighted_astar_512x512(
      state,
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{511, 511, 0}},
      [](WeightedPathScaleWorld&) noexcept {});
}

void BM_path_weighted_astar_axis_detour_512x512(benchmark::State& state) {
  run_weighted_astar_512x512(
      state, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{511, 0, 0}},
      [](WeightedPathScaleWorld& world) {
        for (std::int64_t x = 1; x < 511; ++x) {
          world.template field<CostTag>(tess::Coord3{x, 0, 0}) = 25;
        }
      });
}

void BM_path_weighted_astar_sparse_blockers_512x512(benchmark::State& state) {
  run_weighted_astar_512x512(
      state,
      tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{510, 510, 0}},
      [](WeightedPathScaleWorld& world) { carve_sparse_blockers(world); });
}

void BM_path_weighted_astar_room_portals_512x512(benchmark::State& state) {
  run_weighted_astar_512x512(
      state,
      tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{510, 510, 0}},
      [](WeightedPathScaleWorld& world) { carve_room_portals(world); });
}

void BM_path_weighted_astar_batch_100_mixed_512x512(benchmark::State& state) {
  WeightedPathScaleWorld world;
  fill_path_passable(world, 1);
  fill_path_cost(world, 1);
  carve_sparse_blockers(world);

  std::array<tess::PathRequest, 100> requests{};
  for (std::size_t i = 0; i < requests.size(); ++i) {
    const auto offset = static_cast<std::int64_t>(i);
    const auto start = tess::Coord3{1 + offset % 16, 1 + offset / 16, 0};
    const auto goal = i % 2 == 0 ? tess::Coord3{510, 510, 0}
                                 : tess::Coord3{480, 510 - offset % 32, 0};
    world.template field<PassableTag>(start) = 1;
    world.template field<CostTag>(start) = 1;
    world.template field<PassableTag>(goal) = 1;
    world.template field<CostTag>(goal) = 1;
    requests[i] = tess::PathRequest{start, goal};
  }

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  const auto expected_last =
      tess::weighted_astar_path<WeightedPathScaleWorld, PassableTag, CostTag>(
          world, requests.back(), scratch);
  TESS_PATH_DIAG_DECL(scratch);
  tess::PathResult result;
  std::uint64_t total_cost = 0;
  std::uint64_t total_expanded = 0;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    total_cost = 0;
    total_expanded = 0;
    TESS_PATH_DIAG_RUN(for (const auto request : requests) {
      result = tess::weighted_astar_path<WeightedPathScaleWorld, PassableTag,
                                         CostTag>(world, request, scratch);
      total_cost += result.cost;
      total_expanded += result.expanded_nodes;
    });
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_expanded);
  }
  check_found_path(world, result, requests.back(), expected_last.cost);
  record_path_batch_counters<WeightedPathScaleWorld>(
      state, requests, total_cost, total_expanded);
  TESS_PATH_DIAG_RECORD(state);
}

// Untimed reference run used by batch benchmarks to pin the last request's
// cost for the post-loop correctness check.
[[nodiscard]] auto expected_unit_path_cost(PathScaleWorld& world,
                                           tess::PathRequest request)
    -> std::uint64_t {
  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  const auto result =
      tess::astar_path<PathScaleWorld, PassableTag>(world, request, scratch);
  bench_check(result.status == tess::PathStatus::Found,
              "setup reference path not found");
  return result.cost;
}

void BM_path_astar_wall_gap_512x512(benchmark::State& state) {
  run_unit_astar_bench<PathScaleWorld>(
      state, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{511, 0, 0}},
      true,
      [](PathScaleWorld& world) { block_vertical_wall(world, 256, 511); });
}

void BM_path_astar_alternate_direct_512x512(benchmark::State& state) {
  run_unit_astar_bench<PathScaleWorld>(
      state,
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{511, 511, 0}}, true,
      [](PathScaleWorld& world) {
        world.template field<PassableTag>(tess::Coord3{256, 0, 0}) = 0;
      });
}

void BM_path_astar_axis_detour_512x512(benchmark::State& state) {
  run_unit_astar_bench<PathScaleWorld>(
      state, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{511, 0, 0}},
      true, [](PathScaleWorld& world) {
        world.template field<PassableTag>(tess::Coord3{256, 0, 0}) = 0;
      });
}

void BM_path_astar_no_path_512x512(benchmark::State& state) {
  run_unit_astar_bench<PathScaleWorld>(
      state,
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{511, 511, 0}},
      false,
      [](PathScaleWorld& world) { block_vertical_wall(world, 256, -1); });
}

void BM_path_astar_striped_maze_512x512(benchmark::State& state) {
  run_unit_astar_bench<PathScaleWorld>(
      state,
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{511, 511, 0}}, true,
      [](PathScaleWorld& world) { carve_striped_maze(world); });
}

void BM_path_astar_short_open_512x512(benchmark::State& state) {
  run_unit_astar_bench<PathScaleWorld>(
      state,
      tess::PathRequest{tess::Coord3{32, 32, 0}, tess::Coord3{63, 63, 0}}, true,
      [](PathScaleWorld&) noexcept {});
}

void BM_path_astar_medium_open_512x512(benchmark::State& state) {
  run_unit_astar_bench<PathScaleWorld>(
      state,
      tess::PathRequest{tess::Coord3{64, 64, 0}, tess::Coord3{255, 255, 0}},
      true, [](PathScaleWorld&) noexcept {});
}

void BM_path_astar_long_open_512x512(benchmark::State& state) {
  run_unit_astar_bench<PathScaleWorld>(
      state,
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{511, 511, 0}}, true,
      [](PathScaleWorld&) noexcept {});
}

void BM_path_astar_no_path_1024x1024(benchmark::State& state) {
  run_unit_astar_bench<PathLargeWorld>(
      state,
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{1023, 1023, 0}},
      false,
      [](PathLargeWorld& world) { block_vertical_wall(world, 512, -1); });
}

void BM_path_astar_striped_maze_1024x1024(benchmark::State& state) {
  run_unit_astar_bench<PathLargeWorld>(
      state,
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{1023, 1023, 0}},
      true, [](PathLargeWorld& world) { carve_striped_maze(world); });
}

void BM_path_astar_vertical_open_512x512(benchmark::State& state) {
  run_unit_astar_bench<PathVerticalScaleWorld>(
      state,
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{0, 511, 511}}, true,
      [](PathVerticalScaleWorld&) noexcept {});
}

void BM_path_astar_vertical_wall_gap_512x512(benchmark::State& state) {
  run_unit_astar_bench<PathVerticalScaleWorld>(
      state, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{0, 511, 0}},
      true,
      [](PathVerticalScaleWorld& world) { block_yz_wall(world, 256, 511); });
}

void BM_path_astar_vertical_striped_maze_512x512(benchmark::State& state) {
  run_unit_astar_bench<PathVerticalScaleWorld>(
      state,
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{0, 511, 511}}, true,
      [](PathVerticalScaleWorld& world) {
        carve_vertical_striped_maze(world);
      });
}

void BM_path_astar_open_3d_64x64x16(benchmark::State& state) {
  run_unit_astar_bench<Path3DWorld>(
      state, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{63, 63, 15}},
      true, [](Path3DWorld&) noexcept {});
}

void BM_path_astar_slab_gap_3d_64x64x16(benchmark::State& state) {
  run_unit_astar_bench<Path3DWorld>(
      state, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{63, 0, 0}},
      true, [](Path3DWorld& world) {
        block_3d_x_slab(world, 32, tess::Coord3{32, 63, 15});
      });
}

void BM_path_astar_slab_no_gap_3d_64x64x16(benchmark::State& state) {
  run_unit_astar_bench<Path3DWorld>(
      state, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{63, 63, 15}},
      false, [](Path3DWorld& world) {
        block_3d_x_slab(world, 32, tess::Coord3{32, -1, -1});
      });
}

void BM_path_astar_slab_multi_gap_3d_64x64x16(benchmark::State& state) {
  run_unit_astar_bench<Path3DWorld>(
      state, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{63, 0, 0}},
      true, [](Path3DWorld& world) {
        block_3d_x_slab_with_two_gaps(world, 32, tess::Coord3{32, 8, 0},
                                      tess::Coord3{32, 63, 15});
      });
}

void BM_path_astar_corridor_3d_64x64x16(benchmark::State& state) {
  run_unit_astar_bench<Path3DWorld>(
      state, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{63, 63, 15}},
      true, [](Path3DWorld& world) { carve_3d_corridor(world); });
}

void BM_path_astar_sparse_blockers_512x512(benchmark::State& state) {
  run_unit_astar_bench<PathScaleWorld>(
      state,
      tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{510, 510, 0}}, true,
      [](PathScaleWorld& world) { carve_sparse_blockers(world); });
}

void BM_path_astar_room_portals_512x512(benchmark::State& state) {
  run_unit_astar_bench<PathScaleWorld>(
      state,
      tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{510, 510, 0}}, true,
      [](PathScaleWorld& world) { carve_room_portals(world); });
}

void BM_path_astar_branch_lattice_512x512(benchmark::State& state) {
  run_unit_astar_bench<PathScaleWorld>(
      state,
      tess::PathRequest{tess::Coord3{4, 4, 0}, tess::Coord3{508, 508, 0}}, true,
      [](PathScaleWorld& world) { carve_branch_lattice(world); });
}

void BM_path_astar_batch_100_shared_room_portals_512x512(
    benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);
  carve_room_portals(world);

  std::array<tess::PathRequest, 100> requests{};
  for (std::size_t i = 0; i < requests.size(); ++i) {
    const auto offset = static_cast<std::int64_t>(i);
    const auto start = tess::Coord3{1 + offset % 16, 1 + offset / 16, 0};
    const auto goal = tess::Coord3{510, 510, 0};
    world.template field<PassableTag>(start) = 1;
    world.template field<PassableTag>(goal) = 1;
    requests[i] = tess::PathRequest{
        start,
        goal,
    };
  }

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  const auto expected_last_cost =
      expected_unit_path_cost(world, requests.back());
  TESS_PATH_DIAG_DECL(scratch);
  tess::PathResult result;
  std::uint64_t total_cost = 0;
  std::uint64_t total_expanded = 0;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    total_cost = 0;
    total_expanded = 0;
    TESS_PATH_DIAG_RUN(for (const auto request : requests) {
      result = tess::astar_path<PathScaleWorld, PassableTag>(world, request,
                                                             scratch);
      total_cost += result.cost;
      total_expanded += result.expanded_nodes;
    });
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_expanded);
  }
  check_found_path(world, result, requests.back(), expected_last_cost);
  record_path_batch_counters<PathScaleWorld>(state, requests, total_cost,
                                             total_expanded);
  TESS_PATH_DIAG_RECORD(state);
}

void BM_path_distance_field_batch_100_shared_room_portals_512x512(
    benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);
  carve_room_portals(world);

  std::array<tess::PathRequest, 100> requests{};
  const auto goal = tess::Coord3{510, 510, 0};
  world.template field<PassableTag>(goal) = 1;
  for (std::size_t i = 0; i < requests.size(); ++i) {
    const auto offset = static_cast<std::int64_t>(i);
    const auto start = tess::Coord3{1 + offset % 16, 1 + offset / 16, 0};
    world.template field<PassableTag>(start) = 1;
    requests[i] = tess::PathRequest{start, goal};
  }

  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  const auto expected_last_cost =
      expected_unit_path_cost(world, requests.back());
  TESS_PATH_DIAG_DECL(scratch);
  tess::DistanceFieldResult field;
  tess::PathResult result;
  std::uint64_t total_cost = 0;
  std::uint64_t total_expanded = 0;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    total_cost = 0;
    total_expanded = 0;
    TESS_PATH_DIAG_RUN(
        field = tess::build_distance_field<PathScaleWorld, PassableTag>(
            world, goal, scratch);
        for (const auto request : requests) {
          result = tess::distance_field_path<PathScaleWorld, PassableTag>(
              world, request.start, request.goal, scratch);
          total_cost += result.cost;
          total_expanded += result.expanded_nodes;
        });
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_expanded);
    benchmark::DoNotOptimize(field.expanded_nodes);
  }
  bench_check(field.status == tess::PathStatus::Found,
              "distance field build failed");
  check_found_path(world, result, requests.back(), expected_last_cost);
  record_path_batch_counters<PathScaleWorld>(state, requests, total_cost,
                                             total_expanded);
  state.counters["field_expanded_nodes"] =
      static_cast<double>(field.expanded_nodes);
  state.counters["field_reached_nodes"] =
      static_cast<double>(field.reached_nodes);
  TESS_PATH_DIAG_RECORD(state);
}

void BM_path_astar_batch_100_shared_sparse_512x512(benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);
  carve_sparse_blockers(world);

  std::array<tess::PathRequest, 100> requests{};
  for (std::size_t i = 0; i < requests.size(); ++i) {
    const auto x = static_cast<std::int64_t>(1 + i % 10);
    const auto y = static_cast<std::int64_t>(1 + i / 10);
    const auto goal = tess::Coord3{510, 510, 0};
    world.template field<PassableTag>(tess::Coord3{x, y, 0}) = 1;
    world.template field<PassableTag>(goal) = 1;
    requests[i] = tess::PathRequest{tess::Coord3{x, y, 0}, goal};
  }

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  const auto expected_last_cost =
      expected_unit_path_cost(world, requests.back());
  TESS_PATH_DIAG_DECL(scratch);
  tess::PathResult result;
  std::uint64_t total_cost = 0;
  std::uint64_t total_expanded = 0;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    total_cost = 0;
    total_expanded = 0;
    TESS_PATH_DIAG_RUN(for (const auto request : requests) {
      result = tess::astar_path<PathScaleWorld, PassableTag>(world, request,
                                                             scratch);
      total_cost += result.cost;
      total_expanded += result.expanded_nodes;
    });
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_expanded);
  }
  check_found_path(world, result, requests.back(), expected_last_cost);
  record_path_batch_counters<PathScaleWorld>(state, requests, total_cost,
                                             total_expanded);
  TESS_PATH_DIAG_RECORD(state);
}

void BM_path_distance_field_batch_100_shared_sparse_512x512(
    benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);
  carve_sparse_blockers(world);

  std::array<tess::PathRequest, 100> requests{};
  const auto goal = tess::Coord3{510, 510, 0};
  world.template field<PassableTag>(goal) = 1;
  for (std::size_t i = 0; i < requests.size(); ++i) {
    const auto x = static_cast<std::int64_t>(1 + i % 10);
    const auto y = static_cast<std::int64_t>(1 + i / 10);
    const auto start = tess::Coord3{x, y, 0};
    world.template field<PassableTag>(start) = 1;
    requests[i] = tess::PathRequest{start, goal};
  }

  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  const auto expected_last_cost =
      expected_unit_path_cost(world, requests.back());
  TESS_PATH_DIAG_DECL(scratch);
  tess::DistanceFieldResult field;
  tess::PathResult result;
  std::uint64_t total_cost = 0;
  std::uint64_t total_expanded = 0;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    total_cost = 0;
    total_expanded = 0;
    TESS_PATH_DIAG_RUN(
        field = tess::build_distance_field<PathScaleWorld, PassableTag>(
            world, goal, scratch);
        for (const auto request : requests) {
          result = tess::distance_field_path<PathScaleWorld, PassableTag>(
              world, request.start, request.goal, scratch);
          total_cost += result.cost;
          total_expanded += result.expanded_nodes;
        });
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_expanded);
    benchmark::DoNotOptimize(field.expanded_nodes);
  }
  bench_check(field.status == tess::PathStatus::Found,
              "distance field build failed");
  check_found_path(world, result, requests.back(), expected_last_cost);
  record_path_batch_counters<PathScaleWorld>(state, requests, total_cost,
                                             total_expanded);
  state.counters["field_expanded_nodes"] =
      static_cast<double>(field.expanded_nodes);
  state.counters["field_reached_nodes"] =
      static_cast<double>(field.reached_nodes);
  TESS_PATH_DIAG_RECORD(state);
}

void BM_path_astar_batch_100_multigoal_room_portals_512x512(
    benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);
  carve_room_portals(world);

  constexpr auto goals = std::array{
      tess::Coord3{510, 510, 0}, tess::Coord3{510, 447, 0},
      tess::Coord3{447, 510, 0}, tess::Coord3{383, 510, 0},
      tess::Coord3{510, 383, 0}, tess::Coord3{447, 447, 0},
      tess::Coord3{383, 447, 0}, tess::Coord3{447, 383, 0},
  };
  std::array<tess::PathRequest, 100> requests{};
  for (std::size_t i = 0; i < requests.size(); ++i) {
    const auto offset = static_cast<std::int64_t>(i);
    const auto start = tess::Coord3{1 + offset % 16, 1 + offset / 16, 0};
    const auto goal = goals[i % goals.size()];
    world.template field<PassableTag>(start) = 1;
    world.template field<PassableTag>(goal) = 1;
    requests[i] = tess::PathRequest{
        start,
        goal,
    };
  }

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  const auto expected_last_cost =
      expected_unit_path_cost(world, requests.back());
  TESS_PATH_DIAG_DECL(scratch);
  tess::PathResult result;
  std::uint64_t total_cost = 0;
  std::uint64_t total_expanded = 0;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    total_cost = 0;
    total_expanded = 0;
    TESS_PATH_DIAG_RUN(for (const auto request : requests) {
      result = tess::astar_path<PathScaleWorld, PassableTag>(world, request,
                                                             scratch);
      total_cost += result.cost;
      total_expanded += result.expanded_nodes;
    });
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_expanded);
  }
  check_found_path(world, result, requests.back(), expected_last_cost);
  record_path_batch_counters<PathScaleWorld>(state, requests, total_cost,
                                             total_expanded);
  TESS_PATH_DIAG_RECORD(state);
}

void BM_path_distance_field_batch_100_multigoal_room_portals_512x512(
    benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);
  carve_room_portals(world);

  constexpr auto goals = std::array{
      tess::Coord3{510, 510, 0}, tess::Coord3{510, 447, 0},
      tess::Coord3{447, 510, 0}, tess::Coord3{383, 510, 0},
      tess::Coord3{510, 383, 0}, tess::Coord3{447, 447, 0},
      tess::Coord3{383, 447, 0}, tess::Coord3{447, 383, 0},
  };
  std::array<tess::PathRequest, 100> requests{};
  for (std::size_t i = 0; i < requests.size(); ++i) {
    const auto offset = static_cast<std::int64_t>(i);
    const auto start = tess::Coord3{1 + offset % 16, 1 + offset / 16, 0};
    const auto goal = goals[i % goals.size()];
    world.template field<PassableTag>(start) = 1;
    world.template field<PassableTag>(goal) = 1;
    requests[i] = tess::PathRequest{start, goal};
  }

  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  auto last_request = requests.front();
  for (const auto goal : goals) {
    for (const auto request : requests) {
      if (request.goal == goal) {
        last_request = request;
      }
    }
  }
  const auto expected_last_cost = expected_unit_path_cost(world, last_request);
  TESS_PATH_DIAG_DECL(scratch);
  tess::DistanceFieldResult field;
  tess::PathResult result;
  std::uint64_t total_cost = 0;
  std::uint64_t total_expanded = 0;
  std::uint64_t total_field_expanded = 0;
  std::uint64_t total_field_reached = 0;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    total_cost = 0;
    total_expanded = 0;
    total_field_expanded = 0;
    total_field_reached = 0;
    TESS_PATH_DIAG_RUN(for (const auto goal : goals) {
      field = tess::build_distance_field<PathScaleWorld, PassableTag>(
          world, goal, scratch);
      total_field_expanded += field.expanded_nodes;
      total_field_reached += field.reached_nodes;
      for (const auto request : requests) {
        if (request.goal != goal) {
          continue;
        }
        result = tess::distance_field_path<PathScaleWorld, PassableTag>(
            world, request.start, request.goal, scratch);
        total_cost += result.cost;
        total_expanded += result.expanded_nodes;
      }
    });
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_expanded);
    benchmark::DoNotOptimize(total_field_expanded);
  }
  bench_check(field.status == tess::PathStatus::Found,
              "distance field build failed");
  check_found_path(world, result, last_request, expected_last_cost);
  record_path_batch_counters<PathScaleWorld>(state, requests, total_cost,
                                             total_expanded);
  state.counters["field_expanded_nodes"] =
      static_cast<double>(total_field_expanded);
  state.counters["field_reached_nodes"] =
      static_cast<double>(total_field_reached);
  TESS_PATH_DIAG_RECORD(state);
}

void BM_path_astar_batch_100_mixed_repeated_room_portals_512x512(
    benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);
  carve_room_portals(world);

  std::array<tess::PathRequest, 100> requests{};
  for (std::size_t i = 0; i < requests.size(); ++i) {
    const auto lane = static_cast<std::int64_t>(i % 10);
    if (i % 3 == 0) {
      const auto start = tess::Coord3{1 + lane, 1, 0};
      const auto goal = tess::Coord3{128 + lane, 128, 0};
      world.template field<PassableTag>(start) = 1;
      world.template field<PassableTag>(goal) = 1;
      requests[i] = tess::PathRequest{start, goal};
    } else if (i % 3 == 1) {
      const auto start = tess::Coord3{1 + lane, 64, 0};
      const auto goal = tess::Coord3{320 + lane, 320, 0};
      world.template field<PassableTag>(start) = 1;
      world.template field<PassableTag>(goal) = 1;
      requests[i] = tess::PathRequest{start, goal};
    } else {
      const auto start = tess::Coord3{1 + lane, 96, 0};
      const auto goal = tess::Coord3{510, 510, 0};
      world.template field<PassableTag>(start) = 1;
      world.template field<PassableTag>(goal) = 1;
      requests[i] = tess::PathRequest{start, goal};
    }
  }

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  const auto expected_last_cost =
      expected_unit_path_cost(world, requests.back());
  TESS_PATH_DIAG_DECL(scratch);
  tess::PathResult result;
  std::uint64_t total_cost = 0;
  std::uint64_t total_expanded = 0;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    total_cost = 0;
    total_expanded = 0;
    TESS_PATH_DIAG_RUN(for (const auto request : requests) {
      result = tess::astar_path<PathScaleWorld, PassableTag>(world, request,
                                                             scratch);
      total_cost += result.cost;
      total_expanded += result.expanded_nodes;
    });
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_expanded);
  }
  check_found_path(world, result, requests.back(), expected_last_cost);
  record_path_batch_counters<PathScaleWorld>(state, requests, total_cost,
                                             total_expanded);
  TESS_PATH_DIAG_RECORD(state);
}

void BM_path_cached_astar_batch_100_mixed_repeated_room_portals_512x512(
    benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);
  carve_room_portals(world);

  std::array<tess::PathRequest, 100> requests{};
  for (std::size_t i = 0; i < requests.size(); ++i) {
    const auto lane = static_cast<std::int64_t>(i % 10);
    if (i % 3 == 0) {
      const auto start = tess::Coord3{1 + lane, 1, 0};
      const auto goal = tess::Coord3{128 + lane, 128, 0};
      world.template field<PassableTag>(start) = 1;
      world.template field<PassableTag>(goal) = 1;
      requests[i] = tess::PathRequest{start, goal};
    } else if (i % 3 == 1) {
      const auto start = tess::Coord3{1 + lane, 64, 0};
      const auto goal = tess::Coord3{320 + lane, 320, 0};
      world.template field<PassableTag>(start) = 1;
      world.template field<PassableTag>(goal) = 1;
      requests[i] = tess::PathRequest{start, goal};
    } else {
      const auto start = tess::Coord3{1 + lane, 96, 0};
      const auto goal = tess::Coord3{510, 510, 0};
      world.template field<PassableTag>(start) = 1;
      world.template field<PassableTag>(goal) = 1;
      requests[i] = tess::PathRequest{start, goal};
    }
  }

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  tess::RouteCacheScratch cache;
  cache.reserve_routes(requests.size());
  cache.reserve_path_nodes(path_node_count<PathScaleShape>());
  TESS_PATH_DIAG_DECL(scratch);
  tess::PathResult result;
  tess::RouteCacheStats cache_stats;
  std::uint64_t total_cost = 0;
  std::uint64_t total_expanded = 0;

  for (auto _ : state) {
    cache.clear();
    TESS_PATH_DIAG_RESET();
    total_cost = 0;
    total_expanded = 0;
    TESS_PATH_DIAG_RUN(for (const auto request : requests) {
      result = tess::cached_astar_path<PathScaleWorld, PassableTag>(
          world, request, scratch, cache);
      total_cost += result.cost;
      total_expanded += result.expanded_nodes;
    });
    cache_stats = cache.stats();
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_expanded);
  }
  check_found_path(world, result, requests.back());
  record_path_batch_counters<PathScaleWorld>(state, requests, total_cost,
                                             total_expanded);
  record_route_cache_counters(state, cache_stats);
  TESS_PATH_DIAG_RECORD(state);
}

void BM_path_astar_batch_100_suffix_open_512x512(benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);

  std::array<tess::PathRequest, 100> requests{};
  const auto goal = tess::Coord3{511, 0, 0};
  for (std::size_t i = 0; i < requests.size(); ++i) {
    const auto offset = static_cast<std::int64_t>(i);
    requests[i] = tess::PathRequest{tess::Coord3{offset, 0, 0}, goal};
  }

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  const auto expected_last_cost =
      expected_unit_path_cost(world, requests.back());
  TESS_PATH_DIAG_DECL(scratch);
  tess::PathResult result;
  std::uint64_t total_cost = 0;
  std::uint64_t total_expanded = 0;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    total_cost = 0;
    total_expanded = 0;
    TESS_PATH_DIAG_RUN(for (const auto request : requests) {
      result = tess::astar_path<PathScaleWorld, PassableTag>(world, request,
                                                             scratch);
      total_cost += result.cost;
      total_expanded += result.expanded_nodes;
    });
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_expanded);
  }
  check_found_path(world, result, requests.back(), expected_last_cost);
  record_path_batch_counters<PathScaleWorld>(state, requests, total_cost,
                                             total_expanded);
  TESS_PATH_DIAG_RECORD(state);
}

void BM_path_cached_astar_batch_100_suffix_open_512x512(
    benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);

  std::array<tess::PathRequest, 100> requests{};
  const auto goal = tess::Coord3{511, 0, 0};
  for (std::size_t i = 0; i < requests.size(); ++i) {
    const auto offset = static_cast<std::int64_t>(i);
    requests[i] = tess::PathRequest{tess::Coord3{offset, 0, 0}, goal};
  }

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  tess::RouteCacheScratch cache;
  cache.reserve_routes(requests.size());
  cache.reserve_path_nodes(path_node_count<PathScaleShape>());
  TESS_PATH_DIAG_DECL(scratch);
  tess::PathResult result;
  tess::RouteCacheStats cache_stats;
  std::uint64_t total_cost = 0;
  std::uint64_t total_expanded = 0;

  for (auto _ : state) {
    cache.clear();
    TESS_PATH_DIAG_RESET();
    total_cost = 0;
    total_expanded = 0;
    TESS_PATH_DIAG_RUN(for (const auto request : requests) {
      result = tess::cached_astar_path<PathScaleWorld, PassableTag>(
          world, request, scratch, cache);
      total_cost += result.cost;
      total_expanded += result.expanded_nodes;
    });
    cache_stats = cache.stats();
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_expanded);
  }
  check_found_path(world, result, requests.back());
  record_path_batch_counters<PathScaleWorld>(state, requests, total_cost,
                                             total_expanded);
  record_route_cache_counters(state, cache_stats);
  TESS_PATH_DIAG_RECORD(state);
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
  const auto expected_last_cost =
      expected_unit_path_cost(world, requests.back());
  TESS_PATH_DIAG_DECL(scratch);
  tess::PathResult result;
  std::uint64_t total_cost = 0;
  std::uint64_t total_expanded = 0;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    total_cost = 0;
    total_expanded = 0;
    TESS_PATH_DIAG_RUN(for (const auto request : requests) {
      result = tess::astar_path<PathScaleWorld, PassableTag>(world, request,
                                                             scratch);
      total_cost += result.cost;
      total_expanded += result.expanded_nodes;
    });
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_expanded);
  }
  check_found_path(world, result, requests.back(), expected_last_cost);
  record_path_batch_counters<PathScaleWorld>(state, requests, total_cost,
                                             total_expanded);
  TESS_PATH_DIAG_RECORD(state);
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
  const auto expected_last_cost =
      expected_unit_path_cost(world, requests.back());
  TESS_PATH_DIAG_DECL(scratch);
  tess::PathResult result;
  std::uint64_t total_cost = 0;
  std::uint64_t total_expanded = 0;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    total_cost = 0;
    total_expanded = 0;
    TESS_PATH_DIAG_RUN(for (const auto request : requests) {
      result = tess::astar_path<PathScaleWorld, PassableTag>(world, request,
                                                             scratch);
      total_cost += result.cost;
      total_expanded += result.expanded_nodes;
    });
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_expanded);
  }
  check_found_path(world, result, requests.back(), expected_last_cost);
  record_path_batch_counters<PathScaleWorld>(state, requests, total_cost,
                                             total_expanded);
  TESS_PATH_DIAG_RECORD(state);
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
BENCHMARK(BM_world_dirty_chunks_iteration_16k)
    ->Name("storage/world_dirty_chunks_iteration_16k");
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
BENCHMARK(BM_queued_execute_resident_update_with_results)
    ->Name("queued/execute_resident_update_with_results");
BENCHMARK(BM_path_astar_open_2d)->Name("path/astar_open_2d");
BENCHMARK(BM_path_astar_open_2d_64x64)->Name("path/astar_open_2d_64x64");
BENCHMARK(BM_path_astar_open_2d_512x512)->Name("path/astar_open_2d_512x512");
BENCHMARK(BM_path_astar_open_2d_1024x1024)
    ->Name("path/astar_open_2d_1024x1024");
BENCHMARK(BM_path_weighted_astar_open_512x512)
    ->Name("path/weighted_astar_open_512x512");
BENCHMARK(BM_path_weighted_astar_axis_detour_512x512)
    ->Name("path/weighted_astar_axis_detour_512x512");
BENCHMARK(BM_path_weighted_astar_sparse_blockers_512x512)
    ->Name("path/weighted_astar_sparse_blockers_512x512");
BENCHMARK(BM_path_weighted_astar_room_portals_512x512)
    ->Name("path/weighted_astar_room_portals_512x512");
BENCHMARK(BM_path_weighted_astar_batch_100_mixed_512x512)
    ->Name("path/weighted_astar_batch_100_mixed_512x512");
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
BENCHMARK(BM_path_distance_field_batch_100_shared_room_portals_512x512)
    ->Name("path/distance_field_batch_100_shared_room_portals_512x512");
BENCHMARK(BM_path_astar_batch_100_shared_sparse_512x512)
    ->Name("path/astar_batch_100_shared_sparse_512x512");
BENCHMARK(BM_path_distance_field_batch_100_shared_sparse_512x512)
    ->Name("path/distance_field_batch_100_shared_sparse_512x512");
BENCHMARK(BM_path_astar_batch_100_multigoal_room_portals_512x512)
    ->Name("path/astar_batch_100_multigoal_room_portals_512x512");
BENCHMARK(BM_path_distance_field_batch_100_multigoal_room_portals_512x512)
    ->Name("path/distance_field_batch_100_multigoal_room_portals_512x512");
BENCHMARK(BM_path_astar_batch_100_mixed_repeated_room_portals_512x512)
    ->Name("path/astar_batch_100_mixed_repeated_room_portals_512x512");
BENCHMARK(BM_path_cached_astar_batch_100_mixed_repeated_room_portals_512x512)
    ->Name("path/cached_astar_batch_100_mixed_repeated_room_portals_512x512");
BENCHMARK(BM_path_astar_batch_100_suffix_open_512x512)
    ->Name("path/astar_batch_100_suffix_open_512x512");
BENCHMARK(BM_path_cached_astar_batch_100_suffix_open_512x512)
    ->Name("path/cached_astar_batch_100_suffix_open_512x512");

}  // namespace
