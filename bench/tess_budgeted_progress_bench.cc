// Budgeted-progress benchmark runner: stage-2 saturated subset
// (docs/planning/budgeted-progress-benchmarks.md, sections 6, 11-12,
// 14-15).
//
// A standalone executable, deliberately not a Google Benchmark suite:
// the experiment is a multi-frame queueing loop with its own clock,
// controller, and tess.budgeted_progress.v1 artifact schema, and it
// registers no benchmark names, so it inherits neither the threshold
// literal gate nor the workload-matrix scan. Cell identities live in
// the artifact's workload_refs instead.
//
// First-iteration cells (design section 15): unit A* point paths,
// eight-goal field-product builds, and a real ResumableWorkQueue, all
// saturated mode, serial, unpaced. The A* cell uses the deterministic
// generator/oracle machinery (tests/grid_map_generators.h raster-
// scaled 64x64 -> 512x512 exactly like the colony harness) rather
// than the carve_* layouts of the registered path/astar_unit cells;
// it shares that family identity as its workload_ref while the
// layouts differ, which is deliberate: the generator path provides
// the frozen 10k request pool and the independent Dijkstra oracle the
// design mandates, which the carve fixtures cannot.
//
// Paced cells measure paced-with-idle behavior: the loop sleeps to
// each 60 FPS edge, so the core enters idle states between frames and
// the first work after wake runs at reduced frequency with cooled
// caches. On the dev machine this costs ~14% of within-budget work
// units at a 2 ms budget versus the unpaced loop and stretches worst
// quantum tails ~2x. That is the honest result for a host that idles
// between simulation slices; a spin-paced mode emulating a busy host
// is a possible future variant, not silently substituted here.
//
// Field products are built through the direct build API each
// iteration — the product cache is never involved, so every
// completion is a real build, not a cache hit. The pool is inventory,
// not admitted flow: items are offered and admitted at selection, and
// pool wrap-around re-services are new admissions.

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
#include <string>
#include <vector>

#include "budgeted_progress_arrival.h"
#include "budgeted_progress_artifact.h"
#include "budgeted_progress_clock.h"
#include "budgeted_progress_controller.h"
#include "budgeted_progress_search.h"
#include "grid_benchmark_harness.h"
#include "grid_map_generators.h"

