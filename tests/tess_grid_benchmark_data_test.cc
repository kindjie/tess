#include <gtest/gtest.h>

#include <filesystem>

#ifndef TESS_GRID_BENCHMARK_ENTRY_COUNT
#error "TESS_GRID_BENCHMARK_ENTRY_COUNT must come from the manifest"
#endif

#ifndef TESS_GRID_BENCHMARK_DATA_REQUIRED
#error "TESS_GRID_BENCHMARK_DATA_REQUIRED must come from CMake"
#endif

#ifndef TESS_GRID_BENCHMARK_DATA_DIR
#error "TESS_GRID_BENCHMARK_DATA_DIR must come from CMake"
#endif

#ifndef TESS_GRID_BENCHMARK_CACHE_VERIFIER_READY
#error "TESS_GRID_BENCHMARK_CACHE_VERIFIER_READY must come from CMake"
#endif

TEST(TessGridBenchmarkData, ExternalCorpusIsReady) {
  constexpr auto entry_count = TESS_GRID_BENCHMARK_ENTRY_COUNT;
  constexpr auto required = TESS_GRID_BENCHMARK_DATA_REQUIRED != 0;
  constexpr auto verifier_ready = TESS_GRID_BENCHMARK_CACHE_VERIFIER_READY != 0;
  const auto data_dir = std::filesystem::path{TESS_GRID_BENCHMARK_DATA_DIR};
  const auto ready = verifier_ready && entry_count > 0 &&
                     std::filesystem::is_directory(data_dir);

  if (ready) {
    SUCCEED();
    return;
  }
  if constexpr (required) {
    FAIL() << "external grid benchmark data is required but unavailable; "
              "check the manifest rights gate and cache";
  } else {
    GTEST_SKIP() << "external grid benchmark data is unavailable; the local "
                    "opt-in suite may skip";
  }
}
