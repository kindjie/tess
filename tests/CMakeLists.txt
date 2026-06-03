include(TessGoogleDeps)
include(GoogleTest)

tess_require_googletest()

add_executable(tess_smoke tess_smoke.cpp)
target_link_libraries(tess_smoke PRIVATE tess::tess GTest::gtest_main)

gtest_discover_tests(tess_smoke)
