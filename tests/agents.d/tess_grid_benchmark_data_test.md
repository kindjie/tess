# tess_grid_benchmark_data_test

- `tess_grid_benchmark_data_test` (opt-in): always registers when external
  benchmark data is enabled, skips missing/rights-blocked data locally, and
  fails instead in strict required-data mode. Note: the cache-verifier
  readiness flag is hardcoded off in `tests/CMakeLists.txt`
  (`TESS_GRID_BENCHMARK_CACHE_VERIFIER_READY=0`), so the pass branch is
  currently unreachable — today the test only ever skips or fails.
