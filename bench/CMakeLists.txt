include(TessGoogleDeps)

find_package(Python3 COMPONENTS Interpreter QUIET)

tess_require_google_benchmark()

add_executable(tess_bench tess_bench.cc)
target_link_libraries(tess_bench PRIVATE tess::tess benchmark::benchmark_main)

set(
  TESS_KEY_BENCHMARK_THRESHOLDS
  "${PROJECT_SOURCE_DIR}/bench/thresholds/key-conversions.json"
  CACHE FILEPATH "Thresholds for key conversion benchmark regressions"
)

set(
  TESS_STORAGE_BENCHMARK_THRESHOLDS
  "${PROJECT_SOURCE_DIR}/bench/thresholds/storage.json"
  CACHE FILEPATH "Thresholds for storage benchmark regressions"
)

if(Python3_Interpreter_FOUND)
  add_custom_target(
    tess_bench_key_thresholds
    COMMAND
      tess_bench
      --benchmark_filter=key/.*
      --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/key-conversions.json
      --benchmark_out_format=json
    COMMAND
      "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/benchmark_thresholds.py"
      --results ${CMAKE_CURRENT_BINARY_DIR}/key-conversions.json
      --thresholds ${TESS_KEY_BENCHMARK_THRESHOLDS}
    DEPENDS tess_bench
    COMMENT "Running key conversion benchmark threshold checks"
    VERBATIM
  )

  add_custom_target(
    tess_bench_storage_thresholds
    COMMAND
      tess_bench
      --benchmark_filter=storage/.*
      --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/storage.json
      --benchmark_out_format=json
    COMMAND
      "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/benchmark_thresholds.py"
      --results ${CMAKE_CURRENT_BINARY_DIR}/storage.json
      --thresholds ${TESS_STORAGE_BENCHMARK_THRESHOLDS}
    DEPENDS tess_bench
    COMMENT "Running storage benchmark threshold checks"
    VERBATIM
  )
endif()

if(BUILD_TESTING)
  add_test(
    NAME tess_bench_key_smoke
    COMMAND
      tess_bench
      --benchmark_filter=key/tile_key_2d_u64
      --benchmark_min_time=0.001
  )

  add_test(
    NAME tess_bench_storage_smoke
    COMMAND
      tess_bench
      --benchmark_filter=storage/field_span_acquisition
      --benchmark_min_time=0.001
  )

  add_test(
    NAME tess_bench_world_storage_smoke
    COMMAND
      tess_bench
      --benchmark_filter=storage/world_chunk_lookup_by_key
      --benchmark_min_time=0.001
  )

  add_test(
    NAME tess_bench_world_metadata_smoke
    COMMAND
      tess_bench
      --benchmark_filter=storage/world_metadata_lookup_by_key
      --benchmark_min_time=0.001
  )

  add_test(
    NAME tess_bench_block_smoke
    COMMAND
      tess_bench
      --benchmark_filter=block/chunk_tile_iteration_2d
      --benchmark_min_time=0.001
  )

  add_test(
    NAME tess_bench_block_boundary_smoke
    COMMAND
      tess_bench
      --benchmark_filter=block/chunk_boundary_scan_2d
      --benchmark_min_time=0.001
  )
endif()
