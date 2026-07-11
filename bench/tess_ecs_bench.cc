#include <benchmark/benchmark.h>

#include <entt/entity/registry.hpp>
// entt before the adapter: the header requires the consumer include order.
#include <tess/ecs/entt/entt_adapter.h>
#include <tess/tess.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

// M10 ecs bench family: deterministic collect+sort over scrambled pools,
// write-back, the occupancy-index hot path, and the adapter-overhead
// headline -- the full EnTT tick against a raw PathAgentState span doing
// identical work on an identical world. Every gated case is serial CPU
// time; the entt-vs-raw ratio is trend data, never gated.
namespace {

void ecs_bench_check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "tess_bench correctness check failed: %s\n", message);
    std::abort();
  }
}

struct PassableTag {};
struct CostTag {};
struct OccupancyTag {};
struct ReservationTag {};

// One agent per row marching right, so movement never contends and the
// per-tick work scales linearly with the agent count. 1k agents.
using MarchShape =
    tess::Shape<tess::Extent3{64, 1024, 1}, tess::Extent3{8, 8, 1}>;
// 10k agents.
using MarchShapeLarge =
    tess::Shape<tess::Extent3{64, 10240, 1}, tess::Extent3{8, 8, 1}>;
template <typename Shape>
using MarchSchema = tess::FieldSchema<
    tess::Field<PassableTag, bool>, tess::Field<CostTag, std::uint32_t>,
    tess::Field<OccupancyTag, bool>, tess::Field<ReservationTag, bool>>;
template <typename Shape>
using MarchWorld = tess::AlwaysResidentWorld<Shape, MarchSchema<Shape>>;

// Resets every field: benchmark functions are re-entered by the harness
// (calibration runs), and the static worlds must not leak occupancy from
// a previous fixture's agents into the next spawn pass.
template <typename World>
void fill_march_world(World& world) {
  for (auto& page : world.chunks()) {
    auto passable = page.template field_span<PassableTag>();
    auto cost = page.template field_span<CostTag>();
    auto occupancy = page.template field_span<OccupancyTag>();
    auto reservations = page.template field_span<ReservationTag>();
    for (std::size_t i = 0; i < passable.size(); ++i) {
      passable[i] = true;
      cost[i] = 1u;
      occupancy[i] = false;
      reservations[i] = false;
    }
  }
}

template <typename World>
void reserve_march_runtime(tess::PathRequestRuntime& runtime,
                           std::size_t agents) {
  runtime.reserve_requests(agents);
  runtime.reserve_search_nodes(4096);
  runtime.reserve_path_nodes(1 << 20);
  runtime.reserve_unit_routes(agents);
  runtime.reserve_unit_field_products(agents);
  runtime.reserve_unit_field_product_dependencies(World::chunk_count);
  runtime.reserve_portal_segments(agents);
  runtime.portal_segment_cache().reserve_path_nodes(4096);
}

// Builds a registry whose agent components exist but whose pool packing
// is scrambled by interleaved churn entities, so collect's sort earns
// its keep. Agents are component-only (never placed on a world): collect
// and apply read and write components, nothing else.
struct CollectFixture {
  entt::registry registry;
  tess::EnttPathAgentContext context;

  explicit CollectFixture(std::size_t agent_count) {
    context.reserve(agent_count);
    std::vector<entt::entity> churn;
    for (std::size_t i = 0; i < agent_count; ++i) {
      // Interleave short-lived entities to scatter pool order.
      const auto noise_a = registry.create();
      const auto entity = registry.create();
      const auto noise_b = registry.create();
      registry.destroy(noise_a);
      registry.emplace<tess::AgentId>(entity,
                                      tess::AgentId{context.next_agent_id++});
      registry.emplace<tess::TilePosition>(
          entity, tess::TilePosition{
                      tess::Coord3{static_cast<std::int64_t>(i % 64),
                                   static_cast<std::int64_t>(i / 64), 0}});
      auto state = tess::PathAgentState{};
      state.position = registry.get<tess::TilePosition>(entity).coord;
      registry.emplace<tess::PathState>(entity, tess::PathState{state});
      registry.destroy(noise_b);
    }
  }
};

