#ifndef TESS_BENCH_PARALLEL_PHASE_SUPPORT_H
#define TESS_BENCH_PARALLEL_PHASE_SUPPORT_H

#include <benchmark/benchmark.h>
#include <tess/tess.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <vector>

// Shared scaffolding for partitioned parallel-phase benchmarks.
//
// Two families use it and must not drift apart: the gated `parallel/`
// family (tess_parallel_bench.cc, fixed worker counts on a small world)
// and the ungated `lab/thread_scaling/` sweep
// (tess_thread_scaling_bench.cc, many worker counts on a large world).
// The workload bodies live here so a change to what "chunk_fill" means
// cannot land in one family and not the other.
//
// Everything is templated on the world shape because the two families
// deliberately differ there: dispatch overhead has to stay visible at 256
// chunks, while a 190-worker sweep needs enough chunks that per-worker
// quantization does not dominate.

namespace tess_bench {

inline void bench_check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "tess_bench correctness check failed: %s\n", message);
    std::abort();
  }
}

// Field layout shared by every workload: one 16-bit terrain field, so
// per-chunk cost is set by how many tiles a workload touches rather than
// by the schema. The tag is nested so each shape gets a distinct field
// type and the two families cannot alias each other's storage.
template <tess::Extent3 WorldExtent, tess::Extent3 ChunkExtent>
struct PhaseWorldTraits {
  struct TerrainTag {};

  using Shape = tess::Shape<WorldExtent, ChunkExtent>;
  using Schema = tess::FieldSchema<tess::Field<TerrainTag, std::uint16_t>>;
  using World = tess::AlwaysResidentWorld<Shape, Schema>;

  static constexpr std::size_t chunk_count =
      static_cast<std::size_t>(World::chunk_count);
  static constexpr std::size_t chunk_tile_count =
      static_cast<std::size_t>(ChunkExtent.x * ChunkExtent.y * ChunkExtent.z);
};

inline constexpr std::uint32_t kDirtyTerrain = 1u << 0u;

template <typename World>
[[nodiscard]] auto chunk_keys() -> std::vector<tess::ChunkKey> {
  constexpr auto count = static_cast<std::size_t>(World::chunk_count);
  std::vector<tess::ChunkKey> keys;
  keys.reserve(count);
  for (std::uint64_t key = 0; key < World::chunk_count; ++key) {
    keys.push_back(tess::ChunkKey{key});
  }
  return keys;
}

inline void enqueue_per_chunk_updates(tess::FrameOps& ops,
                                      std::span<const tess::ChunkKey> keys) {
  for (std::size_t i = 0; i < keys.size(); ++i) {
    (void)ops.update_field(
        tess::DomainDesc::explicit_chunks(keys.subspan(i, 1)),
        tess::FieldAccessDesc{0, kDirtyTerrain, kDirtyTerrain},
        tess::WritePolicy::UniquePerChunk);
  }
}

// Whether to execute one untimed phase before the measured loop.
//
// NOT for page faults. `World()` zero-initializes every chunk page in
// its constructor (storage/world.h), so first touch already happens
// before the loop; an A/B on a dev box found no median shift, which is
// what put that theory to rest.
//
// What the first measured iteration does still pay is a cold cache and
// cold pool threads. The sweep's world is 32 MiB against 210 MiB of L3
// on the target machine, so iteration one reads from DRAM and later
// iterations from L3, and the pool's workers have not yet been scheduled
// onto their cores. That is a bias rather than noise, and it lands
// unequally: amortized across a thousand iterations it vanishes, across
// eight it does not, and the sweep's iteration counts span two orders of
// magnitude (measured 55 to 1167 within one workload).
//
// The two families choose differently, and deliberately. The sweep warms
// up, because it publishes a curve whose points must be comparable to
// each other across a wide range of iteration counts. The gated
// `parallel/` family does not, because its value is comparability with
// its own recorded history: warming it up would step every one of its
// baselines and its trend series at once. That bias exists there too,
// smaller at 256 chunks, and is left as an open item rather than fixed
// silently as a side effect of the sweep.
enum class WarmUp { kNo, kYes };

// Runs one-parallel-phase partitioned execution of `fn` over every chunk
// with the given executor and reports worker/chunk counters.
//
// Counters are written after the timed loop, never inside it: a counter
// update in the loop body would be measured as part of the workload.
template <WarmUp Warm, typename World, typename Executor, typename Fn>
void run_parallel_phase(benchmark::State& state, const Executor& executor,
                        double workers, World& world, Fn&& fn) {
  constexpr auto count = static_cast<std::size_t>(World::chunk_count);

  const auto keys = chunk_keys<World>();
  tess::FrameOps ops;
  enqueue_per_chunk_updates(ops, keys);
  const auto report = tess::plan_operations(world, ops);
  bench_check(report.ok(), "parallel bench plan failed");
  const auto phase_plan = tess::plan_parallel_execution_phases(report.plan());
  bench_check(phase_plan.ok(), "parallel bench phase planning failed");
  bench_check(phase_plan.phases().size() == 1,
              "disjoint per-chunk updates must plan to one parallel phase");
  const auto phase = phase_plan.phases()[0];
  bench_check(phase.operation_count() == count,
              "every chunk must plan to one operation");

  tess::PlannedPhaseExecutionScratch scratch;
  scratch.reserve_operations(count);
  scratch.reserve_dirty_records_per_operation(1);
  scratch.reserve_merged_dirty_records(count);

  // Outside the loop, so its cost is not measured. It also warms the
  // pool's worker threads, which otherwise start cold on iteration one.
  if constexpr (Warm == WarmUp::kYes) {
    const auto warmed = tess::execute_phase_partitioned_dirty_with<
        tess::WritePolicy::UniquePerChunk>(executor, world, report.plan(),
                                           phase, scratch, fn);
    bench_check(warmed.chunk_count == count,
                "warm-up phase did not visit every chunk");
  }

  std::uint64_t last_chunk_count = 0;
  for (auto _ : state) {
    const auto result = tess::execute_phase_partitioned_dirty_with<
        tess::WritePolicy::UniquePerChunk>(executor, world, report.plan(),
                                           phase, scratch, fn);
    last_chunk_count = result.chunk_count;
    benchmark::DoNotOptimize(last_chunk_count);
  }

  state.counters["workers"] = workers;
  state.counters["chunks"] = static_cast<double>(count);
  bench_check(last_chunk_count == count,
              "parallel phase did not visit every chunk");
}

