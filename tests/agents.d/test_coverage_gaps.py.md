# test_coverage_gaps.py

- `tests/test_coverage_gaps.py`: pins public-header inventory, llvm-cov gap
  classification and merging, known-gap retirement, reports, and instrumented
  CTest object discovery. Absent export rows are distinct from zero-covered
  regions because an uninstantiated or never-included header produces no row.
  Lookalike dependency paths cannot count as project headers; stale or duplicate
  known gaps fail rather than remaining permanent acknowledgements.
