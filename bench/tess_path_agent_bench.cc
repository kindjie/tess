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

// Scoped-staleness variants of the dirty world-edit tick. Both run the same
// 100-agent replan-every-tick workload under
// UnitRouteStaleness::ScopedFeasible; they differ only in where the toggled
// edit lands. The off-path cell edits chunk (8,15), which the X-first routes
// from row-0 starts to (511,511) provably never enter (checked outside the
// timed region), so surviving entries serve every tick — the scoped win. The
// on-path cell toggles tiles beside the goal inside chunk (15,15): every
// route terminates there, no replan can route around it (an edit elsewhere
// in a corridor just teaches replans to avoid that chunk — measured before
// this cell was pinned to the goal chunk), so every entry retires and
// repopulates each tick — scoped mode's honest cost when invalidation is
// genuinely unavoidable. The goal tile itself is never toggled.
template <bool OffPath>
void scoped_world_edit_tick(benchmark::State& state) {
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
              .unit_route_staleness = tess::UnitRouteStaleness::ScopedFeasible,
          },
  };

  const auto edit_coord = [](std::uint64_t edits) {
    return OffPath
               ? tess::Coord3{256, 480 + static_cast<std::int64_t>(edits % 32),
                              0}
               : tess::Coord3{480 + static_cast<std::int64_t>(edits % 31), 511,
                              0};
  };
  const auto edit_chunk = tess::chunk_key<PathScaleShape>(
      tess::tile_key<PathScaleShape>(edit_coord(0)));

  // Warm outside the timed region: plan every agent once so entries exist,
  // then hold the off-path cell to its premise — no served route may enter
  // the edited chunk, or the "off-path" label lies (chunk-level, not tile-
  // level, disjointness; the on-path cell asserts the opposite).
  tess::PathAgentTickStats tick_stats;
  tess::mark_pathing_dirty(tick_state);
  tick_stats = tess::tick_unit_path_agents<PathWorld, PassableTag>(
      tick_state, world, agents, runtime, options);
  check_all_agents_found(tick_stats.pathing, agents.size());
  auto routes_touching_edit_chunk = std::size_t{0};
  auto route_count = std::size_t{0};
  for (const auto& result : runtime.results()) {
    ++route_count;
    for (const auto node : result.path) {
      if (tess::chunk_key<PathScaleShape>(
              tess::tile_key<PathScaleShape>(node)) == edit_chunk) {
        ++routes_touching_edit_chunk;
        break;
      }
    }
  }
  bench_check(OffPath ? routes_touching_edit_chunk == 0
                      : routes_touching_edit_chunk == route_count,
              OffPath ? "off-path edit chunk intersects a served route"
                      : "on-path edit chunk missed a served route");

  std::uint64_t edits = 0;
  for (auto _ : state) {
    const auto coord = edit_coord(edits);
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
  // Postconditions turn the premise into evidence: the off-path cell must
  // have retired nothing (pure survival), the on-path cell must have
  // survived nothing (every revalidation retired).
  bench_check(OffPath ? (stats.route_cache.retired_entries == 0 &&
                         stats.route_cache.scoped_survivals > 0)
                      : (stats.route_cache.scoped_survivals == 0 &&
                         stats.route_cache.retired_entries > 0),
              OffPath ? "off-path cell retired entries or never revalidated"
                      : "on-path cell let entries survive an epoch change");
  record_route_cache_counters(state, stats.route_cache);
  state.counters["cache.revalidations"] =
      static_cast<double>(stats.route_cache.revalidations);
  state.counters["cache.scoped_survivals"] =
      static_cast<double>(stats.route_cache.scoped_survivals);
  state.counters["cache.retired_entries"] =
      static_cast<double>(stats.route_cache.retired_entries);
  state.counters["runtime.path_nodes"] = static_cast<double>(stats.path_nodes);
}

void BM_path_agent_tick_100_unit_dirty_offpath_edit_scoped_512x512(
    benchmark::State& state) {
  scoped_world_edit_tick<true>(state);
}

