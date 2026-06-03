#include <benchmark/benchmark.h>
#include <tess/tess.h>

namespace {

void BM_library_version_access(benchmark::State& state) {
  for (auto _ : state) {
    auto version = tess::library_version;
    benchmark::DoNotOptimize(version);
  }
}

BENCHMARK(BM_library_version_access);

}  // namespace
