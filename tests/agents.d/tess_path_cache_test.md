# tess_path_cache_test

- `tess_path_cache_test`: verifies path-cache eviction and indexing, including
  the portal segment-cache budget (stale-entry sweeps that compact both the
  entry list and the path-node arena across repeated world edits, sweep
  removal of stale same-request duplicates, insertion-order eviction of live
  entries at budget, immediate eviction when a budget is lowered, movement-
  class-bound views with safe whole-cache rebinding, and sweep/eviction/stale-
  rejection/rebind stats), strong allocation-failure guarantees for store and
  stale compaction (unchanged live entries, paths, and observable statistics,
  plus safe retry), and the route
  cache's hash-indexed lookups (first-stored-entry-wins suffix determinism
  pinned against the pre-index linear scan, exact hits under aliased
  low-bit coordinate patterns, entry/path-node cap breaches invalidating the
  whole cache immediately when caps are lowered and on later stores, with a
  `cap_invalidations` stat, single oversized routes
  skipped without eviction via `oversized_skips`, and cap 0 disabling
  storage).
