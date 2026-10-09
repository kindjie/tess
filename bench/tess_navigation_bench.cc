#include <benchmark/benchmark.h>
#include <tess/navigation/grid.h>
#include <tess/navigation/query.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <vector>

namespace {
auto make_snapshot() -> std::shared_ptr<const tess::NavigationSnapshot> {
  std::vector<tess::NavigationLocalEdge> edges;
  edges.reserve(510);
  for (std::uint64_t i = 0; i < 255; ++i) {
    edges.push_back({i, i + 1, i * 2, 1});
    edges.push_back({i + 1, i, i * 2 + 1, 2});
  }
  auto graph = std::make_shared<tess::NavigationGraph>(256, std::move(edges));
  return std::make_shared<tess::NavigationSnapshot>(
      std::vector<std::shared_ptr<const tess::NavigationDomain>>{graph},
      std::vector<tess::NavigationConnection>{}, graph->identity());
}
void bench_check(bool valid) {
  if (!valid) std::abort();
}
void BM_navigation_route_end_to_end_256(benchmark::State& state) {
  std::shared_ptr<const tess::NavigationResult> result;
  for (auto _ : state) {
    (void)_;
    auto snapshot = make_snapshot();
    const auto domain = snapshot->profile();
    result = tess::navigation_route(snapshot, {domain, 0}, {domain, 255},
                                    {256, 512, 256});
    benchmark::DoNotOptimize(result.get());
  }
  bench_check(result && result->outcome == tess::NavigationOutcome::Found &&
              result->cost == 255 && result->route.size() == 255);
  state.counters["work_items"] = static_cast<double>(result->work_items);
  state.counters["payload_bytes"] =
      static_cast<double>(result->payload_bytes());
}
void BM_navigation_shared_goal_256(benchmark::State& state) {
  std::shared_ptr<const tess::NavigationResult> result;
  // Includes preparation, field construction, 256 reads and retained output.
  std::uint64_t sum = 0;
  for (auto _ : state) {
    (void)_;
    auto snapshot = make_snapshot();
    const auto domain = snapshot->profile();
    tess::NavigationQuery query(snapshot, tess::NavigationLocation{domain, 255},
                                {256, 512, 256});
    while (query.state() == tess::AsyncResultState::Pending) query.advance({8});
    result = query.retained_result();
    sum = 0;
    for (std::uint64_t i = 0; i < 256; ++i) {
      const auto* value = result->guidance_at({domain, i});
      if (value) sum += value->cost;
    }
    benchmark::DoNotOptimize(sum);
  }
  bench_check(result && result->outcome == tess::NavigationOutcome::Found &&
              result->guidance.size() == 256 && sum == 32640);
  state.counters["work_items"] = static_cast<double>(result->work_items);
  state.counters["payload_bytes"] =
      static_cast<double>(result->payload_bytes());
}
struct Passable {};
struct Weight {};
using GridShape = tess::Shape<tess::Extent3{16, 16, 1}, tess::Extent3{8, 8, 1}>;
using OriginWorld =
    tess::AlwaysResidentWorld<GridShape,
                              tess::FieldSchema<tess::Field<Passable, bool>>>;
using DestinationWorld = tess::AlwaysResidentWorld<
    GridShape, tess::FieldSchema<tess::Field<Weight, std::uint32_t>>>;
using OriginMovement = tess::movement::UnitCostFieldMovement<Passable>;
using DestinationMovement =
    tess::movement::MovementClass<tess::movement::NotZero<Weight>,
                                  tess::movement::FieldCost<Weight>>;
using OriginGrid = tess::NavigationGrid<OriginWorld, OriginMovement>;
using DestinationGrid =
    tess::NavigationGrid<DestinationWorld, DestinationMovement>;
struct Composition {
  std::shared_ptr<const tess::NavigationSnapshot> snapshot;
  std::shared_ptr<const OriginGrid> origin;
  tess::NavigationLocation goal;
};
auto make_composition() -> Composition {
  auto first = std::make_shared<OriginWorld>();
  first->fill_field<Passable>(true);
  auto second = std::make_shared<DestinationWorld>();
  second->fill_field<Weight>(2);
  auto origin = std::make_shared<OriginGrid>(std::move(first), 1);
  auto destination = std::make_shared<DestinationGrid>(std::move(second), 1);
  auto road = std::make_shared<tess::NavigationGraph>(
      2, std::vector<tess::NavigationLocalEdge>{{0, 1, 1, 5}, {1, 0, 2, 7}});
  const auto exit = origin->coordinate_location({15, 15, 0});
  const auto entry = destination->coordinate_location({0, 0, 0});
  auto snapshot = std::make_shared<tess::NavigationSnapshot>(
      std::vector<std::shared_ptr<const tess::NavigationDomain>>{origin, road,
                                                                 destination},
      std::vector<tess::NavigationConnection>{
          {exit, road->location(0), 1, 0},
          {road->location(0), exit, 2, 0},
          {road->location(1), entry, 3, 0},
          {entry, road->location(1), 4, 0}});
  return {snapshot, origin, destination->coordinate_location({15, 15, 0})};
}
void BM_navigation_composed(benchmark::State& state, bool shared) {
  std::uint64_t sum = 0;
  std::uint64_t work = 0;
  std::size_t payload = 0;
  // Both cases include preparation and 32 destination requests. The route
  // case retains one full route at a time; shared guidance reads start costs
  // from one field. This is not equivalent full journey extraction/execution.
  // Sixteen unique origins are each requested twice.
  for (auto _ : state) {
    (void)_;
    const auto composition = make_composition();
    constexpr tess::NavigationLimits limits{514, 4096, 514};
    std::shared_ptr<const tess::NavigationResult> field;
    work = 0;
    payload = 0;
    sum = 0;
    if (shared) {
      tess::NavigationQuery query(composition.snapshot, composition.goal,
                                  limits);
      while (query.state() == tess::AsyncResultState::Pending)
        query.advance({64});
      field = query.retained_result();
      state.PauseTiming();
      bench_check(field && field->outcome == tess::NavigationOutcome::Found);
      state.ResumeTiming();
      work = field->work_items;
      payload = field->payload_bytes();
    }
    for (int i = 0; i < 32; ++i) {
      const auto start =
          composition.origin->coordinate_location({0, i % 16, 0});
      if (shared) {
        const auto* entry = field->guidance_at(start);
        state.PauseTiming();
        bench_check(entry != nullptr);
        state.ResumeTiming();
        sum += entry->cost;
      } else {
        const auto route = tess::navigation_route(composition.snapshot, start,
                                                  composition.goal, limits);
        state.PauseTiming();
        bench_check(route && route->outcome == tess::NavigationOutcome::Found);
        state.ResumeTiming();
        sum += route->cost;
        work += route->work_items;
        payload = std::max(payload, route->payload_bytes());
      }
    }
    benchmark::DoNotOptimize(sum);
  }
  // Independent Manhattan costs: (30-y) + 5 + 2*30, each y twice.
  bench_check(sum == 2800);
  state.counters["work_items"] = static_cast<double>(work);
  state.counters["payload_bytes"] = static_cast<double>(payload);
}
BENCHMARK_CAPTURE(BM_navigation_composed, routes, false)
    ->Name("lab/navigation/composed_routes_32_16x16");
BENCHMARK_CAPTURE(BM_navigation_composed, shared_goal, true)
    ->Name("lab/navigation/composed_shared_goal_32_16x16");
BENCHMARK(BM_navigation_route_end_to_end_256)
    ->Name("lab/navigation/route_end_to_end_256");
BENCHMARK(BM_navigation_shared_goal_256)
    ->Name("lab/navigation/shared_goal_256");
}  // namespace
