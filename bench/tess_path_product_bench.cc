#include <benchmark/benchmark.h>
#include <tess/tess.h>

#include <array>
#include <cstdint>
#include <utility>

namespace {

using PathScaleShape =
    tess::Shape<tess::Extent3{512, 512, 1}, tess::Extent3{32, 32, 1}>;

struct PassableTag {};

using PathSchema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>>;
using PathScaleWorld = tess::AlwaysResidentWorld<PathScaleShape, PathSchema>;

template <typename World>
void fill_path_passable(World& world, std::uint8_t value) {
  for (auto& chunk : world.chunks()) {
    auto passable = chunk.template field_span<PassableTag>();
    for (auto& tile : passable) {
      tile = value;
    }
  }
}

template <typename Shape>
[[nodiscard]] constexpr auto path_node_count() noexcept -> std::uint64_t {
  return Shape::size.x * Shape::size.y * Shape::size.z;
}

void carve_room_portals(PathScaleWorld& world) {
  constexpr auto room_size = std::int64_t{32};
  for (std::int64_t x = room_size;
       x < static_cast<std::int64_t>(PathScaleShape::size.x); x += room_size) {
    for (std::int64_t y = 0;
         y < static_cast<std::int64_t>(PathScaleShape::size.y); ++y) {
      const auto room = y / room_size;
      const auto portal = (room * 23 + x / room_size * 17) % room_size;
      if (y % room_size != portal) {
        world.template field<PassableTag>(tess::Coord3{x, y, 0}) = 0;
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
      }
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

void record_field_product_cache_counters(benchmark::State& state,
                                         tess::FieldProductCacheStats stats) {
  state.counters["cache.entries"] = static_cast<double>(stats.entries);
  state.counters["cache.bytes"] = static_cast<double>(stats.bytes);
  state.counters["cache.hits"] = static_cast<double>(stats.hits);
  state.counters["cache.misses"] = static_cast<double>(stats.misses);
  state.counters["cache.evictions"] = static_cast<double>(stats.evictions);
  state.counters["cache.stale_rejections"] =
      static_cast<double>(stats.stale_rejections);
}

#if TESS_DIAGNOSTICS_ENABLED
void record_path_diagnostic_counters(
    benchmark::State& state,
    const tess::diagnostics::PathCounters& diagnostics) {
  state.counters["diag.scratch_clear_calls"] =
      static_cast<double>(diagnostics.scratch_clear_calls);
  state.counters["diag.scratch_clear_nodes"] =
      static_cast<double>(diagnostics.scratch_clear_nodes);
  state.counters["diag.initializations"] =
      static_cast<double>(diagnostics.initializations);
  state.counters["diag.start_passability_checks"] =
      static_cast<double>(diagnostics.start_passability_checks);
  state.counters["diag.goal_passability_checks"] =
      static_cast<double>(diagnostics.goal_passability_checks);
  state.counters["diag.heap_pushes"] =
      static_cast<double>(diagnostics.heap_pushes);
  state.counters["diag.heap_pops"] = static_cast<double>(diagnostics.heap_pops);
  state.counters["diag.stale_pops"] =
      static_cast<double>(diagnostics.stale_pops);
  state.counters["diag.closed_pops"] =
      static_cast<double>(diagnostics.closed_pops);
  state.counters["diag.neighbor_candidates"] =
      static_cast<double>(diagnostics.neighbor_candidates);
  state.counters["diag.passability_checks"] =
      static_cast<double>(diagnostics.passability_checks);
  state.counters["diag.blocked_neighbors"] =
      static_cast<double>(diagnostics.blocked_neighbors);
  state.counters["diag.closed_neighbors"] =
      static_cast<double>(diagnostics.closed_neighbors);
  state.counters["diag.relax_attempts"] =
      static_cast<double>(diagnostics.relax_attempts);
  state.counters["diag.relax_successes"] =
      static_cast<double>(diagnostics.relax_successes);
  state.counters["diag.touched_nodes"] =
      static_cast<double>(diagnostics.touched_nodes);
  state.counters["diag.heuristic_calls"] =
      static_cast<double>(diagnostics.heuristic_calls);
  state.counters["diag.reconstructed_nodes"] =
      static_cast<double>(diagnostics.reconstructed_nodes);
}

void record_allocation_diagnostic_counters(
    benchmark::State& state,
    const tess::diagnostics::AllocationCounters& diagnostics) {
  state.counters["diag.allocations"] =
      static_cast<double>(diagnostics.allocations);
  state.counters["diag.allocation_bytes"] =
      static_cast<double>(diagnostics.allocation_bytes);
  state.counters["diag.deallocations"] =
      static_cast<double>(diagnostics.deallocations);
  state.counters["diag.deallocation_bytes"] =
      static_cast<double>(diagnostics.deallocation_bytes);
}

#define TESS_PATH_DIAG_DECL(scratch)             \
  tess::diagnostics::PathCounters path_counters; \
  tess::diagnostics::AllocationCounters allocation_counters;
#define TESS_PATH_DIAG_RESET()   \
  do {                           \
    path_counters.reset();       \
    allocation_counters.reset(); \
  } while (false)
#define TESS_PATH_DIAG_RUN(...)                                              \
  do {                                                                       \
    tess::diagnostics::ScopedPathCounters path_counter_scope{path_counters}; \
    tess::diagnostics::ScopedAllocationCounters allocation_scope{            \
        allocation_counters};                                                \
    __VA_ARGS__;                                                             \
  } while (false)
#define TESS_PATH_DIAG_RECORD(state)                                     \
  do {                                                                   \
    record_path_diagnostic_counters((state), path_counters);             \
    record_allocation_diagnostic_counters((state), allocation_counters); \
  } while (false)
#else
#define TESS_PATH_DIAG_DECL(scratch) \
  do {                               \
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

[[nodiscard]] auto product_goals() {
  return std::array{
      tess::Coord3{510, 510, 0}, tess::Coord3{510, 447, 0},
      tess::Coord3{447, 510, 0}, tess::Coord3{383, 510, 0},
      tess::Coord3{510, 383, 0}, tess::Coord3{447, 447, 0},
      tess::Coord3{383, 447, 0}, tess::Coord3{447, 383, 0},
  };
}

void add_goals(PathScaleWorld& world, tess::GoalSet& goal_set) {
  const auto goals = product_goals();
  goal_set.reserve(goals.size());
  for (const auto goal : goals) {
    world.template field<PassableTag>(goal) = 1;
    goal_set.add(goal);
  }
}

void BM_path_distance_field_product_build_8_goal_room_portals_512x512(
    benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);
  carve_room_portals(world);

  tess::GoalSet goal_set;
  add_goals(world, goal_set);

  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  tess::DistanceFieldProduct product;
  product.reserve_goals(goal_set.size());
  product.reserve_nodes(path_node_count<PathScaleShape>());
  product.reserve_dependencies(PathScaleWorld::chunk_count);
  TESS_PATH_DIAG_DECL(scratch);
  tess::DistanceFieldResult field;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    TESS_PATH_DIAG_RUN(
        field = tess::build_distance_field_product<PathScaleWorld, PassableTag>(
            world, goal_set, scratch, product));
    benchmark::DoNotOptimize(field.expanded_nodes);
    benchmark::DoNotOptimize(product.byte_size());
  }
  state.counters["field_expanded_nodes"] =
      static_cast<double>(field.expanded_nodes);
  state.counters["field_reached_nodes"] =
      static_cast<double>(field.reached_nodes);
  state.counters["product.bytes"] = static_cast<double>(product.byte_size());
  TESS_PATH_DIAG_RECORD(state);
}

void BM_path_nearest_target_product_100_starts_room_portals_512x512(
    benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);
  carve_room_portals(world);

  tess::GoalSet goal_set;
  add_goals(world, goal_set);

  std::array<tess::Coord3, 100> starts{};
  for (std::size_t i = 0; i < starts.size(); ++i) {
    const auto offset = static_cast<std::int64_t>(i);
    starts[i] = tess::Coord3{1 + offset % 16, 1 + offset / 16, 0};
    world.template field<PassableTag>(starts[i]) = 1;
  }

  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  tess::DistanceFieldProduct product;
  product.reserve_goals(goal_set.size());
  product.reserve_nodes(path_node_count<PathScaleShape>());
  product.reserve_dependencies(PathScaleWorld::chunk_count);
  (void)tess::build_distance_field_product<PathScaleWorld, PassableTag>(
      world, goal_set, scratch, product);

  TESS_PATH_DIAG_DECL(scratch);
  tess::NearestTargetResult result;
  std::uint64_t total_cost = 0;

  for (auto _ : state) {
    TESS_PATH_DIAG_RESET();
    total_cost = 0;
    TESS_PATH_DIAG_RUN(for (const auto start : starts) {
      result = tess::nearest_target<PathScaleWorld, PassableTag>(
          world, start, product, scratch);
      total_cost += result.cost;
    });
    benchmark::DoNotOptimize(total_cost);
    benchmark::DoNotOptimize(result.path.data());
  }
  state.counters["agents"] = static_cast<double>(starts.size());
  state.counters["cost"] = static_cast<double>(result.cost);
  state.counters["path_nodes"] = static_cast<double>(result.path.size());
  state.counters["reached_nodes"] = static_cast<double>(result.reached_nodes);
  TESS_PATH_DIAG_RECORD(state);
}

void BM_path_field_product_cache_hit_replay_room_portals_512x512(
    benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);
  carve_room_portals(world);

  tess::GoalSet goals;
  goals.add(tess::Coord3{510, 510, 0});
  world.template field<PassableTag>(tess::Coord3{510, 510, 0}) = 1;

  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  tess::DistanceFieldProduct product;
  product.reserve_nodes(path_node_count<PathScaleShape>());
  product.reserve_dependencies(PathScaleWorld::chunk_count);
  (void)tess::build_distance_field_product<PathScaleWorld, PassableTag>(
      world, goals, scratch, product);

  tess::FieldProductCache cache{product.byte_size() + 4096u};
  cache.reserve_entries(1);
  (void)cache.store<PathScaleWorld, PassableTag>(std::move(product));

  const auto start = tess::Coord3{1, 1, 0};
  world.template field<PassableTag>(start) = 1;
  tess::PathResult result;
  for (auto _ : state) {
    const auto* cached =
        cache.lookup<PathScaleWorld, PassableTag>(world, goals);
    if (cached != nullptr) {
      result = tess::distance_field_product_path<PathScaleWorld, PassableTag>(
          world, start, *cached, scratch);
    }
    benchmark::DoNotOptimize(cached);
    benchmark::DoNotOptimize(result.path.data());
  }
  record_field_product_cache_counters(state, cache.stats());
  record_path_counters(state, result);
}

void BM_path_field_product_cache_stale_rejection_room_portals_512x512(
    benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);
  carve_room_portals(world);

