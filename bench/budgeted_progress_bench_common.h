// Shared plumbing for the budgeted-progress benchmark executables:
// the run options, saturated-cell structures, windowed flow
// accumulation, artifact assembly, calibration, and RSS sampling used
// by both translation units of tess_bench_budgeted_progress (and its
// diagnostics twin). Internal to the bench; never a public header.

#ifndef TESS_BENCH_BUDGETED_PROGRESS_COMMON_H
#define TESS_BENCH_BUDGETED_PROGRESS_COMMON_H

#if defined(__APPLE__)
#include <mach/mach.h>
#else
#include <unistd.h>
#endif
#include <tess/tess.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "budgeted_progress_arrival.h"
#include "budgeted_progress_artifact.h"
#include "budgeted_progress_clock.h"
#include "budgeted_progress_controller.h"
#include "budgeted_progress_search.h"
#include "colony_harness.h"
#include "grid_benchmark_harness.h"
#include "grid_map_generators.h"

namespace bpb_bench {

namespace budgeted = tess_test::budgeted;
namespace grid = tess_test::grid_benchmark;
namespace gen = tess_test::grid_benchmark;  // Generators share the namespace.

using budgeted::FrameBudgetConfig;
using budgeted::FrameBudgetController;
using budgeted::FrameRecord;
using budgeted::Nanos;
using budgeted::SteadyClock;

using PathScaleShape =
    tess::Shape<tess::Extent3{512, 512, 1}, tess::Extent3{32, 32, 1}>;
struct PassableTag {};
using PathSchema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>>;
using PathScaleWorld = tess::AlwaysResidentWorld<PathScaleShape, PathSchema>;

inline constexpr std::uint64_t kSeed =
    0x5C0107;  // Colony-harness canonical seed.
inline constexpr std::size_t kLogicalExtent = 64;
inline constexpr std::int64_t kScale = 8;  // 64x64 logical -> 512x512 world.
inline constexpr std::array<Nanos, 4> kBudgetsNs = {125'000, 500'000, 2'000'000,
                                                    8'000'000};

inline void fail(const char* message) {
  std::fprintf(stderr, "tess_bench_budgeted_progress: %s\n", message);
  std::exit(EXIT_FAILURE);
}

inline void check(bool condition, const char* message) {
  if (!condition) {
    fail(message);
  }
}

struct RunOptions {
  std::string out_dir;
  // Counter pass (design section 11.2): runs only the comparable
  // cells over the identical demand traces and stamps artifacts
  // pass:"counter"; wall numbers from this build are never published.
  bool counter_pass = false;
  std::uint64_t warmup_frames = 120;
  std::uint64_t measured_frames = 600;
  std::uint64_t repetitions = 10;
  std::size_t pool_size = 10'000;
  std::size_t validation_requests = 64;
};

// Current resident set size, sampled at repetition boundaries; the
// per-cell peak is the maximum of these samples (the design's
// "sampled at repetition boundaries" contract). Deliberately NOT
// ru_maxrss: that is a process-lifetime high-water mark, so later
// cells would inherit earlier cells' peaks in one sequential run.
[[nodiscard]] inline auto current_rss_bytes() -> std::uint64_t {
#if defined(__APPLE__)
  mach_task_basic_info info{};
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) {
    return 0;
  }
  return static_cast<std::uint64_t>(info.resident_size);
#else
  std::ifstream statm{"/proc/self/statm"};
  std::uint64_t total_pages = 0;
  std::uint64_t resident_pages = 0;
  if (!(statm >> total_pages >> resident_pages)) {
    return 0;
  }
  return resident_pages * static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
#endif
}

// --- Section 11.1 calibration -----------------------------------------

[[nodiscard]] inline auto calibrate(SteadyClock& clock)
    -> budgeted::CalibrationBlock {
  budgeted::CalibrationBlock calibration;
  calibration.clock_identity = "std::chrono::steady_clock";

  std::vector<std::uint64_t> read_costs;
  read_costs.reserve(4096);
  for (int i = 0; i < 4096; ++i) {
    const Nanos before = clock.now();
    const Nanos after = clock.now();
    read_costs.push_back(budgeted::sub_clamped(after, before));
  }
  calibration.clock_read_cost_ns = budgeted::summarize_family(
      "back_to_back_clock_reads", std::move(read_costs));

  // Empty controller loop: full frame machinery, no eligible work.
  FrameBudgetConfig config;
  config.budget_ns = 1'000'000;
  config.base_tps = 60;
  FrameBudgetController controller{clock, config};
  std::vector<std::uint64_t> loop_costs;
  loop_costs.reserve(2048);
  for (int i = 0; i < 2048; ++i) {
    const FrameRecord record = controller.run_frame(
        [](std::uint64_t) {}, []() -> bool { return false; });
    loop_costs.push_back(record.elapsed_ns);
  }
  calibration.empty_controller_loop_ns = budgeted::summarize_family(
      "empty_controller_frames", std::move(loop_costs));
  return calibration;
}

