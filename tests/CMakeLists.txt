include(TessGoogleDeps)
include(GoogleTest)

tess_require_googletest()

add_executable(tess_smoke tess_smoke.cc)
target_link_libraries(tess_smoke PRIVATE tess::tess GTest::gtest_main)

gtest_discover_tests(tess_smoke)

add_executable(tess_shape_test tess_shape_test.cc)
target_link_libraries(tess_shape_test PRIVATE tess::tess GTest::gtest_main)

gtest_discover_tests(tess_shape_test)
