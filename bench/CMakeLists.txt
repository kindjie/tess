include(TessGoogleDeps)

find_package(Python3 COMPONENTS Interpreter QUIET)

tess_require_google_benchmark()

add_executable(
  tess_bench
  tess_bench.cc
  tess_path_agent_bench.cc
  tess_path_product_bench.cc
  tess_path_scan_bench.cc
  tess_path_weighted_bench.cc
)
target_link_libraries(tess_bench PRIVATE tess::tess benchmark::benchmark_main)
tess_apply_project_options(tess_bench)

add_executable(
  tess_bench_diagnostics
  tess_bench.cc
  tess_path_agent_bench.cc
  tess_path_product_bench.cc
  tess_path_scan_bench.cc
  tess_path_weighted_bench.cc
  tess_diagnostics_alloc_hooks.cc
)
target_link_libraries(
  tess_bench_diagnostics
  PRIVATE tess::tess benchmark::benchmark_main
)
target_compile_definitions(
  tess_bench_diagnostics
  PRIVATE TESS_ENABLE_DIAGNOSTICS
)
tess_apply_project_options(tess_bench_diagnostics)

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

set(
  TESS_BLOCK_BENCHMARK_THRESHOLDS
  "${PROJECT_SOURCE_DIR}/bench/thresholds/block.json"
  CACHE FILEPATH "Thresholds for block benchmark regressions"
)

set(
  TESS_QUEUED_BENCHMARK_THRESHOLDS
  "${PROJECT_SOURCE_DIR}/bench/thresholds/queued.json"
  CACHE FILEPATH "Thresholds for queued benchmark regressions"
)

set(
  TESS_PATH_BENCHMARK_THRESHOLDS
  "${PROJECT_SOURCE_DIR}/bench/thresholds/path.json"
  CACHE FILEPATH "Thresholds for path benchmark regressions"
)

set(
  TESS_BENCHMARK_BASELINE_REPETITIONS
  "10"
  CACHE STRING "Repeated samples for non-gating CI benchmark baselines"
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

  add_custom_target(
    tess_bench_block_thresholds
    COMMAND
      tess_bench
      --benchmark_filter=block/.*
      --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/block.json
      --benchmark_out_format=json
    COMMAND
      "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/benchmark_thresholds.py"
      --results ${CMAKE_CURRENT_BINARY_DIR}/block.json
      --thresholds ${TESS_BLOCK_BENCHMARK_THRESHOLDS}
    DEPENDS tess_bench
    COMMENT "Running block benchmark threshold checks"
    VERBATIM
  )

  add_custom_target(
    tess_bench_queued_thresholds
    COMMAND
      tess_bench
      --benchmark_filter=queued/.*
      --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/queued.json
      --benchmark_out_format=json
    COMMAND
      "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/benchmark_thresholds.py"
      --results ${CMAKE_CURRENT_BINARY_DIR}/queued.json
      --thresholds ${TESS_QUEUED_BENCHMARK_THRESHOLDS}
    DEPENDS tess_bench
    COMMENT "Running queued benchmark threshold checks"
    VERBATIM
  )

  add_custom_target(
    tess_bench_path_thresholds
    COMMAND
      tess_bench
      --benchmark_filter=path/.*
      --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/path.json
      --benchmark_out_format=json
    COMMAND
      "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/benchmark_thresholds.py"
      --results ${CMAKE_CURRENT_BINARY_DIR}/path.json
      --thresholds ${TESS_PATH_BENCHMARK_THRESHOLDS}
    DEPENDS tess_bench
    COMMENT "Running path benchmark threshold checks"
    VERBATIM
  )
endif()

add_custom_target(
  tess_bench_ci_baselines
  COMMAND
    "${CMAKE_COMMAND}" -E make_directory
    ${CMAKE_CURRENT_BINARY_DIR}/ci-baselines
  COMMAND
    tess_bench
    --benchmark_filter=key/.*
    --benchmark_repetitions=${TESS_BENCHMARK_BASELINE_REPETITIONS}
    --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/ci-baselines/key.json
    --benchmark_out_format=json
  COMMAND
    tess_bench
    --benchmark_filter=storage/.*
    --benchmark_repetitions=${TESS_BENCHMARK_BASELINE_REPETITIONS}
    --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/ci-baselines/storage.json
    --benchmark_out_format=json
  COMMAND
    tess_bench
    --benchmark_filter=block/.*
    --benchmark_repetitions=${TESS_BENCHMARK_BASELINE_REPETITIONS}
    --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/ci-baselines/block.json
    --benchmark_out_format=json
  COMMAND
    tess_bench
    --benchmark_filter=queued/.*
    --benchmark_repetitions=${TESS_BENCHMARK_BASELINE_REPETITIONS}
    --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/ci-baselines/queued.json
    --benchmark_out_format=json
  COMMAND
    tess_bench
    --benchmark_filter=path/.*
    --benchmark_repetitions=${TESS_BENCHMARK_BASELINE_REPETITIONS}
    --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/ci-baselines/path.json
    --benchmark_out_format=json
  DEPENDS tess_bench
  COMMENT "Collecting non-gating CI benchmark baseline samples"
  VERBATIM
)

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
    NAME tess_bench_block_context_smoke
    COMMAND
      tess_bench
      --benchmark_filter=block/context_iteration_2d
      --benchmark_min_time=0.001
  )

  add_test(
    NAME tess_bench_block_boundary_smoke
    COMMAND
      tess_bench
      --benchmark_filter=block/chunk_boundary_scan_2d
      --benchmark_min_time=0.001
  )

  add_test(
    NAME tess_bench_queued_execute_smoke
    COMMAND
      tess_bench
      --benchmark_filter=queued/execute_resident_update
      --benchmark_min_time=0.001
  )

  add_test(
    NAME tess_bench_path_astar_smoke
    COMMAND
      tess_bench
      --benchmark_filter=path/astar_open_2d
      --benchmark_min_time=0.001
  )

  add_test(
    NAME tess_bench_path_astar_diagnostics_smoke
    COMMAND
      tess_bench_diagnostics
      --benchmark_filter=path/astar_open_2d
      --benchmark_min_time=0.001
  )
endif()
