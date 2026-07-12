#include <benchmark/benchmark.h>
#include <tess/tess.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <span>
#include <vector>

namespace {

using PathScaleShape =
    tess::Shape<tess::Extent3{512, 512, 1}, tess::Extent3{32, 32, 1}>;

struct CostTag {};
struct PassableTag {};

using WeightedPathSchema =
    tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>,
                      tess::Field<CostTag, std::uint32_t>>;
using WeightedPathScaleWorld =
    tess::AlwaysResidentWorld<PathScaleShape, WeightedPathSchema>;

template <typename Shape>
[[nodiscard]] constexpr auto path_node_count() noexcept -> std::size_t {
  return static_cast<std::size_t>(tess::ShapeTraits<Shape>::size.x *
                                  tess::ShapeTraits<Shape>::size.y *
                                  tess::ShapeTraits<Shape>::size.z);
}

void fill_path_passable(WeightedPathScaleWorld& world, std::uint8_t value) {
  for (auto& page : world.chunks()) {
    auto passable = page.template field_span<PassableTag>();
    for (auto& tile : passable) {
      tile = value;
    }
  }
}

void fill_path_cost(WeightedPathScaleWorld& world, std::uint32_t value) {
  for (auto& page : world.chunks()) {
    auto costs = page.template field_span<CostTag>();
    for (auto& tile : costs) {
      tile = value;
    }
  }
}

[[nodiscard]] constexpr auto path_hash(std::uint64_t x,
                                       std::uint64_t y) noexcept
    -> std::uint64_t {
  auto value = x * 0x9e3779b97f4a7c15ull;
  value ^= y + 0xbf58476d1ce4e5b9ull + (value << 6u) + (value >> 2u);
  value ^= value >> 30u;
  value *= 0xbf58476d1ce4e5b9ull;
  value ^= value >> 27u;
  value *= 0x94d049bb133111ebull;
  value ^= value >> 31u;
  return value;
}

template <typename World>
void carve_sparse_blockers(World& world) {
  for (std::int64_t y = 1;
       y + 1 < static_cast<std::int64_t>(PathScaleShape::size.y); ++y) {
    for (std::int64_t x = 1;
         x + 1 < static_cast<std::int64_t>(PathScaleShape::size.x); ++x) {
      const auto hash = path_hash(static_cast<std::uint64_t>(x),
                                  static_cast<std::uint64_t>(y));
      if (hash % 100u < 18u) {
        world.template field<PassableTag>(tess::Coord3{x, y, 0}) = 0;
      }
      if (hash % 100u >= 18u && hash % 100u < 32u) {
        world.template field<CostTag>(tess::Coord3{x, y, 0}) = 7;
      }
    }
  }

  for (std::int64_t x = 32; x < 512; x += 64) {
    world.template field<PassableTag>(tess::Coord3{x, 1, 0}) = 0;
  }
  for (std::int64_t y = 64; y < 512; y += 64) {
    world.template field<PassableTag>(tess::Coord3{510, y, 0}) = 0;
  }
}

void carve_room_portals(WeightedPathScaleWorld& world) {
  constexpr auto room_size = std::int64_t{32};
  for (std::int64_t x = room_size;
       x < static_cast<std::int64_t>(PathScaleShape::size.x); x += room_size) {
    for (std::int64_t y = 0;
         y < static_cast<std::int64_t>(PathScaleShape::size.y); ++y) {
      const auto room = y / room_size;
      const auto portal = (room * 23 + x / room_size * 17) % room_size;
      if (y % room_size != portal) {
        world.template field<PassableTag>(tess::Coord3{x, y, 0}) = 0;
      } else {
        world.template field<CostTag>(tess::Coord3{x, y, 0}) = 5;
      }
    }
  }
  for (std::int64_t y = room_size;
       y < static_cast<std::int64_t>(PathScaleShape::size.y); y += room_size) {
    for (std::int64_t x = 0;
         x < static_cast<std::int64_t>(PathScaleShape::size.x); ++x) {
      const auto room = x / room_size;
      const auto portal = (room * 29 + y / room_size * 19) % room_size;
      if (x % room_size != portal) {
        world.template field<PassableTag>(tess::Coord3{x, y, 0}) = 0;
      } else {
        world.template field<CostTag>(tess::Coord3{x, y, 0}) = 5;
      }
    }
  }
}

auto make_room_portal_waypoints(tess::PathRequest request)
    -> std::vector<tess::Coord3> {
  constexpr auto room_size = std::int64_t{32};
  std::vector<tess::Coord3> waypoints;
  auto room_x = request.start.x / room_size;
  auto room_y = request.start.y / room_size;
  const auto goal_room_x = request.goal.x / room_size;
  const auto goal_room_y = request.goal.y / room_size;

  while (room_x != goal_room_x) {
    const auto next_room_x = room_x + (room_x < goal_room_x ? 1 : -1);
    const auto wall_x =
        room_x < goal_room_x ? next_room_x * room_size : room_x * room_size;
    const auto wall_index = wall_x / room_size;
    const auto portal_y =
        room_y * room_size + (room_y * 23 + wall_index * 17) % room_size;
    waypoints.push_back(tess::Coord3{wall_x, portal_y, 0});
    room_x = next_room_x;
  }

  while (room_y != goal_room_y) {
    const auto next_room_y = room_y + (room_y < goal_room_y ? 1 : -1);
    const auto wall_y =
        room_y < goal_room_y ? next_room_y * room_size : room_y * room_size;
    const auto wall_index = wall_y / room_size;
    const auto portal_x =
        room_x * room_size + (room_x * 29 + wall_index * 19) % room_size;
    waypoints.push_back(tess::Coord3{portal_x, wall_y, 0});
    room_y = next_room_y;
  }

  return waypoints;
}

template <std::size_t Count>
auto make_shared_room_portal_requests()
    -> std::array<tess::PathRequest, Count> {
  auto requests = std::array<tess::PathRequest, Count>{};
  const auto goal = tess::Coord3{510, 510, 0};
  for (std::size_t i = 0; i < Count; ++i) {
    requests[i] = tess::PathRequest{
        tess::Coord3{1 + static_cast<std::int64_t>(i % 10u),
                     1 + static_cast<std::int64_t>(i / 10u), 0},
        goal};
  }
  return requests;
}

void record_path_counters(benchmark::State& state,
                          const tess::PathResult& result) {
  state.counters["cost"] = static_cast<double>(result.cost);
  state.counters["path_nodes"] = static_cast<double>(result.path.size());
  state.counters["expanded_nodes"] = static_cast<double>(result.expanded_nodes);
  state.counters["reached_nodes"] = static_cast<double>(result.reached_nodes);
}

// Correctness checks mandated by docs/planning/benchmark-plan.md run outside
// the timed regions; a failed check aborts the benchmark binary so threshold
// runs cannot silently gate on wrong results.
void bench_check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "tess_bench correctness check failed: %s\n", message);
    std::abort();
  }
}

