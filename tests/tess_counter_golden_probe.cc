// Deterministic counter-golden probe (redesign section 3.3, shadow mode).
//
// Runs fixed, serial-only workloads under scoped diagnostics counter
// sinks and emits the observed PathCounters/QueuedPhaseCounters values
// as JSON to the path in argv[1]. tools/check_counter_goldens.py
// compares that output against tests/goldens/counters.json; an
// intentional behavior change regenerates the golden in the same pull
// request with the checker's --update flag.
//
// Serial-only by design: counters are recorded through thread-local
// sink pointers installed by scoped installers, and pool workers do not
// aggregate into the caller's sink, so a pooled run would under-count
// rather than merely vary. Every workload asserts its functional
// outcome and the probe exits nonzero on any surprise, so a golden diff
// can never hide a broken workload. The serpentine fixture replicates
// the tests/path_test_util.h recipe (two two-gap walls with gaps at
// opposite ends) so the Found result can only come from the real heap
// search loop, without pulling the gtest-dependent fixture header into
// this gtest-free executable.

#include <tess/diagnostics/diagnostics.h>
#include <tess/tess.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <vector>

namespace {

namespace mv = tess::movement;

struct PassableTag {};
struct CostTag {};
struct TerrainTag {};

constexpr std::uint32_t kDirtyTerrain = 1u << 0u;

using PathSchema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                     tess::Field<CostTag, std::uint32_t>>;
using QueueSchema = tess::FieldSchema<tess::Field<TerrainTag, std::uint16_t>>;
using TopDown2D = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{4, 4, 1}>;
using PathWorld = tess::AlwaysResidentWorld<TopDown2D, PathSchema>;
using QueueWorld = tess::AlwaysResidentWorld<TopDown2D, QueueSchema>;
using Walker =
    mv::MovementClass<mv::Field<PassableTag>, mv::FieldCost<CostTag>>;

// Functional pins for the fixed serpentine fixture. The optimal route
// (through the x = 2 gap at high y, back down to the x = 5 gap at low
// y, then up to the goal) is a constant of the fixture; a change here
// is a behavior change the golden diff must not hide.
constexpr std::uint32_t kSerpentineCost = 24;
constexpr std::size_t kSerpentinePathNodes = 25;

[[noreturn]] void fail(const char* workload, const char* what) {
  std::fprintf(stderr, "counter probe failed: %s: %s\n", workload, what);
  std::exit(EXIT_FAILURE);
}

void check(bool condition, const char* workload, const char* what) {
  if (!condition) {
    fail(workload, what);
  }
}

void check_eq(unsigned long long actual, unsigned long long expected,
              const char* workload, const char* what) {
  if (actual != expected) {
    std::fprintf(stderr,
                 "counter probe failed: %s: %s: expected %llu got %llu\n",
                 workload, what, expected, actual);
    std::exit(EXIT_FAILURE);
  }
}

struct Endpoints {
  tess::Coord3 start{};
  tess::Coord3 goal{};
};

auto build_serpentine(PathWorld& world) -> Endpoints {
  for (std::int64_t y = 0; y < 8; ++y) {
    for (std::int64_t x = 0; x < 8; ++x) {
      world.field<PassableTag>(tess::Coord3{x, y, 0}) = true;
      world.field<CostTag>(tess::Coord3{x, y, 0}) = 1;
    }
  }
  // Wall at x = 2 with gaps at y = 6, 7; wall at x = 5 with gaps at
  // y = 0, 1 — gaps at opposite ends defeat every pre-A* fast path.
  for (std::int64_t y = 0; y < 8; ++y) {
    if (y != 6 && y != 7) {
      world.field<PassableTag>(tess::Coord3{2, y, 0}) = false;
    }
    if (y != 0 && y != 1) {
      world.field<PassableTag>(tess::Coord3{5, y, 0}) = false;
    }
  }
  return Endpoints{tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}};
}

