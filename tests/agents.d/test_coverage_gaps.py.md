# test_coverage_gaps.py

- `tests/test_coverage_gaps.py`: pytest coverage for the benchmark
  coverage gap-finder (`tools/coverage_gaps.py`). It pins the
  declared-public-header inventory (parsed from
  `set(TESS_PUBLIC_HEADERS ...)`, excluding implementation headers and
  including the generated `version.h`), gap classification (executed
  versus zero-covered-regions versus absent-from-export, the latter
  because never-included headers and uninstantiated templates produce
  no llvm-cov file row at all), best-coverage merging across multiple
  exports, resolved-prefix matching (lookalike dependency paths
  containing `include/tess/` do not count), subsystem summaries, the
  exact-header known-gaps manifest (acknowledged gaps render
  separately; entries whose header gained coverage or no longer exists
  are reported stale; duplicates fail), advisory zero-exit with gaps
  present, fail-closed behavior on malformed inputs and unwritable
  outputs, markdown report ordering, and the `ctest-objects` mode that
  lists deduplicated instrumented test executables for llvm-cov
  `-object` arguments — including gtest launcher commands where the
  binary hides in a `TEST_EXECUTABLE=` argument.
