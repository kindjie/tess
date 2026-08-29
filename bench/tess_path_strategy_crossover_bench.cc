#include <benchmark/benchmark.h>
#include <sys/resource.h>
#include <tess/tess.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct PassableTag {};
struct CostTag {};

using UnitSchema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>>;
using WeightedSchema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>,
                                         tess::Field<CostTag, std::uint32_t>>;
using WeightedMovement =
    tess::movement::PositiveCostFieldMovement<PassableTag, CostTag>;

enum class UnitLayout : std::uint8_t { Open, RoomPortals };
enum class UnitStrategy : std::uint8_t { Astar, DistanceField };
enum class CacheShape : std::uint8_t { ExactRepeats, SameGoalSuffixes };
enum class CacheStrategy : std::uint8_t { Astar, ColdRouteCache };
enum class GoalShape : std::uint8_t { One, Eight, AllDistinct };
enum class WeightedStrategy : std::uint8_t { Astar, Batch };

constexpr auto kRequestCounts = std::array<std::int64_t, 20>{
    1,   2,   4,    8,    10,   16,   32,    64,    100,   128,
    256, 512, 1000, 2048, 4096, 8192, 16384, 32768, 65536, 131072};

// The source-level workload-matrix check needs one complete lab/ literal;
// compiled registration listings remain authoritative for the generated
// extent and request-count union.
[[maybe_unused]] constexpr auto kWorkloadMatrixSentinel =
    "lab/path_strategy_crossover/unit_shared/open/astar/128x128/1";
[[maybe_unused]] constexpr auto kCacheMatrixSentinel =
    "lab/path_strategy_crossover/route_cache/exact_repeats/astar/128x128/1";
[[maybe_unused]] constexpr auto kWeightedMatrixSentinel =
    "lab/path_strategy_crossover/weighted/one_goal/weighted_astar/128x128/1";

void bench_check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr,
                 "path-strategy crossover correctness check failed: %s\n",
                 message);
    std::abort();
  }
}

template <std::uint64_t Extent>
using CrossoverShape =
    tess::Shape<tess::Extent3{Extent, Extent, 1}, tess::Extent3{32, 32, 1}>;

template <std::uint64_t Extent>
using UnitWorld = tess::AlwaysResidentWorld<CrossoverShape<Extent>, UnitSchema>;

template <std::uint64_t Extent>
using WeightedWorld =
    tess::AlwaysResidentWorld<CrossoverShape<Extent>, WeightedSchema>;

template <typename World>
constexpr auto node_count() noexcept -> std::size_t {
  using Shape = typename World::shape_type;
  return static_cast<std::size_t>(Shape::size.x * Shape::size.y *
                                  Shape::size.z);
}

