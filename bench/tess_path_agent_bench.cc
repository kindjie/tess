#include <benchmark/benchmark.h>
#include <tess/tess.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

// Correctness checks mandated by docs/planning/benchmark-plan.md run outside
// the timed regions; a failed check aborts the benchmark binary so threshold
// runs cannot silently gate on wrong results.
void bench_check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "tess_bench correctness check failed: %s\n", message);
    std::abort();
  }
}

void check_all_agents_found(tess::PathAgentFrameStats stats,
                            std::size_t agent_count) {
  bench_check(stats.submitted == agent_count,
              "not every agent submitted a path request");
  bench_check(stats.completed == stats.submitted,
              "not every submitted request completed");
  bench_check(stats.found == agent_count, "not every agent found a path");
  bench_check(
      stats.invalid_start == 0 && stats.invalid_goal == 0 && stats.no_path == 0,
      "agent batch reported failed requests");
}

using PathScaleShape =
    tess::Shape<tess::Extent3{512, 512, 1}, tess::Extent3{32, 32, 1}>;

struct PassableTag {};
struct CostTag {};

using PathSchema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>>;
using WeightedPathSchema =
    tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>,
                      tess::Field<CostTag, std::uint32_t>>;
using PathWorld = tess::AlwaysResidentWorld<PathScaleShape, PathSchema>;
using WeightedPathWorld =
    tess::AlwaysResidentWorld<PathScaleShape, WeightedPathSchema>;

constexpr auto PathNodeCount =
    PathScaleShape::size.x * PathScaleShape::size.y * PathScaleShape::size.z;

template <typename World>
void fill_passable(World& world, std::uint8_t value) {
  for (auto& page : world.chunks()) {
    auto field = page.template field_span<PassableTag>();
    for (auto& tile : field) {
      tile = value;
    }
  }
}

void fill_cost(WeightedPathWorld& world, std::uint32_t value) {
  for (auto& page : world.chunks()) {
    auto field = page.template field_span<CostTag>();
    for (auto& tile : field) {
      tile = value;
    }
  }
}

void carve_sparse_blockers(WeightedPathWorld& world) {
  for (std::int64_t x = 32; x < 512; x += 37) {
    for (std::int64_t y = 0; y < 512; ++y) {
      if ((y + x) % 29 != 0) {
        world.template field<PassableTag>(tess::Coord3{x, y, 0}) = 0;
      } else {
        world.template field<CostTag>(tess::Coord3{x, y, 0}) = 4;
      }
    }
  }
}

void reserve_runtime(tess::PathRequestRuntime& runtime,
                     std::size_t agent_count) {
  runtime.reserve_requests(agent_count);
  runtime.reserve_search_nodes(PathNodeCount);
  runtime.reserve_path_nodes(PathNodeCount);
  runtime.reserve_unit_routes(agent_count);
}

void record_agent_counters(benchmark::State& state,
                           tess::PathAgentFrameStats stats) {
  state.counters["agents.submitted"] = static_cast<double>(stats.submitted);
  state.counters["agents.completed"] = static_cast<double>(stats.completed);
  state.counters["agents.found"] = static_cast<double>(stats.found);
  state.counters["agents.invalid_start"] =
      static_cast<double>(stats.invalid_start);
  state.counters["agents.invalid_goal"] =
      static_cast<double>(stats.invalid_goal);
  state.counters["agents.no_path"] = static_cast<double>(stats.no_path);
}

void record_tick_counters(benchmark::State& state,
                          tess::PathAgentTickStats stats) {
  state.counters["tick"] = static_cast<double>(stats.tick);
  state.counters["tick.processed_paths"] = stats.processed_paths ? 1.0 : 0.0;
  state.counters["tick.movement.advanced"] =
      static_cast<double>(stats.movement.advanced);
  state.counters["tick.movement.arrived"] =
      static_cast<double>(stats.movement.arrived);
}

void record_route_cache_counters(benchmark::State& state,
                                 tess::RouteCacheStats stats) {
  state.counters["cache.entries"] = static_cast<double>(stats.entries);
  state.counters["cache.hits"] = static_cast<double>(stats.hits);
  state.counters["cache.suffix_hits"] = static_cast<double>(stats.suffix_hits);
  state.counters["cache.misses"] = static_cast<double>(stats.misses);
  state.counters["cache.path_nodes"] = static_cast<double>(stats.path_nodes);
}

