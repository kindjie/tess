#include <benchmark/benchmark.h>
#include <tess/tess.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "parallel_phase_support.h"

// Worker-count sweep for the persistent pool executor: how phase execution
// scales from one worker to a full bare-metal socket pair, and where the
// serial executor stops losing.
//
// This lives in its own binary (tess_bench_thread_scaling), not in
// tess_bench, and that is load-bearing. Four call sites run tess_bench with
// no --benchmark_filter -- the weekly coverage job, the paired-base
// comparison, the workload-matrix drift check, and the metal campaign's main
// timing stage. Registering a 190-worker sweep there would build 190-thread
// pools on four-core runners and would run the sweep twice per campaign.
// Negative filters at four sites fail open; a separate binary does not.
// It is also kept out of tess_bench_diagnostics, whose allocation hooks
// distort timings by 12-21% (docs/planning/optimization-log.md).
//
// Nothing here is threshold-gated: the `lab/` prefix marks families the CI
// gate does not score, and executor regression detection stays with the
// gated `parallel/` family. What CI does enforce is that this binary builds
// and that every registration below is classified by the workload matrix.
//
// Results are published per release campaign, never from CI: a four-core
// shared runner cannot measure this.

namespace {

// 4096 chunks, up from the parallel family's 256. At 256 chunks a
// 190-worker pool gives each worker one or two chunks, so the measurement
// would be dominated by that quantization rather than by the executor.
//
// This does not make quantization go away. The pool claims runs of
// `stride = max(1, chunks / (workers * 4))` chunks
// (include/tess/ops/phase_executor.h), so the efficiency ceiling is
// deterministic and scale-invariant -- stride grows with the world -- and
// as a fraction of the workers asked for it sawtooths: 100% at 32 and 64
// workers, 81% at 48 in between, 86% at 190. Analysis must report the
// ceiling alongside the measurement (tools/thread_scaling_report.py), or
// a scheduling artifact reads as a hardware knee.
using SweepTraits = tess_bench::PhaseWorldTraits<tess::Extent3{4096, 4096, 1},
                                                 tess::Extent3{64, 64, 1}>;

constexpr auto kChunkCount = SweepTraits::chunk_count;
static_assert(kChunkCount == 4096, "sweep world sizing changed");

// One untimed phase before the measured loop, so the first measured
// iteration does not pay for a cold cache and unscheduled pool threads.
// The 32 MiB world fits in the target machine's 210 MiB L3, so iteration
// one reads from DRAM and the rest from L3. That bias lands unequally
// here because iteration counts span two orders of magnitude (measured
// 55 to 1167 within tile_touch alone). It is not about page faults --
// World() zero-fills every page in its constructor. The gated parallel/
// family deliberately does not warm up; see WarmUp in
// parallel_phase_support.h.
constexpr auto kWarm = tess_bench::WarmUp::kYes;

// Topology-shaped, not powers of two, for the c3-standard-192-metal target:
// 24 is one NUMA node, 48 one socket, 96 all physical cores, 190 leaves a
// core for the dispatcher. Powers of two miss every one of those boundaries.
// The list is also the expected-point manifest for analysis: the workload
// matrix cannot notice a single missing worker count, because its family
// rule still matches the other ten.
constexpr std::array<std::int64_t, 11> kWorkerCounts{1,  2,  4,  8,  16, 24,
                                                     32, 48, 64, 96, 190};

void worker_counts(benchmark::Benchmark* registration) {
  for (const auto workers : kWorkerCounts) {
    registration->Arg(workers);
  }
}

// Seven points on the work-per-chunk axis. The crossover between "the pool
// helps" and "the pool costs more than it saves" is the one adopter-facing
// result this sweep produces, so the axis is densest at the low end where
// that crossover lives.
//
// The three original workloads bracket it only between 16 ns and 236 ns,
// which is an order of magnitude too loose to state as guidance. A dev-box
// run of the first two partial fills then showed both already sat above the
// crossover -- tile_touch lost at 0.70x while partial_fill_640 already won
// at 2.28x -- so the 64- and 192-tile points were added underneath them.
//
// Per-chunk serial cost, measured on a 4-core dev box; the metal figures
// are higher (tile_touch ~16 ns there) and the crossover moves with them,
// which is the whole reason the low end is sampled four times:
//
//   tile_touch          ~6 ns   (one tile: dispatch overhead only)
//   partial_fill_64    ~10 ns   (1 row)
//   partial_fill_192   ~20 ns   (3 rows)
//   partial_fill_640   ~53 ns   (10 rows)
//   partial_fill_1536 ~125 ns   (24 rows)
//   chunk_fill        ~244 ns   (all 4096 tiles)
//   chunk_compute    ~6887 ns   (serial hash chain over 4096 tiles)
enum class Workload {
  kTileTouch,
  kPartialFill64,
  kPartialFill192,
  kPartialFill640,
  kPartialFill1536,
  kChunkFill,
  kChunkCompute,
};

template <Workload W, typename Executor>
void run_workload(benchmark::State& state, const Executor& executor,
                  double workers) {
  if constexpr (W == Workload::kTileTouch) {
    tess_bench::run_tile_touch<SweepTraits, kWarm>(state, executor, workers);
  } else if constexpr (W == Workload::kPartialFill64) {
    tess_bench::run_partial_fill<SweepTraits, 64, kWarm>(state, executor,
                                                         workers);
  } else if constexpr (W == Workload::kPartialFill192) {
    tess_bench::run_partial_fill<SweepTraits, 192, kWarm>(state, executor,
                                                          workers);
  } else if constexpr (W == Workload::kPartialFill640) {
    tess_bench::run_partial_fill<SweepTraits, 640, kWarm>(state, executor,
                                                          workers);
  } else if constexpr (W == Workload::kPartialFill1536) {
    tess_bench::run_partial_fill<SweepTraits, 1536, kWarm>(state, executor,
                                                           workers);
  } else if constexpr (W == Workload::kChunkFill) {
    tess_bench::run_chunk_fill<SweepTraits, kWarm>(state, executor, workers);
  } else {
    tess_bench::run_chunk_compute<SweepTraits, kWarm>(state, executor, workers);
  }
}

// The serial baseline at the sweep's world size. A one-worker pool is not
// this: it measures the pool's floor, including dispatch and the handoff to
// a worker thread. Without a real serial point at 4096 chunks there is no
// denominator for the serial-versus-pool crossover, and the parallel
// family's serial numbers are from a different world.
template <Workload W>
void BM_thread_scaling_serial(benchmark::State& state) {
  const tess::SerialPhaseExecutor executor;
  run_workload<W>(state, executor, 1.0);
}

template <Workload W>
void BM_thread_scaling_pool(benchmark::State& state) {
  const auto workers = static_cast<std::size_t>(state.range(0));
  const tess::WorkerPoolPhaseExecutor executor{workers};
  executor.reserve_operations(kChunkCount);
  run_workload<W>(state, executor, static_cast<double>(workers));
}

// UseRealTime is required here and deliberately absent from the gated
// parallel family. The dispatching thread blocks in done_cv_.wait for the
// whole phase, so its cpu_time is near zero; without UseRealTime, Google
// Benchmark's iteration-count convergence never triggers and every point
// terminates via the 5x-min_time escape hatch, making iteration counts an
// artifact of the library rather than of the workload. The convention that
// keeps cpu_time as the reported metric exists to protect threshold gating
// (bench/thresholds/parallel.json), and this family has no thresholds.
//
// Names are spelled out rather than assembled, because
// check_workload_matrix.py --bench-sources greps bench/*.cc for whole
// `lab/...` literals; a macro or a concatenation yields a fragment it
// cannot classify. Arg(N) appends `/N` to the pool names, so the literal
// here is their common prefix -- the same reason lab/joint_movement's
// family rule makes its trailing segment optional.
BENCHMARK(BM_thread_scaling_serial<Workload::kTileTouch>)
    ->Name("lab/thread_scaling/tile_touch/serial")
    ->UseRealTime();
BENCHMARK(BM_thread_scaling_pool<Workload::kTileTouch>)
    ->Name("lab/thread_scaling/tile_touch")
    ->Apply(worker_counts)
    ->UseRealTime();

BENCHMARK(BM_thread_scaling_serial<Workload::kPartialFill64>)
    ->Name("lab/thread_scaling/partial_fill_64/serial")
    ->UseRealTime();
BENCHMARK(BM_thread_scaling_pool<Workload::kPartialFill64>)
    ->Name("lab/thread_scaling/partial_fill_64")
    ->Apply(worker_counts)
    ->UseRealTime();

BENCHMARK(BM_thread_scaling_serial<Workload::kPartialFill192>)
    ->Name("lab/thread_scaling/partial_fill_192/serial")
    ->UseRealTime();
BENCHMARK(BM_thread_scaling_pool<Workload::kPartialFill192>)
    ->Name("lab/thread_scaling/partial_fill_192")
    ->Apply(worker_counts)
    ->UseRealTime();

BENCHMARK(BM_thread_scaling_serial<Workload::kPartialFill640>)
    ->Name("lab/thread_scaling/partial_fill_640/serial")
    ->UseRealTime();
BENCHMARK(BM_thread_scaling_pool<Workload::kPartialFill640>)
    ->Name("lab/thread_scaling/partial_fill_640")
    ->Apply(worker_counts)
    ->UseRealTime();

BENCHMARK(BM_thread_scaling_serial<Workload::kPartialFill1536>)
    ->Name("lab/thread_scaling/partial_fill_1536/serial")
    ->UseRealTime();
BENCHMARK(BM_thread_scaling_pool<Workload::kPartialFill1536>)
    ->Name("lab/thread_scaling/partial_fill_1536")
    ->Apply(worker_counts)
    ->UseRealTime();

BENCHMARK(BM_thread_scaling_serial<Workload::kChunkFill>)
    ->Name("lab/thread_scaling/chunk_fill/serial")
    ->UseRealTime();
BENCHMARK(BM_thread_scaling_pool<Workload::kChunkFill>)
    ->Name("lab/thread_scaling/chunk_fill")
    ->Apply(worker_counts)
    ->UseRealTime();

BENCHMARK(BM_thread_scaling_serial<Workload::kChunkCompute>)
    ->Name("lab/thread_scaling/chunk_compute/serial")
    ->UseRealTime();
BENCHMARK(BM_thread_scaling_pool<Workload::kChunkCompute>)
    ->Name("lab/thread_scaling/chunk_compute")
    ->Apply(worker_counts)
    ->UseRealTime();

}  // namespace
