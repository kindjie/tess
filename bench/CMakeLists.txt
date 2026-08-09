include(TessGoogleDeps)

find_package(Python3 COMPONENTS Interpreter QUIET)

tess_require_google_benchmark()

add_executable(
  tess_bench
  tess_bench.cc
  tess_fields_bench.cc
  tess_parallel_bench.cc
  tess_persistence_bench.cc
  tess_pipeline_query_bench.cc
  tess_path_agent_bench.cc
  tess_queued_planning_bench.cc
  tess_path_product_bench.cc
  tess_path_scan_bench.cc
  tess_path_weighted_bench.cc
  tess_render_delta_bench.cc
  tess_residency_bench.cc
  tess_scheduler_bench.cc
  tess_joint_movement_bench.cc
  tess_pibt_movement_bench.cc
  tess_spatial_bench.cc
  tess_maintenance_bench.cc
  tess_topology_bench.cc
  tess_transition_bench.cc
)
target_link_libraries(tess_bench PRIVATE tess::tess benchmark::benchmark_main)
tess_apply_project_options(tess_bench)

add_executable(
  tess_bench_diagnostics
  tess_bench.cc
  tess_fields_bench.cc
  tess_parallel_bench.cc
  tess_pipeline_query_bench.cc
  tess_path_agent_bench.cc
  tess_queued_planning_bench.cc
  tess_path_product_bench.cc
  tess_path_scan_bench.cc
  tess_path_weighted_bench.cc
  tess_persistence_bench.cc
  tess_render_delta_bench.cc
  tess_scheduler_bench.cc
  tess_joint_movement_bench.cc
  tess_pibt_movement_bench.cc
  tess_spatial_bench.cc
  tess_maintenance_bench.cc
  tess_transition_bench.cc
  tess_diagnostics_bench.cc
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

# The thread-scaling sweep is its own binary on purpose. Four call sites run
# tess_bench unfiltered (weekly coverage, paired-base comparison, the
# workload-matrix drift check, and the metal campaign's main timing stage);
# a 190-worker sweep registered there would build 190-thread pools on
# four-core runners and run twice per campaign. Negative filters at four
# sites fail open. It is kept out of tess_bench_diagnostics as well, whose
# allocation hooks distort timings by 12-21%.
add_executable(tess_bench_thread_scaling tess_thread_scaling_bench.cc)
target_link_libraries(
  tess_bench_thread_scaling
  PRIVATE tess::tess benchmark::benchmark_main
)
tess_apply_project_options(tess_bench_thread_scaling)

# The budgeted-progress runner is likewise its own binary (design
# docs/planning/budgeted-progress-benchmarks.md section 14.1): a
# multi-frame queueing experiment with its own clock, controller, and
# artifact schema, not a Google Benchmark suite. It registers no
# benchmark names, so the threshold literal gate and workload-matrix
# scan do not apply; cell identities live in artifact workload_refs.
# It quote-includes the deterministic harness headers from tests/
# (grid generators/oracle and the budgeted controller), which are
# harness support, never public headers.
add_executable(tess_bench_budgeted_progress tess_budgeted_progress_bench.cc)
target_link_libraries(
  tess_bench_budgeted_progress
  PRIVATE tess::tess
)
target_include_directories(
  tess_bench_budgeted_progress
  PRIVATE "${PROJECT_SOURCE_DIR}/tests"
)
tess_apply_project_options(tess_bench_budgeted_progress)

# The counter-pass twin (design section 11.2): identical sources with
# the compile-gated diagnostics enabled. Its wall numbers are never
# published — it exists to explain the timing pass's numbers, compared
# under the section 11.2 tolerances by tools/compare_budgeted_passes.py.
# Deliberately excludes tess_diagnostics_alloc_hooks.cc: global
# new/delete replacement is the documented 12-21% distortion source
# and this increment's counters block does not use allocation counts.
add_executable(tess_bench_budgeted_progress_diagnostics
               tess_budgeted_progress_bench.cc)
target_link_libraries(
  tess_bench_budgeted_progress_diagnostics
  PRIVATE tess::tess
)
target_include_directories(
  tess_bench_budgeted_progress_diagnostics
  PRIVATE "${PROJECT_SOURCE_DIR}/tests"
)
target_compile_definitions(
  tess_bench_budgeted_progress_diagnostics
  PRIVATE TESS_ENABLE_DIAGNOSTICS
)
tess_apply_project_options(tess_bench_budgeted_progress_diagnostics)

if(TESS_ENABLE_ENTT)
  include(TessEnttDeps)
  tess_require_entt()

  # The ecs family joins both bench binaries only when EnTT is enabled;
  # the header gate stays a per-target macro on EnTT-linked targets.
  target_sources(tess_bench PRIVATE tess_ecs_bench.cc)
  target_link_libraries(tess_bench PRIVATE EnTT::EnTT)
  target_compile_definitions(tess_bench PRIVATE TESS_ENABLE_ENTT)

  target_sources(tess_bench_diagnostics PRIVATE tess_ecs_bench.cc)
  target_link_libraries(tess_bench_diagnostics PRIVATE EnTT::EnTT)
  target_compile_definitions(tess_bench_diagnostics PRIVATE TESS_ENABLE_ENTT)
endif()

if(TESS_ENABLE_FLECS)
  include(TessFlecsDeps)
  tess_require_flecs()

  target_sources(tess_bench PRIVATE tess_flecs_ecs_bench.cc)
  target_link_libraries(tess_bench PRIVATE flecs::flecs_static)
  target_compile_definitions(tess_bench PRIVATE TESS_ENABLE_FLECS)

  target_sources(tess_bench_diagnostics PRIVATE tess_flecs_ecs_bench.cc)
  target_link_libraries(tess_bench_diagnostics PRIVATE flecs::flecs_static)
  target_compile_definitions(tess_bench_diagnostics PRIVATE TESS_ENABLE_FLECS)
endif()

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
  TESS_BLOCK_PIPELINE_BENCHMARK_THRESHOLDS
  "${PROJECT_SOURCE_DIR}/bench/thresholds/block-pipeline.json"
  CACHE FILEPATH "Thresholds for block-pipeline benchmark regressions"
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
  TESS_TOPOLOGY_BENCHMARK_THRESHOLDS
  "${PROJECT_SOURCE_DIR}/bench/thresholds/topology.json"
  CACHE FILEPATH "Thresholds for topology benchmark regressions"
)

