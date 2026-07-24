#include <benchmark/benchmark.h>
#include <tess/tess.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

struct PassableTag {};
struct CostTag {};
struct StairTag {};

using Schema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>,
                                 tess::Field<CostTag, std::uint32_t>,
                                 tess::Field<StairTag, std::uint8_t>>;
using SquareShape =
    tess::Shape<tess::Extent3{128, 128, 1}, tess::Extent3{32, 32, 1}>;
using HexShape = tess::Shape<tess::Extent3{128, 128, 1},
                             tess::Extent3{32, 32, 1}, tess::lattice::HexAxial>;
using StairShape =
    tess::Shape<tess::Extent3{128, 128, 2}, tess::Extent3{32, 32, 2}>;
using SquareWorld = tess::AlwaysResidentWorld<SquareShape, Schema>;
using HexWorld = tess::AlwaysResidentWorld<HexShape, Schema>;
using StairWorld = tess::AlwaysResidentWorld<StairShape, Schema>;

namespace mv = tess::movement;

using DiagonalClass =
    mv::MovementClass<mv::Field<PassableTag>, mv::UnitCost,
                      mv::DiagonalSteps<mv::CornerRule::RequireBothClear>>;
using DefaultClass = mv::MovementClass<mv::Field<PassableTag>, mv::UnitCost>;

void bench_check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "tess_bench correctness check failed: %s\n", message);
    std::abort();
  }
}

template <typename World>
void fill_open(World& world) {
  for (auto& page : world.chunks()) {
    auto passable = page.template field_span<PassableTag>();
    auto costs = page.template field_span<CostTag>();
    std::fill(passable.begin(), passable.end(), 1);
    std::fill(costs.begin(), costs.end(), 1u);
  }
}

template <typename World, typename Class, typename Provider>
void check_resolved_path(const World& world, const tess::PathResult& result,
                         tess::PathRequest request, const Provider& provider) {
  bench_check(result.status == tess::PathStatus::Found,
              "resolved path status is not Found");
  bench_check(!result.path.empty(), "resolved path is empty");
  bench_check(result.path.front() == request.start,
              "resolved path has the wrong start");
  bench_check(result.path.back() == request.goal,
              "resolved path has the wrong goal");
  const auto model =
      tess::ResolvedTransitionModel<World, Class, Provider>{provider};
  for (std::size_t i = 1; i < result.path.size(); ++i) {
    auto legal = false;
    const auto from = result.path[i - 1];
    model.for_each_forward(
        world, from,
        static_cast<std::uint64_t>(
            tess::tile_key<typename World::shape_type>(from).value),
        [&](auto probe) {
          legal = legal ||
                  (probe.to == result.path[i] &&
                   probe.availability == tess::TransitionAvailability::Legal);
        });
    bench_check(legal, "resolved path contains an illegal edge");
  }
}

template <typename World, typename Class, typename Provider>
void run_resolved_bench(benchmark::State& state, World& world,
                        tess::PathRequest request, const Provider& provider) {
  tess::PathScratch scratch;
  scratch.reserve_nodes(
      static_cast<std::size_t>(World::chunk_count * World::local_tile_count));
  auto result = tess::weighted_astar_path<World, Class>(
      world, request, scratch, tess::MissingChunkPolicy::TreatAsBlocked,
      provider);
  const auto expected_cost = result.cost;
  check_resolved_path<World, Class>(world, result, request, provider);

  for (auto _ : state) {
    result = tess::weighted_astar_path<World, Class>(
        world, request, scratch, tess::MissingChunkPolicy::TreatAsBlocked,
        provider);
    benchmark::DoNotOptimize(result.path.data());
  }

  check_resolved_path<World, Class>(world, result, request, provider);
  bench_check(result.cost == expected_cost,
              "resolved path cost changed across warm runs");
  state.counters["cost"] = static_cast<double>(result.cost);
  state.counters["cost_scale"] = static_cast<double>(result.cost_scale);
  state.counters["expanded_nodes"] = static_cast<double>(result.expanded_nodes);
  state.counters["path_nodes"] = static_cast<double>(result.path.size());
}

void BM_path_resolved_diagonal_open_128x128(benchmark::State& state) {
  SquareWorld world;
  fill_open(world);
  run_resolved_bench<SquareWorld, DiagonalClass>(
      state, world,
      tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{126, 126, 0}},
      tess::AdjacentTransitions{});
}

void BM_path_resolved_hex_open_128x128(benchmark::State& state) {
  HexWorld world;
  fill_open(world);
  run_resolved_bench<HexWorld, DefaultClass>(
      state, world,
      tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{126, 126, 0}},
      tess::AdjacentTransitions{});
}

void BM_path_resolved_stair_provider_open_128x128x2(benchmark::State& state) {
  StairWorld world;
  fill_open(world);
  constexpr auto foot = tess::Coord3{1, 1, 0};
  world.field<StairTag>(foot) =
      static_cast<std::uint8_t>(tess::StairDirection::PositiveX);
  run_resolved_bench<StairWorld, DefaultClass>(
      state, world, tess::PathRequest{foot, tess::Coord3{126, 126, 1}},
      tess::StairTransitions<StairTag>{});
}

BENCHMARK(BM_path_resolved_diagonal_open_128x128)
    ->Name("path/resolved_diagonal_open_128x128");
BENCHMARK(BM_path_resolved_hex_open_128x128)
    ->Name("path/resolved_hex_open_128x128");
BENCHMARK(BM_path_resolved_stair_provider_open_128x128x2)
    ->Name("path/resolved_stair_provider_open_128x128x2");

}  // namespace