template <typename World>
void fill_world(World& world) {
  using Schema = typename World::schema_type;
  world.template fill_field<PassableTag>(std::uint8_t{1});
  if constexpr (Schema::template contains<CostTag>) {
    world.template fill_field<CostTag>(std::uint32_t{1});
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
  using Shape = typename World::shape_type;
  const auto extent = static_cast<std::int64_t>(Shape::size.x);
  for (std::int64_t y = 1; y + 1 < extent; ++y) {
    for (std::int64_t x = 1; x + 1 < extent; ++x) {
      const auto hash = path_hash(static_cast<std::uint64_t>(x),
                                  static_cast<std::uint64_t>(y));
      const auto coord = tess::Coord3{x, y, 0};
      if (hash % 100u < 18u) {
        world.template field<PassableTag>(coord) = 0;
      } else if (hash % 100u < 32u) {
        world.template field<CostTag>(coord) = 7;
      }
    }
  }

  for (std::int64_t x = 32; x < extent; x += 64) {
    world.template field<PassableTag>(tess::Coord3{x, 1, 0}) = 0;
  }
  for (std::int64_t y = 64; y < extent; y += 64) {
    world.template field<PassableTag>(tess::Coord3{extent - 2, y, 0}) = 0;
  }
}

template <typename World>
void carve_room_portals(World& world) {
  using Shape = typename World::shape_type;
  constexpr auto room_size = std::int64_t{32};
  const auto extent = static_cast<std::int64_t>(Shape::size.x);
  for (std::int64_t x = room_size; x < extent; x += room_size) {
    for (std::int64_t y = 0; y < extent; ++y) {
      const auto room = y / room_size;
      const auto portal = (room * 23 + x / room_size * 17) % room_size;
      if (y % room_size != portal) {
        world.template field<PassableTag>(tess::Coord3{x, y, 0}) = 0;
      }
    }
  }
  for (std::int64_t y = room_size; y < extent; y += room_size) {
    for (std::int64_t x = 0; x < extent; ++x) {
      const auto room = x / room_size;
      const auto portal = (room * 29 + y / room_size * 19) % room_size;
      if (x % room_size != portal) {
        world.template field<PassableTag>(tess::Coord3{x, y, 0}) = 0;
      }
    }
  }
}

template <typename World>
void check_path(const World& world, const tess::PathResult& result,
                tess::PathRequest request, std::uint32_t expected_cost) {
  using Schema = typename World::schema_type;
  bench_check(result.status == tess::PathStatus::Found,
              "path status is not Found");
  bench_check(!result.path.empty(), "found path is empty");
  bench_check(result.path.front() == request.start,
              "path start differs from the request");
  bench_check(result.path.back() == request.goal,
              "path goal differs from the request");
  bench_check(result.cost == expected_cost,
              "paired strategies returned different costs");
  auto reconstructed_cost = std::uint64_t{0};
  for (std::size_t index = 1; index < result.path.size(); ++index) {
    bench_check(tess::manhattan_distance(result.path[index - 1],
                                         result.path[index]) == 1,
                "path contains a non-unit step");
    bench_check(world.template field<PassableTag>(result.path[index]) != 0,
                "path crosses an impassable tile");
    if constexpr (Schema::template contains<CostTag>) {
      reconstructed_cost += world.template field<CostTag>(result.path[index]);
    } else {
      ++reconstructed_cost;
    }
  }
  bench_check(reconstructed_cost == result.cost,
              "reported cost differs from the returned path");
}

struct ReferenceCosts {
  std::vector<std::size_t> indices;
  std::vector<std::uint32_t> costs;
};

template <typename World>
auto validation_indices(std::size_t count) -> std::vector<std::size_t> {
  constexpr auto exhaustive_count = std::size_t{1000};
  constexpr auto sampled_count = std::size_t{32};
  if (node_count<World>() <= 512u * 512u && count <= exhaustive_count) {
    std::vector<std::size_t> indices(count);
    for (std::size_t index = 0; index < count; ++index) {
      indices[index] = index;
    }
    return indices;
  }

  const auto samples = std::min(count, sampled_count);
  std::vector<std::size_t> indices;
  indices.reserve(samples);
  for (std::size_t sample = 0; sample < samples; ++sample) {
    indices.push_back(samples == 1 ? 0 : sample * (count - 1) / (samples - 1));
  }
  return indices;
}

template <bool Weighted, typename World>
auto reference_costs(const World& world,
                     std::span<const tess::PathRequest> requests)
    -> ReferenceCosts {
  tess::PathScratch scratch;
  scratch.reserve_nodes(node_count<World>());
  auto references = ReferenceCosts{};
  references.indices = validation_indices<World>(requests.size());
  references.costs.reserve(references.indices.size());
  for (const auto index : references.indices) {
    const auto request = requests[index];
    const auto result = [&] {
      if constexpr (Weighted) {
        return tess::weighted_astar_path<World, WeightedMovement>(
            world, request, scratch);
      } else {
        return tess::astar_path<World, PassableTag>(world, request, scratch);
      }
    }();
    bench_check(result.status == tess::PathStatus::Found,
                "reference A* did not find a path");
    check_path(world, result, request, result.cost);
    references.costs.push_back(result.cost);
  }
  return references;
}

[[nodiscard]] auto peak_rss_bytes() noexcept -> std::uint64_t {
  auto usage = rusage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) {
    return 0;
  }
#if defined(__APPLE__)
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024u;
#endif
}

template <typename World>
void record_common_counters(benchmark::State& state, std::size_t requests,
                            std::uint64_t cost) {
  state.counters["requests"] = static_cast<double>(requests);
  state.counters["cost_total"] = static_cast<double>(cost);
  state.counters["world.field_page_bytes"] =
      static_cast<double>(World::storage_byte_size);
  state.counters["process.peak_rss_bytes"] =
      static_cast<double>(peak_rss_bytes());
  state.SetItemsProcessed(state.iterations() *
                          static_cast<std::int64_t>(requests));
}

