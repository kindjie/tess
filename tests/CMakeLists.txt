include(TessGoogleDeps)
include(GoogleTest)

tess_require_googletest()

# Every discovered test carries a target:<name> label plus the
# subsystem:<dir> labels of its forward impact set — the subsystems
# whose changes must run it (redesign sections 3.7 and 6; the pre-push
# hook selects tests by these labels, and tests/test_git_hooks.py
# checks the mapping). ALWAYS adds prepush:always for tests that run
# on every selective push.
function(tess_discover_tests target)
  cmake_parse_arguments(ARG "ALWAYS" "PREFIX" "CONFIGS;LABELS" ${ARGN})
  set(labels "target:${target}")
  if(ARG_ALWAYS)
    list(APPEND labels "prepush:always")
  endif()
  foreach(subsystem IN LISTS ARG_LABELS)
    list(APPEND labels "subsystem:${subsystem}")
  endforeach()
  foreach(config IN LISTS ARG_CONFIGS)
    list(APPEND labels "config:${config}")
  endforeach()
  # gtest_discover_tests forwards PROPERTIES through an unquoted
  # expansion: CMake 3.x splices a bare list (LABELS keeps only the
  # first element; the rest become property NAMES), while CMake 4
  # forwards it intact. Escaping the separators is stable on both,
  # at the cost of the labels landing as ONE ;-joined label string —
  # the pre-push selection regex matches labels delimiter-aware
  # ((^|;)label(;|$)) so both forms select identically.
  string(REPLACE ";" "\\;" labels "${labels}")
  set(discovery_options)
  if(ARG_PREFIX)
    list(APPEND discovery_options TEST_PREFIX "${ARG_PREFIX}")
  endif()
  gtest_discover_tests(
    ${target}
    ${discovery_options}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 30
    PROPERTIES TIMEOUT 60 LABELS "${labels}"
  )
endfunction()

function(tess_add_test test_name target)
  cmake_parse_arguments(ARG "" "ARGUMENT" "CONFIGS;LABELS" ${ARGN})
  set(labels "target:${target}")
  foreach(subsystem IN LISTS ARG_LABELS)
    list(APPEND labels "subsystem:${subsystem}")
  endforeach()
  foreach(config IN LISTS ARG_CONFIGS)
    list(APPEND labels "config:${config}")
  endforeach()
  add_test(NAME "${test_name}" COMMAND "${target}" "${ARG_ARGUMENT}")
  set_tests_properties(
    "${test_name}" PROPERTIES TIMEOUT 60 LABELS "${labels}")
endfunction()

add_executable(
  tess_allocation_counter_test
  allocation_counter.cc
  tess_allocation_counter_test.cc
)
target_link_libraries(
  tess_allocation_counter_test
  PRIVATE GTest::gtest_main
)
tess_apply_project_options(tess_allocation_counter_test)

tess_discover_tests(tess_allocation_counter_test ALWAYS)

add_executable(tess_smoke tess_smoke.cc)
target_link_libraries(tess_smoke PRIVATE tess::tess GTest::gtest_main)
target_compile_definitions(
  tess_smoke
  PRIVATE
    TESS_EXPECTED_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    TESS_EXPECTED_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    TESS_EXPECTED_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)
tess_apply_project_options(tess_smoke)

tess_discover_tests(tess_smoke LABELS block core diagnostics ecs experimental gpu ops path persistence query sim spatial storage topology)

