# tess_path_cache_test

- `tess_path_cache_test`: pins portal-segment and route-cache budget,
  compaction, indexing, staleness, and statistics contracts. Allocation
  failure during store or compaction must preserve live paths and observable
  statistics for a safe retry. Route suffix lookup deliberately retains the
  pre-index first-stored-entry behavior; cap breaches invalidate the whole
  cache, but one oversized route is skipped without disturbing residents.
