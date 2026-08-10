# test_cmake_compatibility.py

- `tests/test_cmake_compatibility.py`: regression coverage for the supported
  CMake floor. It simulates the 3.25 and 3.28 feature branches, proving that
  3.25 omits module scanning and `FetchContent(EXCLUDE_FROM_ALL)` while newer
  CMake retains both build-hygiene options, and pins the root project and
  preset minimum versions together (including the `3.25...3.28` policy
  range). It requires all Git-fetched dependencies to use the shared,
  exact-revision shallow population helper, and behaviorally pins a retry of
  the complete fetch/checkout sequence after checkout failure, that the
  populator scrubs inherited `GIT_DIR`-family hook environment before
  running Git, and that population failures report every attempt's error. It
  also proves incompatible external-data options fail before an inherited
  compiler launcher is needed, and pins the `consumer` preset
  consumer-shaped: dev facilities and EnTT off, no warnings-as-errors, no
  inheritance from a dev preset. Flecs dependency defaults preserve explicit
  parent cache choices and reject an incompatible static-target choice. The
  network-free `examples` preset and
  tracked installed-package and `FetchContent` consumer fixtures are covered
  as adopter-facing build contracts. Opted-in exception-free executables are
  required to join build-all so registered CTest cases cannot name unbuilt
  targets; the standalone-header verifier remains explicitly targeted.