void emit_path_counters(std::FILE* out,
                        const tess::diagnostics::PathCounters& counters) {
  std::fprintf(
      out,
      "{\"scratch_clear_calls\": %llu, \"scratch_clear_nodes\": %llu, "
      "\"initializations\": %llu, \"start_passability_checks\": %llu, "
      "\"goal_passability_checks\": %llu, \"heap_pushes\": %llu, "
      "\"heap_pops\": %llu, \"stale_pops\": %llu, \"closed_pops\": %llu, "
      "\"neighbor_candidates\": %llu, \"passability_checks\": %llu, "
      "\"cost_reads\": %llu, \"blocked_neighbors\": %llu, "
      "\"closed_neighbors\": %llu, \"relax_attempts\": %llu, "
      "\"relax_successes\": %llu, \"touched_nodes\": %llu, "
      "\"heuristic_calls\": %llu, \"reconstructed_nodes\": %llu}",
      static_cast<unsigned long long>(counters.scratch_clear_calls),
      static_cast<unsigned long long>(counters.scratch_clear_nodes),
      static_cast<unsigned long long>(counters.initializations),
      static_cast<unsigned long long>(counters.start_passability_checks),
      static_cast<unsigned long long>(counters.goal_passability_checks),
      static_cast<unsigned long long>(counters.heap_pushes),
      static_cast<unsigned long long>(counters.heap_pops),
      static_cast<unsigned long long>(counters.stale_pops),
      static_cast<unsigned long long>(counters.closed_pops),
      static_cast<unsigned long long>(counters.neighbor_candidates),
      static_cast<unsigned long long>(counters.passability_checks),
      static_cast<unsigned long long>(counters.cost_reads),
      static_cast<unsigned long long>(counters.blocked_neighbors),
      static_cast<unsigned long long>(counters.closed_neighbors),
      static_cast<unsigned long long>(counters.relax_attempts),
      static_cast<unsigned long long>(counters.relax_successes),
      static_cast<unsigned long long>(counters.touched_nodes),
      static_cast<unsigned long long>(counters.heuristic_calls),
      static_cast<unsigned long long>(counters.reconstructed_nodes));
}

void emit_queued_counters(
    std::FILE* out, const tess::diagnostics::QueuedPhaseCounters& counters) {
  std::fprintf(
      out,
      "{\"phase_calls\": %llu, \"phase_operations\": %llu, "
      "\"phase_invalid_ranges\": %llu, \"phase_failures\": %llu, "
      "\"partitioned_phase_calls\": %llu, \"dirty_partitions\": %llu, "
      "\"scoped_thread_calls\": %llu, \"scoped_thread_workers\": %llu, "
      "\"worker_pool_calls\": %llu, \"worker_pool_workers\": %llu, "
      "\"dirty_records_collected\": %llu, \"dirty_chunks_merged\": %llu}",
      static_cast<unsigned long long>(counters.phase_calls),
      static_cast<unsigned long long>(counters.phase_operations),
      static_cast<unsigned long long>(counters.phase_invalid_ranges),
      static_cast<unsigned long long>(counters.phase_failures),
      static_cast<unsigned long long>(counters.partitioned_phase_calls),
      static_cast<unsigned long long>(counters.dirty_partitions),
      static_cast<unsigned long long>(counters.scoped_thread_calls),
      static_cast<unsigned long long>(counters.scoped_thread_workers),
      static_cast<unsigned long long>(counters.worker_pool_calls),
      static_cast<unsigned long long>(counters.worker_pool_workers),
      static_cast<unsigned long long>(counters.dirty_records_collected),
      static_cast<unsigned long long>(counters.dirty_chunks_merged));
}

