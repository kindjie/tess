#include <benchmark/benchmark.h>
#include <tess/tess.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <vector>

// Parallel phase-execution benchmarks for the v1 concurrency stream.
//
// These compare the serial baseline against the scoped-thread prototype and
// the persistent worker-pool prototype on identical partitioned queued
// workloads: one UniquePerChunk operation per chunk, planned into a single
// parallel phase. Per docs/tdd/tdd_addendum_concurrent_tile_world.md,
// threshold gating for the parallel cases waits for CI baseline data
// (shared-runner scheduling makes parallel dispatch times noisy); the serial
// cases are the gate candidates once baselines accumulate. Worker counts are
// fixed so results stay comparable across runs, and each benchmark reports
// worker and chunk counts as counters.

namespace {

void bench_check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "tess_bench correctness check failed: %s\n", message);
    std::abort();
  }
}

struct ParallelTerrainTag {};

using ParallelShape =
    tess::Shape<tess::Extent3{1024, 1024, 1}, tess::Extent3{64, 64, 1}>;
using ParallelSchema =
    tess::FieldSchema<tess::Field<ParallelTerrainTag, std::uint16_t>>;
using ParallelWorld = tess::AlwaysResidentWorld<ParallelShape, ParallelSchema>;

constexpr std::uint32_t kDirtyTerrain = 1u << 0u;
constexpr auto kChunkCount =
    static_cast<std::size_t>(ParallelWorld::chunk_count);

[[nodiscard]] auto chunk_keys() -> std::vector<tess::ChunkKey> {
  std::vector<tess::ChunkKey> keys;
  keys.reserve(kChunkCount);
  for (std::uint64_t key = 0; key < ParallelWorld::chunk_count; ++key) {
    keys.push_back(tess::ChunkKey{key});
  }
  return keys;
}

void enqueue_per_chunk_updates(tess::FrameOps& ops,
                               std::span<const tess::ChunkKey> keys) {
  for (std::size_t i = 0; i < keys.size(); ++i) {
    (void)ops.update_field(
        tess::DomainDesc::explicit_chunks(keys.subspan(i, 1)),
        tess::FieldAccessDesc{0, kDirtyTerrain, kDirtyTerrain},
        tess::WritePolicy::UniquePerChunk);
  }
}

// Runs one-parallel-phase partitioned execution of `fn` over every chunk
// with the given executor and reports worker/chunk counters.
template <typename Executor, typename Fn>
void run_parallel_phase(benchmark::State& state, const Executor& executor,
                        double workers, ParallelWorld& world, Fn&& fn) {
  const auto keys = chunk_keys();
  tess::FrameOps ops;
  enqueue_per_chunk_updates(ops, keys);
  const auto report = tess::plan_operations(world, ops);
  bench_check(report.ok(), "parallel bench plan failed");
  const auto phase_plan = tess::plan_parallel_execution_phases(report.plan());
  bench_check(phase_plan.ok(), "parallel bench phase planning failed");
  bench_check(phase_plan.phases().size() == 1,
              "disjoint per-chunk updates must plan to one parallel phase");
  const auto phase = phase_plan.phases()[0];
  bench_check(phase.operation_count == kChunkCount,
              "every chunk must plan to one operation");

  tess::PlannedPhaseExecutionScratch scratch;
  scratch.reserve_operations(kChunkCount);
  scratch.reserve_dirty_records_per_operation(1);
  scratch.reserve_merged_dirty_records(kChunkCount);

  std::uint64_t last_chunk_count = 0;
  for (auto _ : state) {
    const auto result = tess::execute_phase_partitioned_dirty_with<
        tess::WritePolicy::UniquePerChunk>(executor, world, report.plan(),
                                           phase, scratch, fn);
    last_chunk_count = result.chunk_count;
    benchmark::DoNotOptimize(last_chunk_count);
  }

  state.counters["workers"] = workers;
  state.counters["chunks"] = static_cast<double>(kChunkCount);
  bench_check(last_chunk_count == kChunkCount,
              "parallel phase did not visit every chunk");
}

// Every tile of every chunk is written each iteration, so per-operation
// work is a full 64x64 span fill: enough work per chunk for parallel
// dispatch to amortize, small enough that dispatch overhead stays visible.
template <typename Executor>
void run_chunk_fill(benchmark::State& state, const Executor& executor,
                    double workers) {
  ParallelWorld world;
  run_parallel_phase(state, executor, workers, world, [](auto view) {
    auto terrain = view.template field_span<ParallelTerrainTag>();
    for (auto& tile : terrain) {
      tile = static_cast<std::uint16_t>(view.key().value + 1);
    }
  });

  bool filled = true;
  for (auto& page : world.chunks()) {
    const auto expected =
        static_cast<std::uint16_t>(page.chunk_key().value + 1);
    for (const auto tile : page.field_span<ParallelTerrainTag>()) {
      filled = filled && tile == expected;
    }
  }
  bench_check(filled, "parallel chunk fill missed tiles");
}

