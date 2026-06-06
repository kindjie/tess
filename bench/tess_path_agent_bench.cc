#include <benchmark/benchmark.h>
#include <tess/tess.h>

#include <array>
#include <cstdint>

namespace {

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

void record_route_cache_counters(benchmark::State& state,
                                 tess::RouteCacheStats stats) {
  state.counters["cache.entries"] = static_cast<double>(stats.entries);
  state.counters["cache.hits"] = static_cast<double>(stats.hits);
  state.counters["cache.suffix_hits"] = static_cast<double>(stats.suffix_hits);
  state.counters["cache.misses"] = static_cast<double>(stats.misses);
  state.counters["cache.path_nodes"] = static_cast<double>(stats.path_nodes);
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

  record_agent_counters(state, frame_stats);
  const auto stats = runtime.stats();
  record_route_cache_counters(state, stats.route_cache);
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

  record_agent_counters(state, frame_stats);
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

  record_agent_counters(state, frame_stats);
  const auto stats = runtime.stats();
  record_route_cache_counters(state, stats.route_cache);
  state.counters["runtime.cache_clears"] =
      static_cast<double>(stats.cache_clears);
  state.counters["runtime.path_nodes"] = static_cast<double>(stats.path_nodes);
}

BENCHMARK(BM_path_agent_runtime_100_unit_suffix_512x512)
    ->Name("path/agent_runtime_100_unit_suffix_512x512");
BENCHMARK(BM_path_agent_runtime_100_weighted_shared_512x512)
    ->Name("path/agent_runtime_100_weighted_shared_512x512");
BENCHMARK(BM_path_agent_runtime_100_weighted_mixed_512x512)
    ->Name("path/agent_runtime_100_weighted_mixed_512x512");
BENCHMARK(BM_path_agent_runtime_100_unit_world_edit_512x512)
    ->Name("path/agent_runtime_100_unit_world_edit_512x512");

}  // namespace