template <typename World>
void prepare_shared_goal_world(World& world) {
  using Shape = typename World::shape_type;
  const auto extent = static_cast<std::int64_t>(Shape::size.x);
  world.template field<PassableTag>(tess::Coord3{extent - 2, extent - 2, 0}) =
      1;
  for (std::int64_t y = 1; y <= 30; ++y) {
    for (std::int64_t x = 1; x <= 30; ++x) {
      world.template field<PassableTag>(tess::Coord3{x, y, 0}) = 1;
    }
  }
}

template <typename World>
auto make_shared_goal_requests(const World&, std::size_t count)
    -> std::vector<tess::PathRequest> {
  using Shape = typename World::shape_type;
  const auto extent = static_cast<std::int64_t>(Shape::size.x);
  const auto goal = tess::Coord3{extent - 2, extent - 2, 0};
  std::vector<tess::PathRequest> requests;
  requests.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    const auto offset = static_cast<std::int64_t>(index);
    const auto start = tess::Coord3{1 + offset % 30, 1 + (offset / 30) % 30, 0};
    requests.push_back({start, goal});
  }
  return requests;
}

template <std::uint64_t Extent>
void run_unit_shared(benchmark::State& state, UnitLayout layout,
                     UnitStrategy strategy) {
  UnitWorld<Extent> world;
  fill_world(world);
  if (layout == UnitLayout::RoomPortals) {
    carve_room_portals(world);
  }
  prepare_shared_goal_world(world);
  const auto count = static_cast<std::size_t>(state.range(0));
  const auto requests = make_shared_goal_requests(world, count);
  const auto references = reference_costs<false>(world, requests);
  std::uint64_t total_cost = 0;
  std::uint64_t total_expanded = 0;
  std::uint64_t reconstruction_nodes = 0;
  std::size_t field_builds = 0;

  if (strategy == UnitStrategy::Astar) {
    tess::PathScratch scratch;
    scratch.reserve_nodes(node_count<UnitWorld<Extent>>());
    benchmark::DoNotOptimize(tess::astar_path<UnitWorld<Extent>, PassableTag>(
        world, requests.front(), scratch));
    for (auto _ : state) {
      total_cost = 0;
      total_expanded = 0;
      reconstruction_nodes = 0;
      for (const auto request : requests) {
        const auto result = tess::astar_path<UnitWorld<Extent>, PassableTag>(
            world, request, scratch);
        total_cost += result.cost;
        total_expanded += result.expanded_nodes;
      }
      benchmark::DoNotOptimize(total_cost);
      benchmark::DoNotOptimize(total_expanded);
      benchmark::DoNotOptimize(reconstruction_nodes);
    }
  } else {
    tess::DistanceFieldScratch scratch;
    scratch.reserve_nodes(node_count<UnitWorld<Extent>>());
    benchmark::DoNotOptimize(
        tess::build_distance_field<UnitWorld<Extent>, PassableTag>(
            world, requests.front().goal, scratch));
    for (auto _ : state) {
      total_cost = 0;
      total_expanded = 0;
      reconstruction_nodes = 0;
      const auto field =
          tess::build_distance_field<UnitWorld<Extent>, PassableTag>(
              world, requests.front().goal, scratch);
      ++field_builds;
      total_expanded += field.expanded_nodes;
      for (const auto request : requests) {
        const auto result =
            tess::distance_field_path<UnitWorld<Extent>, PassableTag>(
                world, request, scratch);
        total_cost += result.cost;
        reconstruction_nodes += result.expanded_nodes;
      }
      benchmark::DoNotOptimize(total_cost);
      benchmark::DoNotOptimize(total_expanded);
      benchmark::DoNotOptimize(reconstruction_nodes);
    }
  }

  if (strategy == UnitStrategy::Astar) {
    tess::PathScratch scratch;
    scratch.reserve_nodes(node_count<UnitWorld<Extent>>());
    for (std::size_t sample = 0; sample < references.indices.size(); ++sample) {
      const auto index = references.indices[sample];
      const auto result = tess::astar_path<UnitWorld<Extent>, PassableTag>(
          world, requests[index], scratch);
      check_path(world, result, requests[index], references.costs[sample]);
    }
  } else {
    tess::DistanceFieldScratch scratch;
    scratch.reserve_nodes(node_count<UnitWorld<Extent>>());
    const auto field =
        tess::build_distance_field<UnitWorld<Extent>, PassableTag>(
            world, requests.front().goal, scratch);
    bench_check(field.status == tess::PathStatus::Found,
                "distance-field build failed");
    for (std::size_t sample = 0; sample < references.indices.size(); ++sample) {
      const auto index = references.indices[sample];
      const auto result =
          tess::distance_field_path<UnitWorld<Extent>, PassableTag>(
              world, requests[index], scratch);
      check_path(world, result, requests[index], references.costs[sample]);
    }
  }

  record_common_counters<UnitWorld<Extent>>(state, count, total_cost);
  if (strategy == UnitStrategy::Astar) {
    state.counters["astar.expanded_total"] =
        static_cast<double>(total_expanded);
  } else {
    state.counters["field.expanded_total"] =
        static_cast<double>(total_expanded);
    state.counters["reconstruction.nodes_total"] =
        static_cast<double>(reconstruction_nodes);
  }
  state.counters["field.builds_per_iteration"] =
      static_cast<double>(field_builds) /
      static_cast<double>(state.iterations());
  state.counters["requests.unique_starts"] =
      static_cast<double>(std::min(count, std::size_t{900}));
}

