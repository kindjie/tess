# tess_topology_sparse_test

- `tess_topology_sparse_test`: verifies `RegionGraphT<SparseResident>`
  (`SparseRegionGraph`): building over only the resident chunk set (sized by
  resident count, not chunk count), reachability across resident chunks,
  `Indeterminate` across a non-resident boundary and for a non-resident
  endpoint, `Unreachable` for a fully-resident enclosed component (single-chunk
  world), sparse `update_region_graph` equivalence with a fresh build after a
  seam edit (graph-for-graph plus a reachability probe), safe allocation-
  failure invalidation without torn derived state, and the
  residency-generation staleness guard forcing a full rebuild after a chunk
  loads post-build. A direct local build for an in-range non-resident chunk
  returns `MissingChunk` without accessing sparse storage.