// Validates a Found route: endpoints match the request and every step is a
// unit move onto a passable tile.
template <typename World>
void check_found_route(World& world, const tess::PathResult& result,
                       tess::PathRequest request) {
  bench_check(result.status == tess::PathStatus::Found,
              "route status is not Found");
  bench_check(!result.path.empty(), "found route is empty");
  bench_check(result.path.front() == request.start,
              "route does not begin at the requested start");
  bench_check(result.path.back() == request.goal,
              "route does not end at the requested goal");
  for (std::size_t i = 1; i < result.path.size(); ++i) {
    bench_check(
        tess::manhattan_distance(result.path[i - 1], result.path[i]) == 1,
        "route contains a non-unit step");
    bench_check(world.template field<PassableTag>(result.path[i]) != 0,
                "route crosses an impassable tile");
  }
}

// Additionally pins the cost to an expected value captured from an untimed
// setup run of the same deterministic search.
template <typename World>
void check_found_route(World& world, const tess::PathResult& result,
                       tess::PathRequest request, std::uint64_t expected_cost) {
  check_found_route(world, result, request);
  bench_check(result.cost == expected_cost,
              "route cost differs from the expected setup-run cost");
}

// Untimed reference run used by batch benchmarks to pin the last request's
// cost for the post-loop correctness check.
template <typename World>
[[nodiscard]] auto expected_weighted_path_cost(World& world,
                                               tess::PathRequest request)
    -> std::uint64_t {
  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  const auto result = tess::weighted_astar_path<World, PassableTag, CostTag>(
      world, request, scratch);
  bench_check(result.status == tess::PathStatus::Found,
              "setup reference path not found");
  return result.cost;
}

// Mirrors the grouped multigoal batch loops: the last processed request is
// the last request whose goal matches the final first-occurrence goal group.
template <std::size_t Count>
[[nodiscard]] auto last_grouped_request(
    const std::array<tess::PathRequest, Count>& requests) -> tess::PathRequest {
  auto last_request = requests.front();
  for (std::size_t i = 0; i < requests.size(); ++i) {
    auto already_built = false;
    for (std::size_t j = 0; j < i; ++j) {
      already_built = already_built || requests[j].goal == requests[i].goal;
    }
    if (already_built) {
      continue;
    }
    for (const auto request : requests) {
      if (request.goal == requests[i].goal) {
        last_request = request;
      }
    }
  }
  return last_request;
}

void record_portal_product_counters(
    benchmark::State& state, const tess::WeightedPortalRouteProduct& product,
    const tess::PathResult& result, std::uint32_t optimal_cost) {
  state.counters["portal.waypoints"] =
      static_cast<double>(product.waypoints().size());
  state.counters["portal.route_candidates"] =
      static_cast<double>(product.route_candidates());
  state.counters["portal.scan_tiles"] =
      static_cast<double>(product.portal_scan_tiles());
  state.counters["route.optimal_cost"] = static_cast<double>(optimal_cost);
  if (optimal_cost != 0) {
    state.counters["route.cost_ratio"] =
        static_cast<double>(result.cost) / static_cast<double>(optimal_cost);
  }
}

template <std::size_t Count>
void record_batch_counters(benchmark::State& state, std::uint64_t total_cost,
                           std::uint64_t total_expanded) {
  state.counters["agents"] = static_cast<double>(Count);
  // Whole-batch aggregates: the per-request counter names (cost,
  // expanded_nodes, ...) previously published the LAST request only, which
  // made per-node timing math ~100x off for 100-request batches.
  state.counters["batch.cost_total"] = static_cast<double>(total_cost);
  state.counters["batch.expanded_total"] = static_cast<double>(total_expanded);
  state.counters["batch.avg_expanded_nodes"] =
      static_cast<double>(total_expanded) / static_cast<double>(Count);
}

#ifdef TESS_ENABLE_DIAGNOSTICS
void record_path_diagnostic_counters(
    benchmark::State& state, const tess::diagnostics::PathCounters& counters) {
  state.counters["diag.cost_reads"] = static_cast<double>(counters.cost_reads);
  state.counters["diag.heap_pops"] = static_cast<double>(counters.heap_pops);
  state.counters["diag.heap_pushes"] =
      static_cast<double>(counters.heap_pushes);
  state.counters["diag.neighbor_candidates"] =
      static_cast<double>(counters.neighbor_candidates);
  state.counters["diag.passability_checks"] =
      static_cast<double>(counters.passability_checks);
  state.counters["diag.relax_attempts"] =
      static_cast<double>(counters.relax_attempts);
  state.counters["diag.relax_successes"] =
      static_cast<double>(counters.relax_successes);
}

#define TESS_PATH_DIAG_DECL() tess::diagnostics::PathCounters path_counters;
#define TESS_PATH_DIAG_RESET() path_counters.reset()
#define TESS_PATH_DIAG_RUN(...)                                      \
  do {                                                               \
    tess::diagnostics::ScopedPathCounters path_scope{path_counters}; \
    __VA_ARGS__;                                                     \
  } while (false)
#define TESS_PATH_DIAG_RECORD(state) \
  record_path_diagnostic_counters((state), path_counters)
#else
#define TESS_PATH_DIAG_DECL() \
  do {                        \
  } while (false)
#define TESS_PATH_DIAG_RESET() \
  do {                         \
  } while (false)
#define TESS_PATH_DIAG_RUN(...) \
  do {                          \
    __VA_ARGS__;                \
  } while (false)
#define TESS_PATH_DIAG_RECORD(state) \
  do {                               \
  } while (false)
#endif

auto make_shared_sparse_requests(WeightedPathScaleWorld& world)
    -> std::array<tess::PathRequest, 100> {
  std::array<tess::PathRequest, 100> requests{};
  const auto goal = tess::Coord3{510, 510, 0};
  world.template field<PassableTag>(goal) = 1;
  world.template field<CostTag>(goal) = 1;
  for (std::size_t i = 0; i < requests.size(); ++i) {
    const auto x = static_cast<std::int64_t>(1 + i % 10);
    const auto y = static_cast<std::int64_t>(1 + i / 10);
    const auto start = tess::Coord3{x, y, 0};
    world.template field<PassableTag>(start) = 1;
    world.template field<CostTag>(start) = 1;
    requests[i] = tess::PathRequest{start, goal};
  }
  return requests;
}

