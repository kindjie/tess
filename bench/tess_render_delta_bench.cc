#include <benchmark/benchmark.h>
#include <tess/tess.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// M11 render_delta bench family: sparse-tile collection scaling with the
// dirty count (not the world size), box-granular emission, entity
// recording with and without coalescing, and full-baseline emission.
// Replay==projection correctness lives in the randomized test suite;
// the bench checks are structural (record counts, consumed dirty state).
namespace {

void render_bench_check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "tess_bench correctness check failed: %s\n", message);
    std::abort();
  }
}

struct TerrainTag {};
struct DecorTag {};

// 512x512 tiles in 8x8 chunks: 4096 chunks of metadata per collect scan.
using DeltaShape =
    tess::Shape<tess::Extent3{512, 512, 1}, tess::Extent3{8, 8, 1}>;
using DeltaSchema = tess::FieldSchema<tess::Field<TerrainTag, std::uint8_t>,
                                      tess::Field<DecorTag, std::uint8_t>>;
using DeltaWorld = tess::AlwaysResidentWorld<DeltaShape, DeltaSchema>;

constexpr std::uint32_t kTerrainBit = 1U << 0U;

void mark_tile(DeltaWorld& world, tess::Coord3 coord) {
  world.mark_dirty(
      tess::chunk_key<DeltaShape>(tess::tile_key<DeltaShape>(coord)),
      kTerrainBit, tess::Box3{coord, tess::Extent3{1, 1, 1}});
}

// K scattered single-tile marks; collection cost tracks the dirty count
// plus the fixed chunk-metadata scan, never the tile count.
void BM_render_delta_collect_sparse(benchmark::State& state) {
  static auto* world = new DeltaWorld();
  tess::DeltaCollector collector;
  collector.reserve(DeltaWorld::chunk_count, 4096, 8);

  constexpr std::int64_t kDirtyTiles = 64;
  std::size_t last_chunks = 0;
  for (auto _ : state) {
    for (std::int64_t i = 0; i < kDirtyTiles; ++i) {
      // Deterministic scatter, one tile per touched chunk.
      mark_tile(*world, tess::Coord3{(i * 61) % 512, (i * 127) % 512, 0});
    }
    tess::collect_tile_deltas(collector, *world, kTerrainBit);
    const auto frame = collector.publish();
    last_chunks = frame.chunks.size();
    benchmark::DoNotOptimize(last_chunks);
  }
  render_bench_check(last_chunks > 0 && last_chunks <= kDirtyTiles,
                     "sparse collect emitted an unexpected chunk count");
}

// One whole-chunk dirty box in each of 64 chunks: the box-granular path
// (threshold below the 64-tile chunk so the box encoding is exercised).
void BM_render_delta_collect_box(benchmark::State& state) {
  static auto* world = new DeltaWorld();
  tess::DeltaCollector collector(tess::DeltaCollectorOptions{16, true});
  collector.reserve(DeltaWorld::chunk_count, 4096, 8);

  std::size_t last_chunks = 0;
  for (auto _ : state) {
    for (std::int64_t chunk = 0; chunk < 64; ++chunk) {
      const auto origin = tess::Coord3{(chunk % 8) * 8, (chunk / 8) * 8, 0};
      world->mark_dirty(
          tess::chunk_key<DeltaShape>(tess::tile_key<DeltaShape>(origin)),
          kTerrainBit, tess::Box3{origin, tess::Extent3{8, 8, 1}});
    }
    tess::collect_tile_deltas(collector, *world, kTerrainBit);
    const auto frame = collector.publish();
    last_chunks = frame.chunks.size();
    benchmark::DoNotOptimize(last_chunks);
  }
  render_bench_check(last_chunks == 64,
                     "box collect did not emit one record per chunk");
  render_bench_check(collector.stats().tile_records == 0,
                     "box collect unexpectedly emitted per-tile records");
}

