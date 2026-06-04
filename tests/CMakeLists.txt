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