template <typename World>
auto make_multigoal_sparse_requests(World& world)
    -> std::array<tess::PathRequest, 100> {
  constexpr auto goals = std::array{
      tess::Coord3{510, 510, 0}, tess::Coord3{480, 510, 0},
      tess::Coord3{510, 480, 0}, tess::Coord3{448, 510, 0},
      tess::Coord3{510, 448, 0}, tess::Coord3{416, 510, 0},
      tess::Coord3{510, 416, 0}, tess::Coord3{384, 510, 0},
  };
  std::array<tess::PathRequest, 100> requests{};
  for (const auto goal : goals) {
    world.template field<PassableTag>(goal) = 1;
    world.template field<CostTag>(goal) = 1;
  }
  for (std::size_t i = 0; i < requests.size(); ++i) {
    const auto offset = static_cast<std::int64_t>(i);
    const auto start = tess::Coord3{1 + offset % 10, 1 + offset / 10, 0};
    world.template field<PassableTag>(start) = 1;
    world.template field<CostTag>(start) = 1;
    requests[i] = tess::PathRequest{start, goals[i % goals.size()]};
  }
  return requests;
}

template <std::size_t Count>
[[nodiscard]] auto unique_goal_count(
    const std::array<tess::PathRequest, Count>& requests) -> std::size_t {
  std::array<tess::Coord3, Count> goals{};
  std::size_t count = 0;
  for (const auto request : requests) {
    auto found = false;
    for (std::size_t i = 0; i < count; ++i) {
      found = found || goals[i] == request.goal;
    }
    if (!found) {
      goals[count] = request.goal;
      ++count;
    }
  }
  return count;
}

void BM_path_weighted_portal_product_room_portals_512x512(
    benchmark::State& state) {
  WeightedPathScaleWorld world;
  fill_path_passable(world, 1);
  fill_path_cost(world, 1);
  carve_room_portals(world);
  const auto request =
      tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{510, 510, 0}};
  const auto waypoints = make_room_portal_waypoints(request);

  tess::PathScratch optimal_scratch;
  optimal_scratch.reserve_nodes(path_node_count<PathScaleShape>());
  const auto optimal =
      tess::weighted_astar_path<WeightedPathScaleWorld, PassableTag, CostTag>(
          world, request, optimal_scratch);
  bench_check(optimal.status == tess::PathStatus::Found,
              "setup optimal path not found");
  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  tess::WeightedPortalRouteProduct product;
  product.reserve_waypoints(waypoints.size());
  product.reserve_path_nodes(2048);
  product.reserve_dependencies(64);
  TESS_PATH_DIAG_DECL();
  tess::PathResult result;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    TESS_PATH_DIAG_RUN(result = tess::build_weighted_portal_route_product<
                           WeightedPathScaleWorld, PassableTag, CostTag>(
                           world, request, waypoints, scratch, product));
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  check_found_route(world, result, request);
  bench_check(result.cost >= optimal.cost,
              "portal route cost beat the optimal cost");
  record_path_counters(state, result);
  record_portal_product_counters(state, product, result, optimal.cost);
  TESS_PATH_DIAG_RECORD(state);
}

void BM_path_weighted_portal_product_replay_room_portals_512x512(
    benchmark::State& state) {
  WeightedPathScaleWorld world;
  fill_path_passable(world, 1);
  fill_path_cost(world, 1);
  carve_room_portals(world);
  const auto request =
      tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{510, 510, 0}};
  const auto waypoints = make_room_portal_waypoints(request);

  tess::PathScratch optimal_scratch;
  optimal_scratch.reserve_nodes(path_node_count<PathScaleShape>());
  const auto optimal =
      tess::weighted_astar_path<WeightedPathScaleWorld, PassableTag, CostTag>(
          world, request, optimal_scratch);
  bench_check(optimal.status == tess::PathStatus::Found,
              "setup optimal path not found");
  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  tess::WeightedPortalRouteProduct product;
  product.reserve_waypoints(waypoints.size());
  product.reserve_path_nodes(2048);
  product.reserve_dependencies(64);
  auto result =
      tess::build_weighted_portal_route_product<WeightedPathScaleWorld,
                                                PassableTag, CostTag>(
          world, request, waypoints, scratch, product);

  for (auto _ : state) {
    result = tess::weighted_portal_route_product_path(world, product);
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  check_found_route(world, result, request);
  bench_check(result.cost >= optimal.cost,
              "portal route cost beat the optimal cost");
  record_path_counters(state, result);
  record_portal_product_counters(state, product, result, optimal.cost);
}

void BM_path_weighted_chunk_portal_product_room_portals_512x512(
    benchmark::State& state) {
  WeightedPathScaleWorld world;
  fill_path_passable(world, 1);
  fill_path_cost(world, 1);
  carve_room_portals(world);
  const auto request =
      tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{510, 510, 0}};

  tess::PathScratch optimal_scratch;
  optimal_scratch.reserve_nodes(path_node_count<PathScaleShape>());
  const auto optimal =
      tess::weighted_astar_path<WeightedPathScaleWorld, PassableTag, CostTag>(
          world, request, optimal_scratch);
  bench_check(optimal.status == tess::PathStatus::Found,
              "setup optimal path not found");
  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  tess::WeightedPortalRouteProduct product;
  product.reserve_waypoints(32);
  product.reserve_path_nodes(2048);
  product.reserve_dependencies(64);
  TESS_PATH_DIAG_DECL();
  tess::PathResult result;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    TESS_PATH_DIAG_RUN(result = tess::build_weighted_chunk_portal_route_product<
                           WeightedPathScaleWorld, PassableTag, CostTag>(
                           world, request, scratch, product));
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  check_found_route(world, result, request);
  bench_check(result.cost >= optimal.cost,
              "portal route cost beat the optimal cost");
  record_path_counters(state, result);
  record_portal_product_counters(state, product, result, optimal.cost);
  TESS_PATH_DIAG_RECORD(state);
}

