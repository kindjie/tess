include(TessGoogleDeps)

tess_require_google_benchmark()

add_executable(tess_bench tess_bench.cpp)
target_link_libraries(tess_bench PRIVATE tess::tess benchmark::benchmark_main)
