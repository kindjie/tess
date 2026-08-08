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
// Field products are built through the direct build API each
// iteration — the product cache is never involved, so every
// completion is a real build, not a cache hit. The pool is inventory,
// not admitted flow: items are offered and admitted at selection, and
// pool wrap-around re-services are new admissions.

#include <sys/resource.h>
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

#include "budgeted_progress_artifact.h"
#include "budgeted_progress_clock.h"
#include "budgeted_progress_controller.h"
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

[[nodiscard]] auto peak_rss_bytes() -> std::uint64_t {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) {
    return 0;
  }
#if defined(__APPLE__)
  return static_cast<std::uint64_t>(usage.ru_maxrss);  // Bytes on macOS.
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024;  // KiB on Linux.
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

// Accumulates one saturated repetition's frame samples plus slim
// per-completion accounting (admit-on-selection: offered == admitted
// == terminal, outstanding returns to zero every quantum).
struct SaturatedRep {
  std::vector<std::uint64_t> frame_elapsed_ns;
  std::vector<std::uint64_t> overshoot_quantum_tail_ns;
  std::vector<std::uint64_t> overshoot_mandatory_ns;
  std::uint64_t overshoot_frames = 0;
  std::uint64_t useful_completions = 0;
};

struct SaturatedCellResult {
  tess::diagnostics::FlowAccounting accounting;
  std::vector<SaturatedRep> reps;
  std::uint64_t peak_rss = 0;
};

