#include <benchmark/benchmark.h>
#include <tess/tess.h>

#include <cstddef>

#include "parallel_phase_support.h"

// Parallel phase-execution benchmarks for the pre-1.0 concurrency stream.
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
//
// The workload bodies live in parallel_phase_support.h, shared with the
// ungated lab/thread_scaling sweep so the two families cannot drift.
// This family's world stays at 256 chunks: it is threshold-gated, and its
// job is to keep dispatch overhead visible, not to scale.

namespace {

using ParallelTraits =
    tess_bench::PhaseWorldTraits<tess::Extent3{1024, 1024, 1},
                                 tess::Extent3{64, 64, 1}>;

constexpr auto kChunkCount = ParallelTraits::chunk_count;

void BM_parallel_chunk_fill_serial(benchmark::State& state) {
  const tess::SerialPhaseExecutor executor;
  tess_bench::run_chunk_fill<ParallelTraits, tess_bench::WarmUp::kNo>(
      state, executor, 1.0);
}

void BM_parallel_chunk_fill_scoped_threads_w4(benchmark::State& state) {
  const tess::ScopedThreadPhaseExecutor executor{4};
  tess_bench::run_chunk_fill<ParallelTraits, tess_bench::WarmUp::kNo>(
      state, executor, 4.0);
}

void BM_parallel_chunk_fill_pool_w2(benchmark::State& state) {
  const tess::WorkerPoolPhaseExecutor executor{2};
  executor.reserve_operations(kChunkCount);
  tess_bench::run_chunk_fill<ParallelTraits, tess_bench::WarmUp::kNo>(
      state, executor, 2.0);
}

void BM_parallel_chunk_fill_pool_w4(benchmark::State& state) {
  const tess::WorkerPoolPhaseExecutor executor{4};
  executor.reserve_operations(kChunkCount);
  tess_bench::run_chunk_fill<ParallelTraits, tess_bench::WarmUp::kNo>(
      state, executor, 4.0);
}

void BM_parallel_chunk_compute_serial(benchmark::State& state) {
  const tess::SerialPhaseExecutor executor;
  tess_bench::run_chunk_compute<ParallelTraits, tess_bench::WarmUp::kNo>(
      state, executor, 1.0);
}

void BM_parallel_chunk_compute_pool_w2(benchmark::State& state) {
  const tess::WorkerPoolPhaseExecutor executor{2};
  executor.reserve_operations(kChunkCount);
  tess_bench::run_chunk_compute<ParallelTraits, tess_bench::WarmUp::kNo>(
      state, executor, 2.0);
}

void BM_parallel_chunk_compute_pool_w4(benchmark::State& state) {
  const tess::WorkerPoolPhaseExecutor executor{4};
  executor.reserve_operations(kChunkCount);
  tess_bench::run_chunk_compute<ParallelTraits, tess_bench::WarmUp::kNo>(
      state, executor, 4.0);
}

void BM_parallel_tile_touch_serial(benchmark::State& state) {
  const tess::SerialPhaseExecutor executor;
  tess_bench::run_tile_touch<ParallelTraits, tess_bench::WarmUp::kNo>(
      state, executor, 1.0);
}

void BM_parallel_tile_touch_scoped_threads_w4(benchmark::State& state) {
  const tess::ScopedThreadPhaseExecutor executor{4};
  tess_bench::run_tile_touch<ParallelTraits, tess_bench::WarmUp::kNo>(
      state, executor, 4.0);
}

void BM_parallel_tile_touch_pool_w4(benchmark::State& state) {
  const tess::WorkerPoolPhaseExecutor executor{4};
  executor.reserve_operations(kChunkCount);
  tess_bench::run_tile_touch<ParallelTraits, tess_bench::WarmUp::kNo>(
      state, executor, 4.0);
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
