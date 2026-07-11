include(TessGoogleDeps)
include(GoogleTest)

tess_require_googletest()

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

gtest_discover_tests(tess_allocation_counter_test)

add_executable(tess_smoke tess_smoke.cc)
target_link_libraries(tess_smoke PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_smoke)

gtest_discover_tests(tess_smoke)

add_executable(tess_shape_test allocation_counter.cc tess_shape_test.cc)
target_link_libraries(tess_shape_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_shape_test)

gtest_discover_tests(tess_shape_test)

add_executable(tess_storage_test allocation_counter.cc tess_storage_test.cc)
target_link_libraries(tess_storage_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_storage_test)

gtest_discover_tests(tess_storage_test)

add_executable(tess_residency_test allocation_counter.cc tess_residency_test.cc)
target_link_libraries(tess_residency_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_residency_test)

gtest_discover_tests(tess_residency_test)

add_executable(tess_block_test allocation_counter.cc tess_block_test.cc)
target_link_libraries(tess_block_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_block_test)

gtest_discover_tests(tess_block_test)

add_executable(tess_queued_test allocation_counter.cc tess_queued_test.cc)
target_link_libraries(tess_queued_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_queued_test)

gtest_discover_tests(tess_queued_test)

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

gtest_discover_tests(tess_phase_executor_test)

add_executable(
  tess_queued_results_test allocation_counter.cc tess_queued_results_test.cc)
target_link_libraries(
  tess_queued_results_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_queued_results_test)

gtest_discover_tests(tess_queued_results_test)

add_executable(tess_movement_class_test tess_movement_class_test.cc)
target_link_libraries(
  tess_movement_class_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_movement_class_test)

gtest_discover_tests(tess_movement_class_test)

add_executable(tess_path_movement_class_test tess_path_movement_class_test.cc)
target_link_libraries(
  tess_path_movement_class_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_path_movement_class_test)

gtest_discover_tests(tess_path_movement_class_test)

add_executable(tess_topology_movement_test tess_topology_movement_test.cc
                                            allocation_counter.cc)
target_link_libraries(
  tess_topology_movement_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_topology_movement_test)

gtest_discover_tests(tess_topology_movement_test)

add_executable(tess_topology_test tess_topology_test.cc)
target_link_libraries(tess_topology_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_topology_test)

gtest_discover_tests(tess_topology_test)

add_executable(tess_topology_sparse_test tess_topology_sparse_test.cc)
target_link_libraries(
  tess_topology_sparse_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_topology_sparse_test)

gtest_discover_tests(tess_topology_sparse_test)

add_executable(
  tess_path_precheck_test allocation_counter.cc tess_path_precheck_test.cc)
target_link_libraries(
  tess_path_precheck_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_path_precheck_test)

gtest_discover_tests(tess_path_precheck_test)

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

gtest_discover_tests(tess_path_precheck_runtime_test)

add_executable(tess_path_runtime_sparse_test tess_path_runtime_sparse_test.cc)
target_link_libraries(
  tess_path_runtime_sparse_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_path_runtime_sparse_test)

gtest_discover_tests(tess_path_runtime_sparse_test)

add_executable(tess_path_test allocation_counter.cc tess_path_test.cc)
target_link_libraries(tess_path_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_path_test)

gtest_discover_tests(tess_path_test)

add_executable(tess_path_view_test allocation_counter.cc tess_path_view_test.cc)
target_link_libraries(tess_path_view_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_path_view_test)

gtest_discover_tests(tess_path_view_test)

add_executable(tess_path_search_test tess_path_search_test.cc)
target_link_libraries(
  tess_path_search_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_path_search_test)

gtest_discover_tests(tess_path_search_test)

add_executable(tess_path_cache_test tess_path_cache_test.cc)
target_link_libraries(tess_path_cache_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_path_cache_test)

gtest_discover_tests(tess_path_cache_test)

add_executable(tess_path_product_test tess_path_product_test.cc)
target_link_libraries(
  tess_path_product_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_path_product_test)

gtest_discover_tests(tess_path_product_test)

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

gtest_discover_tests(tess_path_runtime_test)

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

gtest_discover_tests(tess_path_weighted_batch_test)

add_executable(tess_path_portal_route_test tess_path_portal_route_test.cc)
target_link_libraries(
  tess_path_portal_route_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_path_portal_route_test)

gtest_discover_tests(tess_path_portal_route_test)

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

gtest_discover_tests(tess_path_agent_test)

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

gtest_discover_tests(tess_path_agent_tick_test)

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

gtest_discover_tests(tess_ecs_adapter_test)

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

gtest_discover_tests(tess_render_delta_frame_test)

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

  gtest_discover_tests(tess_ecs_entt_test)
endif()

add_executable(tess_sim_auto_exec_test tess_sim_auto_exec_test.cc)
target_link_libraries(
  tess_sim_auto_exec_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_sim_auto_exec_test)

gtest_discover_tests(tess_sim_auto_exec_test)

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

gtest_discover_tests(tess_sim_schedule_test)

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

gtest_discover_tests(tess_sim_scheduler_test)

add_executable(tess_assert_test tess_assert_test.cc)
target_link_libraries(tess_assert_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_assert_test)

gtest_discover_tests(tess_assert_test)

add_executable(tess_diagnostics_default_test tess_diagnostics_default_test.cc)
target_link_libraries(
  tess_diagnostics_default_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_diagnostics_default_test)

gtest_discover_tests(tess_diagnostics_default_test)

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

gtest_discover_tests(tess_diagnostics_enabled_test)

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

gtest_discover_tests(tess_diagnostics_trace_test)

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

gtest_discover_tests(tess_diagnostics_panels_test)