void BM_path_agent_tick_100_unit_dirty_onpath_edit_scoped_512x512(
    benchmark::State& state) {
  scoped_world_edit_tick<false>(state);
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

// One goal re-arm per tick on the weighted mixed map. Pre-split, the
// shared pathing_dirty flag meant this replanned ALL 100 agents every
// tick (the full runtime_100_weighted_mixed cost, ~100 searches); with
// per-agent dirt only the re-armed agent replans (NeedsOnly scope) while
// the other 99 keep their retained routes. max_steps = 0 pins agents in
// place so every iteration measures the same 1-search + scan work.
void BM_path_agent_tick_100_weighted_goal_churn_512x512(
    benchmark::State& state) {
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
  const auto churn_goals =
      std::array{tess::Coord3{479, 500, 0}, tess::Coord3{500, 479, 0}};
  for (const auto goal : churn_goals) {
    world.template field<PassableTag>(goal) = 1;
    world.template field<CostTag>(goal) = 1;
  }

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::PathAgentTickState tick_state;
  const auto options = tess::PathAgentTickOptions{.max_steps = 0};
  // Warm full pass: everyone plans once and turns Following.
  (void)tess::tick_weighted_path_agents<WeightedPathWorld, PassableTag, CostTag,
                                        8>(tick_state, world, agents, runtime,
                                           options);

  tess::PathAgentTickStats tick_stats;
  std::size_t churn = 0;
  for (auto _ : state) {
    auto& agent = agents[churn % agents.size()];
    tess::set_path_agent_goal(tick_state, agent,
                              churn_goals[churn % churn_goals.size()]);
    tick_stats = tess::tick_weighted_path_agents<WeightedPathWorld, PassableTag,
                                                 CostTag, 8>(
        tick_state, world, agents, runtime, options);
    benchmark::DoNotOptimize(tick_stats.tick);
    ++churn;
  }

  bench_check(tick_stats.processed_paths && tick_stats.pathing.submitted == 1,
              "goal churn tick replanned more than the re-armed agent");
  record_tick_counters(state, tick_stats);
}

// PortalFirst variants of the goal-churn tick. All three pin the premium
// cap explicitly rather than inheriting the policy default, so a later
// default change cannot silently flip a postcondition. The repeated cell
// keeps the base cell's two-goal churn; the fresh cell re-arms into the
// map's always-passable band right of the last barrier column (x = 476),
// never repeating a goal within a run; the rejected cell's cap of 21/20
// sits below every portal route's premium on this map, so every attempt
// pays candidates + stitching + rejection + the full exact search — the
// measured worst case.
enum class ChurnGoalMode : std::uint8_t { Repeated, Fresh, Sealed };

template <ChurnGoalMode Mode, std::uint32_t CapNum, std::uint32_t CapDen,
          bool ExpectAccepts>
void portal_first_goal_churn_tick(benchmark::State& state) {
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
  const auto churn_goals =
      std::array{tess::Coord3{479, 500, 0}, tess::Coord3{500, 479, 0}};
  for (const auto goal : churn_goals) {
    world.template field<PassableTag>(goal) = 1;
    world.template field<CostTag>(goal) = 1;
  }
  // Open ground between the x=439 and x=476 barrier columns, away from
  // every agent start and goal, so sealing it perturbs nothing else.
  const auto sealed_goal = tess::Coord3{450, 450, 0};
  if (Mode == ChurnGoalMode::Sealed) {
    // Ring the sealed goal with blockers: candidates still select (chunk
    // seams stay passable) but the final segment cannot enter the pocket,
    // so every attempt is a verification failure followed by the full
    // exact search proving NoPath — the no-portal-route worst case.
    world.template field<PassableTag>(sealed_goal) = 1;
    world.template field<CostTag>(sealed_goal) = 1;
    for (std::int64_t dx = -1; dx <= 1; ++dx) {
      for (std::int64_t dy = -1; dy <= 1; ++dy) {
        if (dx != 0 || dy != 0) {
          world.template field<PassableTag>(
              tess::Coord3{sealed_goal.x + dx, sealed_goal.y + dy, 0}) = 0;
        }
      }
    }
  }

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, agents.size());
  tess::PathAgentTickState tick_state;
  auto options = tess::PathAgentTickOptions{.max_steps = 0};
  options.cache_policy.weighted_replan_strategy =
      tess::WeightedReplanStrategy::PortalFirst;
  options.cache_policy.portal_premium_limit_num = CapNum;
  options.cache_policy.portal_premium_limit_den = CapDen;

  (void)tess::tick_weighted_path_agents<WeightedPathWorld, PassableTag, CostTag,
                                        8>(tick_state, world, agents, runtime,
                                           options);

  // Fresh goals walk the always-passable band x in [477, 511]: the last
  // barrier column is 476, so the band's tiles are cost-1 passable by
  // construction and the map stays byte-identical to the guard cell.
  const auto fresh_goal = [](std::size_t index) {
    return tess::Coord3{477 + static_cast<std::int64_t>(index % 35u),
                        static_cast<std::int64_t>((index / 35u) % 512u), 0};
  };

  // Runtime stats reset per process call, so aggregate claims need
  // per-tick accumulation. The stats read costs a small constant per tick
  // and is identical across the portal cells, so their relative numbers
  // are unaffected; the recorded bootstrap ceilings were measured with it
  // in place.
  tess::WeightedPortalReplanStats sums{};
  tess::PathAgentTickStats tick_stats;
  std::size_t churn = 0;
  for (auto _ : state) {
    // Sealed mode re-arms the SAME agent every tick: a NoPath agent
    // retries its goal on the next tick, so rotating agents would stack
    // one more retrying submitter per tick and break the one-replan
    // shape every other mode measures.
    auto& agent = Mode == ChurnGoalMode::Sealed ? agents[0]
                                                : agents[churn % agents.size()];
    const auto goal = Mode == ChurnGoalMode::Fresh ? fresh_goal(churn)
                      : Mode == ChurnGoalMode::Sealed
                          ? sealed_goal
                          : churn_goals[churn % churn_goals.size()];
    tess::set_path_agent_goal(tick_state, agent, goal);
    tick_stats = tess::tick_weighted_path_agents<WeightedPathWorld, PassableTag,
                                                 CostTag, 8>(
        tick_state, world, agents, runtime, options);
    benchmark::DoNotOptimize(tick_stats.tick);
    const auto tick_replan = runtime.stats().portal_replan;
    sums.attempts += tick_replan.attempts;
    sums.accepted += tick_replan.accepted;
    sums.no_candidates += tick_replan.no_candidates;
    sums.verification_failures += tick_replan.verification_failures;
    sums.premium_rejections += tick_replan.premium_rejections;
    sums.exact_fallbacks += tick_replan.exact_fallbacks;
    ++churn;
  }

  bench_check(tick_stats.processed_paths && tick_stats.pathing.submitted == 1,
              "goal churn tick replanned more than the re-armed agent");
  if (Mode == ChurnGoalMode::Fresh) {
    bench_check(churn < 512u * 35u,
                "fresh-goal cell exhausted the non-repeating band");
  }
  bench_check(sums.attempts == churn,
              "portal pass missed a tick's singleton attempt");
  if (Mode == ChurnGoalMode::Sealed) {
    // Every attempt selects candidates, fails stitching into the sealed
    // pocket, and pays the full exact NoPath search.
    bench_check(sums.verification_failures == churn && sums.accepted == 0,
                "sealed cell did not fail verification on every tick");
  } else {
    bench_check(sums.no_candidates == 0 && sums.verification_failures == 0,
                "portal pass structure failed");
  }
  if (ExpectAccepts && Mode == ChurnGoalMode::Repeated) {
    bench_check(sums.accepted == churn && sums.exact_fallbacks == 0,
                "portal-first cell fell back to exact A*");
  } else if (ExpectAccepts && Mode == ChurnGoalMode::Fresh) {
    // Fresh band goals carry premiums between 4/3 and 2 on this map
    // (measured: all rejected at 4/3, all accepted at 2/1), so this cell
    // pins the 2/1 cap and requires every fresh goal accepted — the
    // fresh-goal portal win with mixed cold and warm segments. The
    // default-cap rejection economics live in the rejected cell.
    bench_check(sums.accepted == churn && sums.exact_fallbacks == 0,
                "fresh cell rejected a fresh-band goal at cap 2/1");
  } else if (!ExpectAccepts && Mode == ChurnGoalMode::Repeated) {
    bench_check(sums.accepted == 0 && sums.premium_rejections == churn,
                "rejection cell accepted a portal route");
  }
  record_tick_counters(state, tick_stats);
  state.counters["portal.attempts"] = static_cast<double>(sums.attempts);
  state.counters["portal.accepted"] = static_cast<double>(sums.accepted);
  state.counters["portal.premium_rejections"] =
      static_cast<double>(sums.premium_rejections);
  state.counters["portal.verification_failures"] =
      static_cast<double>(sums.verification_failures);
  const auto segments = runtime.stats().portal_segment_cache;
  state.counters["segments.sweeps"] = static_cast<double>(segments.sweeps);
  state.counters["segments.evictions"] =
      static_cast<double>(segments.evictions);
  state.counters["segments.stale_rejections"] =
      static_cast<double>(segments.stale_rejections);
}

