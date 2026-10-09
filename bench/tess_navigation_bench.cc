#include <benchmark/benchmark.h>
#include <tess/navigation/grid.h>
#include <tess/navigation/query.h>

#include <algorithm>
#include <cstdlib>
#include <map>
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
// Chained legal transitions from start to goal whose step costs sum to total.
void check_route_chain(const std::vector<tess::NavigationTransition>& route,
                       tess::NavigationLocation start,
                       tess::NavigationLocation goal, std::uint64_t total) {
  bench_check(!route.empty() && route.front().from == start &&
              route.back().to == goal);
  std::uint64_t sum = 0;
  auto at = start;
  for (const auto& step : route) {
    bench_check(step.from == at && step.to != step.from &&
                step.availability == tess::NavigationAvailability::Legal);
    sum += step.cost;
    at = step.to;
  }
  bench_check(sum == total);
}
// The 256-node line graph: forward edge i -> i + 1 has id 2i and cost 1.
void check_line_step(const tess::NavigationTransition& step,
                     std::uint64_t domain) {
  bench_check(!step.connection && step.from.domain == domain &&
              step.to.domain == domain && step.to.node == step.from.node + 1 &&
              step.id == step.from.node * 2 && step.cost == 1);
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
  const auto line = result->snapshot->profile();
  check_route_chain(result->route, {line, 0}, {line, 255}, 255);
  for (const auto& step : result->route) check_line_step(step, line);
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
  const auto line = result->snapshot->profile();
  for (std::uint64_t i = 0; i < 256; ++i) {
    const auto* value = result->guidance_at({line, i});
    bench_check(value != nullptr && value->cost == 255 - i &&
                value->has_next == (i != 255));
    if (!value->has_next) continue;
    bench_check(value->next.from == tess::NavigationLocation{line, i});
    check_line_step(value->next, line);
  }
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
  // Authored seam endpoints: exit -> road 0 (id 1), road 1 -> entry (id 3).
  tess::NavigationLocation exit, road0, road1, entry;
};
// From origin (0, y): 30 - y unit origin steps, a zero-cost exit connection,
// the cost-5 road edge, a zero-cost entry connection, then 30 weight-2
// destination steps.
constexpr auto composed_cost(int y) -> std::uint64_t {
  return static_cast<std::uint64_t>(95 - y);
}
constexpr auto composed_steps(int y) -> std::size_t {
  return static_cast<std::size_t>(63 - y);
}
// One axial step within a single grid layer.
auto unit_step(tess::Coord3 from, tess::Coord3 to) -> bool {
  const auto dx = from.x > to.x ? from.x - to.x : to.x - from.x;
  const auto dy = from.y > to.y ? from.y - to.y : to.y - from.y;
  return from.z == to.z && dx + dy == 1;
}
void check_composed_step(const tess::NavigationTransition& step,
                         const Composition& composition) {
  const auto origin = composition.origin->coordinate_location({0, 0, 0}).domain;
  const auto destination = composition.goal.domain;
  bench_check(step.availability == tess::NavigationAvailability::Legal);
  bench_check(step.key == 0);
  if (step.connection) {
    // Only the two forward seams can lie on a route toward the goal.
    bench_check(step.cost == 0 &&
                ((step.from == composition.exit &&
                  step.to == composition.road0 && step.id == 1) ||
                 (step.from == composition.road1 &&
                  step.to == composition.entry && step.id == 3)));
    return;
  }
  bench_check(step.from.domain == step.to.domain);
  // Both grids share GridShape, so one node-to-coordinate table built from
  // the public coordinate_location inverts either grid's node keys.
  static const auto coordinates = [&] {
    std::map<std::uint64_t, tess::Coord3> table;
    for (std::int64_t y = 0; y < 16; ++y)
      for (std::int64_t x = 0; x < 16; ++x)
        table[composition.origin->coordinate_location({x, y, 0}).node] = {x, y,
                                                                          0};
    return table;
  }();
  // A unit step whose id is the destination's ordinal among the source's
  // in-bounds axial candidates (+x, -x, +y, -y), derived from geometry.
  const auto grid_step = [&] {
    const auto from = coordinates.find(step.from.node);
    const auto to = coordinates.find(step.to.node);
    if (from == coordinates.end() || to == coordinates.end() ||
        !unit_step(from->second, to->second))
      return false;
    constexpr std::int64_t offsets[4][2]{{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    std::uint64_t ordinal = 0;
    for (const auto& offset : offsets) {
      const auto x = from->second.x + offset[0];
      const auto y = from->second.y + offset[1];
      if (x < 0 || y < 0 || x >= 16 || y >= 16) continue;
      if (x == to->second.x && y == to->second.y) return step.id == ordinal;
      ++ordinal;
    }
    return false;
  };
  if (step.from.domain == origin) {
    bench_check(step.cost == 1 && grid_step());
  } else if (step.from.domain == destination) {
    bench_check(step.cost == 2 && grid_step());
  } else {
    // The only selected road edge is 0 -> 1 (id 1, cost 5).
    bench_check(step.from.node == 0 && step.to.node == 1 && step.id == 1 &&
                step.cost == 5);
  }
}
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
  return {snapshot,
          origin,
          destination->coordinate_location({15, 15, 0}),
          exit,
          road->location(0),
          road->location(1),
          entry};
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
        bench_check(entry != nullptr && entry->cost == composed_cost(i % 16));
        {
          // Follow guidance to the goal: chained legal steps whose costs
          // account exactly for each label's remaining cost.
          auto at = start;
          const auto* label = entry;
          std::size_t steps = 0;
          while (label->has_next) {
            const auto& next = label->next;
            check_composed_step(next, composition);
            const auto* following = field->guidance_at(next.to);
            bench_check(next.from == at && following != nullptr &&
                        following->cost + next.cost == label->cost &&
                        ++steps <= composed_steps(i % 16));
            at = next.to;
            label = following;
          }
          bench_check(at == composition.goal && label->cost == 0 &&
                      steps == composed_steps(i % 16));
        }
        state.ResumeTiming();
        sum += entry->cost;
      } else {
        const auto route = tess::navigation_route(composition.snapshot, start,
                                                  composition.goal, limits);
        state.PauseTiming();
        bench_check(route && route->outcome == tess::NavigationOutcome::Found &&
                    route->cost == composed_cost(i % 16) &&
                    route->route.size() == composed_steps(i % 16));
        check_route_chain(route->route, start, composition.goal, route->cost);
        for (const auto& step : route->route)
          check_composed_step(step, composition);
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