  tess::GoalSet goals;
  goals.add(tess::Coord3{510, 510, 0});
  world.template field<PassableTag>(tess::Coord3{510, 510, 0}) = 1;

  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());
  tess::DistanceFieldProduct product;
  product.reserve_nodes(path_node_count<PathScaleShape>());
  product.reserve_dependencies(PathScaleWorld::chunk_count);
  (void)tess::build_distance_field_product<PathScaleWorld, PassableTag>(
      world, goals, scratch, product);
  world.mark_dirty(tess::ChunkKey{0}, 1u,
                   tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}});

  tess::FieldProductCache cache{product.byte_size() + 4096u};
  cache.reserve_entries(1);
  const tess::DistanceFieldProduct* cached = nullptr;
  for (auto _ : state) {
    state.PauseTiming();
    cache.clear();
    cache.reset_stats();
    auto stored_product = product;
    (void)cache.store<PathScaleWorld, PassableTag>(std::move(stored_product));
    state.ResumeTiming();

    cached = cache.lookup<PathScaleWorld, PassableTag>(world, goals);
    benchmark::DoNotOptimize(cached);
  }
  record_field_product_cache_counters(state, cache.stats());
}

void BM_path_field_product_cache_lru_eviction_512x512(benchmark::State& state) {
  PathScaleWorld world;
  fill_path_passable(world, 1);

  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(path_node_count<PathScaleShape>());

  tess::GoalSet first_goals;
  first_goals.add(tess::Coord3{511, 511, 0});
  tess::DistanceFieldProduct first;
  first.reserve_nodes(path_node_count<PathScaleShape>());
  first.reserve_dependencies(PathScaleWorld::chunk_count);
  (void)tess::build_distance_field_product<PathScaleWorld, PassableTag>(
      world, first_goals, scratch, first);

  tess::FieldProductCache sizing_cache{first.byte_size() + 4096u};
  auto sizing_product = first;
  (void)sizing_cache.store<PathScaleWorld, PassableTag>(
      std::move(sizing_product));
  const auto budget = sizing_cache.stats().bytes + 1u;

  tess::GoalSet second_goals;
  second_goals.add(tess::Coord3{511, 0, 0});
  tess::DistanceFieldProduct second;
  second.reserve_nodes(path_node_count<PathScaleShape>());
  second.reserve_dependencies(PathScaleWorld::chunk_count);
  (void)tess::build_distance_field_product<PathScaleWorld, PassableTag>(
      world, second_goals, scratch, second);

  tess::FieldProductCache cache{budget};
  cache.reserve_entries(2);
  for (auto _ : state) {
    state.PauseTiming();
    cache.clear();
    cache.reset_stats();
    auto first_copy = first;
    (void)cache.store<PathScaleWorld, PassableTag>(std::move(first_copy));
    auto second_copy = second;
    state.ResumeTiming();

    auto stored =
        cache.store<PathScaleWorld, PassableTag>(std::move(second_copy)) ? 1
                                                                         : 0;
    benchmark::DoNotOptimize(stored);
  }
  record_field_product_cache_counters(state, cache.stats());
}

BENCHMARK(BM_path_distance_field_product_build_8_goal_room_portals_512x512)
    ->Name("path/distance_field_product_build_8_goal_room_portals_512x512");
BENCHMARK(BM_path_nearest_target_product_100_starts_room_portals_512x512)
    ->Name("path/nearest_target_product_100_starts_room_portals_512x512");
BENCHMARK(BM_path_field_product_cache_hit_replay_room_portals_512x512)
    ->Name("path/field_product_cache_hit_replay_room_portals_512x512");
BENCHMARK(BM_path_field_product_cache_stale_rejection_room_portals_512x512)
    ->Name("path/field_product_cache_stale_rejection_room_portals_512x512");
BENCHMARK(BM_path_field_product_cache_lru_eviction_512x512)
    ->Name("path/field_product_cache_lru_eviction_512x512");

}  // namespace
