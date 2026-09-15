cmake_minimum_required(VERSION 3.25)
project(tess_compatibility_consumer LANGUAGES CXX)
find_package(tess CONFIG REQUIRED)
add_executable(tess_snapshot_consumer consumer.cc)
target_link_libraries(tess_snapshot_consumer PRIVATE tess::tess)
add_executable(tess_snapshot_archive_consumer archive_consumer.cc)
target_link_libraries(tess_snapshot_archive_consumer PRIVATE tess::tess)
enable_testing()
add_test(NAME tess_snapshot_consumer COMMAND tess_snapshot_consumer)
add_test(NAME tess_snapshot_archive_consumer
  COMMAND tess_snapshot_archive_consumer "${TESS_SNAPSHOT_DIR}"
)
