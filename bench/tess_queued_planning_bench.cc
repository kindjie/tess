#include <benchmark/benchmark.h>
#include <tess/tess.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <vector>

// Queued per-frame PLANNING cost, split out from tess_bench.cc, which is
// at its token ceiling. Every other queued benchmark plans once outside
// its measured loop; this family times planning itself.
namespace {

void planning_bench_check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "tess_bench correctness check failed: %s\n", message);
    std::abort();
  }
}

struct TerrainTag {};
struct CostTag {};

constexpr std::uint32_t kDirtyTerrain = 1u << 0u;

using PlanningSchema = tess::FieldSchema<tess::Field<TerrainTag, std::uint16_t>,
                                         tess::Field<CostTag, float>>;
// 4096 chunks at 8x8 tiles: enough chunks to show the planner's growth
// without a large tile footprint.
using PlanningShape =
    tess::Shape<tess::Extent3{512, 512, 1}, tess::Extent3{8, 8, 1}>;
using PlanningWorld = tess::AlwaysResidentWorld<PlanningShape, PlanningSchema>;

// Per-frame planning cost, which nothing timed before: every other queued
// benchmark plans once OUTSIDE its measured loop, and the one in-loop
// planner call plans a single operation. Hazard detection scans every
// previously accepted operation per new operation, and parallel-phase
// grouping compares each operation against every member of the current
// phase, so both are quadratic in the operation count. Two sizes are
// registered so the growth reads as a shape rather than a point: a purely
// constant-factor change moves both together, a complexity change does
// not.
//
// The operations are per-chunk and disjoint, which is the worst case for
// phase grouping (the phase never closes, so it grows to n) and the
// ordinary case for a consumer enqueuing one edit per dirty chunk.
template <std::size_t Operations>
void BM_queued_plan_frame(benchmark::State& state) {
  PlanningWorld world;
  static_assert(Operations <= PlanningWorld::chunk_count,
                "planning bench needs one distinct chunk per operation");
  std::vector<tess::ChunkKey> keys;
  keys.reserve(Operations);
  for (std::size_t i = 0; i < Operations; ++i) {
    keys.push_back(tess::ChunkKey{static_cast<std::uint64_t>(i)});
  }

  tess::FrameOps ops;
  for (std::size_t i = 0; i < Operations; ++i) {
    (void)ops.update_field(
        tess::DomainDesc::explicit_chunks(
            std::span<const tess::ChunkKey>{keys.data() + i, 1}),
        tess::FieldAccessDesc{0, kDirtyTerrain, kDirtyTerrain},
        tess::WritePolicy::UniquePerChunk);
  }

  std::size_t last_phase_count = 0;
  for (auto _ : state) {
    auto report = tess::plan_operations(world, ops);
    const auto phases = tess::plan_parallel_execution_phases(report.plan());
    last_phase_count = phases.phases().size();
    benchmark::DoNotOptimize(last_phase_count);
    benchmark::ClobberMemory();
  }
  planning_bench_check(last_phase_count == 1,
                       "disjoint per-chunk operations must plan to one phase");
}

BENCHMARK(BM_queued_plan_frame<256>)->Name("queued/plan_frame_256");
BENCHMARK(BM_queued_plan_frame<4096>)->Name("queued/plan_frame_4096");

}  // namespace