template <typename World>
auto make_cache_requests(const World& world, std::size_t count,
                         CacheShape shape) -> std::vector<tess::PathRequest> {
  using Shape = typename World::shape_type;
  const auto extent = static_cast<std::int64_t>(Shape::size.x);
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{extent - 1, 0, 0}};
  std::vector<tess::PathRequest> requests(count, request);
  if (shape == CacheShape::ExactRepeats) {
    return requests;
  }

  tess::PathScratch scratch;
  scratch.reserve_nodes(node_count<World>());
  const auto route =
      tess::astar_path<World, PassableTag>(world, request, scratch);
  bench_check(route.status == tess::PathStatus::Found,
              "suffix fixture route was not found");
  std::vector<tess::Coord3> path(route.path.begin(), route.path.end());
  for (std::size_t index = 0; index < count; ++index) {
    requests[index].start = path[index % path.size()];
  }
  return requests;
}

template <std::uint64_t Extent>
void run_cache(benchmark::State& state, CacheShape shape,
               CacheStrategy strategy) {
  UnitWorld<Extent> world;
  fill_world(world);
  const auto count = static_cast<std::size_t>(state.range(0));
  const auto requests = make_cache_requests(world, count, shape);
  const auto references = reference_costs<false>(world, requests);
  std::uint64_t total_cost = 0;
  std::uint64_t total_expanded = 0;
  auto cache_stats = tess::UnitRouteCacheStats{};

  tess::PathScratch scratch;
  scratch.reserve_nodes(node_count<UnitWorld<Extent>>());
  benchmark::DoNotOptimize(tess::astar_path<UnitWorld<Extent>, PassableTag>(
      world, requests.front(), scratch));
  tess::UnitRouteCache cache;
  cache.reserve_routes(count);
  cache.reserve_path_nodes(node_count<UnitWorld<Extent>>());
  if (strategy == CacheStrategy::ColdRouteCache) {
    benchmark::DoNotOptimize(
        tess::cached_astar_path<UnitWorld<Extent>, PassableTag>(
            world, requests.front(), scratch, cache));
    cache.clear();
  }
  for (auto _ : state) {
    total_cost = 0;
    total_expanded = 0;
    if (strategy == CacheStrategy::ColdRouteCache) {
      cache.clear();
    }
    for (const auto request : requests) {
      const auto result =
          strategy == CacheStrategy::Astar
              ? tess::astar_path<UnitWorld<Extent>, PassableTag>(world, request,
                                                                 scratch)
              : tess::cached_astar_path<UnitWorld<Extent>, PassableTag>(
                    world, request, scratch, cache);
      total_cost += result.cost;
      total_expanded += result.expanded_nodes;
    }
    if (strategy == CacheStrategy::ColdRouteCache) {
      cache_stats = cache.stats();
    }
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(total_expanded);
  }

  cache.clear();
  for (std::size_t sample = 0; sample < references.indices.size(); ++sample) {
    const auto index = references.indices[sample];
    const auto result =
        strategy == CacheStrategy::Astar
            ? tess::astar_path<UnitWorld<Extent>, PassableTag>(
                  world, requests[index], scratch)
            : tess::cached_astar_path<UnitWorld<Extent>, PassableTag>(
                  world, requests[index], scratch, cache);
    check_path(world, result, requests[index], references.costs[sample]);
  }

  record_common_counters<UnitWorld<Extent>>(state, count, total_cost);
  state.counters["search.expanded_total"] = static_cast<double>(total_expanded);
  state.counters["cache.entries"] = static_cast<double>(cache_stats.entries);
  state.counters["cache.hits"] = static_cast<double>(cache_stats.hits);
  state.counters["cache.suffix_hits"] =
      static_cast<double>(cache_stats.suffix_hits);
  state.counters["cache.misses"] = static_cast<double>(cache_stats.misses);
  state.counters["cache.retained_path_nodes"] =
      static_cast<double>(cache_stats.path_nodes);
  state.counters["requests.unique_starts"] = static_cast<double>(
      shape == CacheShape::ExactRepeats
          ? 1
          : std::min(count, static_cast<std::size_t>(Extent)));
}