void run_collect_sort_bench(benchmark::State& state, std::size_t agent_count) {
  CollectFixture fixture(agent_count);
  tess::EnttPathAgentSource source(fixture.registry, fixture.context);

  for (auto _ : state) {
    auto info = source.collect(fixture.context.batch);
    benchmark::DoNotOptimize(info.count);
  }

  ecs_bench_check(fixture.context.batch.size() == agent_count,
                  "collect missed agents");
  const auto& entries = fixture.context.entries;
  for (std::size_t i = 1; i < entries.size(); ++i) {
    ecs_bench_check(entries[i - 1].agent_id < entries[i].agent_id,
                    "collect order not ascending by AgentId");
  }
}

void BM_ecs_collect_sort_1k(benchmark::State& state) {
  run_collect_sort_bench(state, 1000);
}

void BM_ecs_collect_sort_10k(benchmark::State& state) {
  run_collect_sort_bench(state, 10000);
}

void BM_ecs_collect_sort_100k(benchmark::State& state) {
  run_collect_sort_bench(state, 100000);
}

void run_apply_bench(benchmark::State& state, std::size_t agent_count) {
  CollectFixture fixture(agent_count);
  tess::EnttPathAgentSource source(fixture.registry, fixture.context);
  tess::EnttPathAgentSink sink(fixture.registry, fixture.context);
  (void)source.collect(fixture.context.batch);

  for (auto _ : state) {
    sink.apply(fixture.context.batch);
    auto applied = fixture.context.batch.size();
    benchmark::DoNotOptimize(applied);
  }
}

void BM_ecs_apply_1k(benchmark::State& state) { run_apply_bench(state, 1000); }

void BM_ecs_apply_10k(benchmark::State& state) {
  run_apply_bench(state, 10000);
}

// The movement-commit hot path in isolation: one erase + shifted insert
// on a table holding 4096 live mappings.
void BM_ecs_index_move(benchmark::State& state) {
  tess::TileOccupancyIndex index;
  constexpr std::int64_t kEntries = 4096;
  index.reserve(kEntries + 1);
  for (std::int64_t i = 0; i < kEntries; ++i) {
    ecs_bench_check(
        index.insert(tess::Coord3{i % 64, i / 64, 0},
                     tess::EntityHandle{static_cast<std::uint64_t>(i) + 1}),
        "index setup insert failed");
  }
  const auto mover = tess::EntityHandle{kEntries + 1};
  ecs_bench_check(index.insert(tess::Coord3{0, 4096, 0}, mover),
                  "mover insert failed");

  auto at_a = true;
  for (auto _ : state) {
    const auto from =
        at_a ? tess::Coord3{0, 4096, 0} : tess::Coord3{1, 4096, 0};
    const auto to = at_a ? tess::Coord3{1, 4096, 0} : tess::Coord3{0, 4096, 0};
    index.move(from, to, mover);
    at_a = !at_a;
    auto size = index.size();
    benchmark::DoNotOptimize(size);
  }
  ecs_bench_check(index.size() == kEntries + 1, "index size drifted");
}

// Full-tick fixtures: N agents, one per row, marching toward the right
// edge. Every kResetInterval ticks the fixture pauses timing, teleports
// the column back, re-arms goals, and absorbs the re-path tick outside
// the timed region -- timed iterations are pure steady-state ticks
// (collect + movement + apply; no path processing, no arrivals).
constexpr std::int64_t kMarchStart = 2;
constexpr std::int64_t kMarchGoal = 60;
constexpr int kResetInterval = 40;

template <typename Shape>
struct EnttMarchFixture {
  MarchWorld<Shape>& world;
  entt::registry registry;
  tess::EnttPathAgentContext context;
  tess::TileOccupancyIndex index;
  tess::PathRequestRuntime runtime;
  std::vector<entt::entity> agents;