// One-tile writes per chunk keep per-operation work near zero, so this
// family measures per-backend dispatch overhead amplification.
template <typename Executor>
void run_tile_touch(benchmark::State& state, const Executor& executor,
                    double workers) {
  ParallelWorld world;
  run_parallel_phase(state, executor, workers, world, [](auto view) {
    auto terrain = view.template field_span<ParallelTerrainTag>();
    terrain[0] = static_cast<std::uint16_t>(view.key().value);
  });
}

// A serial per-tile dependency chain makes each operation compute-bound
// (tens of microseconds), so this family shows how backends scale when
// per-operation work actually amortizes dispatch.
template <typename Executor>
void run_chunk_compute(benchmark::State& state, const Executor& executor,
                       double workers) {
  ParallelWorld world;
  run_parallel_phase(state, executor, workers, world, [](auto view) {
    auto terrain = view.template field_span<ParallelTerrainTag>();
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
    for (const auto tile : page.field_span<ParallelTerrainTag>()) {
      nonzero = nonzero || tile != 0;
    }
  }
  bench_check(nonzero, "parallel chunk compute produced no output");
}

void BM_parallel_chunk_fill_serial(benchmark::State& state) {
  const tess::SerialPhaseExecutor executor;
  run_chunk_fill(state, executor, 1.0);
}

void BM_parallel_chunk_fill_scoped_threads_w4(benchmark::State& state) {
  const tess::ScopedThreadPhaseExecutor executor{4};
  run_chunk_fill(state, executor, 4.0);
}

void BM_parallel_chunk_fill_pool_w2(benchmark::State& state) {
  const tess::WorkerPoolPhaseExecutor executor{2};
  executor.reserve_operations(kChunkCount);
  run_chunk_fill(state, executor, 2.0);
}

void BM_parallel_chunk_fill_pool_w4(benchmark::State& state) {
  const tess::WorkerPoolPhaseExecutor executor{4};
  executor.reserve_operations(kChunkCount);
  run_chunk_fill(state, executor, 4.0);
}

void BM_parallel_chunk_compute_serial(benchmark::State& state) {
  const tess::SerialPhaseExecutor executor;
  run_chunk_compute(state, executor, 1.0);
}

void BM_parallel_chunk_compute_pool_w2(benchmark::State& state) {
  const tess::WorkerPoolPhaseExecutor executor{2};
  executor.reserve_operations(kChunkCount);
  run_chunk_compute(state, executor, 2.0);
}

void BM_parallel_chunk_compute_pool_w4(benchmark::State& state) {
  const tess::WorkerPoolPhaseExecutor executor{4};
  executor.reserve_operations(kChunkCount);
  run_chunk_compute(state, executor, 4.0);
}

void BM_parallel_tile_touch_serial(benchmark::State& state) {
  const tess::SerialPhaseExecutor executor;
  run_tile_touch(state, executor, 1.0);
}

void BM_parallel_tile_touch_scoped_threads_w4(benchmark::State& state) {
  const tess::ScopedThreadPhaseExecutor executor{4};
  run_tile_touch(state, executor, 4.0);
}

void BM_parallel_tile_touch_pool_w4(benchmark::State& state) {
  const tess::WorkerPoolPhaseExecutor executor{4};
  executor.reserve_operations(kChunkCount);
  run_tile_touch(state, executor, 4.0);
}

BENCHMARK(BM_parallel_chunk_fill_serial)->Name("parallel/chunk_fill_serial");
BENCHMARK(BM_parallel_chunk_fill_scoped_threads_w4)
    ->Name("parallel/chunk_fill_scoped_threads_w4");
BENCHMARK(BM_parallel_chunk_fill_pool_w2)->Name("parallel/chunk_fill_pool_w2");
BENCHMARK(BM_parallel_chunk_fill_pool_w4)->Name("parallel/chunk_fill_pool_w4");
BENCHMARK(BM_parallel_chunk_compute_serial)
    ->Name("parallel/chunk_compute_serial");
BENCHMARK(BM_parallel_chunk_compute_pool_w2)
    ->Name("parallel/chunk_compute_pool_w2");
BENCHMARK(BM_parallel_chunk_compute_pool_w4)
    ->Name("parallel/chunk_compute_pool_w4");
BENCHMARK(BM_parallel_tile_touch_serial)->Name("parallel/tile_touch_serial");
BENCHMARK(BM_parallel_tile_touch_scoped_threads_w4)
    ->Name("parallel/tile_touch_scoped_threads_w4");
BENCHMARK(BM_parallel_tile_touch_pool_w4)->Name("parallel/tile_touch_pool_w4");

}  // namespace