auto run_astar_serpentine() -> tess::diagnostics::PathCounters {
  PathWorld world;
  const auto endpoints = build_serpentine(world);
  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::diagnostics::PathCounters counters;
  {
    tess::diagnostics::ScopedPathCounters scope{counters};
    const auto result = tess::astar_path<PathWorld, PassableTag>(
        world, tess::PathRequest{endpoints.start, endpoints.goal}, scratch);
    check(result.status == tess::PathStatus::Found, "astar_serpentine",
          "path not found");
    check_eq(result.cost, kSerpentineCost, "astar_serpentine", "path cost");
    check_eq(result.path.size(), kSerpentinePathNodes, "astar_serpentine",
             "path node count");
  }
  check(counters.heap_pushes > 0, "astar_serpentine",
        "fast path answered the maze; strengthen the fixture");
  return counters;
}

auto run_weighted_serpentine() -> tess::diagnostics::PathCounters {
  PathWorld world;
  const auto endpoints = build_serpentine(world);
  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::diagnostics::PathCounters counters;
  {
    tess::diagnostics::ScopedPathCounters scope{counters};
    const auto result =
        tess::weighted_astar_path<PathWorld, PassableTag, CostTag>(
            world, tess::PathRequest{endpoints.start, endpoints.goal}, scratch);
    check(result.status == tess::PathStatus::Found, "weighted_serpentine",
          "path not found");
    check_eq(result.cost, kSerpentineCost, "weighted_serpentine",
             "weighted cost (uniform entry costs)");
  }
  check(counters.heap_pushes > 0, "weighted_serpentine",
        "fast path answered the maze; strengthen the fixture");
  return counters;
}

auto run_unit_product_replay() -> tess::diagnostics::PathCounters {
  PathWorld world;
  (void)build_serpentine(world);
  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(64);
  tess::GoalSet goals;
  goals.add(tess::Coord3{7, 7, 0});
  tess::DistanceFieldProduct product;
  tess::diagnostics::PathCounters counters;
  {
    tess::diagnostics::ScopedPathCounters scope{counters};
    const auto build =
        tess::build_distance_field_product<PathWorld, PassableTag>(
            world, goals, scratch, product);
    check(build.status == tess::PathStatus::Found, "unit_product_replay",
          "product build failed");
    const auto replay =
        tess::distance_field_product_path<PathWorld, PassableTag>(
            world, tess::Coord3{0, 0, 0}, product, scratch);
    check(replay.status == tess::PathStatus::Found, "unit_product_replay",
          "replay failed");
    check_eq(replay.cost, kSerpentineCost, "unit_product_replay",
             "replay cost");
    check_eq(replay.path.size(), kSerpentinePathNodes, "unit_product_replay",
             "replay path node count");
  }
  return counters;
}

// The weighted product exercises the entry-cost read path
// (PathCounters::cost_reads stays zero on unit searches).
auto run_weighted_product_nearest() -> tess::diagnostics::PathCounters {
  PathWorld world;
  (void)build_serpentine(world);
  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(64);
  tess::GoalSet goals;
  goals.add(tess::Coord3{7, 7, 0});
  tess::DistanceFieldProduct product;
  tess::diagnostics::PathCounters counters;
  {
    tess::diagnostics::ScopedPathCounters scope{counters};
    const auto build =
        tess::build_weighted_distance_field_product<PathWorld, Walker>(
            world, goals, scratch, product);
    check(build.status == tess::PathStatus::Found, "weighted_product_nearest",
          "product build failed");
    const auto nearest = tess::weighted_nearest_target<PathWorld, Walker>(
        world, tess::Coord3{0, 0, 0}, product, scratch);
    check(nearest.status == tess::PathStatus::Found, "weighted_product_nearest",
          "nearest-target failed");
    check(nearest.target == (tess::Coord3{7, 7, 0}), "weighted_product_nearest",
          "wrong nearest target");
    check_eq(nearest.cost, kSerpentineCost, "weighted_product_nearest",
             "nearest cost");
  }
  check(counters.cost_reads > 0, "weighted_product_nearest",
        "weighted build read no entry costs");
  return counters;
}