  EnttMarchFixture(MarchWorld<Shape>& world_ref, std::size_t agent_count)
      : world(world_ref) {
    fill_march_world(world);
    context.reserve(agent_count);
    context.tick_state.pathing_dirty = false;
    index.reserve(agent_count);
    reserve_march_runtime<MarchWorld<Shape>>(runtime, agent_count);
    for (std::size_t i = 0; i < agent_count; ++i) {
      const auto row = static_cast<std::int64_t>(i);
      const auto entity =
          tess::spawn_entt_path_agent<MarchWorld<Shape>, OccupancyTag>(
              registry, context, world, index,
              tess::Coord3{kMarchStart, row, 0});
      ecs_bench_check(entity != entt::null, "march spawn refused");
      agents.push_back(entity);
      tess::set_entt_path_agent_goal(registry, entity,
                                     tess::Coord3{kMarchGoal, row, 0});
    }
    // Absorb the initial path-processing tick.
    (void)tick();
  }

  auto tick() -> tess::PathAgentTickStats {
    return tess::tick_entt_unit_path_agents<MarchWorld<Shape>, PassableTag,
                                            OccupancyTag, ReservationTag>(
        registry, context, world, runtime, index);
  }

  // Teleport everyone back to the start column, re-arm goals (arrival
  // consumes PathGoal, so a returning agent needs a fresh one), and
  // absorb the re-path tick; caller pauses timing around this.
  void reset() {
    for (std::size_t i = 0; i < agents.size(); ++i) {
      const auto row = static_cast<std::int64_t>(i);
      ecs_bench_check(
          (tess::teleport_entt_path_agent<MarchWorld<Shape>, OccupancyTag>(
              registry, world, index, agents[i],
              tess::Coord3{kMarchStart, row, 0})),
          "march reset teleport refused");
      tess::set_entt_path_agent_goal(registry, agents[i],
                                     tess::Coord3{kMarchGoal, row, 0});
    }
    (void)tick();
  }
};

template <typename Shape>
void run_tick_entt_bench(benchmark::State& state, MarchWorld<Shape>& world,
                         std::size_t agent_count) {
  EnttMarchFixture<Shape> fixture(world, agent_count);

  int steps = 1;
  std::uint64_t advanced = 0;
  for (auto _ : state) {
    if (steps >= kResetInterval) {
      state.PauseTiming();
      fixture.reset();
      steps = 1;
      state.ResumeTiming();
    }
    const auto stats = fixture.tick();
    advanced = stats.movement.advanced;
    benchmark::DoNotOptimize(advanced);
    ++steps;
  }
  if (state.iterations() > 0) {
    ecs_bench_check(advanced == agent_count,
                    "steady-state entt tick did not advance every agent");
  }
}

void BM_ecs_tick_entt_1k(benchmark::State& state) {
  static auto* world = new MarchWorld<MarchShape>();
  run_tick_entt_bench<MarchShape>(state, *world, 1000);
}

void BM_ecs_tick_entt_10k(benchmark::State& state) {
  static auto* world = new MarchWorld<MarchShapeLarge>();
  run_tick_entt_bench<MarchShapeLarge>(state, *world, 10000);
}