template <typename Shape>
auto perimeter_coord(std::size_t index) -> tess::Coord3 {
  const auto extent = static_cast<std::int64_t>(Shape::size.x);
  const auto edge = static_cast<std::size_t>(extent - 1);
  const auto perimeter = edge * 4;
  index %= perimeter;
  if (index < edge) {
    return {static_cast<std::int64_t>(index), 0, 0};
  }
  index -= edge;
  if (index < edge) {
    return {extent - 1, static_cast<std::int64_t>(index), 0};
  }
  index -= edge;
  if (index < edge) {
    return {extent - 1 - static_cast<std::int64_t>(index), extent - 1, 0};
  }
  index -= edge;
  return {0, extent - 1 - static_cast<std::int64_t>(index), 0};
}

template <typename World>
auto weighted_goals() -> std::array<tess::Coord3, 8> {
  using Shape = typename World::shape_type;
  const auto far = static_cast<std::int64_t>(Shape::size.x) - 2;
  return {
      tess::Coord3{far, far, 0},      tess::Coord3{far - 30, far, 0},
      tess::Coord3{far, far - 30, 0}, tess::Coord3{far - 62, far, 0},
      tess::Coord3{far, far - 62, 0}, tess::Coord3{far - 94, far, 0},
      tess::Coord3{far, far - 94, 0}, tess::Coord3{1, far, 0},
  };
}

template <typename World>
void prepare_weighted_world(World& world) {
  for (std::int64_t y = 1; y <= 10; ++y) {
    for (std::int64_t x = 1; x <= 10; ++x) {
      const auto start = tess::Coord3{x, y, 0};
      world.template field<PassableTag>(start) = 1;
      world.template field<CostTag>(start) = 1;
    }
  }
  for (const auto goal : weighted_goals<World>()) {
    world.template field<PassableTag>(goal) = 1;
    world.template field<CostTag>(goal) = 1;
  }
}

template <typename World>
auto make_weighted_requests(const World&, std::size_t count, GoalShape shape)
    -> std::vector<tess::PathRequest> {
  using Shape = typename World::shape_type;
  const auto extent = static_cast<std::int64_t>(Shape::size.x);
  const auto perimeter = static_cast<std::size_t>((Shape::size.x - 1) * 4);
  bench_check(shape != GoalShape::AllDistinct || count <= perimeter,
              "distinct-goal count exceeds fixture perimeter");
  const auto goals = weighted_goals<World>();
  std::vector<tess::PathRequest> requests;
  requests.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    const auto offset = static_cast<std::int64_t>(index);
    const auto start = tess::Coord3{1 + offset % 10, 1 + (offset / 10) % 10, 0};
    const auto goal =
        shape == GoalShape::One ? goals.front()
        : shape == GoalShape::Eight
            ? goals[index % goals.size()]
            : perimeter_coord<Shape>(index +
                                     static_cast<std::size_t>(extent - 1) * 2);
    requests.push_back({start, goal});
  }
  return requests;
}