void BM_path_weighted_chunk_portal_product_replay_room_portals_512x512(
    benchmark::State& state) {
  WeightedPathScaleWorld world;
  fill_path_passable(world, 1);
  fill_path_cost(world, 1);
  carve_room_portals(world);
  const auto request =
      tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{510, 510, 0}};

  tess::PathScratch optimal_scratch;
  optimal_scratch.reserve_nodes(path_node_count<PathScaleShape>());
  const auto optimal =
      tess::weighted_astar_path<WeightedPathScaleWorld, PassableTag, CostTag>(
          world, request, optimal_scratch);
  bench_check(optimal.status == tess::PathStatus::Found,
              "setup optimal path not found");
  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  tess::WeightedPortalRouteProduct product;
  product.reserve_waypoints(32);
  product.reserve_path_nodes(2048);
  product.reserve_dependencies(64);
  auto result =
      tess::build_weighted_chunk_portal_route_product<WeightedPathScaleWorld,
                                                      PassableTag, CostTag>(
          world, request, scratch, product);

  for (auto _ : state) {
    result = tess::weighted_portal_route_product_path(world, product);
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  check_found_route(world, result, request);
  bench_check(result.cost >= optimal.cost,
              "portal route cost beat the optimal cost");
  record_path_counters(state, result);
  record_portal_product_counters(state, product, result, optimal.cost);
}

void BM_path_weighted_chunk_portal_candidates_room_portals_512x512(
    benchmark::State& state) {
  constexpr auto orders = std::array{
      std::array{tess::detail::Axis::X, tess::detail::Axis::Y,
                 tess::detail::Axis::Z},
      std::array{tess::detail::Axis::X, tess::detail::Axis::Z,
                 tess::detail::Axis::Y},
      std::array{tess::detail::Axis::Y, tess::detail::Axis::X,
                 tess::detail::Axis::Z},
      std::array{tess::detail::Axis::Y, tess::detail::Axis::Z,
                 tess::detail::Axis::X},
      std::array{tess::detail::Axis::Z, tess::detail::Axis::X,
                 tess::detail::Axis::Y},
      std::array{tess::detail::Axis::Z, tess::detail::Axis::Y,
                 tess::detail::Axis::X},
  };

  WeightedPathScaleWorld world;
  fill_path_passable(world, 1);
  fill_path_cost(world, 1);
  carve_room_portals(world);
  const auto request =
      tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{510, 510, 0}};
  std::vector<tess::Coord3> waypoints;
  waypoints.reserve(32);
  tess::detail::PortalRouteCandidate candidate;
  auto best_score = std::numeric_limits<std::uint32_t>::max();
  auto total_scan_tiles = std::size_t{0};
  auto total_waypoints = std::size_t{0};

  for (auto _ : state) {
    best_score = std::numeric_limits<std::uint32_t>::max();
    total_scan_tiles = 0;
    total_waypoints = 0;
    for (const auto& order : orders) {
      candidate =
          tess::detail::build_chunk_portal_candidate<WeightedPathScaleWorld,
                                                     PassableTag>(
              world, request, order, waypoints);
      total_scan_tiles += candidate.scan_tiles;
      total_waypoints += waypoints.size();
      if (candidate.found && candidate.score < best_score) {
        best_score = candidate.score;
      }
    }
    candidate = tess::detail::build_greedy_chunk_portal_candidate<
        WeightedPathScaleWorld, PassableTag>(world, request, waypoints);
    total_scan_tiles += candidate.scan_tiles;
    total_waypoints += waypoints.size();
    if (candidate.found && candidate.score < best_score) {
      best_score = candidate.score;
    }
    benchmark::DoNotOptimize(best_score);
    benchmark::DoNotOptimize(total_scan_tiles);
    benchmark::DoNotOptimize(total_waypoints);
  }
  bench_check(best_score != std::numeric_limits<std::uint32_t>::max(),
              "no portal route candidate was found");
  state.counters["portal.route_candidates"] =
      static_cast<double>(orders.size() + 1u);
  // Publish the values the loop actually measured; this previously reported
  // a hardcoded scan-tile formula while the measured totals were discarded.
  state.counters["portal.scan_tiles"] = static_cast<double>(total_scan_tiles);
  state.counters["portal.waypoints"] = static_cast<double>(total_waypoints);
}

void BM_path_weighted_portal_segment_cache_room_portals_512x512(
    benchmark::State& state) {
  WeightedPathScaleWorld world;
  fill_path_passable(world, 1);
  fill_path_cost(world, 1);
  carve_room_portals(world);
  const auto request =
      tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{510, 510, 0}};
  const auto waypoints = make_room_portal_waypoints(request);

  tess::PathScratch optimal_scratch;
  optimal_scratch.reserve_nodes(path_node_count<PathScaleShape>());
  const auto optimal =
      tess::weighted_astar_path<WeightedPathScaleWorld, PassableTag, CostTag>(
          world, request, optimal_scratch);
  bench_check(optimal.status == tess::PathStatus::Found,
              "setup optimal path not found");
  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  tess::WeightedPortalSegmentCache cache;
  cache.reserve_segments(waypoints.size() + 1);
  cache.reserve_path_nodes(2048);
  tess::WeightedPortalRouteProduct product;
  product.reserve_waypoints(waypoints.size());
  product.reserve_path_nodes(2048);
  product.reserve_dependencies(64);
  auto result =
      tess::build_weighted_portal_route_product<WeightedPathScaleWorld,
                                                PassableTag, CostTag>(
          world, request, waypoints, scratch, cache, product);

  for (auto _ : state) {
    result = tess::build_weighted_portal_route_product<WeightedPathScaleWorld,
                                                       PassableTag, CostTag>(
        world, request, waypoints, scratch, cache, product);
    auto cost = result.cost;
    benchmark::DoNotOptimize(cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  check_found_route(world, result, request);
  bench_check(result.cost >= optimal.cost,
              "portal route cost beat the optimal cost");
  record_path_counters(state, result);
  record_portal_product_counters(state, product, result, optimal.cost);
  state.counters["portal.segment_cache_entries"] =
      static_cast<double>(cache.size());
}

void BM_path_weighted_portal_segment_cache_batch_100_room_portals_512x512(
    benchmark::State& state) {
  WeightedPathScaleWorld world;
  fill_path_passable(world, 1);
  fill_path_cost(world, 1);
  carve_room_portals(world);

  constexpr auto count = std::size_t{100};
  const auto requests = make_shared_room_portal_requests<count>();
  const auto waypoints = make_room_portal_waypoints(requests.front());

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  tess::WeightedPortalSegmentCache cache;
  cache.reserve_segments((waypoints.size() + 1u) * count);
  cache.reserve_path_nodes(4096 + count * 96u);
  tess::WeightedPortalRouteProduct product;
  product.reserve_waypoints(waypoints.size());
  product.reserve_path_nodes(2048);
  product.reserve_dependencies(64);
  tess::PathResult result;
  std::uint64_t total_cost = 0;
  std::uint64_t total_expanded = 0;

  for (auto _ : state) {
    cache.clear();
    total_expanded = 0;
    total_cost = 0;
    for (const auto request : requests) {
      result = tess::build_weighted_portal_route_product<WeightedPathScaleWorld,
                                                         PassableTag, CostTag>(
          world, request, waypoints, scratch, cache, product);
      total_expanded += result.expanded_nodes;
      total_cost += result.cost;
    }
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_expanded);
    benchmark::DoNotOptimize(result.path.data());
  }
  check_found_route(world, result, requests.back());
  record_batch_counters<count>(state, total_cost, total_expanded);
  state.counters["portal.segment_cache_entries"] =
      static_cast<double>(cache.size());
  state.counters["portal.waypoints"] = static_cast<double>(waypoints.size());
}