void record_field_product_cache_counters(benchmark::State& state,
                                         tess::FieldProductCacheStats stats) {
  state.counters["field_cache.entries"] = static_cast<double>(stats.entries);
  state.counters["field_cache.bytes"] = static_cast<double>(stats.bytes);
  state.counters["field_cache.hits"] = static_cast<double>(stats.hits);
  state.counters["field_cache.misses"] = static_cast<double>(stats.misses);
  state.counters["field_cache.evictions"] =
      static_cast<double>(stats.evictions);
  state.counters["field_cache.stale_rejections"] =
      static_cast<double>(stats.stale_rejections);
}

void BM_path_agent_runtime_100_unit_suffix_512x512(benchmark::State& state) {
  PathWorld world;
  fill_passable(world, 1);

  std::array<tess::PathAgentState, 100> agents{};
  const auto goal = tess::Coord3{511, 0, 0};
  for (std::size_t i = 0; i < agents.size(); ++i) {
    agents[i].position = tess::Coord3{static_cast<std::int64_t>(i), 0, 0};
    tess::set_path_agent_goal(agents[i], goal);
  }

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  (void)tess::process_unit_path_agents<PathWorld, PassableTag>(world, agents,
                                                               runtime);

  tess::PathAgentFrameStats frame_stats;
  for (auto _ : state) {
    frame_stats = tess::process_unit_path_agents<PathWorld, PassableTag>(
        world, agents, runtime);
    benchmark::DoNotOptimize(frame_stats.found);
    benchmark::DoNotOptimize(runtime.results().data());
  }

  check_all_agents_found(frame_stats, agents.size());
  record_agent_counters(state, frame_stats);
  const auto stats = runtime.stats();
  record_route_cache_counters(state, stats.route_cache);
  state.counters["runtime.path_nodes"] = static_cast<double>(stats.path_nodes);
}

void BM_path_agent_tick_100_unit_clean_512x512(benchmark::State& state) {
  PathWorld world;
  fill_passable(world, 1);

  std::array<tess::PathAgentState, 100> agents{};
  std::array<tess::Coord3, 100> starts{};
  const auto goal = tess::Coord3{511, 0, 0};
  for (std::size_t i = 0; i < agents.size(); ++i) {
    starts[i] = tess::Coord3{static_cast<std::int64_t>(i), 0, 0};
    agents[i].position = starts[i];
    tess::set_path_agent_goal(agents[i], goal);
  }

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::PathAgentTickState tick_state;
  const auto options = tess::PathAgentTickOptions{.max_steps = 1};
  (void)tess::tick_unit_path_agents<PathWorld, PassableTag>(
      tick_state, world, agents, runtime, options);

  tess::PathAgentTickStats tick_stats;
  for (auto _ : state) {
    // The agent reset stays inside the timed region on purpose: it is ~500
    // trivial stores, while the per-iteration PauseTiming()/ResumeTiming()
    // pair it previously hid behind costs a comparable amount to the whole
    // measured tick and distorted this sub-microsecond benchmark.
    for (std::size_t i = 0; i < agents.size(); ++i) {
      agents[i].position = starts[i];
      agents[i].goal = goal;
      agents[i].path_index = 0;
      agents[i].status = tess::PathStatus::Found;
      agents[i].has_goal = true;
    }

    tick_stats = tess::tick_unit_path_agents<PathWorld, PassableTag>(
        tick_state, world, agents, runtime, options);
    benchmark::DoNotOptimize(tick_stats.tick);
    benchmark::DoNotOptimize(agents.data());
  }

  bench_check(tick_stats.movement.advanced == agents.size(),
              "clean tick did not advance every agent");
  record_tick_counters(state, tick_stats);
  const auto stats = runtime.stats();
  record_route_cache_counters(state, stats.route_cache);
  state.counters["runtime.path_nodes"] = static_cast<double>(stats.path_nodes);
}