set(
  TESS_SCHEDULER_BENCHMARK_THRESHOLDS
  "${PROJECT_SOURCE_DIR}/bench/thresholds/scheduler.json"
  CACHE FILEPATH "Thresholds for scheduler benchmark regressions"
)

set(
  TESS_RESIDENCY_BENCHMARK_THRESHOLDS
  "${PROJECT_SOURCE_DIR}/bench/thresholds/residency.json"
  CACHE FILEPATH "Thresholds for residency benchmark regressions"
)

set(
  TESS_MAINTENANCE_BENCHMARK_THRESHOLDS
  "${PROJECT_SOURCE_DIR}/bench/thresholds/maintenance.json"
  CACHE FILEPATH "Thresholds for maintenance benchmark regressions"
)

set(
  TESS_PERSISTENCE_BENCHMARK_THRESHOLDS
  "${PROJECT_SOURCE_DIR}/bench/thresholds/persistence.json"
  CACHE FILEPATH "Thresholds for persistence benchmark regressions"
)

set(
  TESS_QUERY_BENCHMARK_THRESHOLDS
  "${PROJECT_SOURCE_DIR}/bench/thresholds/query.json"
  CACHE FILEPATH "Thresholds for query benchmark regressions"
)

set(
  TESS_SPATIAL_BENCHMARK_THRESHOLDS
  "${PROJECT_SOURCE_DIR}/bench/thresholds/spatial.json"
  CACHE FILEPATH "Thresholds for spatial benchmark regressions"
)