void BM_path_weighted_portal_endpoint_segments_batch_100_room_portals_512x512(
    benchmark::State& state) {
  WeightedPathScaleWorld world;
  fill_path_passable(world, 1);
  fill_path_cost(world, 1);
  carve_room_portals(world);

  constexpr auto count = std::size_t{100};
  const auto requests = make_shared_room_portal_requests<count>();
  const auto waypoints = make_room_portal_waypoints(requests.front());
  const auto first_portal = waypoints.front();
  const auto last_portal = waypoints.back();

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  tess::PathResult result;
  std::uint64_t total_first_expanded = 0;
  std::uint64_t total_cost = 0;
  std::size_t last_expanded = 0;

  for (auto _ : state) {
    total_first_expanded = 0;
    total_cost = 0;
    for (const auto request : requests) {
      result = tess::weighted_astar_path<WeightedPathScaleWorld, PassableTag,
                                         CostTag>(
          world, tess::PathRequest{request.start, first_portal}, scratch);
      total_first_expanded += result.expanded_nodes;
      total_cost += result.cost;
    }
    result =
        tess::weighted_astar_path<WeightedPathScaleWorld, PassableTag, CostTag>(
            world, tess::PathRequest{last_portal, requests.front().goal},
            scratch);
    last_expanded = result.expanded_nodes;
    total_cost += result.cost;
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_first_expanded);
    benchmark::DoNotOptimize(last_expanded);
  }
  check_found_route(world, result,
                    tess::PathRequest{last_portal, requests.front().goal});
  record_batch_counters<count>(state, total_cost, total_first_expanded);
  state.counters["portal.first_segment_avg_expanded"] =
      static_cast<double>(total_first_expanded) / static_cast<double>(count);
  state.counters["portal.last_segment_expanded"] =
      static_cast<double>(last_expanded);
}

void BM_path_weighted_portal_first_field_cache_batch_100_room_portals_512x512(
    benchmark::State& state) {
  WeightedPathScaleWorld world;
  fill_path_passable(world, 1);
  fill_path_cost(world, 1);
  carve_room_portals(world);

  constexpr auto count = std::size_t{100};
  const auto requests = make_shared_room_portal_requests<count>();
  const auto waypoints = make_room_portal_waypoints(requests.front());
  const auto first_portal = waypoints.front();
  const auto tail_waypoints = std::span<const tess::Coord3>{
      waypoints.data() + 1u, waypoints.size() - 1u};

  tess::DistanceFieldScratch field_scratch;
  field_scratch.reserve_nodes(path_node_count<PathScaleShape>());
  tess::PathScratch segment_scratch;
  segment_scratch.reserve_nodes(path_node_count<PathScaleShape>());
  tess::WeightedPortalSegmentCache cache;
  cache.reserve_segments(waypoints.size());
  cache.reserve_path_nodes(4096);
  tess::WeightedPortalRouteProduct product;
  product.reserve_waypoints(tail_waypoints.size());
  product.reserve_path_nodes(2048);
  product.reserve_dependencies(64);
  tess::DistanceFieldResult field;
  tess::PathResult result;
  std::uint64_t total_reconstruct_expanded = 0;
  std::uint64_t total_cost = 0;

  for (auto _ : state) {
    cache.clear();
    total_reconstruct_expanded = 0;
    total_cost = 0;
    field = tess::build_weighted_distance_field_in_box<WeightedPathScaleWorld,
                                                       PassableTag, CostTag>(
        world, first_portal,
        tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{33, 32, 1}},
        field_scratch);
    for (const auto request : requests) {
      const auto first =
          tess::weighted_distance_field_path<WeightedPathScaleWorld,
                                             PassableTag, CostTag>(
              world, request.start, first_portal, field_scratch);
      total_reconstruct_expanded += first.expanded_nodes;
      total_cost += first.cost;
      result = tess::build_weighted_portal_route_product<WeightedPathScaleWorld,
                                                         PassableTag, CostTag>(
          world, tess::PathRequest{first_portal, request.goal}, tail_waypoints,
          segment_scratch, cache, product);
      total_cost += result.cost;
    }
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_reconstruct_expanded);
    benchmark::DoNotOptimize(field.expanded_nodes);
    benchmark::DoNotOptimize(result.path.data());
  }
  check_found_route(world, result,
                    tess::PathRequest{first_portal, requests.back().goal});
  record_batch_counters<count>(state, total_cost, total_reconstruct_expanded);
  state.counters["field_expanded_nodes"] =
      static_cast<double>(field.expanded_nodes);
  state.counters["field_reached_nodes"] =
      static_cast<double>(field.reached_nodes);
  state.counters["portal.segment_cache_entries"] =
      static_cast<double>(cache.size());
  state.counters["portal.waypoints"] =
      static_cast<double>(tail_waypoints.size());
}

void BM_path_weighted_astar_batch_100_shared_sparse_512x512(
    benchmark::State& state) {
  WeightedPathScaleWorld world;
  fill_path_passable(world, 1);
  fill_path_cost(world, 1);
  carve_sparse_blockers(world);
  const auto requests = make_shared_sparse_requests(world);

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  const auto expected_last_cost =
      expected_weighted_path_cost(world, requests.back());
  TESS_PATH_DIAG_DECL();
  tess::PathResult result;
  std::uint64_t total_cost = 0;
  std::uint64_t total_expanded = 0;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    total_cost = 0;
    total_expanded = 0;
    TESS_PATH_DIAG_RUN(for (const auto request : requests) {
      result = tess::weighted_astar_path<WeightedPathScaleWorld, PassableTag,
                                         CostTag>(world, request, scratch);
      total_cost += result.cost;
      total_expanded += result.expanded_nodes;
    });
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_expanded);
  }
  check_found_route(world, result, requests.back(), expected_last_cost);
  record_batch_counters<requests.size()>(state, total_cost, total_expanded);
  TESS_PATH_DIAG_RECORD(state);
}

