// Worst-case benchmarks for the pre-A* fast-path scans (see
// docs/architecture/path.md, "Pre-A* scan cost model", and the
// 2026-07-06 entry in docs/planning/optimization-log.md). Their
// thresholds are deliberately generous documentation ceilings, not tuned
// gates.
#include <benchmark/benchmark.h>
#include <tess/tess.h>

#include <cstdint>

namespace {

using PathScaleShape =
    tess::Shape<tess::Extent3{512, 512, 1}, tess::Extent3{32, 32, 1}>;
using Path3DShape =
    tess::Shape<tess::Extent3{64, 64, 16}, tess::Extent3{16, 16, 4}>;

struct PassableTag {};

using PathSchema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>>;
using PathScaleWorld = tess::AlwaysResidentWorld<PathScaleShape, PathSchema>;
using Path3DWorld = tess::AlwaysResidentWorld<Path3DShape, PathSchema>;

template <typename Shape>
[[nodiscard]] constexpr auto path_node_count() noexcept -> std::size_t {
  return static_cast<std::size_t>(Shape::size.x * Shape::size.y *
                                  Shape::size.z);
}

template <typename World>
void fill_path_passable(World& world, std::uint8_t value) {
  for (auto& page : world.chunks()) {
    auto passable = page.template field_span<PassableTag>();
    for (auto& tile : passable) {
      tile = value;
    }
  }
}

void record_path_counters(benchmark::State& state,
                          const tess::PathResult& result) {
  state.counters["cost"] = static_cast<double>(result.cost);
  state.counters["path_nodes"] = static_cast<double>(result.path.size());
  state.counters["expanded_nodes"] = static_cast<double>(result.expanded_nodes);
  state.counters["reached_nodes"] = static_cast<double>(result.reached_nodes);
}

// The direct probes, barrier scan, plane-gap scan, forced-gap scan, and
// axis detours all run and all fail: the single wall gap at (256, 300) is
// sealed on both sides, so every pre-A* scan is pure overhead before a
// full-flood NoPath A*.
void BM_path_astar_plane_gap_miss_512x512(benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);
  for (std::int64_t y = 0; y < 512; ++y) {
    if (y != 300) {
      world.template field<PassableTag>(tess::Coord3{256, y, 0}) = 0;
    }
  }
  world.template field<PassableTag>(tess::Coord3{255, 300, 0}) = 0;
  world.template field<PassableTag>(tess::Coord3{257, 300, 0}) = 0;

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  tess::PathResult result;

  for (auto _ : state) {
    result = tess::astar_path<PathScaleWorld, PassableTag>(
        world,
        tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{511, 0, 0}},
        scratch);
    auto expanded = result.expanded_nodes;
    benchmark::DoNotOptimize(expanded);
  }
  record_path_counters(state, result);
}

// 3D companion: the x-plane scan is O(size.y * size.z); the best-scoring
// gap at (32, 20, 8) is sealed so every stitched segment order fails, and
// A* must still route through the second (worse-scoring) gap.
void BM_path_astar_plane_gap_miss_3d_64x64x16(benchmark::State& state) {
  Path3DWorld world;
  fill_path_passable(world, 1);
  for (std::int64_t z = 0; z < 16; ++z) {
    for (std::int64_t y = 0; y < 64; ++y) {
      const auto first_gap = y == 20 && z == 8;
      const auto second_gap = y == 60 && z == 15;
      if (!first_gap && !second_gap) {
        world.template field<PassableTag>(tess::Coord3{32, y, z}) = 0;
      }
    }
  }
  world.template field<PassableTag>(tess::Coord3{31, 20, 8}) = 0;

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<Path3DShape>());
  tess::PathResult result;

  for (auto _ : state) {
    result = tess::astar_path<Path3DWorld, PassableTag>(
        world,
        tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{63, 63, 15}},
        scratch);
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  record_path_counters(state, result);
}

BENCHMARK(BM_path_astar_plane_gap_miss_512x512)
    ->Name("path/astar_plane_gap_miss_512x512");
BENCHMARK(BM_path_astar_plane_gap_miss_3d_64x64x16)
    ->Name("path/astar_plane_gap_miss_3d_64x64x16");

}  // namespace