void BM_path_agent_tick_100_unit_dirty_world_edit_512x512(
    benchmark::State& state) {
  PathWorld world;
  fill_passable(world, 1);

  std::array<tess::PathAgentState, 100> agents{};
  for (std::size_t i = 0; i < agents.size(); ++i) {
    const auto offset = static_cast<std::int64_t>(i);
    agents[i].position = tess::Coord3{offset % 16, offset / 16, 0};
    tess::set_path_agent_goal(agents[i], tess::Coord3{511, 511, 0});
  }

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::PathAgentTickState tick_state;
  const auto options = tess::PathAgentTickOptions{
      .max_steps = 0,
      .cache_policy =
          {
              .clear_every_world_change = 4,
              .invalidate_unit_route_cache_on_world_change = true,
          },
  };

  tess::PathAgentTickStats tick_stats;
  std::uint64_t edits = 0;
  for (auto _ : state) {
    const auto coord =
        tess::Coord3{256, static_cast<std::int64_t>(edits % 32), 0};
    const auto passable = edits % 2 == 0;
    world.template field<PassableTag>(coord) = passable ? 1 : 0;
    world.mark_dirty(
        tess::chunk_key<PathScaleShape>(tess::tile_key<PathScaleShape>(coord)),
        1u, tess::Box3{coord, tess::Extent3{1, 1, 1}});
    ++edits;

    tess::mark_pathing_dirty(tick_state);
    tick_stats = tess::tick_unit_path_agents<PathWorld, PassableTag>(
        tick_state, world, agents, runtime, options);
    benchmark::DoNotOptimize(tick_stats.pathing.found);
    benchmark::DoNotOptimize(runtime.results().data());
  }

  record_tick_counters(state, tick_stats);
  check_all_agents_found(tick_stats.pathing, agents.size());
  record_agent_counters(state, tick_stats.pathing);
  const auto stats = runtime.stats();
  record_route_cache_counters(state, stats.route_cache);
  state.counters["runtime.cache_clears"] =
      static_cast<double>(stats.cache_clears);
  state.counters["runtime.path_nodes"] = static_cast<double>(stats.path_nodes);
}

void BM_path_agent_runtime_100_weighted_shared_512x512(
    benchmark::State& state) {
  WeightedPathWorld world;
  fill_passable(world, 1);
  fill_cost(world, 1);
  carve_sparse_blockers(world);

  std::array<tess::PathAgentState, 100> agents{};
  const auto goal = tess::Coord3{510, 510, 0};
  for (std::size_t i = 0; i < agents.size(); ++i) {
    const auto offset = static_cast<std::int64_t>(i);
    agents[i].position = tess::Coord3{1 + offset % 16, 1 + offset / 16, 0};
    world.template field<PassableTag>(agents[i].position) = 1;
    world.template field<CostTag>(agents[i].position) = 1;
    world.template field<PassableTag>(goal) = 1;
    world.template field<CostTag>(goal) = 1;
    tess::set_path_agent_goal(agents[i], goal);
  }

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());

  tess::PathAgentFrameStats frame_stats;
  for (auto _ : state) {
    frame_stats =
        tess::process_weighted_path_agents<WeightedPathWorld, PassableTag,
                                           CostTag, 8>(world, agents, runtime);
    benchmark::DoNotOptimize(frame_stats.found);
    benchmark::DoNotOptimize(runtime.results().data());
  }

  check_all_agents_found(frame_stats, agents.size());
  record_agent_counters(state, frame_stats);
  const auto stats = runtime.stats();
  state.counters["batch.unique_goals"] =
      static_cast<double>(stats.weighted_batch.unique_goals);
  state.counters["batch.field_builds"] =
      static_cast<double>(stats.weighted_batch.field_builds);
  state.counters["runtime.path_nodes"] = static_cast<double>(stats.path_nodes);
}