void BM_path_weighted_distance_field_batch_100_shared_sparse_512x512(
    benchmark::State& state) {
  WeightedPathScaleWorld world;
  fill_path_passable(world, 1);
  fill_path_cost(world, 1);
  carve_sparse_blockers(world);
  const auto requests = make_shared_sparse_requests(world);
  const auto goal = requests.front().goal;

  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  const auto expected_last_cost =
      expected_weighted_path_cost(world, requests.back());
  TESS_PATH_DIAG_DECL();
  tess::DistanceFieldResult field;
  tess::PathResult result;
  std::uint64_t total_cost = 0;
  std::uint64_t total_expanded = 0;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    total_cost = 0;
    total_expanded = 0;
    TESS_PATH_DIAG_RUN(
        field = tess::build_weighted_distance_field<WeightedPathScaleWorld,
                                                    PassableTag, CostTag>(
            world, goal, scratch);
        for (const auto request : requests) {
          result = tess::weighted_distance_field_path<WeightedPathScaleWorld,
                                                      PassableTag, CostTag>(
              world, request.start, request.goal, scratch);
          total_cost += result.cost;
          total_expanded += result.expanded_nodes;
        });
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_expanded);
    benchmark::DoNotOptimize(field.expanded_nodes);
  }
  bench_check(field.status == tess::PathStatus::Found,
              "distance field build failed");
  check_found_route(world, result, requests.back(), expected_last_cost);
  record_batch_counters<requests.size()>(state, total_cost, total_expanded);
  state.counters["field_expanded_nodes"] =
      static_cast<double>(field.expanded_nodes);
  state.counters["field_reached_nodes"] =
      static_cast<double>(field.reached_nodes);
  TESS_PATH_DIAG_RECORD(state);
}

void BM_path_bounded_weighted_distance_field_batch_100_shared_sparse_512x512(
    benchmark::State& state) {
  WeightedPathScaleWorld world;
  fill_path_passable(world, 1);
  fill_path_cost(world, 1);
  carve_sparse_blockers(world);
  const auto requests = make_shared_sparse_requests(world);
  const auto goal = requests.front().goal;

  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  const auto expected_last_cost =
      expected_weighted_path_cost(world, requests.back());
  TESS_PATH_DIAG_DECL();
  tess::DistanceFieldResult field;
  tess::PathResult result;
  std::uint64_t total_cost = 0;
  std::uint64_t total_expanded = 0;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    total_cost = 0;
    total_expanded = 0;
    TESS_PATH_DIAG_RUN(
        field = tess::build_bounded_weighted_distance_field<
            WeightedPathScaleWorld, PassableTag, CostTag, 7>(world, goal,
                                                             scratch);
        for (const auto request : requests) {
          result = tess::weighted_distance_field_path<WeightedPathScaleWorld,
                                                      PassableTag, CostTag>(
              world, request.start, request.goal, scratch);
          total_cost += result.cost;
          total_expanded += result.expanded_nodes;
        });
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_expanded);
    benchmark::DoNotOptimize(field.expanded_nodes);
  }
  bench_check(field.status == tess::PathStatus::Found,
              "distance field build failed");
  check_found_route(world, result, requests.back(), expected_last_cost);
  record_batch_counters<requests.size()>(state, total_cost, total_expanded);
  state.counters["field_expanded_nodes"] =
      static_cast<double>(field.expanded_nodes);
  state.counters["field_reached_nodes"] =
      static_cast<double>(field.reached_nodes);
  TESS_PATH_DIAG_RECORD(state);
}

void BM_path_weighted_astar_batch_100_multigoal_sparse_512x512(
    benchmark::State& state) {
  WeightedPathScaleWorld world;
  fill_path_passable(world, 1);
  fill_path_cost(world, 1);
  carve_sparse_blockers(world);
  const auto requests = make_multigoal_sparse_requests(world);

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  const auto expected_last_cost =
      expected_weighted_path_cost(world, requests.back());
  TESS_PATH_DIAG_DECL();
  tess::PathResult result;
  std::uint64_t total_cost = 0;
  std::uint64_t total_expanded = 0;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    total_cost = 0;
    total_expanded = 0;
    TESS_PATH_DIAG_RUN(for (const auto request : requests) {
      result = tess::weighted_astar_path<WeightedPathScaleWorld, PassableTag,
                                         CostTag>(world, request, scratch);
      total_cost += result.cost;
      total_expanded += result.expanded_nodes;
    });
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_expanded);
  }
  check_found_route(world, result, requests.back(), expected_last_cost);
  record_batch_counters<requests.size()>(state, total_cost, total_expanded);
  state.counters["batch.unique_goals"] =
      static_cast<double>(unique_goal_count(requests));
  TESS_PATH_DIAG_RECORD(state);
}

void BM_path_weighted_distance_field_batch_100_multigoal_sparse_512x512(
    benchmark::State& state) {
  WeightedPathScaleWorld world;
  fill_path_passable(world, 1);
  fill_path_cost(world, 1);
  carve_sparse_blockers(world);
  const auto requests = make_multigoal_sparse_requests(world);

  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  const auto last_request = last_grouped_request(requests);
  const auto expected_last_cost =
      expected_weighted_path_cost(world, last_request);
  TESS_PATH_DIAG_DECL();
  tess::DistanceFieldResult field;
  tess::PathResult result;
  std::uint64_t total_cost = 0;
  std::uint64_t total_expanded = 0;
  std::uint64_t total_field_expanded = 0;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    total_cost = 0;
    total_expanded = 0;
    total_field_expanded = 0;
    TESS_PATH_DIAG_RUN(for (std::size_t i = 0; i < requests.size(); ++i) {
      auto already_built = false;
      for (std::size_t j = 0; j < i; ++j) {
        already_built = already_built || requests[j].goal == requests[i].goal;
      }
      if (already_built) {
        continue;
      }
      field = tess::build_weighted_distance_field<WeightedPathScaleWorld,
                                                  PassableTag, CostTag>(
          world, requests[i].goal, scratch);
      total_field_expanded += field.expanded_nodes;
      for (const auto request : requests) {
        if (request.goal != requests[i].goal) {
          continue;
        }
        result = tess::weighted_distance_field_path<WeightedPathScaleWorld,
                                                    PassableTag, CostTag>(
            world, request.start, request.goal, scratch);
        total_cost += result.cost;
        total_expanded += result.expanded_nodes;
      }
    });
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_expanded);
    benchmark::DoNotOptimize(total_field_expanded);
  }
  bench_check(field.status == tess::PathStatus::Found,
              "distance field build failed");
  check_found_route(world, result, last_request, expected_last_cost);
  record_batch_counters<requests.size()>(state, total_cost, total_expanded);
  state.counters["batch.unique_goals"] =
      static_cast<double>(unique_goal_count(requests));
  state.counters["field_expanded_nodes"] =
      static_cast<double>(total_field_expanded);
  state.counters["field_reached_nodes"] =
      static_cast<double>(field.reached_nodes);
  TESS_PATH_DIAG_RECORD(state);
}