// Runs `measured` frames after `warmup`, with `quantum` returning the
// number of work units consumed by one completion (0 = no work).
template <typename ResetFn, typename QuantumFn, typename TickFn>
[[nodiscard]] auto run_saturated_cell(const RunOptions& options,
                                      Nanos budget_ns, ResetFn&& reset,
                                      TickFn&& on_tick, QuantumFn&& quantum)
    -> SaturatedCellResult {
  SaturatedCellResult result;
  SteadyClock clock;
  for (std::uint64_t rep = 0; rep < options.repetitions; ++rep) {
    reset(rep);
    FrameBudgetConfig config;
    config.budget_ns = budget_ns;
    config.base_tps = 60;  // One tick per frame keeps ticks flowing.
    FrameBudgetController controller{clock, config};
    SaturatedRep samples;
    samples.frame_elapsed_ns.reserve(options.measured_frames);
    samples.overshoot_quantum_tail_ns.reserve(options.measured_frames);
    samples.overshoot_mandatory_ns.reserve(options.measured_frames);

    for (std::uint64_t frame = 0;
         frame < options.warmup_frames + options.measured_frames; ++frame) {
      const bool measured = frame >= options.warmup_frames;
      auto mandatory = [&](std::uint64_t tick) { on_tick(tick); };
      std::uint64_t completions = 0;
      auto quantum_fn = [&]() -> bool {
        const std::uint64_t work = quantum(result.accounting);
        if (work == 0) {
          return false;
        }
        ++completions;
        return true;
      };
      const FrameRecord record = controller.run_frame(mandatory, quantum_fn);
      if (measured) {
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
    }
    result.reps.push_back(std::move(samples));
    // Sampled at repetition boundaries, outside any timed frame.
    result.peak_rss = std::max(result.peak_rss, peak_rss_bytes());
  }
  return result;
}

[[nodiscard]] auto build_artifact(const RunOptions& options,
                                  const SaturatedCellResult& cell,
                                  Nanos budget_ns) -> budgeted::Artifact {
  budgeted::Artifact artifact;
  const char* commit = std::getenv("GITHUB_SHA");
  artifact.run.commit = commit != nullptr ? commit : "local";
  artifact.run.machine_fingerprint = "local-uncontrolled";
  artifact.run.compiler =
#if defined(__clang__)
      "clang " __clang_version__;
#elif defined(__GNUC__)
      "gcc";
#else
      "unknown";
#endif
  artifact.run.bench_flags = "";

  artifact.experiment.kind = "isolated_saturated";
  artifact.experiment.seed = kSeed;
  artifact.experiment.sim_tps = 60;
  artifact.experiment.pacing = "unpaced";
  artifact.experiment.budget_ns = budget_ns;
  artifact.experiment.settlement_ticks = 0;

  artifact.flow = cell.accounting.counters;

  auto& summary = artifact.summary;
  summary.measured_frames = options.measured_frames;
  summary.repetitions = options.repetitions;
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
    summary.useful_completions += rep.useful_completions;
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
  summary.consumed_work_units = cell.accounting.counters.consumed_work_units;
  summary.peak_rss_bytes = cell.peak_rss;
  return artifact;
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
  std::string pool_sha256;
  tess::PathScratch scratch;
  std::size_t next = 0;
};

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

// Pre-timing correctness: the first K pool requests against the
// independent Dijkstra oracle on the scaled map, and a repeated
// contiguous pass to pin determinism.
void validate_astar_cell(AstarCell& cell, std::size_t count) {
  budgeted::Sha256 first_pass;
  budgeted::Sha256 second_pass;
  for (int pass = 0; pass < 2; ++pass) {
    budgeted::Sha256& hasher = pass == 0 ? first_pass : second_pass;
    for (std::size_t i = 0; i < count && i < cell.pool.size(); ++i) {
      const tess::PathRequest& request = cell.pool[i];
      const tess::PathResult result =
          tess::astar_path<PathScaleWorld, PassableTag>(cell.world, request,
                                                        cell.scratch);
      const auto reference =
          grid::reference_cost(cell.scaled_map, request.start, request.goal,
                               grid::ReferenceMovement::Orthogonal);
      if (pass == 0) {
        if (reference.has_value()) {
          check(result.status == tess::PathStatus::Found,
                "oracle found a path the search missed");
          check(result.cost == *reference, "path cost diverges from oracle");
        } else {
          check(result.status != tess::PathStatus::Found,
                "search found a path the oracle rejects");
        }
      }
      const std::uint64_t words[2] = {static_cast<std::uint64_t>(result.status),
                                      result.cost};
      hasher.update(words, sizeof(words));
    }
  }
  check(first_pass.hex_digest() == second_pass.hex_digest(),
        "contiguous reference not deterministic");
}

void run_astar_cell(const RunOptions& options) {
  AstarCell cell = build_astar_cell(options);
  validate_astar_cell(cell, options.validation_requests);

  for (const Nanos budget : kBudgetsNs) {
    auto reset = [&cell](std::uint64_t) { cell.next = 0; };
    auto on_tick = [](std::uint64_t) {};
    auto quantum =
        [&cell](
            tess::diagnostics::FlowAccounting& accounting) -> std::uint64_t {
      const tess::PathRequest& request = cell.pool[cell.next];
      cell.next = (cell.next + 1) % cell.pool.size();
      // Admit-on-selection: offer, admit, and resolve around one call.
      ++accounting.counters.offered;
      accounting.record_admitted();
      const tess::PathResult result =
          tess::astar_path<PathScaleWorld, PassableTag>(cell.world, request,
                                                        cell.scratch);
      const auto work = static_cast<std::uint64_t>(result.expanded_nodes);
      ++accounting.counters.completed;
      accounting.record_left_outstanding();
      accounting.counters.offered_work_units += work;
      accounting.counters.consumed_work_units += work;
      return work > 0 ? work : 1;
    };
    SaturatedCellResult result =
        run_saturated_cell(options, budget, reset, on_tick, quantum);
    budgeted::Artifact artifact = build_artifact(options, result, budget);
    artifact.experiment.scenario_id = "astar-unit-roomcorridor-512-v1";
    artifact.experiment.workload_refs = {"path/astar_unit"};
    artifact.trace.sha256 = cell.pool_sha256;
    SteadyClock clock;
    artifact.calibration = calibrate(clock);
    write_artifact(options, artifact, "astar_unit", budget);
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

  for (const Nanos budget : kBudgetsNs) {
    auto reset = [](std::uint64_t) {};
    auto on_tick = [](std::uint64_t) {};
    auto quantum =
        [&](tess::diagnostics::FlowAccounting& accounting) -> std::uint64_t {
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
    SaturatedCellResult result =
        run_saturated_cell(options, budget, reset, on_tick, quantum);
    budgeted::Artifact artifact = build_artifact(options, result, budget);
    artifact.experiment.scenario_id = "field-product-8goal-roomportals-512-v1";
    artifact.experiment.workload_refs = {"path/field_product"};
    artifact.trace.sha256 = trace_hash;
    SteadyClock clock;
    artifact.calibration = calibrate(clock);
    write_artifact(options, artifact, "field_product", budget);
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

void run_resumable_cell(const RunOptions& options) {
  for (const Nanos budget : kBudgetsNs) {
    tess::ResumableWorkQueue<std::uint64_t> queue;
    tess::diagnostics::FlowAccounting accounting;
    ResumableWork work;

    auto reset = [&](std::uint64_t rep) {
      queue.clear();
      if (rep == 0) {
        queue.set_flow_accounting(&accounting);
      }
      queue.reserve_tickets(1024);
      work = ResumableWork{};
    };
    auto on_tick = [&](std::uint64_t tick) { queue.observe_flow_tick(tick); };
    // One quantum = one advance at the finest canonical budget; when
    // the current ticket retires, the pool "wraps": submitting the
    // next ticket is a new admission at selection time.
    auto quantum = [&](tess::diagnostics::FlowAccounting&) -> std::uint64_t {
      tess::AsyncAdvanceStats stats = queue.advance(tess::AsyncWorkBudget{1});
      if (stats.invoked == 0) {
        // All tickets terminal: drop the retired slots (bounding both
        // slot memory and the per-advance scan) and admit the next
        // pool item. Every slot is terminal here, so clear() drops
        // nothing after admission.
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

    SaturatedCellResult result =
        run_saturated_cell(options, budget, reset, on_tick, quantum);
    // The queue's own attached accounting is authoritative here.
    result.accounting = accounting;
    budgeted::Artifact artifact = build_artifact(options, result, budget);
    // Completions for this cell are retired tickets, not advances.
    artifact.summary.useful_completions = accounting.counters.completed;
    artifact.experiment.scenario_id = "resumable-work-64item-v1";
    artifact.experiment.workload_refs = {"scheduler/tick"};
    budgeted::Sha256 hasher;
    const char* tag = "resumable_splitmix_64item_256round_v1";
    hasher.update(tag, std::strlen(tag));
    artifact.trace.sha256 = hasher.hex_digest();
    SteadyClock clock;
    artifact.calibration = calibrate(clock);
    write_artifact(options, artifact, "resumable_work", budget);
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

  run_astar_cell(options);
  run_field_product_cell(options);
  run_resumable_cell(options);
  return 0;
}