void BM_path_agent_tick_100_weighted_shared_dirty_512x512(
    benchmark::State& state) {
  WeightedPathWorld world;
  fill_passable(world, 1);
  fill_cost(world, 1);
  carve_sparse_blockers(world);

  std::array<tess::PathAgentState, 100> agents{};
  const auto goal = tess::Coord3{510, 510, 0};
  for (std::size_t i = 0; i < agents.size(); ++i) {
    const auto offset = static_cast<std::int64_t>(i);
    agents[i].position = tess::Coord3{1 + offset % 16, 1 + offset / 16, 0};
    world.template field<PassableTag>(agents[i].position) = 1;
    world.template field<CostTag>(agents[i].position) = 1;
    world.template field<PassableTag>(goal) = 1;
    world.template field<CostTag>(goal) = 1;
    tess::set_path_agent_goal(agents[i], goal);
  }

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::PathAgentTickState tick_state;
  const auto options = tess::PathAgentTickOptions{.max_steps = 0};

  tess::PathAgentTickStats tick_stats;
  for (auto _ : state) {
    tess::mark_pathing_dirty(tick_state);
    tick_stats = tess::tick_weighted_path_agents<WeightedPathWorld, PassableTag,
                                                 CostTag, 8>(
        tick_state, world, agents, runtime, options);
    benchmark::DoNotOptimize(tick_stats.pathing.found);
    benchmark::DoNotOptimize(runtime.results().data());
  }

  record_tick_counters(state, tick_stats);
  check_all_agents_found(tick_stats.pathing, agents.size());
  record_agent_counters(state, tick_stats.pathing);
  const auto stats = runtime.stats();
  state.counters["batch.unique_goals"] =
      static_cast<double>(stats.weighted_batch.unique_goals);
  state.counters["batch.field_builds"] =
      static_cast<double>(stats.weighted_batch.field_builds);
  state.counters["runtime.path_nodes"] = static_cast<double>(stats.path_nodes);
}

void BM_path_agent_runtime_100_weighted_mixed_512x512(benchmark::State& state) {
  WeightedPathWorld world;
  fill_passable(world, 1);
  fill_cost(world, 1);
  carve_sparse_blockers(world);

  std::array<tess::PathAgentState, 100> agents{};
  for (std::size_t i = 0; i < agents.size(); ++i) {
    const auto offset = static_cast<std::int64_t>(i);
    const auto goal = i % 2 == 0 ? tess::Coord3{510, 510, 0}
                                 : tess::Coord3{480, 510 - offset % 32, 0};
    agents[i].position = tess::Coord3{1 + offset % 16, 1 + offset / 16, 0};
    world.template field<PassableTag>(agents[i].position) = 1;
    world.template field<CostTag>(agents[i].position) = 1;
    world.template field<PassableTag>(goal) = 1;
    world.template field<CostTag>(goal) = 1;
    tess::set_path_agent_goal(agents[i], goal);
  }

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());

  tess::PathAgentFrameStats frame_stats;
  for (auto _ : state) {
    frame_stats =
        tess::process_weighted_path_agents<WeightedPathWorld, PassableTag,
                                           CostTag, 8>(world, agents, runtime);
    benchmark::DoNotOptimize(frame_stats.found);
    benchmark::DoNotOptimize(runtime.results().data());
  }

  check_all_agents_found(frame_stats, agents.size());
  record_agent_counters(state, frame_stats);
  const auto stats = runtime.stats();
  state.counters["batch.unique_goals"] =
      static_cast<double>(stats.weighted_batch.unique_goals);
  state.counters["batch.field_builds"] =
      static_cast<double>(stats.weighted_batch.field_builds);
  state.counters["runtime.path_nodes"] = static_cast<double>(stats.path_nodes);
}

