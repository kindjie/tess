include(TessGoogleDeps)
include(GoogleTest)

tess_require_googletest()

function(tess_discover_tests target)
  gtest_discover_tests(
    ${target}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 30
    PROPERTIES TIMEOUT 60
  )
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

tess_discover_tests(tess_allocation_counter_test)

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

tess_discover_tests(tess_smoke)

add_executable(tess_shape_test allocation_counter.cc tess_shape_test.cc)
target_link_libraries(tess_shape_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_shape_test)

tess_discover_tests(tess_shape_test)

add_executable(
  tess_grid_benchmark_harness_test
  tess_grid_benchmark_harness_test.cc
)
target_link_libraries(
  tess_grid_benchmark_harness_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_grid_benchmark_harness_test)

tess_discover_tests(tess_grid_benchmark_harness_test)

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

tess_discover_tests(tess_storage_test)

add_executable(tess_residency_test allocation_counter.cc tess_residency_test.cc)
target_link_libraries(tess_residency_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_residency_test)

tess_discover_tests(tess_residency_test)

add_executable(tess_block_test allocation_counter.cc tess_block_test.cc)
target_link_libraries(tess_block_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_block_test)

tess_discover_tests(tess_block_test)

add_executable(
  tess_query_span_test allocation_counter.cc tess_query_span_test.cc)
target_link_libraries(tess_query_span_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_query_span_test)

tess_discover_tests(tess_query_span_test)

add_executable(
  tess_block_pipeline_test allocation_counter.cc tess_block_pipeline_test.cc)
target_link_libraries(
  tess_block_pipeline_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_block_pipeline_test)

tess_discover_tests(tess_block_pipeline_test)

add_executable(
  tess_maintenance_test allocation_counter.cc tess_maintenance_test.cc)
target_link_libraries(
  tess_maintenance_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_maintenance_test)

tess_discover_tests(tess_maintenance_test)

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

tess_discover_tests(tess_queued_planning_test)

add_executable(tess_queued_test allocation_counter.cc tess_queued_test.cc)
target_link_libraries(tess_queued_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_queued_test)

tess_discover_tests(tess_queued_test)

add_executable(
  tess_queued_contract_test
  allocation_counter.cc
  tess_queued_contract_test.cc
)
target_link_libraries(
  tess_queued_contract_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_queued_contract_test)

tess_discover_tests(tess_queued_contract_test)

add_executable(
  tess_execution_phase_safety_test
  tess_execution_phase_safety_test.cc
)
target_link_libraries(
  tess_execution_phase_safety_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_execution_phase_safety_test)

tess_discover_tests(tess_execution_phase_safety_test)

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

tess_discover_tests(tess_phase_executor_test)

add_executable(
  tess_queued_results_test allocation_counter.cc tess_queued_results_test.cc)
target_link_libraries(
  tess_queued_results_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_queued_results_test)

tess_discover_tests(tess_queued_results_test)

add_executable(tess_movement_class_test tess_movement_class_test.cc)
target_link_libraries(
  tess_movement_class_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_movement_class_test)

tess_discover_tests(tess_movement_class_test)

add_executable(tess_transition_model_test tess_transition_model_test.cc)
target_link_libraries(
  tess_transition_model_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_transition_model_test)

tess_discover_tests(tess_transition_model_test)

add_executable(tess_path_movement_class_test tess_path_movement_class_test.cc)
target_link_libraries(
  tess_path_movement_class_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_path_movement_class_test)

tess_discover_tests(tess_path_movement_class_test)

add_executable(tess_topology_movement_test tess_topology_movement_test.cc
                                            allocation_counter.cc)
target_link_libraries(
  tess_topology_movement_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_topology_movement_test)

tess_discover_tests(tess_topology_movement_test)

add_executable(tess_topology_test tess_topology_test.cc)
target_link_libraries(tess_topology_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_topology_test)

tess_discover_tests(tess_topology_test)

add_executable(
  tess_topology_coarse_path_test
  allocation_counter.cc
  tess_topology_coarse_path_test.cc
)
target_link_libraries(
  tess_topology_coarse_path_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_topology_coarse_path_test)

tess_discover_tests(tess_topology_coarse_path_test)

add_executable(tess_topology_sparse_test tess_topology_sparse_test.cc)
target_link_libraries(
  tess_topology_sparse_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_topology_sparse_test)

tess_discover_tests(tess_topology_sparse_test)

add_executable(
  tess_path_precheck_test allocation_counter.cc tess_path_precheck_test.cc)