// The raw-span baseline: identical world, goals, and tick driver, over a
// plain vector of PathAgentState with no registry, no components, no
// occupancy index. The entt-tick-minus-this difference IS the adapter
// overhead (collect + sort + write-back + index maintenance).
template <typename Shape>
void run_tick_raw_bench(benchmark::State& state, MarchWorld<Shape>& world,
                        std::size_t agent_count) {
  fill_march_world(world);
  std::vector<tess::PathAgentState> agents(agent_count);
  tess::PathRequestRuntime runtime;
  reserve_march_runtime<MarchWorld<Shape>>(runtime, agent_count);
  tess::PathAgentTickState tick_state;

  const auto arm = [&] {
    for (std::size_t i = 0; i < agent_count; ++i) {
      const auto row = static_cast<std::int64_t>(i);
      auto& agent = agents[i];
      const auto position = tess::Coord3{kMarchStart, row, 0};
      world.template field<OccupancyTag>(agent.position) = false;
      agent = tess::PathAgentState{};
      agent.position = position;
      world.template field<OccupancyTag>(position) = true;
      tess::set_path_agent_goal(tick_state, agent,
                                tess::Coord3{kMarchGoal, row, 0});
    }
  };
  const auto tick = [&] {
    return tess::tick_unit_path_agents_with_movement<
        MarchWorld<Shape>, PassableTag, OccupancyTag, ReservationTag>(
        tick_state, world, agents, runtime);
  };
  arm();
  (void)tick();  // absorb the initial path-processing tick

  int steps = 1;
  std::uint64_t advanced = 0;
  for (auto _ : state) {
    if (steps >= kResetInterval) {
      state.PauseTiming();
      arm();
      (void)tick();
      steps = 1;
      state.ResumeTiming();
    }
    advanced = tick().movement.advanced;
    benchmark::DoNotOptimize(advanced);
    ++steps;
  }
  if (state.iterations() > 0) {
    ecs_bench_check(advanced == agent_count,
                    "steady-state raw tick did not advance every agent");
  }
}

void BM_ecs_tick_raw_span_1k(benchmark::State& state) {
  static auto* world = new MarchWorld<MarchShape>();
  run_tick_raw_bench<MarchShape>(state, *world, 1000);
}

void BM_ecs_tick_raw_span_10k(benchmark::State& state) {
  static auto* world = new MarchWorld<MarchShapeLarge>();
  run_tick_raw_bench<MarchShapeLarge>(state, *world, 10000);
}

#if TESS_DIAGNOSTICS_ENABLED
// Representative warm-path allocation assertion (benchmark-plan.md
// section 14): one steady-state entt tick against warmed state must
// report zero allocations through the diagnostics counters.
void BM_ecs_tick_entt_alloc_gate(benchmark::State& state) {
  static auto* world = new MarchWorld<MarchShape>();
  EnttMarchFixture<MarchShape> fixture(*world, 256);
  (void)fixture.tick();  // one more warm tick before asserting

  tess::diagnostics::AllocationCounters counters;
  for (auto _ : state) {
    counters.reset();
    tess::diagnostics::ScopedAllocationCounters scope{counters};
    const auto stats = fixture.tick();
    auto advanced = stats.movement.advanced;
    benchmark::DoNotOptimize(advanced);
    if (stats.movement.advanced == 0) {
      state.PauseTiming();
      fixture.reset();
      state.ResumeTiming();
    }
  }
  ecs_bench_check(counters.allocations == 0,
                  "steady-state entt tick allocated");
}
BENCHMARK(BM_ecs_tick_entt_alloc_gate)->Name("ecs/tick_entt_alloc_gate");
#endif

BENCHMARK(BM_ecs_collect_sort_1k)->Name("ecs/collect_sort_1k");
BENCHMARK(BM_ecs_collect_sort_10k)->Name("ecs/collect_sort_10k");
BENCHMARK(BM_ecs_collect_sort_100k)->Name("ecs/collect_sort_100k");
BENCHMARK(BM_ecs_apply_1k)->Name("ecs/apply_1k");
BENCHMARK(BM_ecs_apply_10k)->Name("ecs/apply_10k");
BENCHMARK(BM_ecs_index_move)->Name("ecs/index_move");
BENCHMARK(BM_ecs_tick_entt_1k)->Name("ecs/tick_entt_1k");
BENCHMARK(BM_ecs_tick_entt_10k)->Name("ecs/tick_entt_10k");
BENCHMARK(BM_ecs_tick_raw_span_1k)->Name("ecs/tick_raw_span_1k");
BENCHMARK(BM_ecs_tick_raw_span_10k)->Name("ecs/tick_raw_span_10k");

}  // namespace