// --- Saturated cell plumbing ------------------------------------------

// Frame samples for one repetition of one budget cell.
struct SaturatedRep {
  std::vector<std::uint64_t> frame_elapsed_ns;
  std::vector<std::uint64_t> overshoot_quantum_tail_ns;
  std::vector<std::uint64_t> overshoot_mandatory_ns;
  std::vector<std::uint64_t> frame_start_lag_ns;  // Paced cells only.
  std::uint64_t overshoot_frames = 0;
  std::uint64_t useful_completions = 0;
  // Driver-observed work units summed over measured-frame quanta;
  // cross-checked against the accounted window consumption where the
  // accounting is production-attached (the resumable cell).
  std::uint64_t window_work_units = 0;
  // Completions during warmup frames: pool-serviced cells need the
  // window's exact pool offset for the prefix-sum work identity.
  std::uint64_t warmup_completions = 0;
};

// One budget cell's accumulated repetitions. `window_flow` sums the
// per-repetition measured-window snapshot deltas (design section 9.2:
// FlowCounters snapshots at window start and end), so warmup
// transitions never leak into the artifact. Gauges: outstanding is
// the final window-end value (zero at quiescent boundaries), high
// water the cumulative maximum.
struct SaturatedCellResult {
  tess::diagnostics::FlowCounters window_flow;
  std::vector<SaturatedRep> reps;
  std::uint64_t peak_rss = 0;
};

inline void accumulate_window(tess::diagnostics::FlowCounters& window,
                              const tess::diagnostics::FlowCounters& start,
                              const tess::diagnostics::FlowCounters& end) {
  window.offered += end.offered - start.offered;
  window.admitted += end.admitted - start.admitted;
  window.rejected += end.rejected - start.rejected;
  window.coalesced_into_pending +=
      end.coalesced_into_pending - start.coalesced_into_pending;
  window.completed += end.completed - start.completed;
  window.cancelled += end.cancelled - start.cancelled;
  window.superseded += end.superseded - start.superseded;
  window.stale += end.stale - start.stale;
  window.failed += end.failed - start.failed;
  window.dropped_after_admission +=
      end.dropped_after_admission - start.dropped_after_admission;
  window.offered_work_units +=
      end.offered_work_units - start.offered_work_units;
  window.consumed_work_units +=
      end.consumed_work_units - start.consumed_work_units;
  window.inventory_tick_weighted +=
      end.inventory_tick_weighted - start.inventory_tick_weighted;
  window.residence_ticks_accumulated +=
      end.residence_ticks_accumulated - start.residence_ticks_accumulated;
  // Delta gauge: the pre-window drain guarantees a quiescent start
  // boundary, so summing end-of-window outstanding across repetitions
  // keeps the retention identity exact on the summed deltas (the
  // value counts repetitions that ended with an in-flight item).
  window.outstanding_current +=
      end.outstanding_current - start.outstanding_current;
  window.outstanding_high_water =
      std::max(window.outstanding_high_water, end.outstanding_high_water);
  window.oldest_outstanding_age_ticks = end.oldest_outstanding_age_ticks;
}

// One repetition of one budget cell: warmup frames, a quiescing
// drain, the window-start snapshot, measured frames, and the
// window-end snapshot taken immediately — before any further work —
// so nothing outside the measured frames can leak into the window
// flow. The single pre-window drain retires at most one in-flight
// item, outside measured frames, so it cannot erase outstanding
// growth (design section 9.2); in-flight items at window end appear
// in the outstanding delta gauge and the identities hold exactly.
template <typename ResetFn, typename TickFn, typename QuantumFn,
          typename DrainFn, typename WindowBeginFn, typename WindowEndFn>