// Every tile of every chunk is written each iteration, so per-operation
// work is a full span fill: enough work per chunk for parallel dispatch
// to amortize, small enough that dispatch overhead stays visible.
template <typename Traits, WarmUp Warm, typename Executor>
void run_chunk_fill(benchmark::State& state, const Executor& executor,
                    double workers) {
  using TerrainTag = typename Traits::TerrainTag;
  typename Traits::World world;
  run_parallel_phase<Warm>(state, executor, workers, world, [](auto view) {
    auto terrain = view.template field_span<TerrainTag>();
    for (auto& tile : terrain) {
      tile = static_cast<std::uint16_t>(view.key().value + 1);
    }
  });

  bool filled = true;
  for (auto& page : world.chunks()) {
    const auto expected =
        static_cast<std::uint16_t>(page.chunk_key().value + 1);
    for (const auto tile : page.template field_span<TerrainTag>()) {
      filled = filled && tile == expected;
    }
  }
  bench_check(filled, "parallel chunk fill missed tiles");
}

// A prefix fill of `Tiles` tiles per chunk. The full fill and the
// single-tile touch bracket the serial/parallel crossover two orders of
// magnitude apart; these intermediate points narrow that bracket, which
// is the only adopter-facing result the sweep produces.
template <typename Traits, std::size_t Tiles, WarmUp Warm, typename Executor>
void run_partial_fill(benchmark::State& state, const Executor& executor,
                      double workers) {
  using TerrainTag = typename Traits::TerrainTag;
  // Checked here rather than in the callback: anything inside the phase
  // body runs inside the timed loop and would be measured as workload.
  static_assert(Tiles <= Traits::chunk_tile_count,
                "partial fill is wider than the chunk");
  typename Traits::World world;
  run_parallel_phase<Warm>(state, executor, workers, world, [](auto view) {
    auto terrain = view.template field_span<TerrainTag>();
    for (std::size_t i = 0; i < Tiles; ++i) {
      terrain[i] = static_cast<std::uint16_t>(view.key().value + 1);
    }
  });

  bool filled = true;
  for (auto& page : world.chunks()) {
    const auto expected =
        static_cast<std::uint16_t>(page.chunk_key().value + 1);
    const auto terrain = page.template field_span<TerrainTag>();
    for (std::size_t i = 0; i < Tiles; ++i) {
      filled = filled && terrain[i] == expected;
    }
  }
  bench_check(filled, "parallel partial fill missed tiles");
}

// One-tile writes per chunk keep per-operation work near zero, so this
// family measures per-backend dispatch overhead amplification.
template <typename Traits, WarmUp Warm, typename Executor>
void run_tile_touch(benchmark::State& state, const Executor& executor,
                    double workers) {
  using TerrainTag = typename Traits::TerrainTag;
  typename Traits::World world;
  run_parallel_phase<Warm>(state, executor, workers, world, [](auto view) {
    auto terrain = view.template field_span<TerrainTag>();
    terrain[0] = static_cast<std::uint16_t>(view.key().value);
  });
}

// A serial per-tile dependency chain makes each operation compute-bound
// (tens of microseconds), so this family shows how backends scale when
// per-operation work actually amortizes dispatch.
template <typename Traits, WarmUp Warm, typename Executor>
void run_chunk_compute(benchmark::State& state, const Executor& executor,
                       double workers) {
  using TerrainTag = typename Traits::TerrainTag;
  typename Traits::World world;
  run_parallel_phase<Warm>(state, executor, workers, world, [](auto view) {
    auto terrain = view.template field_span<TerrainTag>();
    auto hash = static_cast<std::uint32_t>(view.key().value) * 2654435761u + 1u;
    for (auto& tile : terrain) {
      hash ^= hash << 13u;
      hash ^= hash >> 17u;
      hash ^= hash << 5u;
      tile = static_cast<std::uint16_t>(hash);
    }
  });

  bool nonzero = false;
  for (auto& page : world.chunks()) {
    for (const auto tile : page.template field_span<TerrainTag>()) {
      nonzero = nonzero || tile != 0;
    }
  }
  bench_check(nonzero, "parallel chunk compute produced no output");
}

}  // namespace tess_bench

#endif  // TESS_BENCH_PARALLEL_PHASE_SUPPORT_H
