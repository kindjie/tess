# tess_path_precheck_test

- `tess_path_precheck_test`: pins the pre-A* topology verdicts and their class,
  provider, freshness, missing-chunk policy, and sparse-residency binding. Only
  `Unreachable` licenses skipping A*: unknown boundaries remain `MissingChunk`
  by default but become policy-relative `Unreachable` when
  `AssumeImpassable` is explicit; non-resident endpoints remain invalid. Stale
  or mismatched graph states do not prune. Warm queries allocate nothing.