void BM_path_agent_runtime_100_unit_world_edit_512x512(
    benchmark::State& state) {
  PathWorld world;
  fill_passable(world, 1);

  std::array<tess::PathAgentState, 100> agents{};
  for (std::size_t i = 0; i < agents.size(); ++i) {
    const auto offset = static_cast<std::int64_t>(i);
    agents[i].position = tess::Coord3{offset % 16, offset / 16, 0};
    tess::set_path_agent_goal(agents[i], tess::Coord3{511, 511, 0});
  }

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  const auto policy = tess::PathRuntimeCachePolicy{
      .clear_every_world_change = 4,
      .invalidate_unit_route_cache_on_world_change = true,
  };

  tess::PathAgentFrameStats frame_stats;
  std::uint64_t edits = 0;
  for (auto _ : state) {
    const auto coord =
        tess::Coord3{256, static_cast<std::int64_t>(edits % 32), 0};
    const auto passable = edits % 2 == 0;
    world.template field<PassableTag>(coord) = passable ? 1 : 0;
    world.mark_dirty(
        tess::chunk_key<PathScaleShape>(tess::tile_key<PathScaleShape>(coord)),
        1u, tess::Box3{coord, tess::Extent3{1, 1, 1}});
    ++edits;

    frame_stats = tess::process_unit_path_agents<PathWorld, PassableTag>(
        world, agents, runtime, policy);
    benchmark::DoNotOptimize(frame_stats.found);
    benchmark::DoNotOptimize(runtime.results().data());
  }

  check_all_agents_found(frame_stats, agents.size());
  record_agent_counters(state, frame_stats);
  const auto stats = runtime.stats();
  record_route_cache_counters(state, stats.route_cache);
  state.counters["runtime.cache_clears"] =
      static_cast<double>(stats.cache_clears);
  state.counters["runtime.path_nodes"] = static_cast<double>(stats.path_nodes);
}

template <typename PolicyFactory>
void run_unit_shared_goal_wall_gap_runtime(benchmark::State& state,
                                           PolicyFactory policy_factory) {
  PathWorld world;
  fill_passable(world, 1);
  for (std::int64_t y = 0; y < 511; ++y) {
    world.template field<PassableTag>(tess::Coord3{256, y, 0}) = 0;
  }

  std::array<tess::PathAgentState, 100> agents{};
  const auto goal = tess::Coord3{511, 511, 0};
  for (std::size_t i = 0; i < agents.size(); ++i) {
    const auto offset = static_cast<std::int64_t>(i);
    agents[i].position = tess::Coord3{offset % 16, offset / 16, 0};
    tess::set_path_agent_goal(agents[i], goal);
  }

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  runtime.reserve_unit_field_products(1);
  runtime.reserve_unit_field_product_dependencies(PathWorld::chunk_count);
  const auto policy = policy_factory();

  tess::PathAgentFrameStats frame_stats;
  for (auto _ : state) {
    frame_stats = tess::process_unit_path_agents<PathWorld, PassableTag>(
        world, agents, runtime, policy);
    benchmark::DoNotOptimize(frame_stats.found);
    benchmark::DoNotOptimize(runtime.results().data());
  }

  check_all_agents_found(frame_stats, agents.size());
  record_agent_counters(state, frame_stats);
  const auto stats = runtime.stats();
  record_route_cache_counters(state, stats.route_cache);
  record_field_product_cache_counters(state, stats.field_product_cache);
  state.counters["field_policy.candidates"] =
      static_cast<double>(stats.field_product_candidate_groups);
  state.counters["field_policy.used"] =
      static_cast<double>(stats.field_product_used_groups);
  state.counters["field_policy.skipped"] =
      static_cast<double>(stats.field_product_skipped_groups);
  state.counters["runtime.path_nodes"] = static_cast<double>(stats.path_nodes);
}

void BM_path_agent_runtime_100_unit_shared_wall_gap_route_cache_512x512(
    benchmark::State& state) {
  run_unit_shared_goal_wall_gap_runtime(
      state, [] { return tess::PathRuntimeCachePolicy{}; });
}

void BM_path_agent_runtime_100_unit_shared_wall_gap_field_cache_512x512(
    benchmark::State& state) {
  run_unit_shared_goal_wall_gap_runtime(state, [] {
    return tess::PathRuntimeCachePolicy{
        .use_unit_field_product_cache = true,
    };
  });
}

