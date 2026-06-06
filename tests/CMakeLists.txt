include(TessGoogleDeps)
include(GoogleTest)

tess_require_googletest()

add_executable(tess_smoke tess_smoke.cc)
target_link_libraries(tess_smoke PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_smoke)

gtest_discover_tests(tess_smoke)

add_executable(tess_shape_test tess_shape_test.cc)
target_link_libraries(tess_shape_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_shape_test)

gtest_discover_tests(tess_shape_test)

add_executable(tess_storage_test tess_storage_test.cc)
target_link_libraries(tess_storage_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_storage_test)

gtest_discover_tests(tess_storage_test)

add_executable(tess_block_test tess_block_test.cc)
target_link_libraries(tess_block_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_block_test)

gtest_discover_tests(tess_block_test)

add_executable(tess_queued_test tess_queued_test.cc)
target_link_libraries(tess_queued_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_queued_test)

gtest_discover_tests(tess_queued_test)

add_executable(tess_topology_test tess_topology_test.cc)
target_link_libraries(tess_topology_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_topology_test)

gtest_discover_tests(tess_topology_test)

add_executable(tess_path_test tess_path_test.cc)
target_link_libraries(tess_path_test PRIVATE tess::tess GTest::gtest_main)
tess_apply_project_options(tess_path_test)

gtest_discover_tests(tess_path_test)

add_executable(tess_path_runtime_test tess_path_runtime_test.cc)
target_link_libraries(
  tess_path_runtime_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_path_runtime_test)

gtest_discover_tests(tess_path_runtime_test)

add_executable(tess_path_agent_test tess_path_agent_test.cc)
target_link_libraries(
  tess_path_agent_test
  PRIVATE tess::tess GTest::gtest_main
)
tess_apply_project_options(tess_path_agent_test)

gtest_discover_tests(tess_path_agent_test)

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
