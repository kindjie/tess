# test_cmake_compatibility.py

- `tests/test_cmake_compatibility.py`: pins the CMake 3.25 floor, newer feature
  branches, presets, dependency population, and adopter-facing consumer builds.
  Git dependencies share an exact-revision shallow helper that retries the
  complete fetch/checkout sequence and scrubs inherited `GIT_DIR`-family
  state. Incompatible options fail before an inherited compiler launcher is
  needed. The `consumer` preset remains consumer-shaped rather than inheriting
  development facilities.