void BM_path_agent_tick_100_weighted_goal_churn_portal_512x512(
    benchmark::State& state) {
  portal_first_goal_churn_tick<ChurnGoalMode::Repeated, 4, 3, true>(state);
}

void BM_path_agent_tick_100_weighted_fresh_churn_portal_512x512(
    benchmark::State& state) {
  portal_first_goal_churn_tick<ChurnGoalMode::Fresh, 2, 1, true>(state);
}

void BM_path_agent_tick_100_weighted_fresh_churn_exact_512x512(
    benchmark::State& state) {
  // The exact-strategy twin of the fresh cell: same workload, default
  // strategy, so the fresh-vs-repeat and portal-vs-exact axes separate.
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
  tess::PathAgentTickState tick_state;
  const auto options = tess::PathAgentTickOptions{.max_steps = 0};
  (void)tess::tick_weighted_path_agents<WeightedPathWorld, PassableTag, CostTag,
                                        8>(tick_state, world, agents, runtime,
                                           options);

  const auto fresh_goal = [](std::size_t index) {
    return tess::Coord3{477 + static_cast<std::int64_t>(index % 35u),
                        static_cast<std::int64_t>((index / 35u) % 512u), 0};
  };
  tess::PathAgentTickStats tick_stats;
  std::size_t churn = 0;
  for (auto _ : state) {
    auto& agent = agents[churn % agents.size()];
    tess::set_path_agent_goal(tick_state, agent, fresh_goal(churn));
    tick_stats = tess::tick_weighted_path_agents<WeightedPathWorld, PassableTag,
                                                 CostTag, 8>(
        tick_state, world, agents, runtime, options);
    benchmark::DoNotOptimize(tick_stats.tick);
    ++churn;
  }
  bench_check(tick_stats.processed_paths && tick_stats.pathing.submitted == 1,
              "fresh churn tick replanned more than the re-armed agent");
  bench_check(churn < 512u * 35u,
              "fresh-goal cell exhausted the non-repeating band");
  record_tick_counters(state, tick_stats);
}