set(
  TESS_ECS_BENCHMARK_THRESHOLDS
  "${PROJECT_SOURCE_DIR}/bench/thresholds/ecs.json"
  CACHE FILEPATH "Thresholds for ecs benchmark regressions"
)

set(
  TESS_FIELDS_BENCHMARK_THRESHOLDS
  "${PROJECT_SOURCE_DIR}/bench/thresholds/fields.json"
  CACHE FILEPATH "Thresholds for fields benchmark regressions"
)

set(
  TESS_RENDER_DELTA_BENCHMARK_THRESHOLDS
  "${PROJECT_SOURCE_DIR}/bench/thresholds/render-delta.json"
  CACHE FILEPATH "Thresholds for render_delta benchmark regressions"
)

set(
  TESS_DIAGNOSTICS_BENCHMARK_THRESHOLDS
  "${PROJECT_SOURCE_DIR}/bench/thresholds/diagnostics.json"
  CACHE FILEPATH "Thresholds for diagnostics benchmark regressions"
)

set(
  TESS_BENCHMARK_BASELINE_REPETITIONS
  "10"
  CACHE STRING "Repeated samples for non-gating CI benchmark baselines"
)

# Threshold gates run repetitions and gate on the median aggregate
# (benchmark_thresholds.py's default), so a single outlier sample on a
# noisy shared runner cannot flake a gate. The 2x/25 ns ceilings were
# calibrated from 10-repetition baselines; gating on an unreplicated
# sample was the mismatch (audit 2026-07-11, bench lows).
set(
  TESS_BENCHMARK_GATE_REPETITIONS
  "3"
  CACHE STRING "Repeated samples for gating benchmark threshold checks"
)

set(
  TESS_PARALLEL_BENCHMARK_THRESHOLDS
  "${PROJECT_SOURCE_DIR}/bench/thresholds/parallel.json"
  CACHE FILEPATH "Thresholds for parallel benchmark regressions"
)

