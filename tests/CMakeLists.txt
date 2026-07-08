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

add_executable(tess_topology_test tess_topology_test.cc)
target_link_libraries(tess_topology_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_topology_test)

gtest_discover_tests(tess_topology_test)

add_executable(tess_path_test allocation_counter.cc tess_path_test.cc)
target_link_libraries(tess_path_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_path_test)

gtest_discover_tests(tess_path_test)

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