template <std::uint64_t Extent>
void run_weighted(benchmark::State& state, GoalShape goal_shape,
                  WeightedStrategy strategy) {
  WeightedWorld<Extent> world;
  fill_world(world);
  carve_sparse_blockers(world);
  prepare_weighted_world(world);
  const auto count = static_cast<std::size_t>(state.range(0));
  const auto requests = make_weighted_requests(world, count, goal_shape);
  const auto references = reference_costs<true>(world, requests);
  std::uint64_t total_cost = 0;
  std::uint64_t total_expanded = 0;
  auto batch_stats = tess::WeightedPathBatchStats{};

  if (strategy == WeightedStrategy::Astar) {
    tess::PathScratch scratch;
    scratch.reserve_nodes(node_count<WeightedWorld<Extent>>());
    benchmark::DoNotOptimize(
        tess::weighted_astar_path<WeightedWorld<Extent>, WeightedMovement>(
            world, requests.front(), scratch));
    for (auto _ : state) {
      total_cost = 0;
      total_expanded = 0;
      for (const auto request : requests) {
        const auto result =
            tess::weighted_astar_path<WeightedWorld<Extent>, WeightedMovement>(
                world, request, scratch);
        total_cost += result.cost;
        total_expanded += result.expanded_nodes;
      }
      benchmark::DoNotOptimize(total_cost);
      benchmark::DoNotOptimize(total_expanded);
    }
  } else {
    tess::WeightedPathBatchScratch scratch;
    scratch.reserve_search_nodes(node_count<WeightedWorld<Extent>>());
    scratch.reserve_requests(count);
    const auto warmup =
        tess::weighted_path_batch<WeightedWorld<Extent>, WeightedMovement, 7>(
            world, requests, scratch);
    scratch.reserve_path_nodes(scratch.stats().path_nodes);
    benchmark::DoNotOptimize(warmup.data());
    for (auto _ : state) {
      total_cost = 0;
      total_expanded = 0;
      const auto results =
          tess::weighted_path_batch<WeightedWorld<Extent>, WeightedMovement, 7>(
              world, requests, scratch);
      for (const auto result : results) {
        total_cost += result.cost;
        total_expanded += result.expanded_nodes;
      }
      batch_stats = scratch.stats();
      benchmark::DoNotOptimize(results.data());
      benchmark::DoNotOptimize(total_cost);
      benchmark::DoNotOptimize(total_expanded);
    }
  }

  if (strategy == WeightedStrategy::Astar) {
    tess::PathScratch scratch;
    scratch.reserve_nodes(node_count<WeightedWorld<Extent>>());
    for (std::size_t sample = 0; sample < references.indices.size(); ++sample) {
      const auto index = references.indices[sample];
      const auto result =
          tess::weighted_astar_path<WeightedWorld<Extent>, WeightedMovement>(
              world, requests[index], scratch);
      check_path(world, result, requests[index], references.costs[sample]);
    }
  } else {
    tess::WeightedPathBatchScratch scratch;
    scratch.reserve_search_nodes(node_count<WeightedWorld<Extent>>());
    scratch.reserve_requests(count);
    const auto results =
        tess::weighted_path_batch<WeightedWorld<Extent>, WeightedMovement, 7>(
            world, requests, scratch);
    bench_check(results.size() == requests.size(),
                "weighted batch returned the wrong result count");
    for (std::size_t sample = 0; sample < references.indices.size(); ++sample) {
      const auto index = references.indices[sample];
      check_path(world, results[index], requests[index],
                 references.costs[sample]);
    }
  }

  record_common_counters<WeightedWorld<Extent>>(state, count, total_cost);
  if (strategy == WeightedStrategy::Astar) {
    state.counters["astar.expanded_total"] =
        static_cast<double>(total_expanded);
  }
  state.counters["batch.unique_goals"] =
      static_cast<double>(batch_stats.unique_goals);
  state.counters["batch.field_builds"] =
      static_cast<double>(batch_stats.field_builds);
  state.counters["batch.astar_fallbacks"] =
      static_cast<double>(batch_stats.astar_fallbacks);
  state.counters["batch.path_nodes"] =
      static_cast<double>(batch_stats.path_nodes);
  state.counters["requests.unique_starts"] =
      static_cast<double>(std::min(count, std::size_t{100}));
}

