# tess_block_pipeline_test

- `tess_block_pipeline_test`: verifies block-preserving lazy tile sources,
  filter/map/flat-map composition, for-each and reduce terminals, explicit
  bounded frontier/sequence materialization with overflow reporting, the
  deliberately named allocating terminal, fused/materialized equivalence,
  policy-qualified mutation, reference-preserving `flat_map`, diagnostics,
  deterministic ordering, temporary `BlockCtx` ownership, and a
  zero-allocation fused warm path.