void run_one_rep(const RunOptions& options, Nanos budget_ns, SteadyClock& clock,
                 tess::diagnostics::FlowAccounting& accounting,
                 std::uint64_t rep, std::size_t budget_index, ResetFn& reset,
                 TickFn& on_tick, QuantumFn& quantum, DrainFn& drain,
                 WindowBeginFn& window_begin, WindowEndFn& window_end,
                 SaturatedCellResult& out) {
  reset(rep);
  FrameBudgetConfig config;
  config.budget_ns = budget_ns;
  config.base_tps = 60;  // One tick per frame keeps ticks flowing.
  FrameBudgetController controller{clock, config};
  SaturatedRep samples;
  samples.frame_elapsed_ns.reserve(options.measured_frames);
  samples.overshoot_quantum_tail_ns.reserve(options.measured_frames);
  samples.overshoot_mandatory_ns.reserve(options.measured_frames);

  auto mandatory = [&](std::uint64_t tick) { on_tick(tick); };
  std::uint64_t completions = 0;
  std::uint64_t window_work = 0;
  bool in_window = false;
  auto quantum_fn = [&]() -> bool {
    const std::uint64_t work = quantum();
    if (work == 0) {
      return false;
    }
    ++completions;
    if (in_window) {
      window_work += work;
    }
    return true;
  };

  for (std::uint64_t frame = 0; frame < options.warmup_frames; ++frame) {
    (void)controller.run_frame(mandatory, quantum_fn);
  }
  samples.warmup_completions = completions;
  drain();
  // Window-scope the high-water gauge: it is otherwise a lifetime
  // maximum and would carry warmup backlog into the artifact.
  accounting.counters.outstanding_high_water =
      accounting.counters.outstanding_current;
  const tess::diagnostics::FlowCounters window_start = accounting.counters;
  // Counter-pass sinks install for measured frames only, so untimed
  // validation and warmup never pollute the aggregates.
  window_begin(budget_index);
  in_window = true;

  for (std::uint64_t frame = 0; frame < options.measured_frames; ++frame) {
    completions = 0;
    const FrameRecord record = controller.run_frame(mandatory, quantum_fn);
    samples.frame_elapsed_ns.push_back(record.elapsed_ns);
    samples.overshoot_quantum_tail_ns.push_back(
        record.overshoot_quantum_tail_ns);
    samples.overshoot_mandatory_ns.push_back(record.overshoot_mandatory_ns);
    if (record.overshoot_quantum_tail_ns > 0 ||
        record.overshoot_mandatory_ns > 0) {
      ++samples.overshoot_frames;
    }
    samples.useful_completions += completions;
  }
  in_window = false;
  samples.window_work_units = window_work;
  window_end(budget_index);
  // Window-end snapshot happens before anything else runs: a drained
  // ticket must not leak post-window work into the measured flow.
  // The identities still hold on the deltas because outstanding is
  // accumulated as a delta gauge (start boundary is quiescent).
  accumulate_window(out.window_flow, window_start, accounting.counters);
  out.reps.push_back(std::move(samples));
  // Sampled at repetition boundaries, outside any timed frame.
  out.peak_rss = std::max(out.peak_rss, current_rss_bytes());
}

// Repetition-outer, budget-inner with the starting offset rotated by
// repetition (design section 11.4): budget order decorrelates from
// thermal drift while staying fully deterministic.
inline void no_window_hook(std::size_t) {}

template <typename ResetFn, typename TickFn, typename QuantumFn,
          typename DrainFn, typename WindowBeginFn = decltype(no_window_hook)&,
          typename WindowEndFn = decltype(no_window_hook)&>
[[nodiscard]] auto run_saturated_budgets(
    const RunOptions& options, tess::diagnostics::FlowAccounting& accounting,
    ResetFn&& reset, TickFn&& on_tick, QuantumFn&& quantum, DrainFn&& drain,
    WindowBeginFn&& window_begin = no_window_hook,
    WindowEndFn&& window_end = no_window_hook)
    -> std::array<SaturatedCellResult, kBudgetsNs.size()> {
  std::array<SaturatedCellResult, kBudgetsNs.size()> results;
  SteadyClock clock;
  for (std::uint64_t rep = 0; rep < options.repetitions; ++rep) {
    for (std::size_t k = 0; k < kBudgetsNs.size(); ++k) {
      const std::size_t budget_index = (k + rep) % kBudgetsNs.size();
      run_one_rep(options, kBudgetsNs[budget_index], clock, accounting, rep,
                  budget_index, reset, on_tick, quantum, drain, window_begin,
                  window_end, results[budget_index]);
    }
  }
  return results;
}

[[nodiscard]] inline auto compiler_identity() -> std::string {
#if defined(__clang__)
  return std::string{"clang "} + __clang_version__;
#elif defined(__GNUC__)
  return "gcc";
#else
  return "unknown";
#endif
}