if(Python3_Interpreter_FOUND)
  add_custom_target(
    tess_bench_key_thresholds
    COMMAND
      tess_bench
      --benchmark_filter=key/.*
      --benchmark_repetitions=${TESS_BENCHMARK_GATE_REPETITIONS}
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
      --benchmark_repetitions=${TESS_BENCHMARK_GATE_REPETITIONS}
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
      --benchmark_repetitions=${TESS_BENCHMARK_GATE_REPETITIONS}
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
    tess_bench_block_pipeline_thresholds
    COMMAND
      tess_bench
      --benchmark_filter=block_pipeline/.*
      --benchmark_repetitions=${TESS_BENCHMARK_GATE_REPETITIONS}
      --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/block-pipeline.json
      --benchmark_out_format=json
    COMMAND
      "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/benchmark_thresholds.py"
      --results ${CMAKE_CURRENT_BINARY_DIR}/block-pipeline.json
      --thresholds ${TESS_BLOCK_PIPELINE_BENCHMARK_THRESHOLDS}
    DEPENDS tess_bench
    COMMENT "Running block-pipeline benchmark threshold checks"
    VERBATIM
  )

  add_custom_target(
    tess_bench_queued_thresholds
    COMMAND
      tess_bench
      --benchmark_filter=queued/.*
      --benchmark_repetitions=${TESS_BENCHMARK_GATE_REPETITIONS}
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
      --benchmark_repetitions=${TESS_BENCHMARK_GATE_REPETITIONS}
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

  add_custom_target(
    tess_bench_topology_thresholds
    COMMAND
      tess_bench
      --benchmark_filter=topology/.*
      --benchmark_repetitions=${TESS_BENCHMARK_GATE_REPETITIONS}
      --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/topology.json
      --benchmark_out_format=json
    COMMAND
      "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/benchmark_thresholds.py"
      --results ${CMAKE_CURRENT_BINARY_DIR}/topology.json
      --thresholds ${TESS_TOPOLOGY_BENCHMARK_THRESHOLDS}
    DEPENDS tess_bench
    COMMENT "Running topology benchmark threshold checks"
    VERBATIM
  )

  add_custom_target(
    tess_bench_scheduler_thresholds
    COMMAND
      tess_bench
      --benchmark_filter=scheduler/.*
      --benchmark_repetitions=${TESS_BENCHMARK_GATE_REPETITIONS}
      --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/scheduler.json
      --benchmark_out_format=json
    COMMAND
      "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/benchmark_thresholds.py"
      --results ${CMAKE_CURRENT_BINARY_DIR}/scheduler.json
      --thresholds ${TESS_SCHEDULER_BENCHMARK_THRESHOLDS}
    DEPENDS tess_bench
    COMMENT "Running scheduler benchmark threshold checks"
    VERBATIM
  )

  add_custom_target(
    tess_bench_residency_thresholds
    COMMAND
      tess_bench
      --benchmark_filter=residency/.*
      --benchmark_repetitions=${TESS_BENCHMARK_GATE_REPETITIONS}
      --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/residency.json
      --benchmark_out_format=json
    COMMAND
      "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/benchmark_thresholds.py"
      --results ${CMAKE_CURRENT_BINARY_DIR}/residency.json
      --thresholds ${TESS_RESIDENCY_BENCHMARK_THRESHOLDS}
    DEPENDS tess_bench
    COMMENT "Running residency benchmark threshold checks"
    VERBATIM
  )

  add_custom_target(
    tess_bench_maintenance_thresholds
    COMMAND
      tess_bench
      --benchmark_filter=maintenance/.*
      --benchmark_repetitions=${TESS_BENCHMARK_GATE_REPETITIONS}
      --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/maintenance.json
      --benchmark_out_format=json
    COMMAND
      "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/benchmark_thresholds.py"
      --results ${CMAKE_CURRENT_BINARY_DIR}/maintenance.json
      --thresholds ${TESS_MAINTENANCE_BENCHMARK_THRESHOLDS}
    DEPENDS tess_bench
    COMMENT "Running maintenance benchmark threshold checks"
    VERBATIM
  )

  add_custom_target(
    tess_bench_persistence_thresholds
    COMMAND
      tess_bench
      --benchmark_filter=persistence/.*
      --benchmark_repetitions=${TESS_BENCHMARK_GATE_REPETITIONS}
      --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/persistence.json
      --benchmark_out_format=json
    COMMAND
      "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/benchmark_thresholds.py"
      --results ${CMAKE_CURRENT_BINARY_DIR}/persistence.json
      --thresholds ${TESS_PERSISTENCE_BENCHMARK_THRESHOLDS}
    DEPENDS tess_bench
    COMMENT "Running persistence benchmark threshold checks"
    VERBATIM
  )

  add_custom_target(
    tess_bench_query_thresholds
    COMMAND
      tess_bench
      --benchmark_filter=query/.*
      --benchmark_repetitions=${TESS_BENCHMARK_GATE_REPETITIONS}
      --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/query.json
      --benchmark_out_format=json
    COMMAND
      "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/benchmark_thresholds.py"
      --results ${CMAKE_CURRENT_BINARY_DIR}/query.json
      --thresholds ${TESS_QUERY_BENCHMARK_THRESHOLDS}
    DEPENDS tess_bench
    COMMENT "Running query benchmark threshold checks"
    VERBATIM
  )

  add_custom_target(
    tess_bench_spatial_thresholds
    COMMAND
      tess_bench
      --benchmark_filter=spatial/.*
      --benchmark_repetitions=${TESS_BENCHMARK_GATE_REPETITIONS}
      --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/spatial.json
      --benchmark_out_format=json
    COMMAND
      "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/benchmark_thresholds.py"
      --results ${CMAKE_CURRENT_BINARY_DIR}/spatial.json
      --thresholds ${TESS_SPATIAL_BENCHMARK_THRESHOLDS}
    DEPENDS tess_bench
    COMMENT "Running spatial benchmark threshold checks"
    VERBATIM
  )

  if(TESS_ENABLE_ENTT OR TESS_ENABLE_FLECS)
    # The ecs family runs through the diagnostics binary so the
    # EnTT allocation-gate case executes when that adapter is enabled. Keep
    # one strict shared manifest, but explicitly excuse only the other
    # adapter's absent results in single-adapter configurations.
    set(tess_ecs_optional_missing_args)
    if(NOT TESS_ENABLE_ENTT)
      foreach(
        benchmark_name
        ecs/collect_sort_1k
        ecs/collect_sort_10k
        ecs/collect_sort_100k
        ecs/apply_1k
        ecs/apply_10k
        ecs/index_move
        ecs/tick_entt_1k
        ecs/tick_entt_10k
        ecs/tick_raw_span_1k
        ecs/tick_raw_span_10k
        ecs/tick_entt_alloc_gate
      )
        list(
          APPEND
          tess_ecs_optional_missing_args
          --allow-missing-result
          ${benchmark_name}
        )
      endforeach()
    endif()
    if(NOT TESS_ENABLE_FLECS)
      # Keep the shared threshold manifest strict while explicitly excluding
      # only benchmarks whose optional adapter is absent in this build.
      foreach(
        benchmark_name
        ecs/flecs_collect_1k
        ecs/flecs_collect_10k
        ecs/flecs_collect_100k
        ecs/flecs_collect_apply_10k
      )
        list(
          APPEND
          tess_ecs_optional_missing_args
          --allow-missing-result
          ${benchmark_name}
        )
      endforeach()
    endif()
    add_custom_target(
      tess_bench_ecs_thresholds
      COMMAND
        tess_bench_diagnostics
        --benchmark_filter=ecs/.*
        --benchmark_repetitions=${TESS_BENCHMARK_GATE_REPETITIONS}
        --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/ecs.json
        --benchmark_out_format=json
      COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/benchmark_thresholds.py"
        --results ${CMAKE_CURRENT_BINARY_DIR}/ecs.json
        --thresholds ${TESS_ECS_BENCHMARK_THRESHOLDS}
        ${tess_ecs_optional_missing_args}
      DEPENDS tess_bench_diagnostics
      COMMENT "Running ecs benchmark threshold checks"
      VERBATIM
    )
  endif()

  add_custom_target(
    tess_bench_fields_thresholds
    COMMAND
      tess_bench_diagnostics
      --benchmark_filter=fields/.*
      --benchmark_repetitions=${TESS_BENCHMARK_GATE_REPETITIONS}
      --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/fields.json
      --benchmark_out_format=json
    COMMAND
      "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/benchmark_thresholds.py"
      --results ${CMAKE_CURRENT_BINARY_DIR}/fields.json
      --thresholds ${TESS_FIELDS_BENCHMARK_THRESHOLDS}
    DEPENDS tess_bench_diagnostics
    COMMENT "Running fields benchmark threshold checks"
    VERBATIM
  )

  # Like the ecs family: run through the diagnostics binary so the
  # allocation gate executes in CI.
  add_custom_target(
    tess_bench_render_delta_thresholds
    COMMAND
      tess_bench_diagnostics
      --benchmark_filter=render_delta/.*
      --benchmark_repetitions=${TESS_BENCHMARK_GATE_REPETITIONS}
      --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/render-delta.json
      --benchmark_out_format=json
    COMMAND
      "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/benchmark_thresholds.py"
      --results ${CMAKE_CURRENT_BINARY_DIR}/render-delta.json
      --thresholds ${TESS_RENDER_DELTA_BENCHMARK_THRESHOLDS}
    DEPENDS tess_bench_diagnostics
    COMMENT "Running render_delta benchmark threshold checks"
    VERBATIM
  )

  add_custom_target(
    tess_bench_parallel_thresholds
    COMMAND
      tess_bench
      --benchmark_filter=parallel/.*
      --benchmark_repetitions=${TESS_BENCHMARK_GATE_REPETITIONS}
      --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/parallel.json
      --benchmark_out_format=json
    COMMAND
      "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/benchmark_thresholds.py"
      --results ${CMAKE_CURRENT_BINARY_DIR}/parallel.json
      --thresholds ${TESS_PARALLEL_BENCHMARK_THRESHOLDS}
    DEPENDS tess_bench
    COMMENT "Running parallel benchmark threshold checks"
    VERBATIM
  )

  add_custom_target(
    tess_bench_diagnostics_thresholds
    COMMAND
      tess_bench_diagnostics
      --benchmark_filter=diagnostics/.*
      --benchmark_repetitions=${TESS_BENCHMARK_GATE_REPETITIONS}
      --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/diagnostics.json
      --benchmark_out_format=json
    COMMAND
      "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/benchmark_thresholds.py"
      --results ${CMAKE_CURRENT_BINARY_DIR}/diagnostics.json
      --thresholds ${TESS_DIAGNOSTICS_BENCHMARK_THRESHOLDS}
    DEPENDS tess_bench_diagnostics
    COMMENT "Running diagnostics benchmark threshold checks"
    VERBATIM
  )

  # One home for the gated family list. CI used to loop over eighteen
  # hand-written family names, so a new family with a manifest and a
  # target but no workflow edit simply never gated, and every test still
  # passed. Deriving the set from the targets actually defined here means
  # a new family joins the gate by existing.
  #
  # BUILDSYSTEM_TARGETS is read after every threshold target is declared;
  # it does not see targets added later in this file.
  get_property(tess_directory_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  set(tess_threshold_targets)
  foreach(tess_candidate_target IN LISTS tess_directory_targets)
    if(tess_candidate_target MATCHES "^tess_bench_.+_thresholds$")
      list(APPEND tess_threshold_targets ${tess_candidate_target})
    endif()
  endforeach()

  if(NOT tess_threshold_targets)
    message(
      FATAL_ERROR
      "No tess_bench_*_thresholds targets were discovered. The aggregate "
      "gate would silently check nothing."
    )
  endif()

  # Timing gates must not run concurrently with each other. Callers build
  # this target with --parallel 1; the dependency edges alone do not
  # constrain ordering.
  add_custom_target(
    tess_bench_all_thresholds
    COMMENT "Running every benchmark threshold gate"
  )
  add_dependencies(tess_bench_all_thresholds ${tess_threshold_targets})
endif()

set(TESS_ECS_BASELINE_COMMAND)
if(TESS_ENABLE_ENTT OR TESS_ENABLE_FLECS)
  list(
    APPEND
    TESS_ECS_BASELINE_COMMAND
    COMMAND
      tess_bench_diagnostics
      --benchmark_filter=ecs/.*
      --benchmark_repetitions=${TESS_BENCHMARK_BASELINE_REPETITIONS}
      --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/ci-baselines/ecs.json
      --benchmark_out_format=json
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
    --benchmark_filter=block_pipeline/.*
    --benchmark_repetitions=${TESS_BENCHMARK_BASELINE_REPETITIONS}
    --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/ci-baselines/block-pipeline.json
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
  COMMAND
    tess_bench
    --benchmark_filter=topology/.*
    --benchmark_repetitions=${TESS_BENCHMARK_BASELINE_REPETITIONS}
    --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/ci-baselines/topology.json
    --benchmark_out_format=json
  COMMAND
    tess_bench
    --benchmark_filter=parallel/.*
    --benchmark_repetitions=${TESS_BENCHMARK_BASELINE_REPETITIONS}
    --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/ci-baselines/parallel.json
    --benchmark_out_format=json
  COMMAND
    tess_bench
    --benchmark_filter=scheduler/.*
    --benchmark_repetitions=${TESS_BENCHMARK_BASELINE_REPETITIONS}
    --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/ci-baselines/scheduler.json
    --benchmark_out_format=json
  COMMAND
    tess_bench
    --benchmark_filter=residency/.*
    --benchmark_repetitions=${TESS_BENCHMARK_BASELINE_REPETITIONS}
    --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/ci-baselines/residency.json
    --benchmark_out_format=json
  COMMAND
    tess_bench
    --benchmark_filter=maintenance/.*
    --benchmark_repetitions=${TESS_BENCHMARK_BASELINE_REPETITIONS}
    --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/ci-baselines/maintenance.json
    --benchmark_out_format=json
  COMMAND
    tess_bench
    --benchmark_filter=persistence/.*
    --benchmark_repetitions=${TESS_BENCHMARK_BASELINE_REPETITIONS}
    --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/ci-baselines/persistence.json
    --benchmark_out_format=json
  COMMAND
    tess_bench
    --benchmark_filter=query/.*
    --benchmark_repetitions=${TESS_BENCHMARK_BASELINE_REPETITIONS}
    --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/ci-baselines/query.json
    --benchmark_out_format=json
  COMMAND
    tess_bench
    --benchmark_filter=spatial/.*
    --benchmark_repetitions=${TESS_BENCHMARK_BASELINE_REPETITIONS}
    --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/ci-baselines/spatial.json
    --benchmark_out_format=json
  COMMAND
    tess_bench_diagnostics
    --benchmark_filter=diagnostics/.*
    --benchmark_repetitions=${TESS_BENCHMARK_BASELINE_REPETITIONS}
    --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/ci-baselines/diagnostics.json
    --benchmark_out_format=json
  # ecs/ runs when either adapter is enabled. It uses the diagnostics binary
  # to match the threshold target and, for EnTT, assert allocation-free steady
  # state.
  ${TESS_ECS_BASELINE_COMMAND}
  # render_delta/ and fields/ use the diagnostics binary; fields carries the
  # warm-build allocation gate.
  COMMAND
    tess_bench_diagnostics
    --benchmark_filter=render_delta/.*
    --benchmark_repetitions=${TESS_BENCHMARK_BASELINE_REPETITIONS}
    --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/ci-baselines/render-delta.json
    --benchmark_out_format=json
  COMMAND
    tess_bench_diagnostics
    --benchmark_filter=fields/.*
    --benchmark_repetitions=${TESS_BENCHMARK_BASELINE_REPETITIONS}
    --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/ci-baselines/fields.json
    --benchmark_out_format=json
  DEPENDS tess_bench tess_bench_diagnostics
  COMMENT "Collecting non-gating CI benchmark baseline samples"
  VERBATIM
)

if(BUILD_TESTING)
  # Compile-and-run smoke for the budgeted-progress runner (design
  # section 14.2 CI scope: compile/smoke, correctness validation, and
  # artifact emission — never a timing gate). Smoke mode still runs
  # every cell's pre-timing oracle/determinism validation.
  add_test(
    NAME tess_bench_budgeted_progress_smoke
    COMMAND
      tess_bench_budgeted_progress
      --smoke
      --out-dir ${CMAKE_CURRENT_BINARY_DIR}
  )
  set_tests_properties(tess_bench_budgeted_progress_smoke PROPERTIES
    FIXTURES_SETUP budgeted_smoke_artifacts)

  # The emitted smoke artifacts must pass the fail-closed schema
  # validator (design section 14.2 puts schema validation in CI
  # scope), not just the validator's synthetic-fixture unit tests.
  find_package(Python3 QUIET COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    set(budgeted_smoke_artifact_paths "")
    foreach(budgeted_cell astar_unit field_product resumable_work
            astar_arrival_60 astar_arrival_600 astar_arrival_2400
            astar_arrival_9600 astar_arrival_paced_600 astar_capacity
            colony_topology queued_per_chunk)
      foreach(budgeted_ns 125000 500000 2000000 8000000)
        list(APPEND budgeted_smoke_artifact_paths
          "${CMAKE_CURRENT_BINARY_DIR}/${budgeted_cell}_${budgeted_ns}ns.json")
      endforeach()
    endforeach()
    set(budgeted_counter_dir "${CMAKE_CURRENT_BINARY_DIR}/counter-pass")
    file(MAKE_DIRECTORY "${budgeted_counter_dir}")
    add_test(
      NAME tess_bench_budgeted_progress_counter_smoke
      COMMAND
        tess_bench_budgeted_progress_diagnostics
        --smoke
        --pass counter
        --out-dir ${budgeted_counter_dir}
    )
    set_tests_properties(tess_bench_budgeted_progress_counter_smoke
      PROPERTIES FIXTURES_SETUP budgeted_counter_artifacts)
    set(budgeted_counter_artifact_paths "")
    foreach(budgeted_cell astar_unit field_product resumable_work
            astar_arrival_60 astar_arrival_600)
      foreach(budgeted_ns 125000 500000 2000000 8000000)
        list(APPEND budgeted_counter_artifact_paths
          "${budgeted_counter_dir}/${budgeted_cell}_${budgeted_ns}ns.json")
      endforeach()
    endforeach()
    add_test(
      NAME tess_bench_budgeted_progress_counter_artifacts_valid
      COMMAND
        Python3::Interpreter
        ${PROJECT_SOURCE_DIR}/tools/check_budgeted_artifacts.py
        ${budgeted_counter_artifact_paths}
    )
    set_tests_properties(tess_bench_budgeted_progress_counter_artifacts_valid
      PROPERTIES FIXTURES_REQUIRED budgeted_counter_artifacts)
    add_test(
      NAME tess_bench_budgeted_progress_pass_comparison
      COMMAND
        Python3::Interpreter
        ${PROJECT_SOURCE_DIR}/tools/compare_budgeted_passes.py
        --timing-dir ${CMAKE_CURRENT_BINARY_DIR}
        --counter-dir ${budgeted_counter_dir}
        --smoke-report-only
    )
    set_tests_properties(tess_bench_budgeted_progress_pass_comparison
      PROPERTIES FIXTURES_REQUIRED
      "budgeted_smoke_artifacts;budgeted_counter_artifacts")
    add_test(
      NAME tess_bench_budgeted_progress_smoke_artifacts_valid
      COMMAND
        Python3::Interpreter
        ${PROJECT_SOURCE_DIR}/tools/check_budgeted_artifacts.py
        ${budgeted_smoke_artifact_paths}
    )
    set_tests_properties(tess_bench_budgeted_progress_smoke_artifacts_valid
      PROPERTIES FIXTURES_REQUIRED budgeted_smoke_artifacts)
  endif()

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

  add_test(
    NAME tess_bench_topology_smoke
    COMMAND
      tess_bench
      --benchmark_filter=topology/precheck_unreachable_512x512
      --benchmark_min_time=0.001
  )

  add_test(
    NAME tess_bench_residency_smoke
    COMMAND
      tess_bench
      --benchmark_filter=residency/eviction_churn_64
      --benchmark_min_time=0.001
  )

  add_test(
    NAME tess_bench_parallel_smoke
    COMMAND
      tess_bench
      --benchmark_filter=parallel/chunk_fill_pool_w2
      --benchmark_min_time=0.001
  )

  add_test(
    NAME tess_bench_diagnostics_family_smoke
    COMMAND
      tess_bench_diagnostics
      --benchmark_filter=diagnostics/trace_record
      --benchmark_min_time=0.001
  )
endif()
