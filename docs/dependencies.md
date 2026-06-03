# Dependencies

## GoogleTest

- Version: `v1.17.0`
- Documentation: https://google.github.io/googletest/
- CMake quickstart:
  https://google.github.io/googletest/quickstart-cmake.html
- Release: https://github.com/google/googletest/releases/tag/v1.17.0

Used for C++ unit tests.

## Google Benchmark

- Version: `v1.9.5`
- Documentation: https://google.github.io/benchmark/
- Repository and releases: https://github.com/google/benchmark

Used for opt-in C++ benchmarks.

On macOS, benchmark configure or execution may warn that pthread affinity or CPU
frequency metadata is unavailable. Those warnings do not prevent benchmark
measurements from running.
