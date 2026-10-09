#include <benchmark/benchmark.h>
#include <tess/diagnostics/diagnostics.h>
#include <tess/navigation/grid.h>
#include <tess/navigation/query.h>
#include <tess/storage/sparse_world.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <queue>
#include <source_location>
#include <tuple>
#include <utility>
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
void bench_check(bool valid,
                 std::source_location where = std::source_location::current()) {
  if (!valid) {
    std::fprintf(stderr, "benchmark check failed at %s:%u\n", where.file_name(),
                 where.line());
    std::abort();
  }
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

// ---------------------------------------------------------------------------
// Scale qualification: one heterogeneous composition, complete-route
// extraction for both repeated queries and shared guidance (equal per-request
// oracle cost; the two often follow different equal-cost paths), an
// independent materialized oracle outside every timed loop, and explicit
// phase splits.
//
// Composition (2,368 positions, 2,199 legal states), all content authored by
// the pure functions below so the oracle and the worlds derive from the same
// description:
//   yard   AlwaysResident 32x32 (16x16 chunks), bool passability with pillar
//          obstacles, unit face steps, common-unit scale 4.
//   rubble AlwaysResident 32x32 (8x8 chunks), u32 weight 0..5 from a fixed
//          hash (0 blocks), diagonal steps (128 axial / 181 diagonal, both
//          corners clear), scale 1.
//   cellar SparseResident 16x16 (8x8 chunks), all four chunks resident so
//          the snapshot is complete; bool open + u32 toll, face steps,
//          scale 3.
//   road   explicit 64-node graph: directed ring (cost 7), even chords
//          (cost 20), one cheaper parallel edge and one blocked edge.
// Directed attachments: yard->road, road->rubble, rubble<->cellar with
// asymmetric costs, cellar->yard, plus one blocked parallel seam. The cycle
// makes every legal state reach every other legal state.
namespace scale {
struct YardPassable {};
struct RubbleWeight {};
struct CellarOpen {};
struct CellarToll {};
using YardShape =
    tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{16, 16, 1}>;
using YardWorld = tess::AlwaysResidentWorld<
    YardShape, tess::FieldSchema<tess::Field<YardPassable, bool>>>;
using YardMovement = tess::movement::UnitCostFieldMovement<YardPassable>;
using YardGrid = tess::NavigationGrid<YardWorld, YardMovement>;
using RubbleShape =
    tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{8, 8, 1}>;
using RubbleWorld = tess::AlwaysResidentWorld<
    RubbleShape, tess::FieldSchema<tess::Field<RubbleWeight, std::uint32_t>>>;
using RubbleMovement =
    tess::movement::MovementClass<tess::movement::NotZero<RubbleWeight>,
                                  tess::movement::FieldCost<RubbleWeight>,
                                  tess::movement::DiagonalSteps<>>;
using RubbleGrid = tess::NavigationGrid<RubbleWorld, RubbleMovement>;
using CellarShape =
    tess::Shape<tess::Extent3{16, 16, 1}, tess::Extent3{8, 8, 1}>;
using CellarWorld = tess::SparseResidentWorld<
    CellarShape, tess::FieldSchema<tess::Field<CellarOpen, bool>,
                                   tess::Field<CellarToll, std::uint32_t>>>;
using CellarMovement = tess::movement::MovementClass<
    tess::movement::AllOf<tess::movement::Field<CellarOpen>,
                          tess::movement::NotZero<CellarToll>>,
    tess::movement::FieldCost<CellarToll>>;
using CellarGrid = tess::NavigationGrid<CellarWorld, CellarMovement>;

constexpr std::uint64_t kInfinite = std::numeric_limits<std::uint64_t>::max();
constexpr std::int64_t kYardSize = 32, kRubbleSize = 32, kCellarSize = 16;
constexpr std::uint64_t kRoadNodes = 64;
constexpr std::uint64_t kYardScale = 4, kRubbleScale = 1, kCellarScale = 3;
constexpr std::uint64_t kStates = 32 * 32 + 32 * 32 + 16 * 16 + kRoadNodes;
constexpr tess::NavigationLimits kLimits{2560, 16384, 2560};
constexpr tess::NavigationLimits kRefusedLimits{256, 256, 256};
// Conservative cap on the forward or reverse candidate count of any state,
// attachments and blocked candidates included; the measured maximum is 8
// (interior rubble cells), and border cells with seams have fewer.
constexpr std::uint64_t kCompositionMaxDegree = 10;
// Upper bound on the deterministic items one refused query can spend, from
// the documented per-item accounting in query.h: initialization, one item
// per seed plus the seed-phase exit, one per frontier pop, one per candidate
// probe plus one enumeration exit per settled node, one per reconstruction
// append, publication. Labels never exceed max_nodes, every label is pushed
// at most once per incoming edge, every settled node probes at most its
// degree, and pops never exceed pushes. A refusal whose work grew with world
// size instead of with the configured limits would exceed this bound.
constexpr auto refusal_work_bound(tess::NavigationLimits limits,
                                  std::uint64_t goals, std::uint64_t max_degree)
    -> std::uint64_t {
  return 3 + 2 * goals + limits.max_nodes * (2 * max_degree + 1) +
         limits.max_route_steps;
}
// Oracle-relative domain roles; production identities are per instance.
enum Role : std::uint64_t { Yard = 0, Rubble = 1, Cellar = 2, Road = 3 };

auto yard_passable(std::int64_t x, std::int64_t y) -> bool {
  return !(x % 4 == 2 && y % 4 == 2);
}
auto rubble_weight(std::int64_t x, std::int64_t y) -> std::uint32_t {
  const auto h = (static_cast<std::uint32_t>(x) * 73856093u ^
                  static_cast<std::uint32_t>(y) * 19349663u) *
                 2654435761u;
  const auto bucket = (h >> 7) % 10;
  // Attachment borders stay passable so every seam endpoint is legal.
  if (bucket == 0 && x != 0 && y != kRubbleSize - 1) return 0;
  return 1 + (h >> 11) % 5;
}
auto cellar_open(std::int64_t x, std::int64_t y) -> bool {
  return !(y == 8 && x % 8 != 3);
}
auto cellar_toll(std::int64_t, std::int64_t y) -> std::uint32_t {
  return y % 2 == 0 ? 2 : 3;
}
template <typename Shape>
auto node_of(std::int64_t x, std::int64_t y) -> std::uint64_t {
  return static_cast<std::uint64_t>(
      tess::tile_key<Shape>(tess::Coord3{x, y, 0}).value);
}
auto road_edges() -> std::vector<tess::NavigationLocalEdge> {
  std::vector<tess::NavigationLocalEdge> edges;
  for (std::uint64_t i = 0; i < kRoadNodes; ++i) {
    edges.push_back({i, (i + 1) % kRoadNodes, 1000 + i, 7, 5000 + i});
    if (i % 2 == 0)
      edges.push_back({i, (i + 9) % kRoadNodes, 1100 + i, 20, 5100 + i});
  }
  edges.push_back({5, 6, 1200, 3, 5200});
  edges.push_back({6, 5, 1201, 1, 5201, tess::NavigationAvailability::Blocked});
  return edges;
}
// Attachments in role-relative locations; instantiated per fixture.
auto relative_connections() -> std::vector<tess::NavigationConnection> {
  std::vector<tess::NavigationConnection> seams;
  std::uint64_t id = 100;
  auto add = [&](tess::NavigationLocation from, tess::NavigationLocation to,
                 std::uint64_t cost,
                 tess::NavigationAvailability availability =
                     tess::NavigationAvailability::Legal) {
    seams.push_back({from, to, id, cost, id + 800, availability});
    ++id;
  };
  for (std::int64_t k = 0; k < 8; ++k) {
    add({Yard, node_of<YardShape>(kYardSize - 1, 4 * k + 1)},
        {Road, static_cast<std::uint64_t>(4 * k)}, 0);
    add({Road, static_cast<std::uint64_t>(8 * k + 4)},
        {Rubble, node_of<RubbleShape>(0, 4 * k)}, 2);
    add({Rubble, node_of<RubbleShape>(4 * k + 2, kRubbleSize - 1)},
        {Cellar, node_of<CellarShape>(2 * k, 0)}, 1);
    add({Cellar, node_of<CellarShape>(2 * k, 0)},
        {Rubble, node_of<RubbleShape>(4 * k + 2, kRubbleSize - 1)}, 9);
    add({Cellar, node_of<CellarShape>(2 * k + 1, kCellarSize - 1)},
        {Yard, node_of<YardShape>(0, 4 * k + 2)}, 0);
  }
  add({Yard, node_of<YardShape>(kYardSize - 1, 1)}, {Road, 0}, 0,
      tess::NavigationAvailability::Blocked);
  return seams;
}

struct Fixture {
  std::shared_ptr<const tess::NavigationSnapshot> snapshot;
  std::uint64_t identity[4]{};
  [[nodiscard]] auto absolute(tess::NavigationLocation rel) const
      -> tess::NavigationLocation {
    return {identity[rel.domain], rel.node};
  }
  [[nodiscard]] auto relative(tess::NavigationLocation at) const
      -> tess::NavigationLocation {
    for (std::uint64_t role = 0; role < 4; ++role)
      if (identity[role] == at.domain) return {role, at.node};
    std::abort();
  }
};
// Explicit unbudgeted preparation: world allocation and fills, adapters, the
// road graph, connection instantiation and snapshot validation/indexing.
auto make_fixture() -> Fixture {
  auto yard_world = std::make_shared<YardWorld>();
  yard_world->fill_field<YardPassable>(true);
  for (std::int64_t y = 0; y < kYardSize; ++y)
    for (std::int64_t x = 0; x < kYardSize; ++x)
      if (!yard_passable(x, y))
        yard_world->field<YardPassable>(tess::Coord3{x, y, 0}) = false;
  auto rubble_world = std::make_shared<RubbleWorld>();
  for (std::int64_t y = 0; y < kRubbleSize; ++y)
    for (std::int64_t x = 0; x < kRubbleSize; ++x)
      rubble_world->field<RubbleWeight>(tess::Coord3{x, y, 0}) =
          rubble_weight(x, y);
  auto cellar_world = std::make_shared<CellarWorld>(tess::ResidencyConfig{
      CellarWorld::chunk_count * CellarWorld::page_byte_size});
  for (std::uint64_t key = 0; key < CellarWorld::chunk_count; ++key)
    cellar_world->ensure_resident(tess::ChunkKey{key});
  for (std::int64_t y = 0; y < kCellarSize; ++y)
    for (std::int64_t x = 0; x < kCellarSize; ++x) {
      cellar_world->field<CellarOpen>(tess::Coord3{x, y, 0}) =
          cellar_open(x, y);
      cellar_world->field<CellarToll>(tess::Coord3{x, y, 0}) =
          cellar_toll(x, y);
    }
  auto yard = std::make_shared<YardGrid>(std::move(yard_world), kYardScale);
  auto rubble =
      std::make_shared<RubbleGrid>(std::move(rubble_world), kRubbleScale);
  auto cellar =
      std::make_shared<CellarGrid>(std::move(cellar_world), kCellarScale);
  auto road = std::make_shared<tess::NavigationGraph>(kRoadNodes, road_edges());
  Fixture fixture;
  fixture.identity[Yard] = yard->identity();
  fixture.identity[Rubble] = rubble->identity();
  fixture.identity[Cellar] = cellar->identity();
  fixture.identity[Road] = road->identity();
  auto seams = relative_connections();
  for (auto& seam : seams) {
    seam.from = fixture.absolute(seam.from);
    seam.to = fixture.absolute(seam.to);
  }
  fixture.snapshot = std::make_shared<tess::NavigationSnapshot>(
      std::vector<std::shared_ptr<const tess::NavigationDomain>>{yard, rubble,
                                                                 cellar, road},
      std::move(seams), 1);
  return fixture;
}

// Independent materialization from the authored description: coordinate
// arithmetic and the documented cost rules only (destination entry cost x
// step multiplier x scale; diagonals need both orthogonal corners passable).
// It never consults adapter enumeration or query labels. Grid identities are
// independently computed from geometric candidate order, including blocked
// candidates but excluding positions outside the rectangular world.
struct OracleEdge {
  tess::NavigationLocation from, to;
  std::uint64_t id = 0, cost = 0, key = 0;
  bool identified = false;
};
struct Oracle {
  std::vector<tess::NavigationLocation> states;  // legal, role-relative
  std::map<tess::NavigationLocation, std::uint32_t> index;
  std::vector<OracleEdge> edges;  // legal only
  std::vector<std::vector<std::uint32_t>> reverse;
  std::map<std::pair<tess::NavigationLocation, tess::NavigationLocation>,
           std::vector<std::uint32_t>>
      by_endpoints;
  void add_state(tess::NavigationLocation at) {
    index.emplace(at, static_cast<std::uint32_t>(states.size()));
    states.push_back(at);
  }
  void add_edge(OracleEdge edge) {
    bench_check(index.count(edge.from) != 0 && index.count(edge.to) != 0);
    by_endpoints[{edge.from, edge.to}].push_back(
        static_cast<std::uint32_t>(edges.size()));
    edges.push_back(edge);
  }
  // Array Dijkstra with a binary heap on reversed edges: deliberately a
  // different container shape from production's ordered-map labels.
  [[nodiscard]] auto distances_to(std::uint32_t goal) const
      -> std::vector<std::uint64_t> {
    std::vector<std::uint64_t> dist(states.size(), kInfinite);
    using Item = std::pair<std::uint64_t, std::uint32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> heap;
    dist[goal] = 0;
    heap.push({0, goal});
    while (!heap.empty()) {
      const auto [d, u] = heap.top();
      heap.pop();
      if (d != dist[u]) continue;
      for (const auto e : reverse[u]) {
        const auto v = index.at(edges[e].from);
        const auto candidate = d + edges[e].cost;
        if (candidate < dist[v]) {
          dist[v] = candidate;
          heap.push({candidate, v});
        }
      }
    }
    return dist;
  }
};
auto grid_ordinal(std::int64_t x, std::int64_t y, std::int64_t nx,
                  std::int64_t ny, std::int64_t size, bool diagonal)
    -> std::uint64_t {
  constexpr std::int64_t offsets[8][2]{{1, 0}, {-1, 0}, {0, 1},  {0, -1},
                                       {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
  std::uint64_t ordinal = 0;
  for (std::size_t i = 0; i < (diagonal ? 8u : 4u); ++i) {
    const auto tx = x + offsets[i][0], ty = y + offsets[i][1];
    if (tx < 0 || ty < 0 || tx >= size || ty >= size) continue;
    if (tx == nx && ty == ny) return ordinal;
    ++ordinal;
  }
  std::abort();
}
auto build_oracle() -> Oracle {
  Oracle oracle;
  auto in_square = [](std::int64_t x, std::int64_t y, std::int64_t size) {
    return x >= 0 && y >= 0 && x < size && y < size;
  };
  for (std::int64_t y = 0; y < kYardSize; ++y)
    for (std::int64_t x = 0; x < kYardSize; ++x)
      if (yard_passable(x, y))
        oracle.add_state({Yard, node_of<YardShape>(x, y)});
  for (std::int64_t y = 0; y < kRubbleSize; ++y)
    for (std::int64_t x = 0; x < kRubbleSize; ++x)
      if (rubble_weight(x, y) != 0)
        oracle.add_state({Rubble, node_of<RubbleShape>(x, y)});
  for (std::int64_t y = 0; y < kCellarSize; ++y)
    for (std::int64_t x = 0; x < kCellarSize; ++x)
      if (cellar_open(x, y) && cellar_toll(x, y) != 0)
        oracle.add_state({Cellar, node_of<CellarShape>(x, y)});
  for (std::uint64_t i = 0; i < kRoadNodes; ++i) oracle.add_state({Road, i});
  constexpr std::int64_t faces[4][2]{{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  constexpr std::int64_t diagonals[4][2]{{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
  for (std::int64_t y = 0; y < kYardSize; ++y)
    for (std::int64_t x = 0; x < kYardSize; ++x) {
      if (!yard_passable(x, y)) continue;
      for (const auto& d : faces) {
        const auto nx = x + d[0], ny = y + d[1];
        if (!in_square(nx, ny, kYardSize) || !yard_passable(nx, ny)) continue;
        oracle.add_edge({{Yard, node_of<YardShape>(x, y)},
                         {Yard, node_of<YardShape>(nx, ny)},
                         grid_ordinal(x, y, nx, ny, kYardSize, false),
                         1 * kYardScale});
      }
    }
  for (std::int64_t y = 0; y < kRubbleSize; ++y)
    for (std::int64_t x = 0; x < kRubbleSize; ++x) {
      if (rubble_weight(x, y) == 0) continue;
      for (const auto& d : faces) {
        const auto nx = x + d[0], ny = y + d[1];
        if (!in_square(nx, ny, kRubbleSize) || rubble_weight(nx, ny) == 0)
          continue;
        oracle.add_edge({{Rubble, node_of<RubbleShape>(x, y)},
                         {Rubble, node_of<RubbleShape>(nx, ny)},
                         grid_ordinal(x, y, nx, ny, kRubbleSize, true),
                         rubble_weight(nx, ny) * 128ULL * kRubbleScale});
      }
      for (const auto& d : diagonals) {
        const auto nx = x + d[0], ny = y + d[1];
        if (!in_square(nx, ny, kRubbleSize) || rubble_weight(nx, ny) == 0 ||
            rubble_weight(nx, y) == 0 || rubble_weight(x, ny) == 0)
          continue;
        oracle.add_edge({{Rubble, node_of<RubbleShape>(x, y)},
                         {Rubble, node_of<RubbleShape>(nx, ny)},
                         grid_ordinal(x, y, nx, ny, kRubbleSize, true),
                         rubble_weight(nx, ny) * 181ULL * kRubbleScale});
      }
    }
  for (std::int64_t y = 0; y < kCellarSize; ++y)
    for (std::int64_t x = 0; x < kCellarSize; ++x) {
      if (!cellar_open(x, y) || cellar_toll(x, y) == 0) continue;
      for (const auto& d : faces) {
        const auto nx = x + d[0], ny = y + d[1];
        if (!in_square(nx, ny, kCellarSize) || !cellar_open(nx, ny) ||
            cellar_toll(nx, ny) == 0)
          continue;
        oracle.add_edge({{Cellar, node_of<CellarShape>(x, y)},
                         {Cellar, node_of<CellarShape>(nx, ny)},
                         grid_ordinal(x, y, nx, ny, kCellarSize, false),
                         cellar_toll(nx, ny) * kCellarScale});
      }
    }
  for (const auto& e : road_edges())
    if (e.availability == tess::NavigationAvailability::Legal)
      oracle.add_edge(
          {{Road, e.from}, {Road, e.to}, e.id, e.cost, e.key, true});
  for (const auto& c : relative_connections())
    if (c.availability == tess::NavigationAvailability::Legal)
      oracle.add_edge({c.from, c.to, c.id, c.cost, c.key, true});
  oracle.reverse.assign(oracle.states.size(), {});
  for (std::uint32_t e = 0; e < oracle.edges.size(); ++e)
    oracle.reverse[oracle.index.at(oracle.edges[e].to)].push_back(e);
  bench_check(oracle.states.size() <= kStates);
  return oracle;
}
auto oracle() -> const Oracle& {
  static const Oracle instance = build_oracle();
  return instance;
}

struct Request {
  tess::NavigationLocation start;  // role-relative
  std::size_t goal = 0;
};
struct Workload {
  std::vector<Request> requests;
  std::vector<tess::NavigationLocation> goals;       // role-relative, distinct
  std::vector<std::vector<std::uint64_t>> distance;  // per goal, per state
  std::uint64_t cost_sum = 0;
  [[nodiscard]] auto expected(const Request& request) const -> std::uint64_t {
    return distance[request.goal][oracle().index.at(request.start)];
  }
};
// Deterministic request populations; every request is oracle-reachable.
// `start_role`/`goal_role` of 4 means any role.
auto make_workload(std::size_t requests, std::size_t goals,
                   std::uint64_t start_role, std::uint64_t goal_role)
    -> Workload {
  const auto& reference = oracle();
  std::uint32_t random = static_cast<std::uint32_t>(
      0x9e3779b9u ^ (requests * 131 + goals * 7 + start_role * 3 + goal_role));
  auto next = [&] {
    random = random * 1664525u + 1013904223u;
    return random >> 8;
  };
  auto pick = [&](std::uint64_t role) {
    for (;;) {
      const auto& state = reference.states[next() % reference.states.size()];
      if (role == 4 || state.domain == role) return state;
    }
  };
  Workload workload;
  while (workload.goals.size() < goals) {
    const auto goal = pick(goal_role);
    if (std::find(workload.goals.begin(), workload.goals.end(), goal) ==
        workload.goals.end())
      workload.goals.push_back(goal);
  }
  for (const auto& goal : workload.goals)
    workload.distance.push_back(
        reference.distances_to(reference.index.at(goal)));
  for (std::size_t i = 0; i < requests; ++i) {
    workload.requests.push_back({pick(start_role), i % goals});
    const auto cost = workload.expected(workload.requests.back());
    bench_check(cost != kInfinite);
    workload.cost_sum += cost;
  }
  return workload;
}
auto workload(std::size_t requests, std::size_t goals, bool refused = false)
    -> const Workload& {
  static std::map<std::tuple<std::size_t, std::size_t, bool>, Workload> cache;
  auto found = cache.find({requests, goals, refused});
  if (found == cache.end())
    found = cache
                .emplace(std::tuple{requests, goals, refused},
                         refused ? make_workload(requests, goals, Yard, Cellar)
                                 : make_workload(requests, goals, 4, 4))
                .first;
  return found->second;
}

// Order-sensitive fold over complete extracted transitions. The timed loops
// compute it while consuming; the un-timed checked pass recomputes it from an
// independently validated extraction, so a skipped or shortened extraction
// in the timed loop fails the equality check after the loop.
struct Fold {
  std::uint64_t hash = 14695981039346656037ULL, steps = 0, cost = 0;
  void mix(std::uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
  }
  void step(const tess::NavigationTransition& t) {
    mix(t.from.domain);
    mix(t.from.node);
    mix(t.to.domain);
    mix(t.to.node);
    mix(t.id);
    mix(t.cost);
    ++steps;
    cost += t.cost;
  }
  void finish(std::uint64_t request_cost) {
    mix(request_cost);
    mix(0xA5);
  }
  auto operator==(const Fold&) const -> bool = default;
};
// Timed consumption: the complete ordered route, then the request total.
void consume_route(const tess::NavigationResult& result, Fold& fold) {
  for (const auto& t : result.route) fold.step(t);
  fold.finish(result.cost);
}
// Timed consumption: follow guidance successors from start to the goal.
void follow_guidance(const tess::NavigationResult& field,
                     tess::NavigationLocation start,
                     tess::NavigationLocation goal, Fold& fold) {
  const auto* origin = field.guidance_at(start);
  if (origin == nullptr) std::abort();
  auto at = start;
  const auto* label = origin;
  for (std::size_t steps = 0; label->has_next; ++steps) {
    if (steps == kLimits.max_route_steps) std::abort();
    fold.step(label->next);
    at = label->next.to;
    label = field.guidance_at(at);
    if (label == nullptr) std::abort();
  }
  if (at != goal) std::abort();
  fold.finish(origin->cost);
}

// Un-timed checked extraction against the oracle.
struct Checker {
  explicit Checker(const Fixture& input) : fixture(input) {}
  const Fixture& fixture;
  const Oracle& reference = oracle();
  void transition(const tess::NavigationTransition& t,
                  tess::NavigationLocation cursor) {
    bench_check(t.from == cursor);
    bench_check(t.availability == tess::NavigationAvailability::Legal);
    bench_check(t.connection == (t.from.domain != t.to.domain));
    const auto from = fixture.relative(t.from), to = fixture.relative(t.to);
    const auto found = reference.by_endpoints.find({from, to});
    bench_check(found != reference.by_endpoints.end());
    bool matched = false;
    for (const auto e : found->second) {
      const auto& edge = reference.edges[e];
      if (edge.cost != t.cost) continue;
      if (edge.id != t.id || edge.key != t.key) continue;
      bench_check(edge.identified == (t.connection || from.domain == Road));
      matched = true;
    }
    bench_check(matched);
  }
  void route(const tess::NavigationResult& result, const Request& request,
             const Workload& load, Fold& fold) {
    bench_check(result.outcome == tess::NavigationOutcome::Found &&
                result.proof == tess::NavigationProof::ExactSnapshot &&
                result.cost == load.expected(request) &&
                result.current_for(fixture.snapshot) &&
                result.guidance.empty());
    auto cursor = fixture.absolute(request.start);
    std::uint64_t cost = 0;
    for (const auto& t : result.route) {
      transition(t, cursor);
      cost += t.cost;
      cursor = t.to;
      fold.step(t);
    }
    bench_check(cursor == fixture.absolute(load.goals[request.goal]) &&
                cost == result.cost);
    fold.finish(result.cost);
  }
  void guidance(const tess::NavigationResult& field, const Request& request,
                const Workload& load, Fold& fold) {
    bench_check(field.outcome == tess::NavigationOutcome::Found &&
                field.proof == tess::NavigationProof::ExactSnapshot &&
                field.current_for(fixture.snapshot) && field.route.empty());
    const auto goal = fixture.absolute(load.goals[request.goal]);
    auto at = fixture.absolute(request.start);
    const auto* label = field.guidance_at(at);
    bench_check(label != nullptr && label->cost == load.expected(request));
    const auto origin_cost = label->cost;
    std::uint64_t cost = 0, steps = 0;
    while (label->has_next) {
      bench_check(++steps <= reference.states.size());
      transition(label->next, at);
      at = label->next.to;
      const auto* downstream = field.guidance_at(at);
      bench_check(downstream != nullptr &&
                  label->cost == label->next.cost + downstream->cost);
      cost += label->next.cost;
      fold.step(label->next);
      label = downstream;
    }
    bench_check(at == goal && label->cost == 0 && cost == origin_cost);
    fold.finish(origin_cost);
  }
  // Every legal state must carry a label: the field is complete guidance.
  void complete_field(const tess::NavigationResult& field,
                      const std::vector<std::uint64_t>& distances) {
    bench_check(field.guidance.size() == reference.states.size());
    for (const auto& state : reference.states) {
      const auto* label = field.guidance_at(fixture.absolute(state));
      bench_check(label != nullptr &&
                  label->cost == distances[reference.index.at(state)]);
    }
  }
};

using Result = std::shared_ptr<const tess::NavigationResult>;
auto build_field(const Fixture& fixture, tess::NavigationLocation goal,
                 tess::NavigationLimits limits = kLimits) -> Result {
  tess::NavigationQuery query(fixture.snapshot, fixture.absolute(goal), limits);
  while (query.state() == tess::AsyncResultState::Pending)
    query.advance({1024});
  return query.retained_result();
}
auto build_fields(const Fixture& fixture, const Workload& load,
                  std::uint64_t& work) -> std::vector<Result> {
  std::vector<Result> fields;
  fields.reserve(load.goals.size());
  for (const auto& goal : load.goals) {
    fields.push_back(build_field(fixture, goal));
    if (!fields.back() ||
        fields.back()->outcome != tess::NavigationOutcome::Found)
      std::abort();
    work += fields.back()->work_items;
  }
  return fields;
}
auto route_once(const Fixture& fixture, const Workload& load,
                const Request& request, tess::NavigationLimits limits = kLimits)
    -> Result {
  return tess::navigation_route(
      fixture.snapshot, fixture.absolute(request.start),
      fixture.absolute(load.goals[request.goal]), limits);
}
auto payload(const std::vector<Result>& results) -> std::size_t {
  std::size_t total = 0;
  for (const auto& r : results) total += r->payload_bytes();
  return total;
}

enum class Mode {
  Setup,          // preparation only
  Routes,         // cold: setup + R route queries + full extraction
  RoutesQuery,    // prepared snapshot: R route queries, cost sum only
  RoutesExtract,  // R retained routes: extraction only
  Shared,         // cold: setup + G reverse fields + R guidance walks
  SharedQuery,    // prepared snapshot: G fields (previous G released)
  SharedWarm,     // retained fields: R guidance walks only (= extraction)
  SharedReplace,  // retained fields -> new snapshot: stale check, release,
                  // rebuild G fields, R walks
  Refused         // prepared snapshot, limits {256,256,256}: R refused routes
                  // + one refused field
};

struct Counters {
  std::uint64_t work = 0, transitions = 0;
  std::size_t retained = 0, transient = 0, labels = 0;
};

void BM_navigation_scale(benchmark::State& state, Mode mode,
                         std::size_t requests, std::size_t goals) {
  const auto& load = workload(requests, goals, mode == Mode::Refused);
  Fixture fixture;
  std::vector<Result> fields;
  std::vector<Result> routes;
  const bool prepared =
      mode != Mode::Setup && mode != Mode::Routes && mode != Mode::Shared;
  if (prepared) fixture = make_fixture();
  Counters counters;
  if (mode == Mode::SharedWarm || mode == Mode::SharedReplace)
    fields = build_fields(fixture, load, counters.work);
  if (mode == Mode::RoutesExtract)
    for (const auto& request : load.requests)
      routes.push_back(route_once(fixture, load, request));
  Fold fold;
  std::uint64_t cost_sum = 0;
#if TESS_DIAGNOSTICS_ENABLED
  tess::diagnostics::AllocationCounters allocations;
#endif
  for (auto _ : state) {
    (void)_;
    // Cold modes release the previous iteration's retained snapshot/fields
    // first (timed, but before the allocation scope), so live allocator
    // bytes at scope end describe only what this iteration retains.
    if (mode == Mode::Setup || mode == Mode::Routes || mode == Mode::Shared) {
      fields.clear();
      fixture = {};
    }
#if TESS_DIAGNOSTICS_ENABLED
    allocations.reset();
    tess::diagnostics::ScopedAllocationCounters scope{allocations};
#endif
    fold = {};
    cost_sum = 0;
    counters.transitions = 0;
    counters.transient = 0;
    counters.work = 0;
    switch (mode) {
      case Mode::Setup:
        fixture = make_fixture();
        benchmark::DoNotOptimize(fixture.snapshot.get());
        break;
      case Mode::Routes:
        fixture = make_fixture();
        [[fallthrough]];
      case Mode::RoutesQuery:
        for (const auto& request : load.requests) {
          const auto result = route_once(fixture, load, request);
          if (!result || result->outcome != tess::NavigationOutcome::Found)
            std::abort();
          counters.work += result->work_items;
          counters.transient =
              std::max(counters.transient, result->payload_bytes());
          if (mode == Mode::Routes) consume_route(*result, fold);
          cost_sum += result->cost;
        }
        break;
      case Mode::RoutesExtract:
        for (const auto& result : routes) consume_route(*result, fold);
        break;
      case Mode::SharedReplace: {
        auto replacement = make_fixture();
        for (const auto& field : fields)
          if (field->current_for(replacement.snapshot)) std::abort();
        // Explicit release outside any budgeted step. It runs inside the
        // allocation scope on purpose (host order: build the replacement,
        // detect stale results, then release), so the hook's live-byte clamp
        // drops the replacement snapshot's bytes from this mode's
        // retained_live_allocation_bytes. The fields' bytes are unaffected by
        // the clamp, apart from the previous vector buffer.
        fields.clear();
        fixture = std::move(replacement);
        counters.work = 0;
        fields = build_fields(fixture, load, counters.work);
        for (const auto& request : load.requests)
          follow_guidance(*fields[request.goal],
                          fixture.absolute(request.start),
                          fixture.absolute(load.goals[request.goal]), fold);
        break;
      }
      case Mode::Shared:
        fixture = make_fixture();
        [[fallthrough]];
      case Mode::SharedQuery:
        fields.clear();
        fields = build_fields(fixture, load, counters.work);
        if (mode == Mode::SharedQuery) break;
        [[fallthrough]];
      case Mode::SharedWarm:
        for (const auto& request : load.requests)
          follow_guidance(*fields[request.goal],
                          fixture.absolute(request.start),
                          fixture.absolute(load.goals[request.goal]), fold);
        break;
      case Mode::Refused: {
        for (const auto& request : load.requests) {
          const auto result =
              route_once(fixture, load, request, kRefusedLimits);
          if (!result ||
              result->outcome != tess::NavigationOutcome::CapacityExceeded ||
              !result->route.empty() || !result->guidance.empty())
            std::abort();
          counters.work += result->work_items;
        }
        const auto field =
            build_field(fixture, load.goals.front(), kRefusedLimits);
        if (!field ||
            field->outcome != tess::NavigationOutcome::CapacityExceeded ||
            !field->guidance.empty())
          std::abort();
        counters.work += field->work_items;
        break;
      }
    }
    benchmark::DoNotOptimize(fold.hash);
    benchmark::DoNotOptimize(cost_sum);
  }
  // Checked pass: independent re-extraction validated against the oracle,
  // then fold equality with the timed consumption.
  bench_check(fixture.snapshot && fixture.snapshot->valid() &&
              fixture.snapshot->complete());
  Checker checker{fixture};
  Fold checked;
  switch (mode) {
    case Mode::Setup:
      break;
    case Mode::Refused:
      bench_check(
          counters.work <=
          (load.requests.size() + 1) *
              refusal_work_bound(kRefusedLimits, 1, kCompositionMaxDegree));
      break;
    case Mode::Routes:
    case Mode::RoutesQuery:
    case Mode::RoutesExtract:
      for (std::size_t i = 0; i < load.requests.size(); ++i) {
        const auto result = mode == Mode::RoutesExtract
                                ? routes[i]
                                : route_once(fixture, load, load.requests[i]);
        bench_check(result != nullptr);
        checker.route(*result, load.requests[i], load, checked);
      }
      if (mode == Mode::RoutesQuery)
        bench_check(cost_sum == load.cost_sum);
      else
        bench_check(fold == checked && fold.cost == load.cost_sum);
      counters.retained = mode == Mode::RoutesExtract ? payload(routes) : 0;
      break;
    case Mode::Shared:
    case Mode::SharedQuery:
    case Mode::SharedWarm:
    case Mode::SharedReplace:
      bench_check(fields.size() == load.goals.size());
      for (std::size_t i = 0; i < fields.size(); ++i) {
        checker.complete_field(*fields[i], load.distance[i]);
        counters.labels += fields[i]->guidance.size();
      }
      if (mode != Mode::SharedQuery) {
        for (const auto& request : load.requests)
          checker.guidance(*fields[request.goal], request, load, checked);
        bench_check(fold == checked && fold.cost == load.cost_sum);
      }
      counters.retained = payload(fields);
      break;
  }
  counters.transitions = fold.steps;
  state.counters["work_items"] = static_cast<double>(counters.work);
  state.counters["transitions"] = static_cast<double>(counters.transitions);
  state.counters["retained_payload_bytes"] =
      static_cast<double>(counters.retained);
  state.counters["transient_payload_bytes"] =
      static_cast<double>(counters.transient);
  state.counters["guidance_labels"] = static_cast<double>(counters.labels);
  const auto limits = mode == Mode::Refused ? kRefusedLimits : kLimits;
  if (mode == Mode::Refused)
    state.counters["refusal_work_bound"] = static_cast<double>(
        (load.requests.size() + 1) *
        refusal_work_bound(kRefusedLimits, 1, kCompositionMaxDegree));
  state.counters["limit_nodes"] = static_cast<double>(limits.max_nodes);
  state.counters["limit_frontier"] = static_cast<double>(limits.max_frontier);
  state.counters["limit_route_steps"] =
      static_cast<double>(limits.max_route_steps);
#if TESS_DIAGNOSTICS_ENABLED
  // Allocator measurements for the last timed iteration, distinct from the
  // logical payload/capacity counters above. Peak/live bytes are the hook's
  // best-effort bookkeeping: an unsized delete subtracts nothing, so both are
  // upper bounds and equal the cumulative total on toolchains without sized
  // deallocation. Live bytes at scope end approximate what the iteration
  // retained (except shared_replace, see its release comment), peak minus
  // live the transient scratch. Neither is process RSS.
  state.counters["allocations"] = static_cast<double>(allocations.allocations);
  state.counters["allocation_bytes"] =
      static_cast<double>(allocations.allocation_bytes);
  state.counters["peak_live_allocation_bytes"] =
      static_cast<double>(allocations.peak_live_bytes);
  state.counters["retained_live_allocation_bytes"] =
      static_cast<double>(allocations.live_bytes);
#endif
}

// Large-world admission: one open 256x256 always-resident unit grid (65536
// states, 256 chunks) with no attachments. The refused cell caps discovered
// labels far below the world; the found cell has room for the whole world
// and its route is checked against the exact Manhattan oracle for an open
// unit grid. Both include world fill, adapter and snapshot preparation.
struct LargePassable {};
using LargeShape =
    tess::Shape<tess::Extent3{256, 256, 1}, tess::Extent3{16, 16, 1}>;
using LargeWorld = tess::AlwaysResidentWorld<
    LargeShape, tess::FieldSchema<tess::Field<LargePassable, bool>>>;
using LargeGrid =
    tess::NavigationGrid<LargeWorld,
                         tess::movement::UnitCostFieldMovement<LargePassable>>;
constexpr std::int64_t kLargeSize = 256;
constexpr tess::NavigationLimits kLargeRefused{4096, 8192, 4096};
constexpr tess::NavigationLimits kLargeAdmitted{65536, 131072, 65536};
void BM_navigation_scale_admission(benchmark::State& state, bool admitted) {
  const auto limits = admitted ? kLargeAdmitted : kLargeRefused;
  std::shared_ptr<const LargeGrid> grid;
  Result result;
  std::uint64_t work = 0;
#if TESS_DIAGNOSTICS_ENABLED
  tess::diagnostics::AllocationCounters allocations;
#endif
  for (auto _ : state) {
    (void)_;
    result.reset();
    grid.reset();
#if TESS_DIAGNOSTICS_ENABLED
    allocations.reset();
    tess::diagnostics::ScopedAllocationCounters scope{allocations};
#endif
    auto world = std::make_shared<LargeWorld>();
    world->fill_field<LargePassable>(true);
    grid = std::make_shared<LargeGrid>(std::move(world), 1);
    auto snapshot = std::make_shared<tess::NavigationSnapshot>(
        std::vector<std::shared_ptr<const tess::NavigationDomain>>{grid});
    result = tess::navigation_route(
        snapshot, grid->coordinate_location({0, 0, 0}),
        grid->coordinate_location({kLargeSize - 1, kLargeSize - 1, 0}), limits);
    if (!result) std::abort();
    work = result->work_items;
    benchmark::DoNotOptimize(work);
  }
  bench_check(result && result->current_for(result->snapshot));
  if (admitted) {
    // Open unit grid: exact cost is the Manhattan distance; every step must
    // be one face step within the world and chain from start to goal.
    const auto expected = static_cast<std::uint64_t>(2 * (kLargeSize - 1));
    bench_check(result->outcome == tess::NavigationOutcome::Found &&
                result->proof == tess::NavigationProof::ExactSnapshot &&
                result->cost == expected && result->route.size() == expected);
    auto cursor = grid->coordinate_location({0, 0, 0});
    auto at = tess::Coord3{0, 0, 0};
    for (const auto& t : result->route) {
      bench_check(t.from == cursor && !t.connection && t.cost == 1 &&
                  t.availability == tess::NavigationAvailability::Legal);
      const auto to =
          tess::coord<LargeShape>(tess::TileKey<LargeShape>{t.to.node});
      const auto dx = to.x - at.x, dy = to.y - at.y;
      bench_check((dx == 0 || dy == 0) && dx * dx + dy * dy == 1 && to.z == 0 &&
                  to.x >= 0 && to.y >= 0 && to.x < kLargeSize &&
                  to.y < kLargeSize);
      at = to;
      cursor = t.to;
    }
    bench_check(cursor ==
                grid->coordinate_location({kLargeSize - 1, kLargeSize - 1, 0}));
  } else {
    bench_check(result->outcome == tess::NavigationOutcome::CapacityExceeded &&
                result->route.empty() && result->guidance.empty());
    // Open grid: four regular candidates per state, one goal.
    bench_check(work <= refusal_work_bound(kLargeRefused, 1, 4));
    state.counters["refusal_work_bound"] =
        static_cast<double>(refusal_work_bound(kLargeRefused, 1, 4));
  }
  state.counters["work_items"] = static_cast<double>(work);
  state.counters["transitions"] = static_cast<double>(result->route.size());
  state.counters["retained_payload_bytes"] =
      static_cast<double>(result->payload_bytes());
  state.counters["limit_nodes"] = static_cast<double>(limits.max_nodes);
  state.counters["limit_frontier"] = static_cast<double>(limits.max_frontier);
  state.counters["limit_route_steps"] =
      static_cast<double>(limits.max_route_steps);
#if TESS_DIAGNOSTICS_ENABLED
  state.counters["allocations"] = static_cast<double>(allocations.allocations);
  state.counters["allocation_bytes"] =
      static_cast<double>(allocations.allocation_bytes);
  state.counters["peak_live_allocation_bytes"] =
      static_cast<double>(allocations.peak_live_bytes);
  state.counters["retained_live_allocation_bytes"] =
      static_cast<double>(allocations.live_bytes);
#endif
}
}  // namespace scale

BENCHMARK_CAPTURE(scale::BM_navigation_scale, setup, scale::Mode::Setup,
                  std::size_t{32}, std::size_t{4})
    ->Name("lab/navigation/scale_setup");
BENCHMARK_CAPTURE(scale::BM_navigation_scale, routes_32r_4g,
                  scale::Mode::Routes, std::size_t{32}, std::size_t{4})
    ->Name("lab/navigation/scale_routes_32r_4g");
BENCHMARK_CAPTURE(scale::BM_navigation_scale, shared_32r_4g,
                  scale::Mode::Shared, std::size_t{32}, std::size_t{4})
    ->Name("lab/navigation/scale_shared_32r_4g");
BENCHMARK_CAPTURE(scale::BM_navigation_scale, shared_warm_32r_4g,
                  scale::Mode::SharedWarm, std::size_t{32}, std::size_t{4})
    ->Name("lab/navigation/scale_shared_warm_32r_4g");
BENCHMARK_CAPTURE(scale::BM_navigation_scale, routes_256r_16g,
                  scale::Mode::Routes, std::size_t{256}, std::size_t{16})
    ->Name("lab/navigation/scale_routes_256r_16g");
BENCHMARK_CAPTURE(scale::BM_navigation_scale, routes_query_256r_16g,
                  scale::Mode::RoutesQuery, std::size_t{256}, std::size_t{16})
    ->Name("lab/navigation/scale_routes_query_256r_16g");
BENCHMARK_CAPTURE(scale::BM_navigation_scale, routes_extract_256r_16g,
                  scale::Mode::RoutesExtract, std::size_t{256}, std::size_t{16})
    ->Name("lab/navigation/scale_routes_extract_256r_16g");
BENCHMARK_CAPTURE(scale::BM_navigation_scale, shared_256r_16g,
                  scale::Mode::Shared, std::size_t{256}, std::size_t{16})
    ->Name("lab/navigation/scale_shared_256r_16g");
BENCHMARK_CAPTURE(scale::BM_navigation_scale, shared_query_256r_16g,
                  scale::Mode::SharedQuery, std::size_t{256}, std::size_t{16})
    ->Name("lab/navigation/scale_shared_query_256r_16g");
BENCHMARK_CAPTURE(scale::BM_navigation_scale, shared_warm_256r_16g,
                  scale::Mode::SharedWarm, std::size_t{256}, std::size_t{16})
    ->Name("lab/navigation/scale_shared_warm_256r_16g");
BENCHMARK_CAPTURE(scale::BM_navigation_scale, shared_replace_256r_16g,
                  scale::Mode::SharedReplace, std::size_t{256}, std::size_t{16})
    ->Name("lab/navigation/scale_shared_replace_256r_16g");
BENCHMARK_CAPTURE(scale::BM_navigation_scale, routes_1024r_64g,
                  scale::Mode::Routes, std::size_t{1024}, std::size_t{64})
    ->Name("lab/navigation/scale_routes_1024r_64g");
BENCHMARK_CAPTURE(scale::BM_navigation_scale, shared_1024r_64g,
                  scale::Mode::Shared, std::size_t{1024}, std::size_t{64})
    ->Name("lab/navigation/scale_shared_1024r_64g");
BENCHMARK_CAPTURE(scale::BM_navigation_scale, shared_warm_1024r_64g,
                  scale::Mode::SharedWarm, std::size_t{1024}, std::size_t{64})
    ->Name("lab/navigation/scale_shared_warm_1024r_64g");
BENCHMARK_CAPTURE(scale::BM_navigation_scale, refused_32r_4g,
                  scale::Mode::Refused, std::size_t{32}, std::size_t{4})
    ->Name("lab/navigation/scale_refused_32r_4g");
BENCHMARK_CAPTURE(scale::BM_navigation_scale_admission, refused_256x256, false)
    ->Name("lab/navigation/scale_admission_refused_256x256");
BENCHMARK_CAPTURE(scale::BM_navigation_scale_admission, admitted_256x256, true)
    ->Name("lab/navigation/scale_admission_admitted_256x256");
}  // namespace