void BM_path_bounded_weighted_distance_field_batch_100_multigoal_sparse_512x512(
    benchmark::State& state) {
  WeightedPathScaleWorld world;
  fill_path_passable(world, 1);
  fill_path_cost(world, 1);
  carve_sparse_blockers(world);
  const auto requests = make_multigoal_sparse_requests(world);

  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  const auto last_request = last_grouped_request(requests);
  const auto expected_last_cost =
      expected_weighted_path_cost(world, last_request);
  TESS_PATH_DIAG_DECL();
  tess::DistanceFieldResult field;
  tess::PathResult result;
  std::uint64_t total_cost = 0;
  std::uint64_t total_expanded = 0;
  std::uint64_t total_field_expanded = 0;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    total_cost = 0;
    total_expanded = 0;
    total_field_expanded = 0;
    TESS_PATH_DIAG_RUN(for (std::size_t i = 0; i < requests.size(); ++i) {
      auto already_built = false;
      for (std::size_t j = 0; j < i; ++j) {
        already_built = already_built || requests[j].goal == requests[i].goal;
      }
      if (already_built) {
        continue;
      }
      field =
          tess::build_bounded_weighted_distance_field<WeightedPathScaleWorld,
                                                      PassableTag, CostTag, 7>(
              world, requests[i].goal, scratch);
      total_field_expanded += field.expanded_nodes;
      for (const auto request : requests) {
        if (request.goal != requests[i].goal) {
          continue;
        }
        result = tess::weighted_distance_field_path<WeightedPathScaleWorld,
                                                    PassableTag, CostTag>(
            world, request.start, request.goal, scratch);
        total_cost += result.cost;
        total_expanded += result.expanded_nodes;
      }
    });
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_expanded);
    benchmark::DoNotOptimize(total_field_expanded);
  }
  bench_check(field.status == tess::PathStatus::Found,
              "distance field build failed");
  check_found_route(world, result, last_request, expected_last_cost);
  record_batch_counters<requests.size()>(state, total_cost, total_expanded);
  state.counters["batch.unique_goals"] =
      static_cast<double>(unique_goal_count(requests));
  state.counters["field_expanded_nodes"] =
      static_cast<double>(total_field_expanded);
  state.counters["field_reached_nodes"] =
      static_cast<double>(field.reached_nodes);
  TESS_PATH_DIAG_RECORD(state);
}

void BM_path_weighted_batch_planner_100_multigoal_sparse_512x512(
    benchmark::State& state) {
  WeightedPathScaleWorld world;
  fill_path_passable(world, 1);
  fill_path_cost(world, 1);
  carve_sparse_blockers(world);
  const auto requests = make_multigoal_sparse_requests(world);

  tess::WeightedPathBatchScratch scratch;
  scratch.reserve_search_nodes(path_node_count<PathScaleShape>());
  scratch.reserve_requests(requests.size());
  scratch.reserve_path_nodes(requests.size() * 1024u);
  const auto expected_last_cost =
      expected_weighted_path_cost(world, requests.back());
  TESS_PATH_DIAG_DECL();
  std::span<const tess::PathResult> results;
  std::uint64_t total_expanded = 0;
  std::uint64_t total_cost = 0;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    total_cost = 0;
    total_expanded = 0;
    TESS_PATH_DIAG_RUN(
        results =
            tess::weighted_path_batch<WeightedPathScaleWorld, PassableTag,
                                      CostTag, 7>(world, requests, scratch));
    for (const auto result : results) {
      total_cost += result.cost;
      total_expanded += result.expanded_nodes;
    }
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_expanded);
    benchmark::DoNotOptimize(results.data());
  }
  record_batch_counters<requests.size()>(state, total_cost, total_expanded);
  state.counters["batch.unique_goals"] =
      static_cast<double>(scratch.stats().unique_goals);
  state.counters["batch.field_builds"] =
      static_cast<double>(scratch.stats().field_builds);
  state.counters["batch.astar_fallbacks"] =
      static_cast<double>(scratch.stats().astar_fallbacks);
  state.counters["batch.path_nodes"] =
      static_cast<double>(scratch.stats().path_nodes);
  bench_check(results.size() == requests.size(),
              "planner did not return a result per request");
  check_found_route(world, results.back(), requests.back(), expected_last_cost);
  TESS_PATH_DIAG_RECORD(state);
}

// Same multigoal batch as the planner bench, but on a fully-resident
// SparseResidentWorld: exercises the sparse-only costs the dense planner
// bench cannot see -- residency fingerprint stamps/checks per field build
// and read, and slot-indirect node addressing (audit 2026-07-11 M2).
void BM_path_weighted_batch_planner_100_multigoal_sparse_resident_512x512(
    benchmark::State& state) {
  using SparseScaleWorld =
      tess::SparseResidentWorld<PathScaleShape, WeightedPathSchema>;
  SparseScaleWorld world{tess::ResidencyConfig{
      SparseScaleWorld::chunk_count * SparseScaleWorld::page_byte_size}};
  for (std::uint64_t key = 0; key < SparseScaleWorld::chunk_count; ++key) {
    world.ensure_resident(tess::ChunkKey{key});
    auto& page = world.chunk(tess::ChunkKey{key});
    for (auto& tile : page.template field_span<PassableTag>()) {
      tile = 1;
    }
    for (auto& tile : page.template field_span<CostTag>()) {
      tile = 1;
    }
  }
  carve_sparse_blockers(world);
  const auto requests = make_multigoal_sparse_requests(world);

  tess::WeightedPathBatchScratch scratch;
  scratch.reserve_search_nodes(path_node_count<PathScaleShape>());
  scratch.reserve_requests(requests.size());
  scratch.reserve_path_nodes(requests.size() * 1024u);
  const auto expected_last_cost =
      expected_weighted_path_cost(world, requests.back());
  std::span<const tess::PathResult> results;
  std::uint64_t total_expanded = 0;
  std::uint64_t total_cost = 0;

  for (auto _ : state) {
    total_cost = 0;
    total_expanded = 0;
    results =
        tess::weighted_path_batch<SparseScaleWorld, PassableTag, CostTag, 7>(
            world, requests, scratch);
    for (const auto result : results) {
      total_cost += result.cost;
      total_expanded += result.expanded_nodes;
    }
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_expanded);
    benchmark::DoNotOptimize(results.data());
  }
  record_batch_counters<requests.size()>(state, total_cost, total_expanded);
  state.counters["batch.field_builds"] =
      static_cast<double>(scratch.stats().field_builds);
  bench_check(results.size() == requests.size(),
              "planner did not return a result per request");
  check_found_route(world, results.back(), requests.back(), expected_last_cost);
}

