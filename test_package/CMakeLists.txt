cmake_minimum_required(VERSION 3.25)

project(tess_conan_consumer LANGUAGES CXX)

find_package(tess CONFIG REQUIRED)

add_executable(tess_conan_consumer main.cc)
target_link_libraries(tess_conan_consumer PRIVATE tess::tess)
