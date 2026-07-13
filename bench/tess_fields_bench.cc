#include <benchmark/benchmark.h>
#include <tess/tess.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>

// M9 fields bench family (the milestone was code-complete without its
// own gated family): distance-field product builds scaling with the
// goal count, the nearest-target query over a built product, and the
// byte-budgeted FieldProductCache hit / miss+store / eviction paths.
namespace {

void fields_bench_check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "tess_bench correctness check failed: %s\n", message);
    std::abort();
  }
}

struct PassableTag {};
struct CostTag {};

using FieldShape =
    tess::Shape<tess::Extent3{64, 64, 1}, tess::Extent3{8, 8, 1}>;
using FieldSchemaT = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                       tess::Field<CostTag, std::uint32_t>>;
using FieldWorld = tess::AlwaysResidentWorld<FieldShape, FieldSchemaT>;
constexpr auto kTileCount =
    FieldShape::size.x * FieldShape::size.y * FieldShape::size.z;

auto make_world() -> FieldWorld* {
  auto* world = new FieldWorld();
  for (auto& page : world->chunks()) {
    auto passable = page.template field_span<PassableTag>();
    auto cost = page.template field_span<CostTag>();
    for (std::size_t i = 0; i < passable.size(); ++i) {
      passable[i] = true;
      cost[i] = 1u;
    }
  }
  return world;
}

// Distinct scattered goals for any count up to the tile count: x walks
// the row range, y scatters with a full-period stride plus a lap term,
// so two indices sharing an x (64 apart) always differ in y.
void fill_goals(tess::GoalSet& goals, std::size_t count) {
  goals.clear();
  for (std::size_t i = 0; i < count; ++i) {
    goals.add(tess::Coord3{static_cast<std::int64_t>(i % 64),
                           static_cast<std::int64_t>((i * 29 + i / 64) % 64),
                           0});
  }
}

// Reverse flood from N goals over the open 64x64 world: the goal-count
// scaling headline.
void run_goalset_build_bench(benchmark::State& state, std::size_t goal_count) {
  static auto* world = make_world();
  tess::GoalSet goals;
  goals.reserve(goal_count);
  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(kTileCount);
  tess::DistanceFieldProduct product;
  product.reserve_goals(goal_count);
  product.reserve_nodes(kTileCount);
  product.reserve_dependencies(FieldWorld::chunk_count);

  auto reached = std::size_t{0};
  for (auto _ : state) {
    fill_goals(goals, goal_count);
    const auto result =
        tess::build_distance_field_product<FieldWorld, PassableTag>(
            *world, goals, scratch, product);
    reached = product.reached_nodes();
    auto status = result.status;
    benchmark::DoNotOptimize(status);
  }
  fields_bench_check(reached == kTileCount,
                     "open-world flood did not reach every tile");
}

void BM_fields_goalset_build_1(benchmark::State& state) {
  run_goalset_build_bench(state, 1);
}

void BM_fields_goalset_build_16(benchmark::State& state) {
  run_goalset_build_bench(state, 16);
}

void BM_fields_goalset_build_256(benchmark::State& state) {
  run_goalset_build_bench(state, 256);
}

// Gradient descent over a built product: the per-agent query cost.
void BM_fields_nearest_target(benchmark::State& state) {
  static auto* world = make_world();
  tess::GoalSet goals;
  fill_goals(goals, 16);
  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(kTileCount);
  tess::DistanceFieldProduct product;
  product.reserve_goals(16);
  product.reserve_nodes(kTileCount);
  product.reserve_dependencies(FieldWorld::chunk_count);
  fields_bench_check(
      tess::build_distance_field_product<FieldWorld, PassableTag>(
          *world, goals, scratch, product)
              .status == tess::PathStatus::Found,
      "product build failed");

  auto status = tess::PathStatus::NoPath;
  for (auto _ : state) {
    const auto result = tess::nearest_target<FieldWorld, PassableTag>(
        *world, tess::Coord3{33, 47, 0}, product, scratch);
    status = result.status;
    benchmark::DoNotOptimize(status);
  }
  fields_bench_check(status == tess::PathStatus::Found,
                     "nearest_target failed on an open world");
}

// Cache hit: the steady-state shared-goal reuse path.
void BM_fields_cache_hit(benchmark::State& state) {
  static auto* world = make_world();
  tess::GoalSet goals;
  fill_goals(goals, 8);
  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(kTileCount);
  tess::DistanceFieldProduct product;
  product.reserve_nodes(kTileCount);
  product.reserve_dependencies(FieldWorld::chunk_count);
  tess::FieldProductCache cache;
  cache.reserve_entries(4);
  fields_bench_check(
      tess::build_distance_field_product<FieldWorld, PassableTag>(
          *world, goals, scratch, product)
              .status == tess::PathStatus::Found,
      "product build failed");
  fields_bench_check((cache.store<FieldWorld, PassableTag>(std::move(product))),
                     "store rejected an in-budget product");

  const tess::DistanceFieldProduct* hit = nullptr;
  for (auto _ : state) {
    hit = cache.lookup<FieldWorld, PassableTag>(*world, goals);
    benchmark::DoNotOptimize(hit);
  }
  fields_bench_check(hit != nullptr, "expected a cache hit");
}