template <typename PolicyFactory>
void run_unit_scattered_goal_wall_gap_runtime(benchmark::State& state,
                                              PolicyFactory policy_factory) {
  PathWorld world;
  fill_passable(world, 1);
  for (std::int64_t y = 0; y < 511; ++y) {
    world.template field<PassableTag>(tess::Coord3{256, y, 0}) = 0;
  }

  std::array<tess::PathAgentState, 100> agents{};
  const auto goal = tess::Coord3{511, 511, 0};
  for (std::size_t i = 0; i < agents.size(); ++i) {
    const auto x = static_cast<std::int64_t>((i % 10) * 24);
    const auto y = static_cast<std::int64_t>((i / 10) * 48);
    agents[i].position = tess::Coord3{x, y, 0};
    tess::set_path_agent_goal(agents[i], goal);
  }

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  runtime.reserve_unit_field_products(1);
  runtime.reserve_unit_field_product_dependencies(PathWorld::chunk_count);
  const auto policy = policy_factory();

  tess::PathAgentFrameStats frame_stats;
  for (auto _ : state) {
    frame_stats = tess::process_unit_path_agents<PathWorld, PassableTag>(
        world, agents, runtime, policy);
    benchmark::DoNotOptimize(frame_stats.found);
    benchmark::DoNotOptimize(runtime.results().data());
  }

  check_all_agents_found(frame_stats, agents.size());
  record_agent_counters(state, frame_stats);
  const auto stats = runtime.stats();
  record_route_cache_counters(state, stats.route_cache);
  record_field_product_cache_counters(state, stats.field_product_cache);
  state.counters["field_policy.candidates"] =
      static_cast<double>(stats.field_product_candidate_groups);
  state.counters["field_policy.used"] =
      static_cast<double>(stats.field_product_used_groups);
  state.counters["field_policy.skipped"] =
      static_cast<double>(stats.field_product_skipped_groups);
  state.counters["runtime.path_nodes"] = static_cast<double>(stats.path_nodes);
}

void BM_path_agent_runtime_100_unit_scattered_wall_gap_route_cache_512x512(
    benchmark::State& state) {
  run_unit_scattered_goal_wall_gap_runtime(
      state, [] { return tess::PathRuntimeCachePolicy{}; });
}

void BM_path_agent_runtime_100_unit_scattered_wall_gap_field_cache_512x512(
    benchmark::State& state) {
  run_unit_scattered_goal_wall_gap_runtime(state, [] {
    return tess::PathRuntimeCachePolicy{
        .use_unit_field_product_cache = true,
    };
  });
}

BENCHMARK(BM_path_agent_runtime_100_unit_suffix_512x512)
    ->Name("path/agent_runtime_100_unit_suffix_512x512");
BENCHMARK(BM_path_agent_tick_100_unit_clean_512x512)
    ->Name("path/agent_tick_100_unit_clean_512x512");
BENCHMARK(BM_path_agent_tick_100_unit_dirty_world_edit_512x512)
    ->Name("path/agent_tick_100_unit_dirty_world_edit_512x512");
BENCHMARK(BM_path_agent_runtime_100_weighted_shared_512x512)
    ->Name("path/agent_runtime_100_weighted_shared_512x512");
BENCHMARK(BM_path_agent_tick_100_weighted_shared_dirty_512x512)
    ->Name("path/agent_tick_100_weighted_shared_dirty_512x512");
BENCHMARK(BM_path_agent_runtime_100_weighted_mixed_512x512)
    ->Name("path/agent_runtime_100_weighted_mixed_512x512");
BENCHMARK(BM_path_agent_runtime_100_unit_world_edit_512x512)
    ->Name("path/agent_runtime_100_unit_world_edit_512x512");
BENCHMARK(BM_path_agent_runtime_100_unit_shared_wall_gap_route_cache_512x512)
    ->Name("path/agent_runtime_100_unit_shared_wall_gap_route_cache_512x512");
BENCHMARK(BM_path_agent_runtime_100_unit_shared_wall_gap_field_cache_512x512)
    ->Name("path/agent_runtime_100_unit_shared_wall_gap_field_cache_512x512");
BENCHMARK(BM_path_agent_runtime_100_unit_scattered_wall_gap_route_cache_512x512)
    ->Name(
        "path/agent_runtime_100_unit_scattered_wall_gap_route_cache_512x512");
BENCHMARK(BM_path_agent_runtime_100_unit_scattered_wall_gap_field_cache_512x512)
    ->Name(
        "path/agent_runtime_100_unit_scattered_wall_gap_field_cache_512x512");

}  // namespace
