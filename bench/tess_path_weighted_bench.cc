#include <benchmark/benchmark.h>
#include <tess/tess.h>

#include <array>
#include <cstdint>

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

void carve_sparse_blockers(WeightedPathScaleWorld& world) {
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

void record_path_counters(benchmark::State& state,
                          const tess::PathResult& result) {
  state.counters["cost"] = static_cast<double>(result.cost);
  state.counters["path_nodes"] = static_cast<double>(result.path.size());
  state.counters["expanded_nodes"] = static_cast<double>(result.expanded_nodes);
  state.counters["reached_nodes"] = static_cast<double>(result.reached_nodes);
}

template <std::size_t Count>
void record_batch_counters(benchmark::State& state,
                           std::uint64_t total_expanded) {
  state.counters["agents"] = static_cast<double>(Count);
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

auto make_multigoal_sparse_requests(WeightedPathScaleWorld& world)
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

void BM_path_weighted_astar_batch_100_shared_sparse_512x512(
    benchmark::State& state) {
  WeightedPathScaleWorld world;
  fill_path_passable(world, 1);
  fill_path_cost(world, 1);
  carve_sparse_blockers(world);
  const auto requests = make_shared_sparse_requests(world);

  tess::PathScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  TESS_PATH_DIAG_DECL();
  tess::PathResult result;
  std::uint64_t total_expanded = 0;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    std::uint64_t total_cost = 0;
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
  record_batch_counters<requests.size()>(state, total_expanded);
  record_path_counters(state, result);
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
  TESS_PATH_DIAG_DECL();
  tess::DistanceFieldResult field;
  tess::PathResult result;
  std::uint64_t total_expanded = 0;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    std::uint64_t total_cost = 0;
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
  record_batch_counters<requests.size()>(state, total_expanded);
  state.counters["field_expanded_nodes"] =
      static_cast<double>(field.expanded_nodes);
  state.counters["field_reached_nodes"] =
      static_cast<double>(field.reached_nodes);
  record_path_counters(state, result);
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
  TESS_PATH_DIAG_DECL();
  tess::DistanceFieldResult field;
  tess::PathResult result;
  std::uint64_t total_expanded = 0;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    std::uint64_t total_cost = 0;
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
  record_batch_counters<requests.size()>(state, total_expanded);
  state.counters["field_expanded_nodes"] =
      static_cast<double>(field.expanded_nodes);
  state.counters["field_reached_nodes"] =
      static_cast<double>(field.reached_nodes);
  record_path_counters(state, result);
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
  TESS_PATH_DIAG_DECL();
  tess::PathResult result;
  std::uint64_t total_expanded = 0;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    std::uint64_t total_cost = 0;
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
  record_batch_counters<requests.size()>(state, total_expanded);
  state.counters["batch.unique_goals"] =
      static_cast<double>(unique_goal_count(requests));
  record_path_counters(state, result);
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
  TESS_PATH_DIAG_DECL();
  tess::DistanceFieldResult field;
  tess::PathResult result;
  std::uint64_t total_expanded = 0;
  std::uint64_t total_field_expanded = 0;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    std::uint64_t total_cost = 0;
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
  record_batch_counters<requests.size()>(state, total_expanded);
  state.counters["batch.unique_goals"] =
      static_cast<double>(unique_goal_count(requests));
  state.counters["field_expanded_nodes"] =
      static_cast<double>(total_field_expanded);
  state.counters["field_reached_nodes"] =
      static_cast<double>(field.reached_nodes);
  record_path_counters(state, result);
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
  TESS_PATH_DIAG_DECL();
  tess::DistanceFieldResult field;
  tess::PathResult result;
  std::uint64_t total_expanded = 0;
  std::uint64_t total_field_expanded = 0;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    std::uint64_t total_cost = 0;
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
  record_batch_counters<requests.size()>(state, total_expanded);
  state.counters["batch.unique_goals"] =
      static_cast<double>(unique_goal_count(requests));
  state.counters["field_expanded_nodes"] =
      static_cast<double>(total_field_expanded);
  state.counters["field_reached_nodes"] =
      static_cast<double>(field.reached_nodes);
  record_path_counters(state, result);
  TESS_PATH_DIAG_RECORD(state);
}

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

}  // namespace