// 1k entities each committing 8 steps into one published frame.
void run_entity_moves_bench(benchmark::State& state, bool coalesce) {
  tess::DeltaCollector collector(tess::DeltaCollectorOptions{64, coalesce});
  constexpr std::uint64_t kEntities = 1000;
  collector.reserve(8, 16, coalesce ? kEntities : kEntities * 8);

  std::size_t last_records = 0;
  for (auto _ : state) {
    for (std::uint64_t step = 0; step < 8; ++step) {
      collector.begin_tick(step + 1);
      for (std::uint64_t entity = 0; entity < kEntities; ++entity) {
        const auto from = tess::Coord3{static_cast<std::int64_t>(step),
                                       static_cast<std::int64_t>(entity), 0};
        const auto to = tess::Coord3{static_cast<std::int64_t>(step) + 1,
                                     static_cast<std::int64_t>(entity), 0};
        collector.record_move(tess::EntityHandle{entity + 1}, from, to);
      }
    }
    const auto frame = collector.publish();
    last_records = frame.entities.size();
    benchmark::DoNotOptimize(last_records);
  }
  render_bench_check(last_records == (coalesce ? kEntities : kEntities * 8),
                     "entity record count diverged from the coalesce mode");
}

void BM_render_delta_entity_moves_coalesced(benchmark::State& state) {
  run_entity_moves_bench(state, true);
}

void BM_render_delta_entity_moves_per_step(benchmark::State& state) {
  run_entity_moves_bench(state, false);
}

// Full-world baseline over 4096 chunks.
void BM_render_delta_baseline(benchmark::State& state) {
  static auto* world = new DeltaWorld();
  tess::DeltaCollector collector;
  collector.reserve(DeltaWorld::chunk_count, 16, 8);

  std::size_t last_chunks = 0;
  for (auto _ : state) {
    tess::collect_baseline(collector, *world, kTerrainBit);
    const auto frame = collector.publish();
    last_chunks = frame.chunks.size();
    benchmark::DoNotOptimize(last_chunks);
  }
  render_bench_check(last_chunks == DeltaWorld::chunk_count,
                     "baseline did not cover every chunk");
}

#if TESS_DIAGNOSTICS_ENABLED
// Warm-path allocation gate (benchmark-plan.md section 14): a steady
// mark/collect/record/publish cycle must report zero allocations.
void BM_render_delta_collect_alloc_gate(benchmark::State& state) {
  static auto* world = new DeltaWorld();
  tess::DeltaCollector collector;
  collector.reserve(DeltaWorld::chunk_count, 4096, 8);

  // Warm one full cycle.
  mark_tile(*world, tess::Coord3{5, 5, 0});
  collector.begin_tick(1);
  collector.record_move(tess::EntityHandle{1}, tess::Coord3{0, 0, 0},
                        tess::Coord3{1, 0, 0});
  tess::collect_tile_deltas(collector, *world, kTerrainBit);
  (void)collector.publish();

  tess::diagnostics::AllocationCounters counters;
  std::uint64_t tick = 2;
  for (auto _ : state) {
    counters.reset();
    tess::diagnostics::ScopedAllocationCounters scope{counters};
    mark_tile(*world, tess::Coord3{5, 5, 0});
    collector.begin_tick(tick++);
    collector.record_move(tess::EntityHandle{1}, tess::Coord3{0, 0, 0},
                          tess::Coord3{1, 0, 0});
    tess::collect_tile_deltas(collector, *world, kTerrainBit);
    const auto frame = collector.publish();
    auto chunks = frame.chunks.size();
    benchmark::DoNotOptimize(chunks);
  }
  render_bench_check(counters.allocations == 0,
                     "steady-state delta collection allocated");
}
BENCHMARK(BM_render_delta_collect_alloc_gate)
    ->Name("render_delta/collect_alloc_gate");
#endif

BENCHMARK(BM_render_delta_collect_sparse)
    ->Name("render_delta/collect_sparse_64");
BENCHMARK(BM_render_delta_collect_box)->Name("render_delta/collect_box_64");
BENCHMARK(BM_render_delta_entity_moves_coalesced)
    ->Name("render_delta/entity_moves_1k_coalesced");
BENCHMARK(BM_render_delta_entity_moves_per_step)
    ->Name("render_delta/entity_moves_1k_per_step");
BENCHMARK(BM_render_delta_baseline)->Name("render_delta/baseline_4096");

}  // namespace