// Open map, two shared goals, all 100 starts within a few chunks of the
// goals: the shared-goal field flood covers the whole 512x512 map while
// the consumers need only the near-goal region. Pins the cost audit
// 2026-07-11 M3 (settle-target early termination) is meant to remove.
void BM_path_weighted_batch_planner_100_neargoal_open_512x512(
    benchmark::State& state) {
  WeightedPathScaleWorld world;
  fill_path_passable(world, 1);
  fill_path_cost(world, 1);

  constexpr auto goals =
      std::array{tess::Coord3{256, 256, 0}, tess::Coord3{240, 256, 0}};
  std::array<tess::PathRequest, 100> requests{};
  for (std::size_t i = 0; i < requests.size(); ++i) {
    const auto offset = static_cast<std::int64_t>(i);
    const auto start = tess::Coord3{232 + offset % 10, 244 + offset / 10, 0};
    requests[i] = tess::PathRequest{start, goals[i % goals.size()]};
  }

  tess::WeightedPathBatchScratch scratch;
  scratch.reserve_search_nodes(path_node_count<PathScaleShape>());
  scratch.reserve_requests(requests.size());
  scratch.reserve_path_nodes(requests.size() * 1024u);
  const auto expected_last_cost =
      expected_weighted_path_cost(world, requests.back());
  std::span<const tess::PathResult> results;
  std::uint64_t total_expanded = 0;
  std::uint64_t total_cost = 0;

  for (auto _ : state) {
    total_cost = 0;
    total_expanded = 0;
    results = tess::weighted_path_batch<WeightedPathScaleWorld, PassableTag,
                                        CostTag, 7>(world, requests, scratch);
    for (const auto result : results) {
      total_cost += result.cost;
      total_expanded += result.expanded_nodes;
    }
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_expanded);
    benchmark::DoNotOptimize(results.data());
  }
  record_batch_counters<requests.size()>(state, total_cost, total_expanded);
  state.counters["batch.field_builds"] =
      static_cast<double>(scratch.stats().field_builds);
  bench_check(results.size() == requests.size(),
              "planner did not return a result per request");
  check_found_route(world, results.back(), requests.back(), expected_last_cost);
}

BENCHMARK(BM_path_weighted_portal_product_room_portals_512x512)
    ->Name("path/weighted_portal_product_room_portals_512x512");
BENCHMARK(BM_path_weighted_portal_product_replay_room_portals_512x512)
    ->Name("path/weighted_portal_product_replay_room_portals_512x512");
BENCHMARK(BM_path_weighted_chunk_portal_product_room_portals_512x512)
    ->Name("path/weighted_chunk_portal_product_room_portals_512x512");
BENCHMARK(BM_path_weighted_chunk_portal_product_replay_room_portals_512x512)
    ->Name("path/weighted_chunk_portal_product_replay_room_portals_512x512");
BENCHMARK(BM_path_weighted_chunk_portal_candidates_room_portals_512x512)
    ->Name("path/weighted_chunk_portal_candidates_room_portals_512x512");
BENCHMARK(BM_path_weighted_portal_segment_cache_room_portals_512x512)
    ->Name("path/weighted_portal_segment_cache_room_portals_512x512");
BENCHMARK(BM_path_weighted_portal_segment_cache_batch_100_room_portals_512x512)
    ->Name("path/weighted_portal_segment_cache_batch_100_room_portals_512x512");
BENCHMARK(
    BM_path_weighted_portal_endpoint_segments_batch_100_room_portals_512x512)
    ->Name(
        "path/"
        "weighted_portal_endpoint_segments_batch_100_room_portals_512x512");
BENCHMARK(
    BM_path_weighted_portal_first_field_cache_batch_100_room_portals_512x512)
    ->Name(
        "path/"
        "weighted_portal_first_field_cache_batch_100_room_portals_512x512");
BENCHMARK(BM_path_weighted_astar_batch_100_shared_sparse_512x512)
    ->Name("path/weighted_astar_batch_100_shared_sparse_512x512");
BENCHMARK(BM_path_weighted_distance_field_batch_100_shared_sparse_512x512)
    ->Name("path/weighted_distance_field_batch_100_shared_sparse_512x512");
BENCHMARK(
    BM_path_bounded_weighted_distance_field_batch_100_shared_sparse_512x512)
    ->Name(
        "path/bounded_weighted_distance_field_batch_100_shared_sparse_512x512");
BENCHMARK(BM_path_weighted_astar_batch_100_multigoal_sparse_512x512)
    ->Name("path/weighted_astar_batch_100_multigoal_sparse_512x512");
BENCHMARK(BM_path_weighted_distance_field_batch_100_multigoal_sparse_512x512)
    ->Name("path/weighted_distance_field_batch_100_multigoal_sparse_512x512");
BENCHMARK(
    BM_path_bounded_weighted_distance_field_batch_100_multigoal_sparse_512x512)
    ->Name(
        "path/"
        "bounded_weighted_distance_field_batch_100_multigoal_sparse_512x512");
BENCHMARK(BM_path_weighted_batch_planner_100_multigoal_sparse_resident_512x512)
    ->Name("path/weighted_batch_planner_100_multigoal_sparse_resident_512x512");
BENCHMARK(BM_path_weighted_batch_planner_100_neargoal_open_512x512)
    ->Name("path/weighted_batch_planner_100_neargoal_open_512x512");
BENCHMARK(BM_path_weighted_batch_planner_100_multigoal_sparse_512x512)
    ->Name("path/weighted_batch_planner_100_multigoal_sparse_512x512");

}  // namespace