// Miss -> build -> store, cycling two goal sets under a budget that
// holds only ONE product: every lookup misses and every store evicts
// the other entry, so this stays on the cold build/store path at every
// measured iteration (an unbudgeted cache turns resident after two
// iterations and would time the hit path instead).
//
// Deliberate overlap with cache_eviction below (recorded S11 note):
// both force the miss+build+store+evict path, so their absolute times
// track each other. They are kept separate because they bound different
// regressions: this one is the two-key degenerate case (the evicted
// entry is always the only other entry, so LRU selection is trivial and
// the number is ~pure build/store cost), while cache_eviction cycles
// three keys through a two-product budget, so its delta over this bench
// is the LRU bookkeeping/selection cost under real multi-entry
// pressure. A regression in eviction policy code shows only there.
void BM_fields_cache_miss_store(benchmark::State& state) {
  static auto* world = make_world();
  tess::GoalSet goals_a;
  tess::GoalSet goals_b;
  fill_goals(goals_a, 4);
  fill_goals(goals_b, 8);
  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(kTileCount);
  // One product is ~kTileCount * 4 bytes of distances plus metadata.
  tess::FieldProductCache cache(kTileCount * 4 * 3 / 2);
  cache.reserve_entries(4);

  auto flip = false;
  for (auto _ : state) {
    const auto& goals = flip ? goals_a : goals_b;
    flip = !flip;
    if (cache.lookup<FieldWorld, PassableTag>(*world, goals) == nullptr) {
      tess::DistanceFieldProduct product;
      product.reserve_nodes(kTileCount);
      product.reserve_dependencies(FieldWorld::chunk_count);
      (void)tess::build_distance_field_product<FieldWorld, PassableTag>(
          *world, goals, scratch, product);
      benchmark::DoNotOptimize(
          (cache.store<FieldWorld, PassableTag>(std::move(product))));
    }
  }
  // Guarded by iteration count: the harness's 1-iteration calibration
  // pass has only seen the first miss.
  if (state.iterations() >= 8) {
    fields_bench_check(
        cache.stats().misses >= static_cast<std::size_t>(state.iterations()),
        "cache_miss_store left the miss path");
  }
}

// Byte-budgeted eviction: a budget that holds ~2 products with 3 keys
// cycling forces LRU eviction on every store.
void BM_fields_cache_eviction(benchmark::State& state) {
  static auto* world = make_world();
  tess::GoalSet goals[3];
  fill_goals(goals[0], 2);
  fill_goals(goals[1], 4);
  fill_goals(goals[2], 6);
  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(kTileCount);

  // One product is ~kTileCount * 4 bytes of distances plus metadata.
  tess::FieldProductCache cache(kTileCount * 4 * 5 / 2);
  cache.reserve_entries(4);

  std::size_t index = 0;
  for (auto _ : state) {
    const auto& set = goals[index];
    index = (index + 1) % 3;
    if (cache.lookup<FieldWorld, PassableTag>(*world, set) == nullptr) {
      tess::DistanceFieldProduct product;
      product.reserve_nodes(kTileCount);
      product.reserve_dependencies(FieldWorld::chunk_count);
      (void)tess::build_distance_field_product<FieldWorld, PassableTag>(
          *world, set, scratch, product);
      benchmark::DoNotOptimize(
          (cache.store<FieldWorld, PassableTag>(std::move(product))));
    }
  }
  if (state.iterations() >= 8) {
    fields_bench_check(cache.stats().evictions > 0,
                       "budget cycling never evicted");
  }
}

#if TESS_DIAGNOSTICS_ENABLED
// Warm-path allocation gate (benchmark-plan.md section 14): a rebuild
// into reserved product/scratch storage must not allocate.
void BM_fields_build_alloc_gate(benchmark::State& state) {
  static auto* world = make_world();
  tess::GoalSet goals;
  goals.reserve(16);
  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(kTileCount);
  tess::DistanceFieldProduct product;
  product.reserve_goals(16);
  product.reserve_nodes(kTileCount);
  product.reserve_dependencies(FieldWorld::chunk_count);
  fill_goals(goals, 16);
  (void)tess::build_distance_field_product<FieldWorld, PassableTag>(
      *world, goals, scratch, product);  // warm

  tess::diagnostics::AllocationCounters counters;
  for (auto _ : state) {
    counters.reset();
    tess::diagnostics::ScopedAllocationCounters scope{counters};
    fill_goals(goals, 16);
    const auto result =
        tess::build_distance_field_product<FieldWorld, PassableTag>(
            *world, goals, scratch, product);
    auto status = result.status;
    benchmark::DoNotOptimize(status);
  }
  fields_bench_check(counters.allocations == 0, "warm field build allocated");
}
BENCHMARK(BM_fields_build_alloc_gate)->Name("fields/build_alloc_gate");
#endif

BENCHMARK(BM_fields_goalset_build_1)->Name("fields/goalset_build_1");
BENCHMARK(BM_fields_goalset_build_16)->Name("fields/goalset_build_16");
BENCHMARK(BM_fields_goalset_build_256)->Name("fields/goalset_build_256");
BENCHMARK(BM_fields_nearest_target)->Name("fields/nearest_target");
BENCHMARK(BM_fields_cache_hit)->Name("fields/cache_hit");
BENCHMARK(BM_fields_cache_miss_store)->Name("fields/cache_miss_store");
BENCHMARK(BM_fields_cache_eviction)->Name("fields/cache_eviction");

}  // namespace
