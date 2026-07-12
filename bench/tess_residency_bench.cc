#include <benchmark/benchmark.h>
#include <tess/tess.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

void residency_bench_check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "tess_bench correctness check failed: %s\n", message);
    std::abort();
  }
}

struct TerrainTag {};
struct CostTag {};

using ResidencySchema =
    tess::FieldSchema<tess::Field<TerrainTag, std::uint16_t>,
                      tess::Field<CostTag, float>>;
// Small chunks keep the fixed slot pool cheap to construct while the key
// space stays much larger than any budget, so eviction benches can stream
// distinct keys indefinitely.
using ResidencyShape =
    tess::Shape<tess::Extent3{65536, 65536, 1}, tess::Extent3{32, 32, 1}>;
using ResidencyWorld =
    tess::SparseResidentWorld<ResidencyShape, ResidencySchema>;

[[nodiscard]] auto world_with_budget(std::size_t chunk_capacity)
    -> ResidencyWorld {
  return ResidencyWorld{
      tess::ResidencyConfig{chunk_capacity * ResidencyWorld::page_byte_size}};
}

// Directory probe for a resident chunk: the read that every sparse tile
// access, fingerprint term, and freshness check performs.
void BM_residency_resident_lookup(benchmark::State& state) {
  auto world = world_with_budget(256);
  for (std::uint64_t key = 0; key < 256; ++key) {
    world.ensure_resident(tess::ChunkKey{key});
  }
  std::uint64_t key = 0;
  for (auto _ : state) {
    benchmark::DoNotOptimize(world.resident_slot(tess::ChunkKey{key}));
    key = (key + 1) & 255u;
  }
  residency_bench_check(world.resident_count() == 256,
                        "lookup loop changed the resident set");
  residency_bench_check(
      world.resident_slot(tess::ChunkKey{0}) != ResidencyWorld::npos_slot,
      "resident chunk not found by lookup");
}

// ensure_resident on an already-resident chunk: the steady-state hit path
// (directory probe + MRU bookkeeping), no eviction.
void BM_residency_ensure_resident_hit(benchmark::State& state) {
  auto world = world_with_budget(256);
  for (std::uint64_t key = 0; key < 256; ++key) {
    world.ensure_resident(tess::ChunkKey{key});
  }
  std::uint64_t key = 0;
  for (auto _ : state) {
    benchmark::DoNotOptimize(world.ensure_resident(tess::ChunkKey{key}));
    key = (key + 1) & 255u;
  }
  residency_bench_check(world.resident_count() == 256,
                        "hit loop evicted or loaded a chunk");
}

// Streaming misses at a full budget: every ensure_resident evicts the
// least-recently-used chunk first. This is the path audit 2026-07-11
// finding M11 flags as O(capacity) per miss (linear LRU scan); the two
// capacities exist to expose that scaling. Ungated on purpose -- the
// numbers are the before/after evidence for the intrusive-list fix
// (remediation M11b); gates come with the 10-artifact recalibration rule.
template <std::size_t Capacity>
void BM_residency_eviction_churn(benchmark::State& state) {
  auto world = world_with_budget(Capacity);
  // Cycling 8x capacity distinct keys keeps every timed ensure_resident a
  // miss (a revisited key was evicted 7x capacity insertions ago) while
  // the key space stays bounded for arbitrarily long runs.
  constexpr std::uint64_t key_cycle = Capacity * 8;
  static_assert(key_cycle <= ResidencyWorld::chunk_count);
  // Fill to budget so the timed loop is misses only.
  std::uint64_t key = 0;
  for (; key < Capacity; ++key) {
    world.ensure_resident(tess::ChunkKey{key});
  }
  std::uint64_t last_key = key;
  for (auto _ : state) {
    last_key = key;
    benchmark::DoNotOptimize(world.ensure_resident(tess::ChunkKey{key}));
    key = (key + 1) % key_cycle;
  }
  residency_bench_check(world.resident_count() == Capacity,
                        "churn loop violated the budget");
  residency_bench_check(world.resident_slot(tess::ChunkKey{last_key}) !=
                            ResidencyWorld::npos_slot,
                        "churned chunk did not become resident");
}

void BM_residency_eviction_churn_64(benchmark::State& state) {
  BM_residency_eviction_churn<64>(state);
}

void BM_residency_eviction_churn_512(benchmark::State& state) {
  BM_residency_eviction_churn<512>(state);
}

BENCHMARK(BM_residency_resident_lookup)->Name("residency/resident_lookup");
BENCHMARK(BM_residency_ensure_resident_hit)
    ->Name("residency/ensure_resident_hit");
BENCHMARK(BM_residency_eviction_churn_64)->Name("residency/eviction_churn_64");
BENCHMARK(BM_residency_eviction_churn_512)
    ->Name("residency/eviction_churn_512");

}  // namespace
