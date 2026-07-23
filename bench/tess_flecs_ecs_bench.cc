#include <flecs.h>
// Flecs first: the adapter header requires the consumer include order.
#include <benchmark/benchmark.h>
#include <tess/ecs/flecs/flecs_adapter.h>

#include <cstddef>
#include <cstdint>

namespace {

struct FlecsFixture {
  flecs::world world;
  tess::FlecsPathAgentContext context;

  explicit FlecsFixture(std::size_t count) : context(world) {
    context.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      const auto shuffled_id = (i * 7'919u) % count;
      auto state = tess::PathAgentState{};
      state.position = tess::Coord3{static_cast<std::int64_t>(i), 0, 0};
      world.entity()
          .set<tess::AgentId>(tess::AgentId{shuffled_id})
          .set<tess::TilePosition>(tess::TilePosition{state.position})
          .set<tess::PathState>(tess::PathState{state});
    }
    // Warm component lookup and query caches outside measurement.
    tess::FlecsPathAgentSource source(world, context);
    static_cast<void>(source.collect(context.batch));
  }
};

void run_collect(benchmark::State& state, std::size_t count) {
  FlecsFixture fixture(count);
  tess::FlecsPathAgentSource source(fixture.world, fixture.context);
  for (auto _ : state) {
    auto info = source.collect(fixture.context.batch);
    benchmark::DoNotOptimize(info.count);
    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(state.iterations() *
                          static_cast<std::int64_t>(count));
}

void run_collect_apply(benchmark::State& state, std::size_t count) {
  FlecsFixture fixture(count);
  tess::FlecsPathAgentSource source(fixture.world, fixture.context);
  tess::FlecsPathAgentSink sink(fixture.world, fixture.context);
  for (auto _ : state) {
    auto info = source.collect(fixture.context.batch);
    sink.apply(fixture.context.batch);
    benchmark::DoNotOptimize(info.count);
    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(state.iterations() *
                          static_cast<std::int64_t>(count));
}

void BM_flecs_collect_1k(benchmark::State& state) { run_collect(state, 1'000); }

void BM_flecs_collect_10k(benchmark::State& state) {
  run_collect(state, 10'000);
}

void BM_flecs_collect_100k(benchmark::State& state) {
  run_collect(state, 100'000);
}

void BM_flecs_collect_apply_10k(benchmark::State& state) {
  run_collect_apply(state, 10'000);
}

BENCHMARK(BM_flecs_collect_1k)->Name("ecs/flecs_collect_1k");
BENCHMARK(BM_flecs_collect_10k)->Name("ecs/flecs_collect_10k");
BENCHMARK(BM_flecs_collect_100k)->Name("ecs/flecs_collect_100k");
BENCHMARK(BM_flecs_collect_apply_10k)->Name("ecs/flecs_collect_apply_10k");

}  // namespace
