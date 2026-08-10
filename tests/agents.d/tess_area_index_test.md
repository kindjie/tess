# tess_area_index_test

- `tess_area_index_test`: verifies caller-keyed grouping of region-graph
  regions into area summaries, deterministic area identities and adjacency,
  coordinate lookup, zero-key region omission with incident portals skipped,
  monotonic graph-revision invalidation, no-op update stability, dense and
  sparse graphs including non-contiguous resident keys and sorted-offset
  lookup, and warm allocation-free rebuilds.