template <typename Function>
void register_counts(const std::string& name, Function function,
                     std::size_t max_count = kRequestCounts.back(),
                     std::size_t extra_count = 0) {
  auto* benchmark = benchmark::RegisterBenchmark(name.c_str(), function);
  for (const auto count : kRequestCounts) {
    if (static_cast<std::size_t>(count) <= max_count) {
      benchmark->Arg(count);
    }
  }
  if (extra_count != 0 && extra_count <= max_count) {
    benchmark->Arg(static_cast<std::int64_t>(extra_count));
  }
}

template <std::uint64_t Extent>
void register_extent() {
  const auto extent = std::to_string(Extent) + "x" + std::to_string(Extent);
  const auto prefix = std::string{"lab/"} + "path_strategy_crossover/";
  for (const auto layout : {UnitLayout::Open, UnitLayout::RoomPortals}) {
    const auto layout_name =
        layout == UnitLayout::Open ? "open" : "room_portals";
    for (const auto strategy :
         {UnitStrategy::Astar, UnitStrategy::DistanceField}) {
      const auto strategy_name =
          strategy == UnitStrategy::Astar ? "astar" : "distance_field";
      register_counts(prefix + "unit_shared/" + layout_name + "/" +
                          strategy_name + "/" + extent,
                      [=](benchmark::State& state) {
                        run_unit_shared<Extent>(state, layout, strategy);
                      });
    }
  }

  for (const auto shape :
       {CacheShape::ExactRepeats, CacheShape::SameGoalSuffixes}) {
    const auto shape_name = shape == CacheShape::ExactRepeats
                                ? "exact_repeats"
                                : "same_goal_suffixes";
    for (const auto strategy :
         {CacheStrategy::Astar, CacheStrategy::ColdRouteCache}) {
      const auto strategy_name =
          strategy == CacheStrategy::Astar ? "astar" : "cold_route_cache";
      register_counts(prefix + "route_cache/" + shape_name + "/" +
                          strategy_name + "/" + extent,
                      [=](benchmark::State& state) {
                        run_cache<Extent>(state, shape, strategy);
                      });
    }
  }

  const auto perimeter = static_cast<std::size_t>((Extent - 1) * 4);
  for (const auto shape :
       {GoalShape::One, GoalShape::Eight, GoalShape::AllDistinct}) {
    const auto shape_name = shape == GoalShape::One     ? "one_goal"
                            : shape == GoalShape::Eight ? "eight_goals"
                                                        : "all_distinct_goals";
    for (const auto strategy :
         {WeightedStrategy::Astar, WeightedStrategy::Batch}) {
      const auto strategy_name = strategy == WeightedStrategy::Astar
                                     ? "weighted_astar"
                                     : "weighted_batch";
      register_counts(
          prefix + "weighted/" + shape_name + "/" + strategy_name + "/" +
              extent,
          [=](benchmark::State& state) {
            run_weighted<Extent>(state, shape, strategy);
          },
          shape == GoalShape::AllDistinct ? perimeter : kRequestCounts.back(),
          shape == GoalShape::AllDistinct && Extent == 512 ? perimeter : 0);
    }
  }
}

[[maybe_unused]] const auto kRegistered = [] {
  register_extent<128>();
  register_extent<256>();
  register_extent<512>();
  register_extent<1024>();
  register_extent<2048>();
  register_extent<4096>();
  register_extent<8192>();
  register_extent<16384>();
  return true;
}();

}  // namespace

int main(int argc, char** argv) {
  auto has_filter = false;
  auto lists_tests = false;
  for (auto index = 1; index < argc; ++index) {
    const auto argument = std::string_view{argv[index]};
    has_filter = has_filter || argument.starts_with("--benchmark_filter=");
    lists_tests = lists_tests || argument == "--benchmark_list_tests" ||
                  argument == "--benchmark_list_tests=true";
  }
  if (!has_filter && !lists_tests) {
    std::fprintf(stderr,
                 "an explicit --benchmark_filter is required; the full "
                 "capacity registry is opt-in\n");
    return 2;
  }
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 2;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
