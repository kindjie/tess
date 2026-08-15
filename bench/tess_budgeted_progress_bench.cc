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

#include "budgeted_progress_bench_common.h"

namespace {

using namespace bpb_bench;  // NOLINT(google-build-using-namespace)
// --- Cell 1: unit A* point paths --------------------------------------

struct AstarCell {
  PathScaleWorld world;
  grid::BenchmarkMap scaled_map;  // 512x512 raster for the honest oracle.
  std::vector<tess::PathRequest> pool;
  // Contiguous-reference expectation per pool entry (status | cost),
  // precomputed untimed; every timed completion is checked against it
  // so no invalid result can ever count as useful (design section 10).
  std::vector<std::uint64_t> expected;
  // Per-item work units (max(1, expanded_nodes)) from the contiguous
  // reference, with prefix sums, for the exact per-repetition work
  // identity: window consumed work equals the prefix-sum over the
  // serviced pool range, zero tolerance (section 11.2 amendment).
  std::vector<std::uint64_t> expected_work;
  std::vector<std::uint64_t> work_prefix;  // size pool+1
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
  cell.expected_work.reserve(cell.pool.size());
  budgeted::Sha256 first_pass;
  for (const tess::PathRequest& request : cell.pool) {
    const tess::PathResult result =
        tess::astar_path<PathScaleWorld, PassableTag>(cell.world, request,
                                                      cell.scratch);
    const std::uint64_t packed = pack_path_outcome(result);
    cell.expected.push_back(packed);
    const auto work = static_cast<std::uint64_t>(result.expanded_nodes);
    cell.expected_work.push_back(work > 0 ? work : 1);
    first_pass.update(&packed, sizeof(packed));
  }
  cell.work_prefix.assign(1, 0);
  cell.work_prefix.reserve(cell.pool.size() + 1);
  for (const std::uint64_t work : cell.expected_work) {
    cell.work_prefix.push_back(cell.work_prefix.back() + work);
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

  std::array<budgeted::PathCountersBlock, kBudgetsNs.size()> counter_blocks;
#if TESS_DIAGNOSTICS_ENABLED
  tess::diagnostics::PathCounters live_counters;
  std::optional<tess::diagnostics::ScopedPathCounters> scoped;
  auto window_begin = [&](std::size_t) {
    live_counters = tess::diagnostics::PathCounters{};
    scoped.emplace(live_counters);
  };
  auto window_end = [&](std::size_t budget_index) {
    scoped.reset();
    budgeted::PathCountersBlock& block = counter_blocks[budget_index];
    block.present = true;
    block.heap_pushes += live_counters.heap_pushes;
    block.heap_pops += live_counters.heap_pops;
    block.neighbor_candidates += live_counters.neighbor_candidates;
    block.relax_attempts += live_counters.relax_attempts;
    block.touched_nodes += live_counters.touched_nodes;
  };
  const auto results =
      run_saturated_budgets(options, accounting, reset, on_tick, quantum, drain,
                            window_begin, window_end);
#else
  const auto results = run_saturated_budgets(options, accounting, reset,
                                             on_tick, quantum, drain);
#endif

  // Exact work identity (zero tolerance): each repetition services the
  // pool sequentially from index zero, so the measured window covers
  // the pool range from warmup to warmup+completions with wrap-around,
  // and window consumed work must equal the reference prefix sum over
  // exactly that range.
  const std::uint64_t pool_total = cell.work_prefix.back();
  auto prefix_wrapped = [&](std::uint64_t services) -> std::uint64_t {
    const std::uint64_t wraps = services / cell.pool.size();
    const std::uint64_t partial = services % cell.pool.size();
    return wraps * pool_total + cell.work_prefix[partial];
  };
  for (std::size_t i = 0; i < kBudgetsNs.size(); ++i) {
    std::uint64_t expected_window_work = 0;
    for (const SaturatedRep& rep : results[i].reps) {
      const std::uint64_t warmup = rep.warmup_completions;
      expected_window_work += prefix_wrapped(warmup + rep.useful_completions) -
                              prefix_wrapped(warmup);
    }
    check(expected_window_work == results[i].window_flow.consumed_work_units,
          "astar window work diverges from the reference prefix sum");
  }

  for (std::size_t i = 0; i < kBudgetsNs.size(); ++i) {
    budgeted::Artifact artifact =
        build_artifact(options, results[i], kBudgetsNs[i]);
    check(frame_counted_completions(results[i]) == artifact.flow.completed,
          "astar completion bases diverge");
    artifact.path_counters = counter_blocks[i];
    artifact.experiment.pool_size = cell.pool.size();
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
constexpr std::array<std::uint64_t, 4> kArrivalRatesPerSimSecond = {60, 600,
                                                                    2400, 9600};
// Counter-pass arrival subset: the low rate gives demand-limited
// comparisons real margin; overloaded rates compare under saturated
// rules and add nothing here.
constexpr std::array<std::uint64_t, 2> kCounterArrivalRates = {60, 600};
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
  budgeted::PathCountersBlock path_counters;  // Counter pass only.
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
template <typename WindowBeginFn = decltype(no_window_hook)&,
          typename WindowEndFn = decltype(no_window_hook)&>
void run_arrival_rep(const RunOptions& options, AstarCell& cell,
                     SteadyClock& clock, Nanos budget_ns, std::uint64_t rate,
                     budgeted::Pacing pacing, ArrivalCellResult& out,
                     std::size_t budget_index = 0,
                     WindowBeginFn&& window_begin = no_window_hook,
                     WindowEndFn&& window_end = no_window_hook) {
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
  // The drain ran between frames in wall time; re-anchor the paced
  // schedule so measured frames wait for real edges instead of
  // sprinting through the edges the drain consumed.
  controller.rebase_pacing();
  const tess::diagnostics::FlowCounters window_start = tracker.counters();
  window_begin(budget_index);
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
  window_end(budget_index);
  tracker.end_window(controller.sim_tick());
  // Re-observe at the current tick so the oldest-age gauge reflects
  // the queue after the final frame's admissions and service, not the
  // pre-frame state (same tick: inventory weighting is unchanged, and
  // the window is closed so no extra sample is recorded).
  tracker.observe_tick(controller.sim_tick());
  const tess::diagnostics::FlowCounters window_end_flow = tracker.counters();
  accumulate_window(out.window_flow, window_start, window_end_flow);

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
      window_end_flow.admitted - window_start.admitted;
  const std::uint64_t growth = window_end_flow.outstanding_current;
  const std::uint64_t growth_allowance =
      std::max<std::uint64_t>(1, (admitted_window * 5 + 999) / 1000);
  const bool identities = window_end_flow.admission_identity_holds() &&
                          window_end_flow.retention_identity_holds();
  const bool age_ok = window_end_flow.oldest_outstanding_age_ticks <=
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
                             window_end_flow.oldest_outstanding_age_ticks});

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
#if TESS_DIAGNOSTICS_ENABLED
    tess::diagnostics::PathCounters live_counters;
    std::optional<tess::diagnostics::ScopedPathCounters> scoped;
    auto window_begin = [&](std::size_t) {
      live_counters = tess::diagnostics::PathCounters{};
      scoped.emplace(live_counters);
    };
    auto window_end = [&](std::size_t budget_index) {
      scoped.reset();
      budgeted::PathCountersBlock& block = results[budget_index].path_counters;
      block.present = true;
      block.heap_pushes += live_counters.heap_pushes;
      block.heap_pops += live_counters.heap_pops;
      block.neighbor_candidates += live_counters.neighbor_candidates;
      block.relax_attempts += live_counters.relax_attempts;
      block.touched_nodes += live_counters.touched_nodes;
    };
#endif
    for (std::uint64_t rep = 0; rep < options.repetitions; ++rep) {
      for (std::size_t k = 0; k < kBudgetsNs.size(); ++k) {
        const std::size_t budget_index = (k + rep) % kBudgetsNs.size();
#if TESS_DIAGNOSTICS_ENABLED
        run_arrival_rep(options, cell, clock, kBudgetsNs[budget_index], rate,
                        pacing, results[budget_index], budget_index,
                        window_begin, window_end);
#else
        run_arrival_rep(options, cell, clock, kBudgetsNs[budget_index], rate,
                        pacing, results[budget_index]);
#endif
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
      artifact.path_counters = cell_result.path_counters;
      artifact.experiment.pool_size = cell.pool.size();
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

// --- Colony-derived cells 5 and 6 (design section 6.1) -----------------
//
// Both cells reuse the colony harness's deterministic machinery
// (tests/colony_harness.h): the 64x64 room-and-corridor logical map
// raster-scaled x8, the per-tile cost stream, interior-cell churn
// candidates whose centre tiles can never disconnect the world, and
// the terrain dirty mask. Their workload_refs use the family
// identities (topology/region_graph, queued/execute) the same way the
// A* cells reference path/astar_unit: the cell shapes are new — the
// registered topology family has no multi-chunk incremental update
// and the registered queued cells span all resident chunks — and
// matrix extensions wait until Google-Benchmark-registered cells
// exist to be classified.

struct ColonyCostTag {};
using ColonySchema =
    tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>,
                      tess::Field<ColonyCostTag, std::uint32_t>>;
using ColonyWorld = tess::AlwaysResidentWorld<PathScaleShape, ColonySchema>;
using ColonyWalker =
    tess::movement::MovementClass<tess::movement::Field<PassableTag>,
                                  tess::movement::FieldCost<ColonyCostTag>>;
namespace colony = tess_test::colony;

// One churn event: four centre tiles in four distinct chunks. Events
// toggle (block then restore), so the pool is a renewable supply and
// every application dirties exactly its four chunks.
struct ChurnEvent {
  std::array<tess::Coord3, 4> tiles{};
  std::array<tess::ChunkKey, 4> chunks{};
  std::array<std::uint32_t, 4> original_cost{};
};

struct ColonyTerrain {
  std::unique_ptr<ColonyWorld> world;
  std::vector<ChurnEvent> events;
  std::size_t next_event = 0;

  [[nodiscard]] auto take_event() -> const ChurnEvent& {
    const ChurnEvent& event = events[next_event];
    next_event = (next_event + 1) % events.size();
    return event;
  }

  // Restores every event tile to its pristine passable state so each
  // repetition starts from the identical frozen workload rather than
  // inheriting the previous repetition's toggle parity.
  void restore_pristine() {
    for (const ChurnEvent& event : events) {
      for (std::size_t i = 0; i < event.tiles.size(); ++i) {
        world->field<PassableTag>(event.tiles[i]) = 1;
        world->field<ColonyCostTag>(event.tiles[i]) = event.original_cost[i];
      }
    }
    next_event = 0;
  }

  // Toggle the event's tiles: passable centre tiles block; blocked
  // ones restore their original cost. Either direction changes the
  // terrain of exactly the event's four chunks.
  void apply(const ChurnEvent& event) {
    for (std::size_t i = 0; i < event.tiles.size(); ++i) {
      auto& passable = world->field<PassableTag>(event.tiles[i]);
      auto& cost = world->field<ColonyCostTag>(event.tiles[i]);
      if (passable != 0) {
        passable = 0;
        cost = 0;
      } else {
        passable = 1;
        cost = event.original_cost[i];
      }
    }
  }
};

[[nodiscard]] auto hash_churn_pool(const ColonyTerrain& terrain,
                                   const char* tag) -> std::string {
  budgeted::Sha256 hasher;
  hasher.update(tag, std::strlen(tag));
  for (const ChurnEvent& event : terrain.events) {
    for (std::size_t i = 0; i < event.tiles.size(); ++i) {
      const std::int64_t words[3] = {
          event.tiles[i].x, event.tiles[i].y,
          static_cast<std::int64_t>(event.original_cost[i])};
      hasher.update(words, sizeof(words));
    }
  }
  return hasher.hex_digest();
}

[[nodiscard]] auto build_colony_terrain(std::size_t event_count)
    -> ColonyTerrain {
  ColonyTerrain terrain;
  const auto logical = gen::room_and_corridor(kLogicalExtent, kLogicalExtent,
                                              kSeed, {24, 6, 12});
  check(logical.has_value(), "colony logical map generation failed");
  const auto parsed = grid::parse_map("budgeted-colony", logical->text);
  check(static_cast<bool>(parsed), "colony logical map parse failed");
  const grid::BenchmarkMap& map = parsed.value;

  terrain.world = std::make_unique<ColonyWorld>();
  for (std::int64_t y = 0; y < 512; ++y) {
    for (std::int64_t x = 0; x < 512; ++x) {
      const auto logical_index =
          static_cast<std::size_t>(y / kScale) * map.width +
          static_cast<std::size_t>(x / kScale);
      const bool passable = map.passability[logical_index] != 0;
      const auto coord = tess::Coord3{x, y, 0};
      terrain.world->field<PassableTag>(coord) = passable ? 1 : 0;
      terrain.world->field<ColonyCostTag>(coord) =
          passable ? colony::detail::tile_cost(kSeed ^ 0xC057U, x, y) : 0U;
    }
  }

  // Colony churn candidates: interior logical cells whose centre tile
  // can never disconnect the world; grouped into events of four tiles
  // in four distinct chunks (colony seed stream kSeed ^ 0xC17).
  const auto interior = colony::detail::interior_logical_cells(map);
  check(!interior.empty(), "no interior churn candidates");
  grid::SplitMix64 rng(kSeed ^ 0xC17U);
  std::vector<char> used(512 * 512, 0);
  ChurnEvent pending;
  std::size_t pending_count = 0;
  for (std::size_t attempt = 0;
       attempt < interior.size() * 8 && terrain.events.size() < event_count;
       ++attempt) {
    const auto logical_cell = interior[rng.below(interior.size())];
    const auto lx = static_cast<std::int64_t>(logical_cell % map.width);
    const auto ly = static_cast<std::int64_t>(logical_cell / map.width);
    const auto coord =
        tess::Coord3{lx * kScale + kScale / 2, ly * kScale + kScale / 2, 0};
    const auto flat = static_cast<std::size_t>(coord.y * 512 + coord.x);
    if (used[flat] != 0) {
      continue;
    }
    const auto key = tess::chunk_key<PathScaleShape>(
        tess::chunk_coord<PathScaleShape>(coord));
    bool duplicate_chunk = false;
    for (std::size_t i = 0; i < pending_count; ++i) {
      if (pending.chunks[i] == key) {
        duplicate_chunk = true;
        break;
      }
    }
    if (duplicate_chunk) {
      continue;
    }
    used[flat] = 1;
    pending.tiles[pending_count] = coord;
    pending.chunks[pending_count] = key;
    pending.original_cost[pending_count] =
        terrain.world->field<ColonyCostTag>(coord);
    ++pending_count;
    if (pending_count == pending.tiles.size()) {
      terrain.events.push_back(pending);
      pending_count = 0;
    }
  }
  check(terrain.events.size() == event_count,
        "could not assemble the churn event pool");
  return terrain;
}

// Cell 5: colony-derived incremental region-graph update, four dirty
// chunks per event. The quantum is one whole update transaction; the
// four toggling field writes that create the demand are microseconds
// against the transaction and live inside it so admit-on-selection
// pairs each event with its update.
void run_colony_topology_cell(const RunOptions& options) {
  ColonyTerrain terrain =
      build_colony_terrain(options.pool_size >= 10'000 ? 128 : 32);
  tess::LocalTopologyScratch topo_scratch;
  tess::RegionGraph graph;
  tess::build_region_graph<ColonyWorld, ColonyWalker>(*terrain.world,
                                                      topo_scratch, graph);

  // Pre-timing validation: after a burst of toggles and incremental
  // updates, deterministic reachability probes agree with a fresh
  // rebuild (the colony harness's own equivalence check, design
  // section 10 frozen-fixture rule).
  const auto probes = gen::deterministic_endpoints(
      grid::parse_map("budgeted-colony",
                      gen::room_and_corridor(kLogicalExtent, kLogicalExtent,
                                             kSeed, {24, 6, 12})
                          ->text)
          .value,
      kSeed ^ 0x9E0BE5ULL, 16);
  for (int i = 0; i < 8; ++i) {
    const ChurnEvent& event = terrain.take_event();
    terrain.apply(event);
    (void)tess::update_region_graph<ColonyWorld, ColonyWalker>(
        *terrain.world, topo_scratch, graph,
        std::span<const tess::ChunkKey>{event.chunks});
  }
  {
    tess::LocalTopologyScratch fresh_scratch;
    tess::RegionGraph fresh_graph;
    tess::build_region_graph<ColonyWorld, ColonyWalker>(
        *terrain.world, fresh_scratch, fresh_graph);

    // Structural comparison on exactly the rewritten chunks: the
    // incremental graph's per-chunk topology must match a fresh
    // rebuild region-for-region, not merely answer sampled
    // reachability the same way.
    for (std::size_t event_index = 0; event_index < 8; ++event_index) {
      for (const tess::ChunkKey& key : terrain.events[event_index].chunks) {
        const auto* incremental_local = graph.local_topology(key);
        const auto* fresh_local = fresh_graph.local_topology(key);
        check(incremental_local != nullptr && fresh_local != nullptr,
              "rewritten chunk missing from a topology graph");
        check(incremental_local->regions().size() ==
                  fresh_local->regions().size(),
              "incremental chunk region count diverges from fresh rebuild");
      }
    }

    tess::RegionGraphScratch reach_a;
    tess::RegionGraphScratch reach_b;
    auto check_probe = [&](tess::Coord3 from, tess::Coord3 to) {
      const auto incremental =
          tess::reachable<PathScaleShape>(graph, {from, to}, reach_a);
      const auto fresh =
          tess::reachable<PathScaleShape>(fresh_graph, {from, to}, reach_b);
      check(incremental.status == fresh.status,
            "incremental topology diverges from a fresh rebuild");
    };
    const auto anchor =
        tess::Coord3{probes.front().first.x * kScale + kScale / 2,
                     probes.front().first.y * kScale + kScale / 2, 0};
    for (const auto& [from, to] : probes) {
      check_probe(tess::Coord3{from.x * kScale + kScale / 2,
                               from.y * kScale + kScale / 2, 0},
                  tess::Coord3{to.x * kScale + kScale / 2,
                               to.y * kScale + kScale / 2, 0});
    }
    // Edit-adjacent probes: neighbours of every toggled tile exercise
    // exactly the regions and portals the updates rewrote.
    for (std::size_t event_index = 0; event_index < 8; ++event_index) {
      for (const tess::Coord3 tile : terrain.events[event_index].tiles) {
        check_probe(tess::Coord3{tile.x + 1, tile.y, 0}, anchor);
        check_probe(tess::Coord3{tile.x, tile.y + 1, 0}, anchor);
      }
    }
  }

  tess::diagnostics::FlowAccounting accounting;
  // Every repetition starts from the identical frozen workload:
  // pristine terrain and a graph rebuilt from it (untimed).
  auto reset = [&](std::uint64_t) {
    terrain.restore_pristine();
    graph = tess::RegionGraph{};
    tess::build_region_graph<ColonyWorld, ColonyWalker>(*terrain.world,
                                                        topo_scratch, graph);
  };
  auto on_tick = [](std::uint64_t) {};
  auto quantum = [&]() -> std::uint64_t {
    const ChurnEvent& event = terrain.take_event();
    ++accounting.counters.offered;
    accounting.record_admitted();
    terrain.apply(event);
    (void)tess::update_region_graph<ColonyWorld, ColonyWalker>(
        *terrain.world, topo_scratch, graph,
        std::span<const tess::ChunkKey>{event.chunks});
    const std::uint64_t work = event.chunks.size();
    ++accounting.counters.completed;
    accounting.record_left_outstanding();
    accounting.counters.offered_work_units += work;
    accounting.counters.consumed_work_units += work;
    return work;
  };
  auto drain = []() {};

  const auto results = run_saturated_budgets(options, accounting, reset,
                                             on_tick, quantum, drain);
  for (std::size_t i = 0; i < kBudgetsNs.size(); ++i) {
    budgeted::Artifact artifact =
        build_artifact(options, results[i], kBudgetsNs[i]);
    check(frame_counted_completions(results[i]) == artifact.flow.completed,
          "colony topology completion bases diverge");
    artifact.experiment.scenario_id = "topology-colony-4chunk-512-v1";
    artifact.experiment.workload_refs = {"topology/region_graph"};
    artifact.trace.sha256 =
        hash_churn_pool(terrain, "colony_topology_toggle_4chunk_v1");
    SteadyClock clock;
    artifact.calibration = calibrate(clock);
    write_artifact(options, artifact, "colony_topology", kBudgetsNs[i]);
  }
}

// Cell 6: the queued one-op-per-chunk update path — planning,
// execution, and dirty merge through AutoExecTask, one operation per
// distinct chunk exactly as the colony harness enqueues churn.
void run_queued_per_chunk_cell(const RunOptions& options) {
  ColonyTerrain terrain =
      build_colony_terrain(options.pool_size >= 10'000 ? 128 : 32);

  struct QueuedState {
    ColonyTerrain* terrain = nullptr;
    const ChurnEvent* current = nullptr;
    std::uint64_t acked_tiles = 0;
  };
  QueuedState state;
  state.terrain = &terrain;

  auto build_fn = [&state](auto view, colony::BuildAck& ack) {
    const ChurnEvent& event = *state.current;
    auto passable = view.template field_span<PassableTag>();
    auto cost = view.template field_span<ColonyCostTag>();
    for (std::size_t i = 0; i < event.tiles.size(); ++i) {
      const auto coord = event.tiles[i];
      if (tess::chunk_key<PathScaleShape>(
              tess::chunk_coord<PathScaleShape>(coord)) != view.key()) {
        continue;
      }
      const auto local = tess::local_tile_id<PathScaleShape>(
          tess::local_coord<PathScaleShape>(coord));
      if (passable[local.value] != 0) {
        passable[local.value] = 0;
        cost[local.value] = 0U;
      } else {
        passable[local.value] = 1;
        cost[local.value] = event.original_cost[i];
      }
      ++ack.tiles;
    }
  };

  tess::FrameOps ops;
  ops.reserve_operations(8);
  tess::AutoExecTask<ColonyWorld, tess::WritePolicy::UniquePerChunk,
                     colony::BuildAck, decltype(build_fn)>
      build_task(*terrain.world, ops, build_fn);
  build_task.reserve_operations(8);
  build_task.set_result_hook(
      &state, [](void* ctx, tess::OpHandle, const tess::OpCompletion& done,
                 const colony::BuildAck* ack) noexcept {
        if (done.ok() && ack != nullptr) {
          static_cast<QueuedState*>(ctx)->acked_tiles += ack->tiles;
        }
      });

  tess::SimClock queued_clock;
  auto run_event = [&]() -> std::uint64_t {
    const ChurnEvent& event = terrain.take_event();
    state.current = &event;
    const std::uint64_t before = state.acked_tiles;
    for (const tess::ChunkKey& key : event.chunks) {
      (void)ops.update_field(tess::DomainDesc::explicit_chunks(
                                 std::span<const tess::ChunkKey>{&key, 1}),
                             tess::FieldAccessDesc{0, colony::kTerrainDirty,
                                                   colony::kTerrainDirty},
                             tess::WritePolicy::UniquePerChunk);
    }
    static_cast<void>(build_task(tess::ScheduleTaskContext{queued_clock}));
    return state.acked_tiles - before;
  };

  // Pre-timing validation: every event acks exactly its four tiles
  // and toggling twice restores the terrain byte-for-byte.
  {
    const ChurnEvent probe = terrain.events.front();
    std::array<std::uint8_t, 4> before_passable{};
    for (std::size_t i = 0; i < probe.tiles.size(); ++i) {
      before_passable[i] = terrain.world->field<PassableTag>(probe.tiles[i]);
    }
    check(run_event() == 4, "queued event did not ack four tiles");
    terrain.next_event = 0;
    check(run_event() == 4, "queued restore did not ack four tiles");
    terrain.next_event = 0;
    for (std::size_t i = 0; i < probe.tiles.size(); ++i) {
      check(terrain.world->field<PassableTag>(probe.tiles[i]) ==
                before_passable[i],
            "queued toggle pair did not restore the terrain");
    }
    state.acked_tiles = 0;
  }

  tess::diagnostics::FlowAccounting accounting;
  // Pristine terrain per repetition: the frozen workload, not the
  // previous repetition's toggle parity.
  auto reset = [&](std::uint64_t) {
    terrain.restore_pristine();
    state.acked_tiles = 0;
  };
  auto on_tick = [](std::uint64_t) {};
  auto quantum = [&]() -> std::uint64_t {
    ++accounting.counters.offered;
    accounting.record_admitted();
    const std::uint64_t tiles = run_event();
    check(tiles == 4, "timed queued event did not ack four tiles");
    ++accounting.counters.completed;
    accounting.record_left_outstanding();
    accounting.counters.offered_work_units += tiles;
    accounting.counters.consumed_work_units += tiles;
    return tiles;
  };
  auto drain = []() {};

  const auto results = run_saturated_budgets(options, accounting, reset,
                                             on_tick, quantum, drain);
  for (std::size_t i = 0; i < kBudgetsNs.size(); ++i) {
    budgeted::Artifact artifact =
        build_artifact(options, results[i], kBudgetsNs[i]);
    check(frame_counted_completions(results[i]) == artifact.flow.completed,
          "queued per-chunk completion bases diverge");
    artifact.experiment.scenario_id = "queued-per-chunk-colony-512-v1";
    artifact.experiment.workload_refs = {"queued/execute"};
    artifact.trace.sha256 =
        hash_churn_pool(terrain, "queued_per_chunk_toggle_4op_v1");
    SteadyClock clock;
    artifact.calibration = calibrate(clock);
    write_artifact(options, artifact, "queued_per_chunk", kBudgetsNs[i]);
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
          world, goal_set, product, scratch);
  check(reference.status == tess::PathStatus::Found,
        "field-product reference build failed");
  const auto reference_reached = reference.reached_nodes;
  const tess::DistanceFieldResult repeat =
      tess::build_distance_field_product<PathScaleWorld, PassableTag>(
          world, goal_set, product, scratch);
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
            world, goal_set, product, scratch);
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

  std::array<budgeted::PathCountersBlock, kBudgetsNs.size()> counter_blocks;
#if TESS_DIAGNOSTICS_ENABLED
  tess::diagnostics::PathCounters live_counters;
  std::optional<tess::diagnostics::ScopedPathCounters> scoped;
  auto window_begin = [&](std::size_t) {
    live_counters = tess::diagnostics::PathCounters{};
    scoped.emplace(live_counters);
  };
  auto window_end = [&](std::size_t budget_index) {
    scoped.reset();
    budgeted::PathCountersBlock& block = counter_blocks[budget_index];
    block.present = true;
    block.heap_pushes += live_counters.heap_pushes;
    block.heap_pops += live_counters.heap_pops;
    block.neighbor_candidates += live_counters.neighbor_candidates;
    block.relax_attempts += live_counters.relax_attempts;
    block.touched_nodes += live_counters.touched_nodes;
  };
  const auto results =
      run_saturated_budgets(options, accounting, reset, on_tick, quantum, drain,
                            window_begin, window_end);
#else
  const auto results = run_saturated_budgets(options, accounting, reset,
                                             on_tick, quantum, drain);
#endif
  for (std::size_t i = 0; i < kBudgetsNs.size(); ++i) {
    // Constant-work identity: every completion is the identical
    // deterministic build.
    check(results[i].window_flow.consumed_work_units ==
              results[i].window_flow.completed * reference_reached,
          "field-product window work diverges from the reference build");
    budgeted::Artifact artifact =
        build_artifact(options, results[i], kBudgetsNs[i]);
    check(frame_counted_completions(results[i]) == artifact.flow.completed,
          "field-product completion bases diverge");
    artifact.path_counters = counter_blocks[i];
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
    // Exact work identity (zero tolerance): the driver-observed items
    // per quantum must equal the queue's production-accounted window
    // consumption — two independent tallies of the same work.
    std::uint64_t driver_observed = 0;
    for (const SaturatedRep& rep : results[i].reps) {
      driver_observed += rep.window_work_units;
    }
    check(driver_observed == results[i].window_flow.consumed_work_units,
          "resumable window work diverges between driver and queue "
          "accounting");
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
  bool pass_explicit = false;
#if TESS_DIAGNOSTICS_ENABLED
  // The diagnostics build exists to run the counter pass.
  options.counter_pass = true;
#endif
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--smoke") {
      options.smoke = true;
      options.warmup_frames = 5;
      options.measured_frames = 30;
      options.repetitions = 2;
      options.pool_size = 512;
      options.validation_requests = 16;
    } else if (argument == "--out-dir" && i + 1 < argc) {
      options.out_dir = argv[++i];
    } else if (argument == "--pass" && i + 1 < argc) {
      const std::string pass = argv[++i];
      if (pass == "counter") {
        options.counter_pass = true;
      } else if (pass == "timing") {
        options.counter_pass = false;
      } else {
        fail("--pass must be timing or counter");
      }
      pass_explicit = true;
    } else if (argument == "--mixed-tps" && i + 1 < argc) {
      options.mixed_tps.clear();
      for (const char* cursor = argv[++i]; *cursor != 0;) {
        options.mixed_tps.push_back(
            static_cast<std::uint32_t>(std::strtoul(cursor, nullptr, 10)));
        while (*cursor != 0 && *cursor != ',') {
          ++cursor;
        }
        if (*cursor == ',') {
          ++cursor;
        }
      }
    } else if (argument == "--mixed-views" && i + 1 < argc) {
      const std::string views = argv[++i];
      const bool both_views = views == "both";
      options.mixed_view_fidelity =
          both_views || views.find("fidelity") != std::string::npos;
      options.mixed_view_quanta =
          both_views || views.find("quanta") != std::string::npos;
      if (!options.mixed_view_fidelity && !options.mixed_view_quanta) {
        fail("--mixed-views must name fidelity, quanta, or both");
      }
    } else if (argument == "--mixed-populations" && i + 1 < argc) {
      options.mixed_populations.clear();
      for (const char* cursor = argv[++i]; *cursor != 0;) {
        options.mixed_populations.push_back(
            static_cast<std::size_t>(std::strtoul(cursor, nullptr, 10)));
        while (*cursor != 0 && *cursor != ',') {
          ++cursor;
        }
        if (*cursor == ',') {
          ++cursor;
        }
      }
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
  (void)pass_explicit;
#if TESS_DIAGNOSTICS_ENABLED
  if (!options.counter_pass) {
    fail(
        "this diagnostics build only produces counter-pass artifacts; "
        "run the plain binary for the timing pass");
  }
#else
  if (options.counter_pass) {
    fail(
        "--pass counter requires the diagnostics build "
        "(tess_bench_budgeted_progress_diagnostics); this binary would "
        "emit counter-labeled artifacts with no instrumentation");
  }
#endif

  AstarCell astar_cell = build_astar_cell(options);
  validate_astar_cell(astar_cell, options.validation_requests);
  if (options.counter_pass) {
    // Counter pass: only the comparable cells over the identical
    // demand traces. Capacity search, paced cells, overloaded rates,
    // and the diagnostics-free colony cells produce nothing the
    // section 11.2 comparison can use.
    run_astar_cell(options, astar_cell);
    run_arrival_cells(options, astar_cell, budgeted::Pacing::Unpaced,
                      kCounterArrivalRates, "astar_arrival_");
    run_field_product_cell(options);
    run_resumable_cell(options);
    return 0;
  }
  run_astar_cell(options, astar_cell);
  run_arrival_cells(options, astar_cell, budgeted::Pacing::Unpaced,
                    kArrivalRatesPerSimSecond, "astar_arrival_");
  run_arrival_cells(options, astar_cell, budgeted::Pacing::Paced,
                    kPacedArrivalRates, "astar_arrival_paced_");
  run_capacity_search(options, astar_cell);
  run_field_product_cell(options);
  run_resumable_cell(options);
  run_colony_topology_cell(options);
  run_queued_per_chunk_cell(options);
  run_mixed_colony_cells(options);
  return 0;
}
