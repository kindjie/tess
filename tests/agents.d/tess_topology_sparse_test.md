# tess_topology_sparse_test

- `tess_topology_sparse_test`: pins sparse region graphs as resident-count-
  sized, residency-generation-bound products with truthful `Indeterminate`
  versus `Unreachable` results. Incremental updates equal fresh builds, and an
  allocation failure may invalidate the graph but never leave torn derived
  state. Local build of a missing chunk must not access sparse storage.
