# test_workload_matrix.py

- `tests/test_workload_matrix.py`: pins the workload catalog's exactly-one-rule
  classification, dimensions, vocabulary, selector retirement, universe
  sources, and fail-closed diagnostics. Dimension tokens in a benchmark name
  must agree with the generated cell; partial captures and contradictory
  defaults fail. Dead rules, stale overrides, and selectors that now match a
  measured cell must be retired rather than silently accumulating.
