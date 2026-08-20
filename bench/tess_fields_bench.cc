#include <benchmark/benchmark.h>
#include <tess/tess.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

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

// The same workload on a world too large to sit in cache (audit
// 2026-08-07 P5). At 64x64 the generation array and the product copy are
// 16 KiB each, so the whole working set is L1/L2 resident and the
// 2026-08-02 bare-metal campaign measured this family at 0.001-0.007 LLC
// MPKI. Every product-layout, per-build `assign`, and dependency-capture
// change was therefore measured only where memory traffic is free. At
// 512x512 each array is 1 MiB. The precedent is exact: the 256-chunk
// `storage/world_dirty_chunks_iteration` was flat at 125 ns both ways
// until the `_16k` variant existed.
using LargeFieldShape =
    tess::Shape<tess::Extent3{512, 512, 1}, tess::Extent3{8, 8, 1}>;
using LargeFieldWorld =
    tess::AlwaysResidentWorld<LargeFieldShape, FieldSchemaT>;
constexpr auto kLargeTileCount =
    LargeFieldShape::size.x * LargeFieldShape::size.y * LargeFieldShape::size.z;

template <typename World>
auto make_world_of() -> World* {
  auto* world = new World();
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

auto make_world() -> FieldWorld* { return make_world_of<FieldWorld>(); }

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

// Distinct goal sets with a CONSTANT cardinality: each `block` occupies
// its own run of tile indices, so two blocks share no goal while every
// set seeds the same number of floods. Varying the count instead would
// change flood seeding, key comparison length, stored byte size and
// therefore the resident entry count, confounding a comparison meant to
// isolate entry count alone.
void fill_goals_block(tess::GoalSet& goals, std::size_t count,
                      std::size_t block) {
  goals.clear();
  for (std::size_t i = 0; i < count; ++i) {
    const auto index = (block * count + i) % (64 * 64);
    goals.add(tess::Coord3{static_cast<std::int64_t>(index % 64),
                           static_cast<std::int64_t>(index / 64), 0});
  }
}

// Reverse flood from N goals over the open 64x64 world.
//
// NOT a goal-count scaling curve, despite the family naming. The flood
// visits every one of the 4096 tiles whatever the seed count, so cost is
// bounded by world size, not goal count; more goals only add seed
// insertions. Measured 1/16/256 goals at 84/98/97 us -- flat, as the
// algorithm implies. Read a flat line here as "goal count is not the
// cost driver", not as evidence of good scaling.
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
            *world, goals, product, scratch);
    reached = product.reached_nodes();
    auto status = result.status;
    benchmark::DoNotOptimize(status);
  }
  // Set AFTER the timed loop, from values the loop already captured, so
  // the published timings are unaffected. Nothing here may move inside
  // the loop: these numbers exist to make a result auditable later, not
  // at the cost of changing what was measured.
  state.counters["reached_nodes"] = static_cast<double>(reached);
  state.counters["goal_count"] = static_cast<double>(goal_count);
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

// The memory-bound counterpart to the three cells above. Goal count is
// held at 16 -- the flood visits every tile regardless, so goal count is
// not the cost driver -- and only the world size changes, which is the
// one dimension the family never varied.
void BM_fields_goalset_build_large(benchmark::State& state) {
  static auto* world = make_world_of<LargeFieldWorld>();
  constexpr auto goal_count = std::size_t{16};
  tess::GoalSet goals;
  goals.reserve(goal_count);
  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(kLargeTileCount);
  tess::DistanceFieldProduct product;
  product.reserve_goals(goal_count);
  product.reserve_nodes(kLargeTileCount);
  product.reserve_dependencies(LargeFieldWorld::chunk_count);

  auto reached = std::size_t{0};
  for (auto _ : state) {
    goals.clear();
    for (std::size_t i = 0; i < goal_count; ++i) {
      goals.add(tess::Coord3{static_cast<std::int64_t>((i * 31) % 512),
                             static_cast<std::int64_t>((i * 97) % 512), 0});
    }
    const auto result =
        tess::build_distance_field_product<LargeFieldWorld, PassableTag>(
            *world, goals, product, scratch);
    reached = product.reached_nodes();
    auto status = result.status;
    benchmark::DoNotOptimize(status);
  }
  state.counters["reached_nodes"] = static_cast<double>(reached);
  state.counters["goal_count"] = static_cast<double>(goal_count);
  fields_bench_check(reached == kLargeTileCount,
                     "open-world flood did not reach every tile");
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
          *world, goals, product, scratch)
              .status == tess::PathStatus::Found,
      "product build failed");

  auto status = tess::PathStatus::NotComputed;
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
          *world, goals, product, scratch)
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
          *world, goals, product, scratch);
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