if(TESS_BUILD_NO_EXCEPTIONS_TESTING AND
   (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" OR MSVC))
  add_executable(tess_no_exceptions_test tess_no_exceptions_test.cc)
  target_link_libraries(tess_no_exceptions_test PRIVATE tess::tess)
  target_compile_definitions(
    tess_no_exceptions_test PRIVATE TESS_INTERNAL_CAPACITY_TESTING)
  tess_apply_project_options(tess_no_exceptions_test)
  tess_target_disable_exceptions(tess_no_exceptions_test)
  tess_add_test(
    TessNoExceptions.AggregateHeaderRunsStorageAndBlockOperations
    tess_no_exceptions_test
    ARGUMENT AggregateHeaderRunsStorageAndBlockOperations
    CONFIGS noexceptions
    LABELS block core storage
  )
  tess_add_test(
    TessNoExceptions.CheckedBlockCapacityFailurePreservesStorage
    tess_no_exceptions_test
    ARGUMENT CheckedBlockCapacityFailurePreservesStorage
    CONFIGS noexceptions
    LABELS block
  )
  tess_add_test(
    TessNoExceptions.LegacyBlockCapacityFailureAborts
    tess_no_exceptions_test
    ARGUMENT LegacyBlockCapacityFailureAborts
    CONFIGS noexceptions
    LABELS block
  )
  tess_add_test(
    TessNoExceptions.LegacyPortalCapacityFailureAborts
    tess_no_exceptions_test
    ARGUMENT LegacyPortalCapacityFailureAborts
    CONFIGS noexceptions
    LABELS path
  )
  tess_add_test(
    TessNoExceptions.OrdinaryCallbacksRunThroughThreadedExecutors
    tess_no_exceptions_test
    ARGUMENT OrdinaryCallbacksRunThroughThreadedExecutors
    CONFIGS noexceptions
    LABELS ops
  )
  tess_add_test(
    TessNoExceptions.MaintenanceSchedulersRunOrdinaryTasks
    tess_no_exceptions_test
    ARGUMENT MaintenanceSchedulersRunOrdinaryTasks
    CONFIGS noexceptions
    LABELS experimental
  )
  tess_add_test(
    TessNoExceptions.PathfindingAndTopologyRunThroughAggregateHeaders
    tess_no_exceptions_test
    ARGUMENT PathfindingAndTopologyRunThroughAggregateHeaders
    CONFIGS noexceptions
    LABELS path topology
  )
  tess_add_test(
    TessNoExceptions.ScheduleRunsOrdinaryTypeErasedCallback
    tess_no_exceptions_test
    ARGUMENT ScheduleRunsOrdinaryTypeErasedCallback
    CONFIGS noexceptions
    LABELS sim
  )
  tess_add_test(
    TessNoExceptions.AutoExecRunsOrdinaryKernelThroughPool
    tess_no_exceptions_test
    ARGUMENT AutoExecRunsOrdinaryKernelThroughPool
    CONFIGS noexceptions
    LABELS ops sim storage
  )
  tess_add_test(
    TessNoExceptions.PortalCacheCheckedReservesRejectCapacity
    tess_no_exceptions_test
    ARGUMENT PortalCacheCheckedReservesRejectCapacity
    CONFIGS noexceptions
    LABELS path
  )
endif()

add_executable(tess_uint128_surface_test tess_uint128_surface_test.cc)
target_link_libraries(
  tess_uint128_surface_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_uint128_surface_test)

tess_discover_tests(tess_uint128_surface_test LABELS core)

add_executable(tess_shape_test allocation_counter.cc tess_shape_test.cc)
target_link_libraries(tess_shape_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_shape_test)

tess_discover_tests(tess_shape_test LABELS core)

add_executable(
  tess_grid_benchmark_harness_test
  tess_grid_benchmark_harness_test.cc
)
target_link_libraries(
  tess_grid_benchmark_harness_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_grid_benchmark_harness_test)

tess_discover_tests(tess_grid_benchmark_harness_test LABELS core path storage topology)

add_executable(
  tess_grid_map_generators_test
  tess_grid_map_generators_test.cc
)
target_link_libraries(
  tess_grid_map_generators_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_grid_map_generators_test)

tess_discover_tests(tess_grid_map_generators_test LABELS core path storage topology)

add_executable(
  tess_colony_harness_test
  tess_colony_harness_test.cc
)
target_link_libraries(
  tess_colony_harness_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_colony_harness_test)

tess_discover_tests(tess_colony_harness_test LABELS block core diagnostics ops path sim storage topology)

add_executable(
  tess_budgeted_progress_test
  tess_budgeted_progress_test.cc
)
target_link_libraries(
  tess_budgeted_progress_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_budgeted_progress_test)

tess_discover_tests(tess_budgeted_progress_test LABELS diagnostics ops sim)

add_executable(
  tess_sparse_stream_test
  tess_sparse_stream_test.cc
)
target_link_libraries(
  tess_sparse_stream_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_sparse_stream_test)

tess_discover_tests(tess_sparse_stream_test LABELS core diagnostics path storage topology)

# Consumer-contract probes: three translation units that include the
# public headers in declaration order, reverse order, and leaf-first
# with no umbrella, linked together so duplicate symbols and lost
# inlines fail the build, then compared for cross-unit identity.
add_executable(
  tess_consumer_contract_test
  tess_consumer_contract_test.cc
  consumer_contract/forward_tu.cc
  consumer_contract/reverse_tu.cc
  consumer_contract/leaf_first_tu.cc
)
target_include_directories(
  tess_consumer_contract_test
  PRIVATE "${TESS_HEADER_SWEEP_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(
  tess_consumer_contract_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_consumer_contract_test)

tess_discover_tests(tess_consumer_contract_test LABELS block core debug diagnostics ecs experimental gpu ops path persistence query sim spatial storage topology)

if(TESS_BUILD_NO_EXCEPTIONS_TESTING AND
   (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" OR MSVC))
  add_executable(
    tess_no_exceptions_consumer_contract_test
    no_exceptions_consumer_contract_main.cc
    consumer_contract/forward_tu.cc
    consumer_contract/reverse_tu.cc
    consumer_contract/leaf_first_tu.cc
  )
  target_include_directories(
    tess_no_exceptions_consumer_contract_test
    PRIVATE "${TESS_HEADER_SWEEP_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  target_link_libraries(
    tess_no_exceptions_consumer_contract_test PRIVATE tess::tess)
  tess_apply_project_options(tess_no_exceptions_consumer_contract_test)
  tess_target_disable_exceptions(tess_no_exceptions_consumer_contract_test)
  tess_add_test(
    TessNoExceptions.ConsumerContract
    tess_no_exceptions_consumer_contract_test
    CONFIGS noexceptions
    LABELS block core debug diagnostics ecs experimental gpu ops
      path persistence query sim spatial storage topology
  )
endif()

add_executable(tess_property_test tess_property_test.cc)
target_link_libraries(tess_property_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_property_test)

add_executable(tess_dirty_property_test tess_dirty_property_test.cc)
target_link_libraries(
  tess_dirty_property_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_dirty_property_test)

add_executable(tess_cache_property_test tess_cache_property_test.cc)
target_link_libraries(
  tess_cache_property_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_cache_property_test)

add_executable(tess_queued_property_test tess_queued_property_test.cc)
target_link_libraries(
  tess_queued_property_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_queued_property_test)

tess_discover_tests(tess_property_test LABELS core ops sim storage)
tess_discover_tests(tess_queued_property_test LABELS core ops storage)
tess_discover_tests(tess_cache_property_test LABELS core path storage)
tess_discover_tests(tess_dirty_property_test LABELS core ops storage)

# Macro-configuration matrix (redesign section 10 item 5). Each cell is
# a compile-and-link target, not a test: the contract is that the
# configuration builds at all. The mixed-adapter cells are the coverage
# no preset provides -- dev presets pin EnTT and Flecs both on, consumer
# presets pin both off.
# Each cell is an EXECUTABLE built from two identically configured
# translation units plus a main that calls into both. An object library
# would only compile: it takes a link step for a definition that should
# be inline and is not to show up as a duplicate symbol.
function(tess_add_macro_cell name)
  cmake_parse_arguments(
    CELL "" "" "DEFINES;INCLUDES;LINKS;OPTIONS" ${ARGN})
  add_executable(
    ${name}
    consumer_contract/macro_cell.cc
    consumer_contract/macro_cell_second.cc
    consumer_contract/macro_cell_main.cc
  )
  target_include_directories(
    ${name} PRIVATE "${TESS_HEADER_SWEEP_DIR}" ${CELL_INCLUDES})
  target_link_libraries(${name} PRIVATE tess::tess ${CELL_LINKS})
  target_compile_definitions(${name} PRIVATE ${CELL_DEFINES})
  target_compile_options(${name} PRIVATE ${CELL_OPTIONS})
  tess_apply_project_options(${name})
endfunction()

# The consumer default: no optional integration, no diagnostics.
tess_add_macro_cell(tess_contract_cell_bare)

# Assertions compiled out, the configuration a release consumer ships.
tess_add_macro_cell(tess_contract_cell_ndebug DEFINES NDEBUG)

# Diagnostics on without any adapter.
tess_add_macro_cell(
  tess_contract_cell_diagnostics DEFINES TESS_ENABLE_DIAGNOSTICS)

if(TESS_ENABLE_ENTT)
  tess_add_macro_cell(
    tess_contract_cell_entt_only
    DEFINES TESS_CONTRACT_CELL_ENTT_ONLY TESS_ENABLE_ENTT
    LINKS EnTT::EnTT
  )
endif()

if(TESS_ENABLE_FLECS)
  tess_add_macro_cell(
    tess_contract_cell_flecs_only
    DEFINES TESS_CONTRACT_CELL_FLECS_ONLY TESS_ENABLE_FLECS
    LINKS flecs::flecs_static
  )
endif()

# ImGui and WebGPU reuse the existing stubs, so the matrix needs no new
# dependency.
tess_add_macro_cell(
  tess_contract_cell_imgui
  DEFINES TESS_CONTRACT_CELL_IMGUI TESS_ENABLE_IMGUI TESS_ENABLE_DIAGNOSTICS
  INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/imgui_stub"
)

tess_add_macro_cell(
  tess_contract_cell_webgpu
  DEFINES TESS_CONTRACT_CELL_WEBGPU TESS_ENABLE_WEBGPU
  INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/webgpu_stub"
)

# One aggregate so CI and the pre-push hook build every cell together.
add_custom_target(
  tess_contract_cells
  DEPENDS
    tess_contract_cell_bare
    tess_contract_cell_ndebug
    tess_contract_cell_diagnostics
    tess_contract_cell_imgui
    tess_contract_cell_webgpu
)
if(TESS_ENABLE_ENTT)
  add_dependencies(tess_contract_cells tess_contract_cell_entt_only)
endif()
if(TESS_ENABLE_FLECS)
  add_dependencies(tess_contract_cells tess_contract_cell_flecs_only)
endif()

if(TESS_BUILD_NO_EXCEPTIONS_TESTING AND
   (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" OR MSVC))
  tess_add_macro_cell(
    tess_no_exceptions_cell_bare
    DEFINES TESS_EXPECT_NO_EXCEPTIONS
  )
  tess_target_disable_exceptions(tess_no_exceptions_cell_bare)
  tess_add_macro_cell(
    tess_no_exceptions_cell_ndebug
    DEFINES NDEBUG TESS_EXPECT_NO_EXCEPTIONS
  )
  tess_target_disable_exceptions(tess_no_exceptions_cell_ndebug)
  tess_add_macro_cell(
    tess_no_exceptions_cell_diagnostics
    DEFINES TESS_ENABLE_DIAGNOSTICS TESS_EXPECT_NO_EXCEPTIONS
  )
  tess_target_disable_exceptions(tess_no_exceptions_cell_diagnostics)
  tess_add_macro_cell(
    tess_no_exceptions_cell_imgui
    DEFINES
      TESS_CONTRACT_CELL_IMGUI
      TESS_ENABLE_IMGUI
      TESS_ENABLE_DIAGNOSTICS
      TESS_EXPECT_NO_EXCEPTIONS
    INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/imgui_stub"
  )
  tess_target_disable_exceptions(tess_no_exceptions_cell_imgui)
  tess_add_macro_cell(
    tess_no_exceptions_cell_webgpu
    DEFINES
      TESS_CONTRACT_CELL_WEBGPU
      TESS_ENABLE_WEBGPU
      TESS_EXPECT_NO_EXCEPTIONS
    INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/webgpu_stub"
  )
  tess_target_disable_exceptions(tess_no_exceptions_cell_webgpu)
  add_custom_target(
    tess_no_exceptions_contract_cells
    DEPENDS
      tess_no_exceptions_cell_bare
      tess_no_exceptions_cell_ndebug
      tess_no_exceptions_cell_diagnostics
      tess_no_exceptions_cell_imgui
      tess_no_exceptions_cell_webgpu
  )
  if(TESS_ENABLE_ENTT)
    tess_add_macro_cell(
      tess_no_exceptions_cell_entt
      DEFINES
        TESS_CONTRACT_CELL_ENTT_ONLY
        TESS_ENABLE_ENTT
        TESS_EXPECT_NO_EXCEPTIONS
      LINKS EnTT::EnTT
    )
    tess_target_disable_exceptions(tess_no_exceptions_cell_entt)
    add_dependencies(
      tess_no_exceptions_contract_cells tess_no_exceptions_cell_entt)
  endif()
  if(TESS_ENABLE_FLECS)
    tess_add_macro_cell(
      tess_no_exceptions_cell_flecs
      DEFINES
        TESS_CONTRACT_CELL_FLECS_ONLY
        TESS_ENABLE_FLECS
        TESS_EXPECT_NO_EXCEPTIONS
      LINKS flecs::flecs_static
    )
    tess_target_disable_exceptions(tess_no_exceptions_cell_flecs)
    add_dependencies(
      tess_no_exceptions_contract_cells tess_no_exceptions_cell_flecs)
  endif()
endif()

if(TESS_ENABLE_GRID_BENCHMARK_DATA)
  set(_tess_grid_manifest
      "${PROJECT_SOURCE_DIR}/tools/grid_benchmark_manifest.json")
  file(READ "${_tess_grid_manifest}" _tess_grid_manifest_json)
  string(
    JSON _tess_grid_entry_count
    LENGTH "${_tess_grid_manifest_json}" entries
  )
  string(
    JSON _tess_grid_rights_status
    GET "${_tess_grid_manifest_json}" content_rights status
  )
  if(_tess_grid_entry_count GREATER 0 AND
     NOT _tess_grid_rights_status STREQUAL "cleared")
    message(
      FATAL_ERROR
        "Grid benchmark entries require content_rights.status=cleared"
    )
  endif()
  if(TESS_GRID_BENCHMARK_DATA_DIR STREQUAL "")
    if(DEFINED ENV{XDG_CACHE_HOME} AND NOT "$ENV{XDG_CACHE_HOME}" STREQUAL "")
      set(_tess_grid_cache_root "$ENV{XDG_CACHE_HOME}")
    elseif(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
      set(_tess_grid_cache_root "$ENV{HOME}/.cache")
    else()
      set(_tess_grid_cache_root "${CMAKE_CURRENT_BINARY_DIR}/cache")
    endif()
    string(
      JSON _tess_grid_revision
      GET "${_tess_grid_manifest_json}" upstream revision
    )
    set(
      _tess_grid_data_dir
      "${_tess_grid_cache_root}/tess/benchmark-data/${_tess_grid_revision}"
    )
  else()
    set(_tess_grid_data_dir "${TESS_GRID_BENCHMARK_DATA_DIR}")
  endif()
  file(TO_CMAKE_PATH "${_tess_grid_data_dir}" _tess_grid_data_dir)

  add_executable(
    tess_grid_benchmark_data_test
    tess_grid_benchmark_data_test.cc
  )
  target_link_libraries(
    tess_grid_benchmark_data_test PRIVATE GTest::gtest_main)
  target_compile_definitions(
    tess_grid_benchmark_data_test
    PRIVATE
      TESS_GRID_BENCHMARK_ENTRY_COUNT=${_tess_grid_entry_count}
      TESS_GRID_BENCHMARK_DATA_REQUIRED=$<BOOL:${TESS_REQUIRE_GRID_BENCHMARK_DATA}>
      TESS_GRID_BENCHMARK_CACHE_VERIFIER_READY=0
      "TESS_GRID_BENCHMARK_DATA_DIR=\"${_tess_grid_data_dir}\""
  )
  tess_apply_project_options(tess_grid_benchmark_data_test)

  tess_discover_tests(tess_grid_benchmark_data_test)
endif()

add_executable(tess_storage_test allocation_counter.cc tess_storage_test.cc)
target_link_libraries(tess_storage_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_storage_test)

tess_discover_tests(tess_storage_test LABELS core storage topology)

add_executable(tess_residency_test allocation_counter.cc tess_residency_test.cc)
target_link_libraries(tess_residency_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_residency_test)

tess_discover_tests(tess_residency_test LABELS core path storage topology)

add_executable(tess_block_test allocation_counter.cc tess_block_test.cc)
target_link_libraries(tess_block_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_block_test)

tess_discover_tests(tess_block_test LABELS block core storage)

add_executable(
  tess_query_span_test allocation_counter.cc tess_query_span_test.cc)
target_link_libraries(tess_query_span_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_query_span_test)

tess_discover_tests(tess_query_span_test LABELS core query)

add_executable(
  tess_block_pipeline_test allocation_counter.cc tess_block_pipeline_test.cc)
target_link_libraries(
  tess_block_pipeline_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_block_pipeline_test)

tess_discover_tests(tess_block_pipeline_test LABELS block core storage)

add_executable(
  tess_maintenance_test allocation_counter.cc tess_maintenance_test.cc)
target_link_libraries(
  tess_maintenance_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_maintenance_test)

tess_discover_tests(tess_maintenance_test LABELS core diagnostics experimental)

add_executable(
  tess_queued_planning_test
  allocation_counter.cc
  tess_queued_planning_test.cc
)
target_link_libraries(
  tess_queued_planning_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_queued_planning_test)

tess_discover_tests(tess_queued_planning_test LABELS block core ops storage)

add_executable(
  tess_queued_planning_index_test
  tess_queued_planning_index_test.cc
)
target_link_libraries(
  tess_queued_planning_index_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_queued_planning_index_test)

tess_discover_tests(
  tess_queued_planning_index_test
  LABELS
  block
  core
  ops
  storage
)

add_executable(tess_queued_test allocation_counter.cc tess_queued_test.cc)
target_link_libraries(tess_queued_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_queued_test)

tess_discover_tests(tess_queued_test LABELS block core ops storage)

add_executable(
  tess_queued_contract_test
  allocation_counter.cc
  tess_queued_contract_test.cc
)
target_link_libraries(
  tess_queued_contract_test
  PRIVATE tess::tess GTest::gtest_main
)
target_compile_definitions(
  tess_queued_contract_test PRIVATE TESS_INTERNAL_CAPACITY_TESTING)
tess_apply_project_options(tess_queued_contract_test)

tess_discover_tests(tess_queued_contract_test LABELS block core ops path sim storage topology)

add_executable(
  tess_execution_phase_safety_test
  tess_execution_phase_safety_test.cc
)
target_link_libraries(
  tess_execution_phase_safety_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_execution_phase_safety_test)

tess_discover_tests(tess_execution_phase_safety_test LABELS block core ops storage)

add_executable(
  tess_phase_executor_test
  allocation_counter.cc
  tess_phase_executor_test.cc
)
target_link_libraries(
  tess_phase_executor_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_phase_executor_test)

tess_discover_tests(tess_phase_executor_test LABELS core ops)

add_executable(
  tess_queued_results_test allocation_counter.cc tess_queued_results_test.cc)
target_link_libraries(
  tess_queued_results_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_queued_results_test)

tess_discover_tests(tess_queued_results_test LABELS block core ops storage)

add_executable(tess_movement_class_test tess_movement_class_test.cc)
target_link_libraries(
  tess_movement_class_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_movement_class_test)

tess_discover_tests(tess_movement_class_test LABELS core storage topology)

add_executable(tess_transition_model_test tess_transition_model_test.cc)
target_link_libraries(
  tess_transition_model_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_transition_model_test)

tess_discover_tests(tess_transition_model_test LABELS core storage topology)

add_executable(tess_path_movement_class_test tess_path_movement_class_test.cc)
target_link_libraries(
  tess_path_movement_class_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_path_movement_class_test)

tess_discover_tests(tess_path_movement_class_test LABELS core path sim storage topology)

add_executable(tess_topology_movement_test tess_topology_movement_test.cc
                                            allocation_counter.cc)
target_link_libraries(
  tess_topology_movement_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_topology_movement_test)

tess_discover_tests(tess_topology_movement_test LABELS core path storage topology)

add_executable(
  tess_topology_test
  allocation_counter.cc
  tess_topology_test.cc
)
target_link_libraries(tess_topology_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_topology_test)

tess_discover_tests(tess_topology_test LABELS core storage topology)

add_executable(
  tess_topology_coarse_path_test
  allocation_counter.cc
  tess_topology_coarse_path_test.cc
)
target_link_libraries(
  tess_topology_coarse_path_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_topology_coarse_path_test)

tess_discover_tests(tess_topology_coarse_path_test LABELS core storage topology)

add_executable(
  tess_topology_sparse_test
  allocation_counter.cc
  tess_topology_sparse_test.cc)
target_link_libraries(
  tess_topology_sparse_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_topology_sparse_test)

tess_discover_tests(tess_topology_sparse_test LABELS core storage topology)

add_executable(
  tess_path_precheck_test allocation_counter.cc tess_path_precheck_test.cc)
target_link_libraries(
  tess_path_precheck_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_path_precheck_test)

tess_discover_tests(tess_path_precheck_test LABELS core path storage topology)

add_executable(
  tess_path_precheck_runtime_test
  allocation_counter.cc
  tess_path_precheck_runtime_test.cc
)
target_link_libraries(
  tess_path_precheck_runtime_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_path_precheck_runtime_test)

tess_discover_tests(tess_path_precheck_runtime_test LABELS core path sim storage topology)

add_executable(tess_path_runtime_sparse_test tess_path_runtime_sparse_test.cc)
target_link_libraries(
  tess_path_runtime_sparse_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_path_runtime_sparse_test)

tess_discover_tests(tess_path_runtime_sparse_test LABELS core path sim storage topology)

add_executable(tess_path_test allocation_counter.cc tess_path_test.cc)
target_link_libraries(tess_path_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_path_test)

tess_discover_tests(tess_path_test LABELS core path storage topology)

add_executable(tess_path_view_test allocation_counter.cc tess_path_view_test.cc)
target_link_libraries(tess_path_view_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_path_view_test)

tess_discover_tests(tess_path_view_test LABELS core path)

add_executable(tess_path_search_test tess_path_search_test.cc)
target_link_libraries(
  tess_path_search_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_path_search_test)

tess_discover_tests(tess_path_search_test LABELS core path sim storage topology)

add_executable(
  tess_path_cache_test
  allocation_counter.cc
  tess_path_cache_test.cc
)
target_link_libraries(tess_path_cache_test PRIVATE tess::tess GTest::gtest_main)
target_compile_definitions(
  tess_path_cache_test PRIVATE TESS_INTERNAL_CAPACITY_TESTING)
tess_apply_project_options(tess_path_cache_test)

tess_discover_tests(tess_path_cache_test LABELS core path storage topology)

add_executable(tess_route_cache_scoped_test tess_route_cache_scoped_test.cc)
target_link_libraries(
  tess_route_cache_scoped_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_route_cache_scoped_test)

tess_discover_tests(tess_route_cache_scoped_test LABELS core path storage)

add_executable(tess_path_product_test tess_path_product_test.cc)
target_link_libraries(
  tess_path_product_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_path_product_test)

tess_discover_tests(tess_path_product_test LABELS core path storage topology)

add_executable(
  tess_weighted_field_product_test
  allocation_counter.cc
  tess_weighted_field_product_test.cc
)
target_link_libraries(
  tess_weighted_field_product_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_weighted_field_product_test)

tess_discover_tests(tess_weighted_field_product_test LABELS core path storage topology)

add_executable(
  tess_area_index_test allocation_counter.cc tess_area_index_test.cc)
target_link_libraries(
  tess_area_index_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_area_index_test)

tess_discover_tests(tess_area_index_test LABELS core spatial storage topology)

add_executable(
  tess_tactical_assignment_test
  allocation_counter.cc
  tess_tactical_assignment_test.cc
)
target_link_libraries(
  tess_tactical_assignment_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_tactical_assignment_test)

tess_discover_tests(tess_tactical_assignment_test LABELS core spatial)

add_executable(
  tess_local_coordination_test
  allocation_counter.cc
  tess_local_coordination_test.cc
)
target_link_libraries(
  tess_local_coordination_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_local_coordination_test)

tess_discover_tests(tess_local_coordination_test LABELS core spatial)

add_executable(
  tess_persistence_test
  tess_persistence_test.cc
)
target_link_libraries(tess_persistence_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_persistence_test)

tess_discover_tests(tess_persistence_test LABELS core path persistence storage topology)

add_executable(
  tess_joint_movement_test
  allocation_counter.cc
  tess_joint_movement_test.cc
)
target_link_libraries(
  tess_joint_movement_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_joint_movement_test)

tess_discover_tests(tess_joint_movement_test LABELS core path sim storage topology)

add_executable(
  tess_pibt_movement_test
  allocation_counter.cc
  tess_pibt_movement_test.cc
)
target_link_libraries(
  tess_pibt_movement_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_pibt_movement_test)

tess_discover_tests(tess_pibt_movement_test LABELS core path sim storage topology)

add_executable(
  tess_path_runtime_test
  allocation_counter.cc
  tess_path_runtime_test.cc
)
target_link_libraries(
  tess_path_runtime_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_path_runtime_test)

tess_discover_tests(tess_path_runtime_test LABELS core path storage topology)

add_executable(
  tess_path_weighted_batch_test
  allocation_counter.cc
  tess_path_weighted_batch_test.cc
)
target_link_libraries(
  tess_path_weighted_batch_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_path_weighted_batch_test)

tess_discover_tests(tess_path_weighted_batch_test LABELS core path storage topology)

add_executable(tess_path_portal_route_test tess_path_portal_route_test.cc)
target_link_libraries(
  tess_path_portal_route_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_path_portal_route_test)

tess_discover_tests(tess_path_portal_route_test LABELS core path storage topology)

add_executable(
  tess_portal_first_replan_test
  tess_portal_first_replan_test.cc
)
target_link_libraries(
  tess_portal_first_replan_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_portal_first_replan_test)

tess_discover_tests(tess_portal_first_replan_test LABELS core path topology)

add_executable(
  tess_seam_scan_equivalence_test
  tess_seam_scan_equivalence_test.cc
)
target_link_libraries(
  tess_seam_scan_equivalence_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_seam_scan_equivalence_test)

tess_discover_tests(tess_seam_scan_equivalence_test LABELS core path)

add_executable(
  tess_path_agent_test
  allocation_counter.cc
  tess_path_agent_test.cc
)
target_link_libraries(
  tess_path_agent_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_path_agent_test)

tess_discover_tests(tess_path_agent_test LABELS core path sim storage topology)

add_executable(
  tess_path_agent_tick_test
  allocation_counter.cc
  tess_path_agent_tick_test.cc
)
target_link_libraries(
  tess_path_agent_tick_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_path_agent_tick_test)

tess_discover_tests(tess_path_agent_tick_test LABELS core path sim storage topology)

add_executable(
  tess_ecs_adapter_test
  allocation_counter.cc
  tess_ecs_adapter_test.cc
)
target_link_libraries(
  tess_ecs_adapter_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_ecs_adapter_test)

tess_discover_tests(tess_ecs_adapter_test LABELS core ecs path sim storage topology)

add_executable(tess_gpu_interface_test tess_gpu_interface_test.cc)
target_link_libraries(
  tess_gpu_interface_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_gpu_interface_test)

tess_discover_tests(tess_gpu_interface_test LABELS core gpu storage)

# Compiles the optional WebGPU backend against an API-matching stable C stub.
add_executable(tess_webgpu_backend_test tess_webgpu_backend_test.cc)
target_link_libraries(
  tess_webgpu_backend_test
  PRIVATE tess::tess GTest::gtest_main
)
target_include_directories(
  tess_webgpu_backend_test
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/webgpu_stub"
)
target_compile_definitions(
  tess_webgpu_backend_test
  PRIVATE TESS_ENABLE_WEBGPU
)
tess_apply_project_options(tess_webgpu_backend_test)

tess_discover_tests(tess_webgpu_backend_test LABELS core gpu storage)

add_executable(
  tess_render_delta_frame_test
  allocation_counter.cc
  tess_render_delta_frame_test.cc
)
target_link_libraries(
  tess_render_delta_frame_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_render_delta_frame_test)

tess_discover_tests(tess_render_delta_frame_test LABELS core ecs path sim storage topology)

if(TESS_ENABLE_ENTT)
  include(TessEnttDeps)
  tess_require_entt()

  add_executable(
    tess_ecs_entt_test
    allocation_counter.cc
    tess_ecs_entt_test.cc
  )
  target_link_libraries(
    tess_ecs_entt_test
    PRIVATE tess::tess EnTT::EnTT GTest::gtest_main
  )
  # The header gate is a per-target macro by design (never a global
  # definition): only EnTT-linked targets may compile the adapter body.
  target_compile_definitions(tess_ecs_entt_test PRIVATE TESS_ENABLE_ENTT)
  tess_apply_project_options(tess_ecs_entt_test)

  tess_discover_tests(tess_ecs_entt_test LABELS core ecs path sim storage topology)
endif()

if(TESS_ENABLE_FLECS)
  include(TessFlecsDeps)
  tess_require_flecs()

  add_executable(
    tess_ecs_flecs_test
    allocation_counter.cc
    tess_ecs_flecs_test.cc
  )
  target_link_libraries(
    tess_ecs_flecs_test
    PRIVATE tess::tess flecs::flecs_static GTest::gtest_main
  )
  # The header gate is target-local: only Flecs-linked targets compile it.
  target_compile_definitions(tess_ecs_flecs_test PRIVATE TESS_ENABLE_FLECS)
  tess_apply_project_options(tess_ecs_flecs_test)

  tess_discover_tests(tess_ecs_flecs_test LABELS core ecs path sim storage topology)
endif()

add_executable(tess_sim_auto_exec_test tess_sim_auto_exec_test.cc)
target_link_libraries(
  tess_sim_auto_exec_test PRIVATE tess::tess GTest::gtest_main)
target_compile_definitions(
  tess_sim_auto_exec_test PRIVATE TESS_INTERNAL_CAPACITY_TESTING)
tess_apply_project_options(tess_sim_auto_exec_test)

tess_discover_tests(tess_sim_auto_exec_test LABELS block core ops sim storage)

add_executable(
  tess_sim_schedule_test
  allocation_counter.cc
  tess_sim_schedule_test.cc
)
target_link_libraries(
  tess_sim_schedule_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_sim_schedule_test)

tess_discover_tests(tess_sim_schedule_test LABELS core diagnostics sim)

add_executable(
  tess_sim_scheduler_test
  allocation_counter.cc
  tess_sim_scheduler_test.cc
)
target_link_libraries(
  tess_sim_scheduler_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_sim_scheduler_test)

tess_discover_tests(tess_sim_scheduler_test LABELS block core ops path sim storage topology)

add_executable(tess_assert_test tess_assert_test.cc)
target_link_libraries(tess_assert_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_assert_test)

tess_discover_tests(tess_assert_test LABELS core path sim storage)

# The same source with assertions compiled out. Three preconditions in this
# file were TESS_ASSERT and are now unconditional fail_fast checks; with
# asserts on, their death tests pass either way, so only this cell can tell
# the two apart. The existing NDEBUG contract cell compiles but never runs,
# so it cannot serve here.
add_executable(tess_assert_ndebug_test tess_assert_test.cc)
target_link_libraries(
  tess_assert_ndebug_test
  PRIVATE tess::tess GTest::gtest_main
)
target_compile_definitions(tess_assert_ndebug_test PRIVATE NDEBUG)
tess_apply_project_options(tess_assert_ndebug_test)

# Prefixed: this target compiles the same source as tess_assert_test, so
# without one both cells register identical gtest names and a failure
# report cannot say which configuration failed -- which is the only
# thing this cell exists to tell you.
tess_discover_tests(
  tess_assert_ndebug_test
  PREFIX ndebug.
  LABELS core path sim storage
)

add_executable(tess_diagnostics_default_test tess_diagnostics_default_test.cc)
target_link_libraries(
  tess_diagnostics_default_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_diagnostics_default_test)

tess_discover_tests(tess_diagnostics_default_test LABELS diagnostics)

add_executable(
  tess_diagnostics_enabled_test
  tess_diagnostics_enabled_test.cc
  ../bench/tess_diagnostics_alloc_hooks.cc
)
target_link_libraries(
  tess_diagnostics_enabled_test
  PRIVATE tess::tess GTest::gtest_main
)
target_compile_definitions(
  tess_diagnostics_enabled_test
  PRIVATE TESS_ENABLE_DIAGNOSTICS
)
tess_apply_project_options(tess_diagnostics_enabled_test)

tess_discover_tests(tess_diagnostics_enabled_test LABELS block core diagnostics ops path storage topology)

add_executable(
  tess_diagnostics_trace_test
  tess_diagnostics_trace_test.cc
  ../bench/tess_diagnostics_alloc_hooks.cc
)
target_link_libraries(
  tess_diagnostics_trace_test
  PRIVATE tess::tess GTest::gtest_main
)
target_compile_definitions(
  tess_diagnostics_trace_test
  PRIVATE TESS_ENABLE_DIAGNOSTICS
)
tess_apply_project_options(tess_diagnostics_trace_test)

tess_discover_tests(tess_diagnostics_trace_test LABELS block core diagnostics ops storage)

# Compile-checks the opt-in ImGui panels against a minimal ImGui stub (no real
# Dear ImGui dependency), with both diagnostics and imgui gates on.
add_executable(
  tess_diagnostics_panels_test
  tess_diagnostics_panels_test.cc
)
target_link_libraries(
  tess_diagnostics_panels_test
  PRIVATE tess::tess GTest::gtest_main
)
target_include_directories(
  tess_diagnostics_panels_test
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/imgui_stub"
)
target_compile_definitions(
  tess_diagnostics_panels_test
  PRIVATE TESS_ENABLE_DIAGNOSTICS TESS_ENABLE_IMGUI
)
tess_apply_project_options(tess_diagnostics_panels_test)

tess_discover_tests(tess_diagnostics_panels_test LABELS core debug diagnostics storage)

# Compile-checks the opt-in world tools with the imgui gate on and diagnostics
# deliberately off, proving that tooling does not depend on diagnostics state.
add_executable(
  tess_imgui_tools_test
  tess_imgui_tools_test.cc
)
target_link_libraries(
  tess_imgui_tools_test
  PRIVATE tess::tess GTest::gtest_main
)
target_include_directories(
  tess_imgui_tools_test
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/imgui_stub"
)
target_compile_definitions(
  tess_imgui_tools_test
  PRIVATE TESS_ENABLE_IMGUI
)
tess_apply_project_options(tess_imgui_tools_test)

tess_discover_tests(tess_imgui_tools_test LABELS core debug storage)

add_executable(
  tess_flow_accounting_test
  tess_flow_accounting_test.cc
)
target_link_libraries(
  tess_flow_accounting_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_flow_accounting_test)

tess_discover_tests(tess_flow_accounting_test LABELS core diagnostics experimental ops path sim storage topology)

# Deterministic counter-golden probe (redesign section 3.3, shadow mode):
# a gtest-free executable emits observed PathCounters/QueuedPhaseCounters
# values, and the python checker compares them against the committed
# golden. Shadow mode reports drift without gating; setting
# TESS_COUNTER_GOLDENS_STRICT=1 in the environment makes drift fail
# (the redesign's phase 4 promotion). The checker only registers when a
# python interpreter exists, so consumers without one lose the check,
# never the build.
add_executable(
  tess_counter_golden_probe
  tess_counter_golden_probe.cc
)
target_link_libraries(
  tess_counter_golden_probe
  PRIVATE tess::tess
)
target_compile_definitions(
  tess_counter_golden_probe
  PRIVATE TESS_ENABLE_DIAGNOSTICS
)
tess_apply_project_options(tess_counter_golden_probe)

add_test(
  NAME TessCounterGoldenProbe
  COMMAND tess_counter_golden_probe
          "${CMAKE_CURRENT_BINARY_DIR}/counter-observed.json"
)
set_tests_properties(
  TessCounterGoldenProbe
  PROPERTIES FIXTURES_SETUP TessCounterGoldens LABELS "prepush:always"
)

find_package(Python3 QUIET COMPONENTS Interpreter)
if(NOT Python3_Interpreter_FOUND)
  message(STATUS
    "tess: no python3 interpreter; counter-golden check unavailable")
endif()
if(Python3_Interpreter_FOUND)
  add_test(
    NAME TessCounterGoldenCheck
    COMMAND
      Python3::Interpreter
      "${PROJECT_SOURCE_DIR}/tools/check_counter_goldens.py"
      --observed "${CMAKE_CURRENT_BINARY_DIR}/counter-observed.json"
      --golden "${CMAKE_CURRENT_SOURCE_DIR}/goldens/counters.json"
      --drift-report "${CMAKE_CURRENT_BINARY_DIR}/counter-golden-drift.md"
  )
  set_tests_properties(
    TessCounterGoldenCheck
    PROPERTIES FIXTURES_REQUIRED TessCounterGoldens
               LABELS "prepush:always"
  )
endif()
