# tess_path_test

- `tess_path_test`: verifies the MVP A* path foundation, including top-down 2D
  paths around blocked tiles, invalid start and goal reporting, no-path
  reporting, direct-path and uniform-cost fast paths across top-down 2D,
  vertical 2D, and 3D layouts, coordinate support, exact route-cache and
  same-goal suffix reuse, explicit cache clearing, invalidation, and
  world-version invalidation, route-cache hit/suffix-hit span validity across
  later misses that grow cache storage (hits copy into the caller scratch),
  explicit chunk-version dependency tracking, exact weighted route-product
  replay and dependency invalidation, shared-goal, supplied-waypoint, and
  chunk-boundary portal route-product replay and dependency invalidation,
  chunk-boundary portal candidate counters, warmed portal segment-cache
  reuse, segment-cache `lookup_append` hit/miss/stale semantics including
  junction-node stitching and untouched caller storage on miss, stale
  segment rejection and caller-managed clear, failed-segment cache bypass,
  shared-goal distance-field builds and reconstruction, unit-cost multi-goal
  distance-field products, nearest-target reconstruction, product
  stale-version rejection, byte-budgeted field-product cache
  hit/miss/eviction/stale stats, move-only field-product store without
  world-sized copies, strong allocation-failure insertion guarantees,
  oversized-store rejection preserving existing entries, zero byte budget,
  same-key replacement byte accounting, least-recently-used
  (not insertion-order) eviction,
  distance-field error-status families
  (InvalidGoal/InvalidStart/empty `GoalSet` across plain, weighted, and
  product builds and reconstruction, with no garbage field or path left
  behind), local-domain weighted field bounds, mismatched-field rejection,
  weighted entry-cost routing, weighted direct and detour fast paths,
  weighted shared-goal fields, bounded weighted field builds and fallback,
  weighted batch grouping, per-request fallback from a global field overflow,
  endpoint validation, and allocation-free repeated queries with pre-reserved
  path scratch.