namespace {

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

constexpr std::uint64_t kSeed = 0x5C0107;  // Colony-harness canonical seed.
constexpr std::size_t kLogicalExtent = 64;
constexpr std::int64_t kScale = 8;  // 64x64 logical -> 512x512 world.
constexpr std::array<Nanos, 4> kBudgetsNs = {125'000, 500'000, 2'000'000,
                                             8'000'000};

void fail(const char* message) {
  std::fprintf(stderr, "tess_bench_budgeted_progress: %s\n", message);
  std::exit(EXIT_FAILURE);
}

void check(bool condition, const char* message) {
  if (!condition) {
    fail(message);
  }
}

struct RunOptions {
  std::string out_dir;
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
[[nodiscard]] auto current_rss_bytes() -> std::uint64_t {
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

[[nodiscard]] auto calibrate(SteadyClock& clock) -> budgeted::CalibrationBlock {
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

void accumulate_window(tess::diagnostics::FlowCounters& window,
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
          typename DrainFn>
void run_one_rep(const RunOptions& options, Nanos budget_ns, SteadyClock& clock,
                 tess::diagnostics::FlowAccounting& accounting,
                 std::uint64_t rep, ResetFn& reset, TickFn& on_tick,
                 QuantumFn& quantum, DrainFn& drain, SaturatedCellResult& out) {
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
  auto quantum_fn = [&]() -> bool {
    const std::uint64_t work = quantum();
    if (work == 0) {
      return false;
    }
    ++completions;
    return true;
  };

  for (std::uint64_t frame = 0; frame < options.warmup_frames; ++frame) {
    (void)controller.run_frame(mandatory, quantum_fn);
  }
  drain();
  // Window-scope the high-water gauge: it is otherwise a lifetime
  // maximum and would carry warmup backlog into the artifact.
  accounting.counters.outstanding_high_water =
      accounting.counters.outstanding_current;
  const tess::diagnostics::FlowCounters window_start = accounting.counters;

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
template <typename ResetFn, typename TickFn, typename QuantumFn,
          typename DrainFn>
[[nodiscard]] auto run_saturated_budgets(
    const RunOptions& options, tess::diagnostics::FlowAccounting& accounting,
    ResetFn&& reset, TickFn&& on_tick, QuantumFn&& quantum, DrainFn&& drain)
    -> std::array<SaturatedCellResult, kBudgetsNs.size()> {
  std::array<SaturatedCellResult, kBudgetsNs.size()> results;
  SteadyClock clock;
  for (std::uint64_t rep = 0; rep < options.repetitions; ++rep) {
    for (std::size_t k = 0; k < kBudgetsNs.size(); ++k) {
      const std::size_t budget_index = (k + rep) % kBudgetsNs.size();
      run_one_rep(options, kBudgetsNs[budget_index], clock, accounting, rep,
                  reset, on_tick, quantum, drain, results[budget_index]);
    }
  }
  return results;
}

[[nodiscard]] auto compiler_identity() -> std::string {
#if defined(__clang__)
  return std::string{"clang "} + __clang_version__;
#elif defined(__GNUC__)
  return "gcc";
#else
  return "unknown";
#endif
}

[[nodiscard]] auto build_artifact(const RunOptions& options,
                                  const SaturatedCellResult& cell,
                                  Nanos budget_ns) -> budgeted::Artifact {
  budgeted::Artifact artifact;
  const char* commit = std::getenv("GITHUB_SHA");
  artifact.run.commit = commit != nullptr ? commit : "local";
  artifact.run.machine_fingerprint = "local-uncontrolled";
  artifact.run.compiler = compiler_identity();
  artifact.run.bench_flags = "";

  artifact.experiment.kind = "isolated_saturated";
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
[[nodiscard]] auto frame_counted_completions(const SaturatedCellResult& cell)
    -> std::uint64_t {
  std::uint64_t total = 0;
  for (const SaturatedRep& rep : cell.reps) {
    total += rep.useful_completions;
  }
  return total;
}

void write_artifact(const RunOptions& options, const budgeted::Artifact& in,
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

// --- Cell 1: unit A* point paths --------------------------------------

struct AstarCell {
  PathScaleWorld world;
  grid::BenchmarkMap scaled_map;  // 512x512 raster for the honest oracle.
  std::vector<tess::PathRequest> pool;
  // Contiguous-reference expectation per pool entry (status | cost),
  // precomputed untimed; every timed completion is checked against it
  // so no invalid result can ever count as useful (design section 10).
  std::vector<std::uint64_t> expected;
  std::string pool_sha256;
  std::string expected_sha256;
  tess::PathScratch scratch;
  std::size_t next = 0;
};

[[nodiscard]] auto pack_path_outcome(const tess::PathResult& result)
    -> std::uint64_t {
  return (static_cast<std::uint64_t>(result.status) << 32) |
         static_cast<std::uint64_t>(result.cost);
}

[[nodiscard]] auto build_astar_cell(const RunOptions& options) -> AstarCell {
  AstarCell cell;
  const auto logical = gen::room_and_corridor(kLogicalExtent, kLogicalExtent,
                                              kSeed, {24, 6, 12});
  check(logical.has_value(), "room_and_corridor generation failed");
  const auto parsed = grid::parse_map("budgeted-logical", logical->text);
  check(static_cast<bool>(parsed), "logical map parse failed");
  const grid::BenchmarkMap& logical_map = parsed.value;

  // Raster-scale 64x64 -> 512x512 (the colony-harness pattern); build
  // the scaled BenchmarkMap alongside the world so the oracle runs on
  // exactly the world the search sees.
  cell.scaled_map.name = "budgeted-scaled";
  cell.scaled_map.width = 512;
  cell.scaled_map.height = 512;
  cell.scaled_map.passability.assign(512 * 512, 0);
  for (std::int64_t y = 0; y < 512; ++y) {
    for (std::int64_t x = 0; x < 512; ++x) {
      const auto logical_index =
          static_cast<std::size_t>(y / kScale) * logical_map.width +
          static_cast<std::size_t>(x / kScale);
      const bool passable = logical_map.passability[logical_index] != 0;
      cell.world.field<PassableTag>(tess::Coord3{x, y, 0}) = passable ? 1 : 0;
      cell.scaled_map.passability[static_cast<std::size_t>(y) * 512 +
                                  static_cast<std::size_t>(x)] =
          passable ? 1 : 0;
    }
  }

  // Frozen request pool: deterministic logical endpoints scaled into
  // the middle of their 8x8 blocks. Pool identity = SHA-256 over the
  // request bytes plus the selection-order tag (design section 6.2).
  const auto endpoints =
      gen::deterministic_endpoints(logical_map, kSeed, options.pool_size);
  check(endpoints.size() == options.pool_size, "endpoint pool short");
  budgeted::Sha256 hasher;
  const char* order_tag = "pool_sequential_wrap_v1";
  hasher.update(order_tag, std::strlen(order_tag));
  cell.pool.reserve(endpoints.size());
  for (const auto& [start, goal] : endpoints) {
    const tess::PathRequest request{
        tess::Coord3{start.x * kScale + kScale / 2,
                     start.y * kScale + kScale / 2, 0},
        tess::Coord3{goal.x * kScale + kScale / 2, goal.y * kScale + kScale / 2,
                     0}};
    cell.pool.push_back(request);
    const std::int64_t words[4] = {request.start.x, request.start.y,
                                   request.goal.x, request.goal.y};
    hasher.update(words, sizeof(words));
  }
  cell.pool_sha256 = hasher.hex_digest();
  cell.scratch.reserve_nodes(PathScaleShape::size.x * PathScaleShape::size.y);
  return cell;
}

// Pre-timing correctness (design section 10): the first K pool
// requests against the independent Dijkstra oracle on the scaled map,
// then a full-pool contiguous reference pass repeated twice to pin
// determinism. The reference outcomes are stored so the timed loop
// can check every completion it counts, not just the oracle subset.
void validate_astar_cell(AstarCell& cell, std::size_t oracle_count) {
  for (std::size_t i = 0; i < oracle_count && i < cell.pool.size(); ++i) {
    const tess::PathRequest& request = cell.pool[i];
    const tess::PathResult result =
        tess::astar_path<PathScaleWorld, PassableTag>(cell.world, request,
                                                      cell.scratch);
    const auto reference =
        grid::reference_cost(cell.scaled_map, request.start, request.goal,
                             grid::ReferenceMovement::Orthogonal);
    if (reference.has_value()) {
      check(result.status == tess::PathStatus::Found,
            "oracle found a path the search missed");
      check(result.cost == *reference, "path cost diverges from oracle");
    } else {
      check(result.status != tess::PathStatus::Found,
            "search found a path the oracle rejects");
    }
  }

  cell.expected.reserve(cell.pool.size());
  budgeted::Sha256 first_pass;
  for (const tess::PathRequest& request : cell.pool) {
    const tess::PathResult result =
        tess::astar_path<PathScaleWorld, PassableTag>(cell.world, request,
                                                      cell.scratch);
    const std::uint64_t packed = pack_path_outcome(result);
    cell.expected.push_back(packed);
    first_pass.update(&packed, sizeof(packed));
  }
  budgeted::Sha256 second_pass;
  for (std::size_t i = 0; i < cell.pool.size(); ++i) {
    const tess::PathResult result =
        tess::astar_path<PathScaleWorld, PassableTag>(cell.world, cell.pool[i],
                                                      cell.scratch);
    const std::uint64_t packed = pack_path_outcome(result);
    check(packed == cell.expected[i], "contiguous reference not deterministic");
    second_pass.update(&packed, sizeof(packed));
  }
  cell.expected_sha256 = first_pass.hex_digest();
  check(cell.expected_sha256 == second_pass.hex_digest(),
        "contiguous reference hash mismatch");
  std::printf("astar contiguous reference sha256 %s\n",
              cell.expected_sha256.c_str());
}

void run_astar_cell(const RunOptions& options, AstarCell& cell) {
  tess::diagnostics::FlowAccounting accounting;
  auto reset = [&cell](std::uint64_t) { cell.next = 0; };
  auto on_tick = [](std::uint64_t) {};
  auto quantum = [&cell, &accounting]() -> std::uint64_t {
    const std::size_t index = cell.next;
    const tess::PathRequest& request = cell.pool[index];
    cell.next = (cell.next + 1) % cell.pool.size();
    // Admit-on-selection: offer, admit, and resolve around one call.
    ++accounting.counters.offered;
    accounting.record_admitted();
    const tess::PathResult result =
        tess::astar_path<PathScaleWorld, PassableTag>(cell.world, request,
                                                      cell.scratch);
    // Two loads and a compare: every completion counted as useful is
    // checked against the precomputed contiguous reference.
    check(pack_path_outcome(result) == cell.expected[index],
          "timed result diverges from contiguous reference");
    const auto work = static_cast<std::uint64_t>(result.expanded_nodes);
    ++accounting.counters.completed;
    accounting.record_left_outstanding();
    accounting.counters.offered_work_units += work;
    accounting.counters.consumed_work_units += work;
    return work > 0 ? work : 1;
  };
  auto drain = []() {};  // Quiescent between quanta by construction.

  const auto results = run_saturated_budgets(options, accounting, reset,
                                             on_tick, quantum, drain);
  for (std::size_t i = 0; i < kBudgetsNs.size(); ++i) {
    budgeted::Artifact artifact =
        build_artifact(options, results[i], kBudgetsNs[i]);
    check(frame_counted_completions(results[i]) == artifact.flow.completed,
          "astar completion bases diverge");
    artifact.experiment.scenario_id = "astar-unit-roomcorridor-512-v1";
    artifact.experiment.workload_refs = {"path/astar_unit"};
    artifact.trace.sha256 = cell.pool_sha256;
    SteadyClock clock;
    artifact.calibration = calibrate(clock);
    write_artifact(options, artifact, "astar_unit", kBudgetsNs[i]);
  }
}

// --- Arrival-rate cells for the unit A* workload (section 6.2) ---------

// Open-loop demand: the interactive point-path class (allowance
// admission + 1 tick, section 8.3), released by the deterministic
// rational-rate accumulator, serviced FIFO. No capacity search yet:
// a small fixed rate ladder exercises stable and unstable points.
constexpr std::array<std::uint64_t, 3> kArrivalRatesPerSimSecond = {600, 2400,
                                                                    9600};
constexpr std::uint64_t kInteractiveAllowanceTicks = 1;
constexpr std::uint64_t kArrivalSettlementTicks =
    2 * kInteractiveAllowanceTicks;

// One repetition's auditable evidence (design section 9.3: every
// repetition is persisted regardless of verdict).
struct ArrivalRepRecord {
  bool stable = false;
  std::uint64_t useful_completions = 0;
  std::uint64_t cohort_admitted = 0;
  std::uint64_t cohort_deadline_met = 0;
  std::uint64_t outstanding_growth = 0;
  std::uint64_t oldest_age_end_ticks = 0;
};

struct ArrivalCellResult {
  tess::diagnostics::FlowCounters window_flow;
  std::vector<ArrivalRepRecord> rep_records;
  std::vector<SaturatedRep> reps;
  // Wall-clock span of the measured frames, summed across
  // repetitions; published as a rate only from paced cells.
  Nanos measured_wall_ns = 0;
  budgeted::ArrivalSummary totals;
  std::vector<std::uint64_t> lateness_ticks;
  std::vector<std::uint64_t> oldest_age_samples;
  std::uint64_t stable_reps = 0;
  std::uint64_t peak_rss = 0;
};

// One arrival repetition: warmup with demand flowing, an untimed
// pre-window drain so the measured window starts with zero backlog
// (draining outside the window never erases in-window growth, design
// section 9.2), measured frames, the window-end snapshot before any
// settlement work, then settlement ticks with admissions stopped but
// service continuing so tick-based deadlines resolve.
void run_arrival_rep(const RunOptions& options, AstarCell& cell,
                     SteadyClock& clock, Nanos budget_ns, std::uint64_t rate,
                     budgeted::Pacing pacing, ArrivalCellResult& out) {
  cell.next = 0;
  FrameBudgetConfig config;
  config.budget_ns = budget_ns;
  config.base_tps = 60;
  config.pacing = pacing;
  FrameBudgetController controller{clock, config};

  const std::uint64_t horizon_ticks =
      options.warmup_frames + options.measured_frames + 8;
  const auto expected_items =
      static_cast<std::size_t>((rate * horizon_ticks) / 60 + 64);
  budgeted::ArrivalTracker tracker{kInteractiveAllowanceTicks, 60,
                                   expected_items};
  budgeted::RationalRate release{rate, 1, 60};

  bool admitting = true;
  auto mandatory = [&](std::uint64_t tick) {
    tracker.observe_tick(tick);
    if (!admitting) {
      return;
    }
    const std::uint64_t events = release.release_at_tick();
    for (std::uint64_t i = 0; i < events; ++i) {
      tracker.admit(tick);
    }
  };
  auto service_one = [&]() -> std::uint64_t {
    const std::size_t item = tracker.next();
    if (item == budgeted::ArrivalTracker::npos) {
      return 0;
    }
    const std::size_t pool_index = item % cell.pool.size();
    const tess::PathResult result =
        tess::astar_path<PathScaleWorld, PassableTag>(
            cell.world, cell.pool[pool_index], cell.scratch);
    check(pack_path_outcome(result) == cell.expected[pool_index],
          "timed arrival result diverges from contiguous reference");
    const auto work = static_cast<std::uint64_t>(result.expanded_nodes);
    tracker.complete(item, controller.sim_tick(), work > 0 ? work : 1);
    return work > 0 ? work : 1;
  };
  auto quantum = [&]() -> bool { return service_one() > 0; };

  for (std::uint64_t frame = 0; frame < options.warmup_frames; ++frame) {
    (void)controller.run_frame(mandatory, quantum);
  }
  // Untimed pre-window drain: window starts with zero backlog.
  while (service_one() > 0) {
  }
  // Window-scope the high-water gauge (lifetime max would carry the
  // warmup backlog into the measured-window artifact).
  tracker.accounting().counters.outstanding_high_water =
      tracker.counters().outstanding_current;
  const tess::diagnostics::FlowCounters window_start = tracker.counters();
  tracker.begin_window(controller.sim_tick() + 1);

  SaturatedRep samples;
  samples.frame_elapsed_ns.reserve(options.measured_frames);
  samples.overshoot_quantum_tail_ns.reserve(options.measured_frames);
  samples.overshoot_mandatory_ns.reserve(options.measured_frames);
  if (pacing == budgeted::Pacing::Paced) {
    samples.frame_start_lag_ns.reserve(options.measured_frames);
  }
  const Nanos wall_start = clock.now();
  for (std::uint64_t frame = 0; frame < options.measured_frames; ++frame) {
    const FrameRecord record = controller.run_frame(mandatory, quantum);
    samples.frame_elapsed_ns.push_back(record.elapsed_ns);
    samples.overshoot_quantum_tail_ns.push_back(
        record.overshoot_quantum_tail_ns);
    samples.overshoot_mandatory_ns.push_back(record.overshoot_mandatory_ns);
    if (pacing == budgeted::Pacing::Paced) {
      samples.frame_start_lag_ns.push_back(record.frame_start_lag_ns);
    }
    if (record.overshoot_quantum_tail_ns > 0 ||
        record.overshoot_mandatory_ns > 0) {
      ++samples.overshoot_frames;
    }
  }
  out.measured_wall_ns += budgeted::sub_clamped(clock.now(), wall_start);
  tracker.end_window(controller.sim_tick());
  // Re-observe at the current tick so the oldest-age gauge reflects
  // the queue after the final frame's admissions and service, not the
  // pre-frame state (same tick: inventory weighting is unchanged, and
  // the window is closed so no extra sample is recorded).
  tracker.observe_tick(controller.sim_tick());
  const tess::diagnostics::FlowCounters window_end = tracker.counters();
  accumulate_window(out.window_flow, window_start, window_end);

  // Settlement: admissions stop, service continues, tick clock runs.
  admitting = false;
  const std::uint64_t settle_until =
      controller.sim_tick() + kArrivalSettlementTicks;
  while (controller.sim_tick() < settle_until) {
    (void)controller.run_frame(mandatory, quantum);
  }
  const budgeted::ArrivalSummary summary =
      tracker.summarize(controller.sim_tick());

  // Section 9.2 flow-stability criteria on this repetition.
  const std::uint64_t admitted_window =
      window_end.admitted - window_start.admitted;
  const std::uint64_t growth = window_end.outstanding_current;
  const std::uint64_t growth_allowance =
      std::max<std::uint64_t>(1, (admitted_window * 5 + 999) / 1000);
  const bool identities = window_end.admission_identity_holds() &&
                          window_end.retention_identity_holds();
  const bool age_ok = window_end.oldest_outstanding_age_ticks <=
                      tracker.starvation_window_ticks();
  const bool deadline_ok =
      summary.cohort_admitted == 0 ||
      summary.cohort_deadline_met * 100 >= summary.cohort_admitted * 99;
  const bool rep_stable =
      identities && growth <= growth_allowance && age_ok && deadline_ok;
  if (rep_stable) {
    ++out.stable_reps;
  }
  out.rep_records.push_back({rep_stable, summary.useful_completions,
                             summary.cohort_admitted,
                             summary.cohort_deadline_met, growth,
                             window_end.oldest_outstanding_age_ticks});

  out.totals.useful_completions += summary.useful_completions;
  out.totals.cohort_admitted += summary.cohort_admitted;
  out.totals.cohort_deadline_met += summary.cohort_deadline_met;
  out.totals.starved_items += summary.starved_items;
  out.lateness_ticks.insert(out.lateness_ticks.end(),
                            summary.lateness_ticks.begin(),
                            summary.lateness_ticks.end());
  out.oldest_age_samples.insert(out.oldest_age_samples.end(),
                                tracker.oldest_age_samples().begin(),
                                tracker.oldest_age_samples().end());
  out.reps.push_back(std::move(samples));
  out.peak_rss = std::max(out.peak_rss, current_rss_bytes());
}

// Paced cells use one canonical mid-ladder rate: a paced repetition
// costs real wall time by construction (measured_frames / 60 Hz), so
// the paced matrix stays deliberately small (design section 3.2).
constexpr std::array<std::uint64_t, 1> kPacedArrivalRates = {600};

template <std::size_t RateCount>
void run_arrival_cells(const RunOptions& options, AstarCell& cell,
                       budgeted::Pacing pacing,
                       const std::array<std::uint64_t, RateCount>& rates,
                       const char* name_prefix) {
  for (const std::uint64_t rate : rates) {
    SteadyClock clock;
    std::array<ArrivalCellResult, kBudgetsNs.size()> results;
    for (std::uint64_t rep = 0; rep < options.repetitions; ++rep) {
      for (std::size_t k = 0; k < kBudgetsNs.size(); ++k) {
        const std::size_t budget_index = (k + rep) % kBudgetsNs.size();
        run_arrival_rep(options, cell, clock, kBudgetsNs[budget_index], rate,
                        pacing, results[budget_index]);
      }
    }
    for (std::size_t i = 0; i < kBudgetsNs.size(); ++i) {
      ArrivalCellResult& cell_result = results[i];
      budgeted::Artifact artifact;
      std::vector<SaturatedRep> shim_reps = std::move(cell_result.reps);
      {
        // Reuse the saturated assembly for run/summary plumbing.
        SaturatedCellResult shim;
        shim.window_flow = cell_result.window_flow;
        shim.reps = shim_reps;
        shim.peak_rss = cell_result.peak_rss;
        artifact = build_artifact(options, shim, kBudgetsNs[i]);
      }
      artifact.experiment.kind = "isolated_arrival_rate";
      artifact.experiment.scenario_id = "astar-arrival-roomcorridor-512-v1";
      artifact.experiment.workload_refs = {"path/astar_unit"};
      artifact.experiment.settlement_ticks = kArrivalSettlementTicks;
      artifact.experiment.arrival_rate_num = rate;
      artifact.experiment.arrival_rate_den = 1;
      if (pacing == budgeted::Pacing::Paced) {
        artifact.experiment.pacing = "paced";
        std::vector<std::uint64_t> lag_samples;
        for (const SaturatedRep& rep : shim_reps) {
          lag_samples.insert(lag_samples.end(), rep.frame_start_lag_ns.begin(),
                             rep.frame_start_lag_ns.end());
        }
        artifact.summary.frame_start_lag_ns = budgeted::summarize_family(
            "paced_measured_frames_pooled", std::move(lag_samples));
        // Measured wall rate: paced cells only (design section 3.2).
        artifact.summary.measured_wall_ns = cell_result.measured_wall_ns;
        const double wall_seconds =
            static_cast<double>(cell_result.measured_wall_ns) / 1e9;
        artifact.summary.useful_per_wall_second =
            wall_seconds > 0
                ? static_cast<double>(cell_result.totals.useful_completions) /
                      wall_seconds
                : 0.0;
      }
      // The trace identity covers the release schedule, not just the
      // request pool: different rates are different demand traces.
      budgeted::Sha256 trace_hasher;
      const char* release_tag = "arrival_bresenham_v1";
      trace_hasher.update(release_tag, std::strlen(release_tag));
      const std::uint64_t rate_words[2] = {rate, 1};
      trace_hasher.update(rate_words, sizeof(rate_words));
      trace_hasher.update(cell.pool_sha256.data(), cell.pool_sha256.size());
      artifact.trace.sha256 = trace_hasher.hex_digest();

      // Section 9 headline: window completions from per-item records.
      artifact.summary.useful_completions =
          cell_result.totals.useful_completions;
      budgeted::DeadlineGroup deadlines;
      deadlines.deadline_success_rate =
          cell_result.totals.cohort_admitted > 0
              ? static_cast<double>(cell_result.totals.cohort_deadline_met) /
                    static_cast<double>(cell_result.totals.cohort_admitted)
              : 1.0;
      deadlines.lateness_ticks = budgeted::summarize_family(
          "completed_cohort_items", cell_result.lateness_ticks);
      deadlines.oldest_age_ticks = budgeted::summarize_family(
          "per_tick_window_observations", cell_result.oldest_age_samples);
      deadlines.starved_items = cell_result.totals.starved_items;
      artifact.summary.deadlines = deadlines;
      artifact.summary.flow_stable_applicable = true;
      artifact.summary.flow_stable =
          cell_result.stable_reps * 2 > options.repetitions;

      budgeted::ClassArtifact interactive;
      interactive.class_id = "interactive_path";
      interactive.deadline_allowance_ticks = kInteractiveAllowanceTicks;
      interactive.useful_completions = cell_result.totals.useful_completions;
      interactive.cohort_admitted = cell_result.totals.cohort_admitted;
      interactive.deadline_success_rate = deadlines.deadline_success_rate;
      interactive.lateness_ticks = deadlines.lateness_ticks;
      interactive.starved_items = cell_result.totals.starved_items;
      artifact.classes.push_back(interactive);

      SteadyClock calibration_clock;
      artifact.calibration = calibrate(calibration_clock);
      write_artifact(options, artifact, name_prefix + std::to_string(rate),
                     kBudgetsNs[i]);
    }
  }
}

// --- Capacity boundary search (section 9.3) ----------------------------

// Probe: 3 warm-configuration repetitions, majority verdict.
// Confirmation: 5 repetitions at 2.5x warmup / 3x measured frames,
// majority verdict. Both reuse the arrival repetition exactly.
void run_capacity_search(const RunOptions& options, AstarCell& cell) {
  // Fixed non-monotone budget order: a single adaptive search per
  // budget cannot rotate across repetitions the way fixed cells do
  // (section 11.4), so decorrelate elapsed time and thermal drift
  // from budget size by ordering deterministically out of size order.
  constexpr std::array<std::size_t, kBudgetsNs.size()> kSearchOrder = {2, 0, 3,
                                                                       1};
  for (const std::size_t budget_index : kSearchOrder) {
    const Nanos budget = kBudgetsNs[budget_index];
    SteadyClock clock;
    std::vector<std::vector<ArrivalRepRecord>> point_reps;
    auto stable_reps_at = [&](std::uint64_t rate, const RunOptions& cfg,
                              std::uint64_t reps) -> std::uint64_t {
      ArrivalCellResult scratch;
      for (std::uint64_t rep = 0; rep < reps; ++rep) {
        run_arrival_rep(cfg, cell, clock, budget, rate,
                        budgeted::Pacing::Unpaced, scratch);
      }
      point_reps.push_back(scratch.rep_records);
      return scratch.stable_reps;
    };
    auto probe = [&](std::uint64_t rate) -> bool {
      return stable_reps_at(rate, options, 3) >= 2;
    };
    auto confirm = [&](std::uint64_t rate) -> bool {
      RunOptions confirmation = options;
      confirmation.warmup_frames = options.warmup_frames * 5 / 2;
      confirmation.measured_frames = options.measured_frames * 3;
      return stable_reps_at(rate, confirmation, 5) >= 3;
    };

    const budgeted::SearchPolicy policy{60, 2, 24};
    const budgeted::SearchResult found =
        budgeted::search_capacity(policy, probe, confirm);

    budgeted::SearchArtifact artifact;
    const char* commit = std::getenv("GITHUB_SHA");
    artifact.run.commit = commit != nullptr ? commit : "local";
    artifact.run.machine_fingerprint = "local-uncontrolled";
    artifact.run.compiler = compiler_identity();
    artifact.scenario_id = "astar-arrival-roomcorridor-512-v1";
    artifact.workload_refs = {"path/astar_unit"};
    artifact.budget_ns = budget;
    artifact.sim_tps = 60;
    artifact.seed_rate = policy.seed_rate;
    artifact.resolution_percent = policy.resolution_percent;
    artifact.points.reserve(found.points.size());
    check(point_reps.size() == found.points.size(),
          "search point evidence out of sync");
    for (std::size_t i = 0; i < found.points.size(); ++i) {
      const budgeted::SearchPoint& point = found.points[i];
      budgeted::SearchArtifactPoint out_point;
      out_point.rate = point.rate;
      out_point.confirmation = point.kind == budgeted::PointKind::Confirmation;
      out_point.stable = point.stable;
      for (const ArrivalRepRecord& rep : point_reps[i]) {
        out_point.reps.push_back({rep.stable, rep.useful_completions,
                                  rep.cohort_admitted, rep.cohort_deadline_met,
                                  rep.outstanding_growth,
                                  rep.oldest_age_end_ticks});
      }
      artifact.points.push_back(out_point);
    }
    artifact.has_confirmed_stable = found.band.confirmed_stable.has_value();
    artifact.confirmed_stable = found.band.confirmed_stable.value_or(0);
    artifact.has_lowest_unstable = found.band.lowest_unstable.has_value();
    artifact.lowest_unstable = found.band.lowest_unstable.value_or(0);
    artifact.flapping = found.flapping;

    const std::string json = budgeted::emit_search_artifact_json(artifact);
    const std::string path = options.out_dir + "/astar_capacity_" +
                             std::to_string(budget) + "ns.json";
    std::ofstream out{path, std::ios::binary};
    out << json;
    out.close();
    check(!out.fail(), "failed to write search artifact");
    std::printf("wrote %s (confirmed %llu, lowest unstable %llu, points %zu)\n",
                path.c_str(),
                static_cast<unsigned long long>(artifact.confirmed_stable),
                static_cast<unsigned long long>(artifact.lowest_unstable),
                artifact.points.size());
  }
}

// --- Cell 4: eight-goal field-product builds ---------------------------

// TU-local copy of the product bench's room-portal carving (bench
// convention: each bench file owns its fixtures).
void carve_room_portals(PathScaleWorld& world) {
  constexpr std::int64_t kRoom = 32;
  for (std::int64_t y = 0; y < 512; ++y) {
    for (std::int64_t x = 0; x < 512; ++x) {
      const bool wall_x = x % kRoom == 0;
      const bool wall_y = y % kRoom == 0;
      if (!wall_x && !wall_y) {
        continue;
      }
      const std::int64_t room_x = x / kRoom;
      const std::int64_t room_y = y / kRoom;
      const std::int64_t portal =
          ((room_x * 23 + room_y * 7 + (wall_x ? 3 : 11)) % (kRoom - 2)) + 1;
      const bool is_portal =
          wall_x ? (y % kRoom) == portal : (x % kRoom) == portal;
      if (!is_portal) {
        world.field<PassableTag>(tess::Coord3{x, y, 0}) = 0;
      }
    }
  }
}

void run_field_product_cell(const RunOptions& options) {
  PathScaleWorld world;
  for (std::int64_t y = 0; y < 512; ++y) {
    for (std::int64_t x = 0; x < 512; ++x) {
      world.field<PassableTag>(tess::Coord3{x, y, 0}) = 1;
    }
  }
  carve_room_portals(world);

  const std::array<tess::Coord3, 8> goals = {
      tess::Coord3{5, 5, 0},     tess::Coord3{250, 12, 0},
      tess::Coord3{500, 40, 0},  tess::Coord3{40, 250, 0},
      tess::Coord3{260, 260, 0}, tess::Coord3{470, 300, 0},
      tess::Coord3{30, 470, 0},  tess::Coord3{447, 510, 0}};
  tess::GoalSet goal_set;
  goal_set.reserve(goals.size());
  for (const tess::Coord3 goal : goals) {
    world.field<PassableTag>(goal) = 1;
    goal_set.add(goal);
  }

  constexpr std::uint64_t node_count =
      PathScaleShape::size.x * PathScaleShape::size.y;
  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(node_count);
  tess::DistanceFieldProduct product;
  product.reserve_goals(goal_set.size());
  product.reserve_nodes(node_count);
  product.reserve_dependencies(PathScaleWorld::chunk_count);

  // Pre-timing validation: two direct builds are deterministic and
  // complete. Every timed completion is a real build through the same
  // direct API — the product cache is never constructed here.
  const tess::DistanceFieldResult reference =
      tess::build_distance_field_product<PathScaleWorld, PassableTag>(
          world, goal_set, scratch, product);
  check(reference.status == tess::PathStatus::Found,
        "field-product reference build failed");
  const auto reference_reached = reference.reached_nodes;
  const tess::DistanceFieldResult repeat =
      tess::build_distance_field_product<PathScaleWorld, PassableTag>(
          world, goal_set, scratch, product);
  check(repeat.reached_nodes == reference_reached,
        "field-product build not deterministic");

  budgeted::Sha256 hasher;
  const char* tag = "field_product_8goal_room_portals_v1";
  hasher.update(tag, std::strlen(tag));
  for (const tess::Coord3 goal : goals) {
    const std::int64_t words[2] = {goal.x, goal.y};
    hasher.update(words, sizeof(words));
  }
  const std::string trace_hash = hasher.hex_digest();

  tess::diagnostics::FlowAccounting accounting;
  auto reset = [](std::uint64_t) {};
  auto on_tick = [](std::uint64_t) {};
  auto quantum = [&]() -> std::uint64_t {
    ++accounting.counters.offered;
    accounting.record_admitted();
    const tess::DistanceFieldResult field =
        tess::build_distance_field_product<PathScaleWorld, PassableTag>(
            world, goal_set, scratch, product);
    check(field.status == tess::PathStatus::Found,
          "timed field-product build failed");
    const auto work = static_cast<std::uint64_t>(field.reached_nodes);
    ++accounting.counters.completed;
    accounting.record_left_outstanding();
    accounting.counters.offered_work_units += work;
    accounting.counters.consumed_work_units += work;
    return work;
  };
  auto drain = []() {};  // Quiescent between quanta by construction.

  const auto results = run_saturated_budgets(options, accounting, reset,
                                             on_tick, quantum, drain);
  for (std::size_t i = 0; i < kBudgetsNs.size(); ++i) {
    budgeted::Artifact artifact =
        build_artifact(options, results[i], kBudgetsNs[i]);
    check(frame_counted_completions(results[i]) == artifact.flow.completed,
          "field-product completion bases diverge");
    artifact.experiment.scenario_id = "field-product-8goal-roomportals-512-v1";
    artifact.experiment.workload_refs = {"path/field_product"};
    artifact.trace.sha256 = trace_hash;
    SteadyClock clock;
    artifact.calibration = calibrate(clock);
    write_artifact(options, artifact, "field_product", kBudgetsNs[i]);
  }
}

// --- Cell 7: real ResumableWorkQueue -----------------------------------

// Deterministic ALU work: each item advances a SplitMix64 stream a
// fixed number of rounds, so item cost is real, tiny, and identical
// across runs.
struct ResumableWork {
  std::uint64_t state = kSeed;
  std::uint32_t items_done = 0;
  std::uint32_t total_items = 64;

  auto operator()(tess::AsyncWorkBudget budget, std::uint64_t& sink)
      -> tess::AsyncWorkStep {
    const auto step = std::min<std::uint32_t>(
        {budget.max_items, 1, total_items - items_done});
    for (std::uint32_t i = 0; i < step; ++i) {
      for (int round = 0; round < 256; ++round) {
        state += 0x9e3779b97f4a7c15ULL;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        sink ^= z ^ (z >> 31);
      }
    }
    items_done += step;
    const auto next_state = items_done >= total_items
                                ? tess::AsyncStepState::Ready
                                : tess::AsyncStepState::Pending;
    return {next_state, step, tess::AsyncVersion{1}};
  }
};

// Design section 10 equivalence for the exact resumable workload: a
// contiguous run (large budget) and a run sliced at AsyncWorkBudget{1}
// must process the same 64 items exactly once and land in the same
// deterministic generator state.
void validate_resumable_workload() {
  auto run_workload = [](std::uint32_t budget_items) -> ResumableWork {
    tess::ResumableWorkQueue<std::uint64_t> queue;
    tess::diagnostics::FlowAccounting accounting;
    queue.set_flow_accounting(&accounting);
    queue.reserve_tickets(1);
    ResumableWork work;
    (void)queue.submit(work);
    while (queue.advance(tess::AsyncWorkBudget{budget_items}).invoked > 0) {
    }
    check(accounting.counters.completed == 1,
          "resumable workload did not complete exactly once");
    check(accounting.counters.retention_identity_holds(),
          "resumable workload violates the retention identity");
    return work;
  };
  const ResumableWork contiguous = run_workload(64);
  const ResumableWork sliced = run_workload(1);
  check(contiguous.items_done == 64 && sliced.items_done == 64,
        "resumable equivalence: item counts diverge");
  check(contiguous.state == sliced.state,
        "resumable equivalence: sliced state diverges from contiguous");
}

void run_resumable_cell(const RunOptions& options) {
  validate_resumable_workload();

  tess::ResumableWorkQueue<std::uint64_t> queue;
  tess::diagnostics::FlowAccounting accounting;
  ResumableWork work;
  bool accounting_attached = false;

  auto reset = [&](std::uint64_t) {
    queue.clear();
    if (!accounting_attached) {
      queue.set_flow_accounting(&accounting);
      accounting_attached = true;
    }
    queue.reserve_tickets(1024);
    work = ResumableWork{};
  };
  auto on_tick = [&](std::uint64_t tick) { queue.observe_flow_tick(tick); };
  // One quantum = one advance at the finest canonical budget; when
  // the current ticket retires, the pool "wraps": submitting the next
  // ticket is a new admission at selection time.
  auto quantum = [&]() -> std::uint64_t {
    tess::AsyncAdvanceStats stats = queue.advance(tess::AsyncWorkBudget{1});
    if (stats.invoked == 0) {
      // All tickets terminal: drop the retired slots (bounding both
      // slot memory and the per-advance scan) and admit the next pool
      // item. Every slot is terminal here, so clear() drops nothing
      // after admission.
      queue.clear();
      work = ResumableWork{};
      (void)queue.submit(work);
      stats = queue.advance(tess::AsyncWorkBudget{1});
      if (stats.invoked == 0) {
        return 0;
      }
    }
    return stats.items_done > 0 ? stats.items_done : 1;
  };
  // Quiesce the at-most-one in-flight ticket at window boundaries so
  // the window deltas are conservation-exact; never submits new work.
  auto drain = [&]() {
    while (queue.advance(tess::AsyncWorkBudget{64}).invoked > 0) {
    }
  };

  const auto results = run_saturated_budgets(options, accounting, reset,
                                             on_tick, quantum, drain);
  for (std::size_t i = 0; i < kBudgetsNs.size(); ++i) {
    // The queue's production-attached accounting is authoritative;
    // useful completions are retired tickets in the measured window
    // (the same section 9 basis as every other cell).
    budgeted::Artifact artifact =
        build_artifact(options, results[i], kBudgetsNs[i]);
    artifact.experiment.scenario_id = "resumable-work-64item-v1";
    artifact.experiment.workload_refs = {"scheduler/tick"};
    budgeted::Sha256 hasher;
    const char* tag = "resumable_splitmix_64item_256round_v1";
    hasher.update(tag, std::strlen(tag));
    artifact.trace.sha256 = hasher.hex_digest();
    SteadyClock clock;
    artifact.calibration = calibrate(clock);
    write_artifact(options, artifact, "resumable_work", kBudgetsNs[i]);
  }
}

}  // namespace

auto main(int argc, char** argv) -> int {
  RunOptions options;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--smoke") {
      options.warmup_frames = 5;
      options.measured_frames = 30;
      options.repetitions = 2;
      options.pool_size = 512;
      options.validation_requests = 16;
    } else if (argument == "--out-dir" && i + 1 < argc) {
      options.out_dir = argv[++i];
    } else if (argument == "--reps" && i + 1 < argc) {
      options.repetitions =
          static_cast<std::uint64_t>(std::strtoull(argv[++i], nullptr, 10));
    } else {
      fail(
          "usage: tess_bench_budgeted_progress --out-dir DIR "
          "[--smoke] [--reps N]");
    }
  }
  if (options.out_dir.empty()) {
    fail("--out-dir is required");
  }

  AstarCell astar_cell = build_astar_cell(options);
  validate_astar_cell(astar_cell, options.validation_requests);
  run_astar_cell(options, astar_cell);
  run_arrival_cells(options, astar_cell, budgeted::Pacing::Unpaced,
                    kArrivalRatesPerSimSecond, "astar_arrival_");
  run_arrival_cells(options, astar_cell, budgeted::Pacing::Paced,
                    kPacedArrivalRates, "astar_arrival_paced_");
  run_capacity_search(options, astar_cell);
  run_field_product_cell(options);
  run_resumable_cell(options);
  return 0;
}