// Member-product rebuild through store_reusing: one product object is
// held across iterations, rebuilt on every miss, and stored with the
// hand-back overload, so the displaced entry's storage returns to the
// caller and the next build reuses its capacity. This is the runtime
// call-site pattern #122 changed; no other cell exercises it — the
// fresh-product cells above use the rvalue store overload that #122
// deliberately kept as a thin wrapper, which is how a binary-level
// fields improvement was once misattributed to #122 (optimization log,
// 2026-08-08). Contrast with cache_miss_store: same two-key cycle and
// budget, differing only in product lifetime and store overload, so
// the pair's delta isolates the hand-back path's steady-state cost.
void BM_fields_cache_store_reusing(benchmark::State& state) {
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
  tess::DistanceFieldProduct product;
  product.reserve_nodes(kTileCount);
  product.reserve_dependencies(FieldWorld::chunk_count);

  auto flip = false;
  const tess::DistanceFieldProduct* stored = nullptr;
  for (auto _ : state) {
    const auto& goals = flip ? goals_a : goals_b;
    flip = !flip;
    if (cache.lookup<FieldWorld, PassableTag>(*world, goals) == nullptr) {
      (void)tess::build_distance_field_product<FieldWorld, PassableTag>(
          *world, goals, product, scratch);
      stored = cache.store_reusing<FieldWorld, PassableTag>(product);
      benchmark::DoNotOptimize(stored);
    }
  }
  // Guarded by iteration count: the harness's 1-iteration calibration
  // pass has only seen the first miss. The stored pointer and the
  // resident entry prove the hand-back store path actually ran — a
  // rejecting store would keep every lookup missing and silently turn
  // this cell into a build-only loop.
  if (state.iterations() >= 8) {
    fields_bench_check(
        cache.stats().misses >= static_cast<std::size_t>(state.iterations()),
        "cache_store_reusing left the miss path");
    fields_bench_check(stored != nullptr && cache.stats().entries >= 1,
                       "store_reusing rejected its stores");
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
          *world, set, product, scratch);
      benchmark::DoNotOptimize(
          (cache.store<FieldWorld, PassableTag>(std::move(product))));
    }
  }
  if (state.iterations() >= 8) {
    fields_bench_check(cache.stats().evictions > 0,
                       "budget cycling never evicted");
  }
}