target_link_libraries(
  tess_path_precheck_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_path_precheck_test)

tess_discover_tests(tess_path_precheck_test)

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

tess_discover_tests(tess_path_precheck_runtime_test)

add_executable(tess_path_runtime_sparse_test tess_path_runtime_sparse_test.cc)
target_link_libraries(
  tess_path_runtime_sparse_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_path_runtime_sparse_test)

tess_discover_tests(tess_path_runtime_sparse_test)

add_executable(tess_path_test allocation_counter.cc tess_path_test.cc)
target_link_libraries(tess_path_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_path_test)

tess_discover_tests(tess_path_test)

add_executable(tess_path_view_test allocation_counter.cc tess_path_view_test.cc)
target_link_libraries(tess_path_view_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_path_view_test)

tess_discover_tests(tess_path_view_test)

add_executable(tess_path_search_test tess_path_search_test.cc)
target_link_libraries(
  tess_path_search_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_path_search_test)

tess_discover_tests(tess_path_search_test)

add_executable(
  tess_path_cache_test
  allocation_counter.cc
  tess_path_cache_test.cc
)
target_link_libraries(tess_path_cache_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_path_cache_test)

tess_discover_tests(tess_path_cache_test)

add_executable(tess_path_product_test tess_path_product_test.cc)
target_link_libraries(
  tess_path_product_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_path_product_test)

tess_discover_tests(tess_path_product_test)

add_executable(
  tess_weighted_field_product_test
  allocation_counter.cc
  tess_weighted_field_product_test.cc
)
target_link_libraries(
  tess_weighted_field_product_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_weighted_field_product_test)

tess_discover_tests(tess_weighted_field_product_test)

add_executable(
  tess_area_index_test allocation_counter.cc tess_area_index_test.cc)
target_link_libraries(
  tess_area_index_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_area_index_test)

tess_discover_tests(tess_area_index_test)

add_executable(
  tess_tactical_assignment_test
  allocation_counter.cc
  tess_tactical_assignment_test.cc
)
target_link_libraries(
  tess_tactical_assignment_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_tactical_assignment_test)

tess_discover_tests(tess_tactical_assignment_test)

add_executable(
  tess_local_coordination_test
  allocation_counter.cc
  tess_local_coordination_test.cc
)
target_link_libraries(
  tess_local_coordination_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_local_coordination_test)

tess_discover_tests(tess_local_coordination_test)

add_executable(
  tess_persistence_test
  tess_persistence_test.cc
)
target_link_libraries(tess_persistence_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_persistence_test)

tess_discover_tests(tess_persistence_test)

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

tess_discover_tests(tess_path_runtime_test)

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

tess_discover_tests(tess_path_weighted_batch_test)

add_executable(tess_path_portal_route_test tess_path_portal_route_test.cc)
target_link_libraries(
  tess_path_portal_route_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_path_portal_route_test)

tess_discover_tests(tess_path_portal_route_test)

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

tess_discover_tests(tess_path_agent_test)

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

tess_discover_tests(tess_path_agent_tick_test)

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

tess_discover_tests(tess_ecs_adapter_test)

add_executable(tess_gpu_interface_test tess_gpu_interface_test.cc)
target_link_libraries(
  tess_gpu_interface_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_gpu_interface_test)

tess_discover_tests(tess_gpu_interface_test)

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

tess_discover_tests(tess_render_delta_frame_test)

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

  tess_discover_tests(tess_ecs_entt_test)
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

  tess_discover_tests(tess_ecs_flecs_test)
endif()

add_executable(tess_sim_auto_exec_test tess_sim_auto_exec_test.cc)
target_link_libraries(
  tess_sim_auto_exec_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_sim_auto_exec_test)

tess_discover_tests(tess_sim_auto_exec_test)

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

tess_discover_tests(tess_sim_schedule_test)

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

tess_discover_tests(tess_sim_scheduler_test)

add_executable(tess_assert_test tess_assert_test.cc)
target_link_libraries(tess_assert_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_assert_test)

tess_discover_tests(tess_assert_test)

add_executable(tess_diagnostics_default_test tess_diagnostics_default_test.cc)
target_link_libraries(
  tess_diagnostics_default_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_diagnostics_default_test)

tess_discover_tests(tess_diagnostics_default_test)

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

tess_discover_tests(tess_diagnostics_enabled_test)

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

tess_discover_tests(tess_diagnostics_trace_test)

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

tess_discover_tests(tess_diagnostics_panels_test)