[[nodiscard]] inline auto build_artifact(const RunOptions& options,
                                         const SaturatedCellResult& cell,
                                         Nanos budget_ns)
    -> budgeted::Artifact {
  budgeted::Artifact artifact;
  const char* commit = std::getenv("GITHUB_SHA");
  artifact.run.commit = commit != nullptr ? commit : "local";
  artifact.run.machine_fingerprint = "local-uncontrolled";
  artifact.run.compiler = compiler_identity();
  artifact.run.bench_flags = "";

  artifact.experiment.kind = "isolated_saturated";
  artifact.experiment.pass = options.counter_pass ? "counter" : "timing";
  artifact.experiment.seed = kSeed;
  artifact.experiment.sim_tps = 60;
  artifact.experiment.pacing = "unpaced";
  artifact.experiment.budget_ns = budget_ns;
  artifact.experiment.settlement_ticks = 0;

  // Measured-window flow only; the identities must hold on the
  // deltas because both window boundaries are quiescent.
  check(cell.window_flow.admission_identity_holds(),
        "window flow violates the admission identity");
  check(cell.window_flow.retention_identity_holds(),
        "window flow violates the retention identity");
  artifact.flow = cell.window_flow;

  auto& summary = artifact.summary;
  summary.measured_frames = options.measured_frames;
  summary.repetitions = options.repetitions;
  // Section 9 throughput basis: completions inside the measured
  // window, uniformly for every cell.
  summary.useful_completions = cell.window_flow.completed;
  summary.consumed_work_units = cell.window_flow.consumed_work_units;
  summary.min_repetition_completions =
      std::numeric_limits<std::uint64_t>::max();
  for (const SaturatedRep& rep : cell.reps) {
    summary.min_repetition_completions =
        std::min(summary.min_repetition_completions, rep.useful_completions);
  }
  if (cell.reps.empty()) {
    summary.min_repetition_completions = 0;
  }
  std::vector<std::uint64_t> elapsed;
  std::vector<std::uint64_t> tail;
  std::vector<std::uint64_t> mandatory;
  std::uint64_t overshoot_frames = 0;
  for (const SaturatedRep& rep : cell.reps) {
    elapsed.insert(elapsed.end(), rep.frame_elapsed_ns.begin(),
                   rep.frame_elapsed_ns.end());
    tail.insert(tail.end(), rep.overshoot_quantum_tail_ns.begin(),
                rep.overshoot_quantum_tail_ns.end());
    mandatory.insert(mandatory.end(), rep.overshoot_mandatory_ns.begin(),
                     rep.overshoot_mandatory_ns.end());
    overshoot_frames += rep.overshoot_frames;
  }
  const auto total_frames = static_cast<double>(elapsed.size());
  summary.overshoot_frame_rate =
      total_frames > 0 ? static_cast<double>(overshoot_frames) / total_frames
                       : 0.0;
  summary.frame_elapsed_ns = budgeted::summarize_family(
      "all_measured_frames_pooled", std::move(elapsed));
  summary.overshoot_quantum_tail_ns =
      budgeted::summarize_family("all_measured_frames_pooled", std::move(tail));
  summary.overshoot_mandatory_ns = budgeted::summarize_family(
      "all_measured_frames_pooled", std::move(mandatory));
  summary.peak_rss_bytes = cell.peak_rss;
  return artifact;
}

// Frame-counted completions, for the slim cells' exact cross-check
// against the window flow.
[[nodiscard]] inline auto frame_counted_completions(
    const SaturatedCellResult& cell) -> std::uint64_t {
  std::uint64_t total = 0;
  for (const SaturatedRep& rep : cell.reps) {
    total += rep.useful_completions;
  }
  return total;
}

inline void write_artifact(const RunOptions& options,
                           const budgeted::Artifact& in,
                           const std::string& cell_name, Nanos budget_ns) {
  const std::string json = budgeted::emit_artifact_json(in);
  const std::string path = options.out_dir + "/" + cell_name + "_" +
                           std::to_string(budget_ns) + "ns.json";
  std::ofstream out{path, std::ios::binary};
  out << json;
  out.close();
  check(!out.fail(), "failed to write artifact");
  std::printf("wrote %s (completions %llu)\n", path.c_str(),
              static_cast<unsigned long long>(in.summary.useful_completions));
}

// Stage 3 (defined in tess_budgeted_progress_mixed.cc).
void run_mixed_colony_cells(const RunOptions& base_options);

}  // namespace bpb_bench

#endif  // TESS_BENCH_BUDGETED_PROGRESS_COMMON_H