// Cache SCAN cost against resident entry count, which nothing measured
// before: the family's other cache benchmarks hold about two entries, so
// every linear scan over `entries_` had at most two candidates.
//
// This measures the AGGREGATE of the three scans a miss-and-store walks,
// not eviction alone: `lookup` scans every resident entry, then
// `store_with_key` scans them again for an existing key, and only then
// does `evict_to_budget` scan to find the least-recently-used entry. All
// three are linear in the entry count, so a complexity change in any of
// them separates the two sizes -- but the delta cannot be attributed to
// eviction specifically, and this deliberately does not claim to.
//
// Per-store work is held identical: same world, same goal CARDINALITY
// (only the goal positions differ, so keys are distinct), same product
// build. The sizes differ only in how many entries are resident when
// those scans run.
template <std::size_t Entries>
void BM_fields_cache_scan_at(benchmark::State& state) {
  static auto* world = make_world();
  // One more key than the cache can hold, so every store evicts.
  constexpr auto kKeys = Entries + 1;
  constexpr auto kGoalsPerKey = std::size_t{8};
  std::vector<tess::GoalSet> goals(kKeys);
  for (std::size_t i = 0; i < kKeys; ++i) {
    fill_goals_block(goals[i], kGoalsPerKey, i);
  }
  tess::DistanceFieldScratch scratch;
  scratch.reserve_nodes(kTileCount);

  // Size the budget from a MEASURED product rather than arithmetic: a
  // product carries goals, dependencies and metadata beyond its distance
  // array, so a computed budget silently held a different number of
  // entries than intended and the cache never reached its limit.
  const auto product_bytes = [&] {
    tess::FieldProductCache probe(std::size_t{1} << 40U);
    tess::DistanceFieldProduct one;
    one.reserve_nodes(kTileCount);
    one.reserve_dependencies(FieldWorld::chunk_count);
    (void)tess::build_distance_field_product<FieldWorld, PassableTag>(
        *world, goals[0], one, scratch);
    (void)probe.store<FieldWorld, PassableTag>(std::move(one));
    return probe.stats().bytes;
  }();
  fields_bench_check(product_bytes > 0, "probe store reported no bytes");

  // Holds exactly `Entries` products, so the next store must evict and
  // scan every resident entry to find the oldest.
  tess::FieldProductCache cache(product_bytes * Entries);
  cache.reserve_entries(Entries + 1);

  std::size_t index = 0;
  for (auto _ : state) {
    const auto& set = goals[index];
    index = (index + 1) % kKeys;
    if (cache.lookup<FieldWorld, PassableTag>(*world, set) == nullptr) {
      tess::DistanceFieldProduct product;
      product.reserve_nodes(kTileCount);
      product.reserve_dependencies(FieldWorld::chunk_count);
      (void)tess::build_distance_field_product<FieldWorld, PassableTag>(
          *world, set, product, scratch);
      benchmark::DoNotOptimize(
          (cache.store<FieldWorld, PassableTag>(std::move(product))));
    }
  }
  // The cache must FILL before it can evict, so the guard has to clear
  // the fill threshold, not a fixed count. Checking at 8 iterations
  // aborted the 128-entry variant, which needs 129 stores before its
  // first eviction.
  if (state.iterations() > static_cast<std::int64_t>(Entries) * 2) {
    fields_bench_check(cache.stats().evictions > 0,
                       "entry-count cycling never evicted");
    fields_bench_check(cache.stats().hits == 0,
                       "every lookup must miss for this to stay on the "
                       "store-and-evict path");
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
      *world, goals, product, scratch);  // warm

  tess::diagnostics::AllocationCounters counters;
  for (auto _ : state) {
    counters.reset();
    tess::diagnostics::ScopedAllocationCounters scope{counters};
    fill_goals(goals, 16);
    const auto result =
        tess::build_distance_field_product<FieldWorld, PassableTag>(
            *world, goals, product, scratch);
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
BENCHMARK(BM_fields_goalset_build_large)
    ->Name("fields/goalset_build_16_512x512");
BENCHMARK(BM_fields_nearest_target)->Name("fields/nearest_target");
BENCHMARK(BM_fields_cache_hit)->Name("fields/cache_hit");
BENCHMARK(BM_fields_cache_miss_store)->Name("fields/cache_miss_store");
BENCHMARK(BM_fields_cache_store_reusing)->Name("fields/cache_store_reusing");
BENCHMARK(BM_fields_cache_eviction)->Name("fields/cache_eviction");
BENCHMARK(BM_fields_cache_scan_at<8>)->Name("fields/cache_scan_entries_8");
BENCHMARK(BM_fields_cache_scan_at<128>)->Name("fields/cache_scan_entries_128");

}  // namespace
