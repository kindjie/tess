# tess_path_precheck_test

- `tess_path_precheck_test`: pins the pre-A* topology verdicts and their class,
  provider, freshness, and sparse-residency binding. Only `Unreachable`
  licenses skipping A*; invalid, missing, stale, or mismatched graph states do
  not. Warm queries allocate nothing.
