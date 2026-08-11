# tess_route_cache_scoped_test

- `tess_route_cache_scoped_test`: pins `ScopedFeasible` route-cache staleness
  against seeded legal-route and truthful-cost oracles. The tombstone cases are
  deliberately sized to avoid index growth bypassing the target branch, and
  compaction while serving is exercised at an entry cap of one to catch
  reference invalidation. Non-Found results remain whole-world-sensitive;
  oversized individual footprints are skipped, while aggregate dependency-cap
  breaches invalidate the cache like other caps.