auto run_queued_serial_update() -> tess::diagnostics::QueuedPhaseCounters {
  QueueWorld world;
  tess::FrameOps ops;
  tess::PlannedPhaseExecutionScratch scratch;
  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      kDirtyTerrain,
      kDirtyTerrain,
  };
  const std::vector<tess::ChunkKey> first_keys{tess::ChunkKey{0}};
  const std::vector<tess::ChunkKey> second_keys{tess::ChunkKey{1}};
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(first_keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(second_keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);
  check(report.ok(), "queued_serial_update", "planning failed");
  const auto phases = tess::plan_parallel_execution_phases(report.plan());
  check(phases.ok(), "queued_serial_update", "phase planning failed");
  check(phases.phases().size() == 1, "queued_serial_update",
        "unexpected phase count");

  tess::diagnostics::QueuedPhaseCounters counters;
  {
    tess::diagnostics::ScopedQueuedPhaseCounters scope{counters};
    const auto result = tess::execute_phase_partitioned_dirty_with<
        tess::WritePolicy::UniquePerChunk>(
        tess::SerialPhaseExecutor{}, world, report.plan(), phases.phases()[0],
        scratch, [](auto view) {
          auto terrain = view.template field_span<TerrainTag>();
          terrain[0] = static_cast<std::uint16_t>(view.key().value + 3);
        });
    check(result.status == tess::PlannedExecutionStatus::Executed,
          "queued_serial_update", "execution failed");
    const auto merged = tess::merge_planned_dirty(world, scratch);
    check(merged.status == tess::PlannedDirtyMergeStatus::Merged,
          "queued_serial_update", "dirty merge failed");
    check_eq(merged.merged_chunk_count, 2, "queued_serial_update",
             "merged chunk count");
  }
  check_eq(world.field<TerrainTag>(tess::Coord3{0, 0, 0}), 3,
           "queued_serial_update", "first chunk write");
  check_eq(world.dirty_flags(tess::ChunkKey{0}), kDirtyTerrain,
           "queued_serial_update", "first chunk dirty flags");
  check_eq(world.meta(tess::ChunkKey{0}).version, 1, "queued_serial_update",
           "first chunk version");
  return counters;
}

}  // namespace

auto main(int argc, char** argv) -> int {
  try {
    if (argc != 2) {
      std::fprintf(stderr, "usage: %s <observed.json>\n", argv[0]);
      return EXIT_FAILURE;
    }
    std::FILE* out = std::fopen(argv[1], "wb");
    if (out == nullptr) {
      std::fprintf(stderr, "cannot open %s\n", argv[1]);
      return EXIT_FAILURE;
    }

    const auto astar = run_astar_serpentine();
    const auto weighted = run_weighted_serpentine();
    const auto unit_product = run_unit_product_replay();
    const auto weighted_product = run_weighted_product_nearest();
    const auto queued = run_queued_serial_update();

    std::fprintf(out, "{\"schema\": 1, \"workloads\": {");
    std::fprintf(out, "\"astar_serpentine\": {\"path\": ");
    emit_path_counters(out, astar);
    std::fprintf(out, "}, \"weighted_serpentine\": {\"path\": ");
    emit_path_counters(out, weighted);
    std::fprintf(out, "}, \"unit_product_replay\": {\"path\": ");
    emit_path_counters(out, unit_product);
    std::fprintf(out, "}, \"weighted_product_nearest\": {\"path\": ");
    emit_path_counters(out, weighted_product);
    std::fprintf(out, "}, \"queued_serial_update\": {\"queued\": ");
    emit_queued_counters(out, queued);
    std::fprintf(out, "}}}\n");
    if (std::fclose(out) != 0) {
      std::fprintf(stderr, "cannot finish writing %s\n", argv[1]);
      return EXIT_FAILURE;
    }
  } catch (const std::exception& error) {
    std::fprintf(stderr, "counter probe failed: %s\n", error.what());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