void BM_path_agent_tick_100_weighted_goal_churn_rejected_512x512(
    benchmark::State& state) {
  portal_first_goal_churn_tick<ChurnGoalMode::Repeated, 21, 20, false>(state);
}

void BM_path_agent_tick_100_weighted_sealed_churn_portal_512x512(
    benchmark::State& state) {
  portal_first_goal_churn_tick<ChurnGoalMode::Sealed, 4, 3, false>(state);
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
BENCHMARK(BM_path_agent_tick_100_unit_dirty_offpath_edit_scoped_512x512)
    ->Name("path/agent_tick_100_unit_dirty_offpath_edit_scoped_512x512");
BENCHMARK(BM_path_agent_tick_100_unit_dirty_onpath_edit_scoped_512x512)
    ->Name("path/agent_tick_100_unit_dirty_onpath_edit_scoped_512x512");
BENCHMARK(BM_path_agent_runtime_100_weighted_shared_512x512)
    ->Name("path/agent_runtime_100_weighted_shared_512x512");
BENCHMARK(BM_path_agent_tick_100_weighted_shared_dirty_512x512)
    ->Name("path/agent_tick_100_weighted_shared_dirty_512x512");
BENCHMARK(BM_path_agent_runtime_100_weighted_mixed_512x512)
    ->Name("path/agent_runtime_100_weighted_mixed_512x512");
BENCHMARK(BM_path_agent_tick_100_weighted_goal_churn_512x512)
    ->Name("path/agent_tick_100_weighted_goal_churn_512x512");
BENCHMARK(BM_path_agent_tick_100_weighted_goal_churn_portal_512x512)
    ->Name("path/agent_tick_100_weighted_goal_churn_portal_512x512");
BENCHMARK(BM_path_agent_tick_100_weighted_fresh_churn_portal_512x512)
    ->Name("path/agent_tick_100_weighted_fresh_churn_portal_512x512");
BENCHMARK(BM_path_agent_tick_100_weighted_fresh_churn_exact_512x512)
    ->Name("path/agent_tick_100_weighted_fresh_churn_exact_512x512");
BENCHMARK(BM_path_agent_tick_100_weighted_goal_churn_rejected_512x512)
    ->Name("path/agent_tick_100_weighted_goal_churn_rejected_512x512");
BENCHMARK(BM_path_agent_tick_100_weighted_sealed_churn_portal_512x512)
    ->Name("path/agent_tick_100_weighted_sealed_churn_portal_512x512");
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
