#include <benchmark/benchmark.h>
#include <tess/tess.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

// Correctness checks mandated by docs/planning/benchmark-plan.md run outside
// the timed regions; a failed check aborts the benchmark binary so threshold
// runs cannot silently gate on wrong results.
void bench_check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "tess_bench correctness check failed: %s\n", message);
    std::abort();
  }
}

using TopoShape =
    tess::Shape<tess::Extent3{512, 512, 1}, tess::Extent3{32, 32, 1}>;

struct PassableTag {};
using TopoSchema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>>;
using TopoWorld = tess::AlwaysResidentWorld<TopoShape, TopoSchema>;

void fill_passable(TopoWorld& world, std::uint8_t value) {
  for (auto& page : world.chunks()) {
    auto field = page.template field_span<PassableTag>();
    for (auto& tile : field) {
      tile = value;
    }
  }
}

// Seals a single passable tile behind its four orthogonal neighbours, making it
// an isolated region. The barrier is not a full-axis wall, so A*'s cheap
// fast-path cannot rule it out -- reaching the same verdict requires flooding
// the whole reachable component, which is exactly what the topology precheck
// short-circuits.
void enclose(TopoWorld& world, tess::Coord3 center) {
  const tess::Coord3 neighbours[] = {
      {center.x - 1, center.y, center.z},
      {center.x + 1, center.y, center.z},
      {center.x, center.y - 1, center.z},
      {center.x, center.y + 1, center.z},
  };
  for (const auto n : neighbours) {
    world.field<PassableTag>(n) = 0;
  }
}

constexpr auto kFarStart = tess::Coord3{0, 0, 0};
constexpr auto kFarGoal = tess::Coord3{511, 511, 0};
constexpr auto kSealedGoal = tess::Coord3{256, 256, 0};

void BM_topology_region_graph_build_open_512x512(benchmark::State& state) {
  TopoWorld world;
  fill_passable(world, 1);

  tess::LocalTopologyScratch scratch;
  tess::RegionGraph graph;
  tess::LocalTopologyResult built{};
  for (auto _ : state) {
    built =
        tess::build_region_graph<TopoWorld, PassableTag>(world, scratch, graph);
    benchmark::DoNotOptimize(built.region_count);
    benchmark::DoNotOptimize(graph.region_count());
  }
  bench_check(built.status == tess::TopologyStatus::Built,
              "region graph build did not report Built");
  bench_check(graph.region_count() > 0,
              "region graph build produced no regions");
  state.counters["regions"] = static_cast<double>(graph.region_count());
}

void BM_topology_region_graph_update_single_chunk_512x512(
    benchmark::State& state) {
  TopoWorld world;
  fill_passable(world, 1);

  tess::LocalTopologyScratch scratch;
  tess::RegionGraph graph;
  const auto built =
      tess::build_region_graph<TopoWorld, PassableTag>(world, scratch, graph);
  bench_check(built.status == tess::TopologyStatus::Built,
              "region graph build did not report Built");

  const auto dirty = std::array{tess::ChunkKey{0}};
  tess::LocalTopologyResult updated{};
  for (auto _ : state) {
    updated = tess::update_region_graph<TopoWorld, PassableTag>(world, scratch,
                                                                graph, dirty);
    benchmark::DoNotOptimize(updated.region_count);
    benchmark::DoNotOptimize(graph.region_count());
  }
  bench_check(updated.status == tess::TopologyStatus::Built,
              "incremental region graph update did not report Built");
}

void BM_topology_reachable_far_512x512(benchmark::State& state) {
  TopoWorld world;
  fill_passable(world, 1);

  tess::LocalTopologyScratch scratch;
  tess::RegionGraph graph;
  const auto built =
      tess::build_region_graph<TopoWorld, PassableTag>(world, scratch, graph);
  bench_check(built.status == tess::TopologyStatus::Built,
              "region graph build did not report Built");

  tess::RegionGraphScratch reach_scratch;
  // Warm the scratch so the timed loop is allocation-free.
  (void)tess::reachable<TopoShape>(graph, kFarStart, kFarGoal, reach_scratch);
  tess::ReachabilityResult result{};
  for (auto _ : state) {
    result =
        tess::reachable<TopoShape>(graph, kFarStart, kFarGoal, reach_scratch);
    benchmark::DoNotOptimize(result.visited_regions);
  }
  bench_check(result.status == tess::ReachabilityStatus::Reachable,
              "far reachability query was not Reachable");
}

void BM_topology_precheck_reachable_512x512(benchmark::State& state) {
  TopoWorld world;
  fill_passable(world, 1);

  tess::LocalTopologyScratch scratch;
  tess::RegionGraph graph;
  const auto built =
      tess::build_region_graph<TopoWorld, PassableTag>(world, scratch, graph);
  bench_check(built.status == tess::TopologyStatus::Built,
              "region graph build did not report Built");

  tess::RegionGraphScratch precheck_scratch;
  (void)tess::precheck_path(graph, world, kFarStart, kFarGoal,
                            precheck_scratch);
  tess::PrecheckStatus status{};
  for (auto _ : state) {
    status = tess::precheck_path(graph, world, kFarStart, kFarGoal,
                                 precheck_scratch);
    benchmark::DoNotOptimize(status);
  }
  bench_check(status == tess::PrecheckStatus::Reachable,
              "reachable precheck was not Reachable");
}

void BM_topology_precheck_unreachable_512x512(benchmark::State& state) {
  TopoWorld world;
  fill_passable(world, 1);
  enclose(world, kSealedGoal);

  tess::LocalTopologyScratch scratch;
  tess::RegionGraph graph;
  const auto built =
      tess::build_region_graph<TopoWorld, PassableTag>(world, scratch, graph);
  bench_check(built.status == tess::TopologyStatus::Built,
              "region graph build did not report Built");

  tess::RegionGraphScratch precheck_scratch;
  (void)tess::precheck_path(graph, world, kFarStart, kSealedGoal,
                            precheck_scratch);
  tess::PrecheckStatus status{};
  for (auto _ : state) {
    status = tess::precheck_path(graph, world, kFarStart, kSealedGoal,
                                 precheck_scratch);
    benchmark::DoNotOptimize(status);
  }
  bench_check(status == tess::PrecheckStatus::Unreachable,
              "sealed-goal precheck was not Unreachable");
}

BENCHMARK(BM_topology_region_graph_build_open_512x512)
    ->Name("topology/region_graph_build_open_512x512");
BENCHMARK(BM_topology_region_graph_update_single_chunk_512x512)
    ->Name("topology/region_graph_update_single_chunk_512x512");
BENCHMARK(BM_topology_reachable_far_512x512)
    ->Name("topology/reachable_far_512x512");
BENCHMARK(BM_topology_precheck_reachable_512x512)
    ->Name("topology/precheck_reachable_512x512");
BENCHMARK(BM_topology_precheck_unreachable_512x512)
    ->Name("topology/precheck_unreachable_512x512");

}  // namespace
