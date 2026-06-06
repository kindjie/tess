# Path Foundation

The current path layer is a minimal always-resident path foundation. It lives
in `include/tess/path/path.h` and is exported by `tess/tess.h`.

## Public Surface

- `PathRequest` contains a start and goal `Coord3`.
- `PathStatus` reports `Found`, `InvalidStart`, `InvalidGoal`, or `NoPath`.
- `PathResult` returns the status, unit movement cost, expanded-node count,
  reached-node count, and a non-owning span of path coordinates.
- `DistanceFieldResult` returns the status and node counts for a reverse
  shared-goal field build.
- `WeightedPathBatchStats` returns request count, unique-goal count, field
  build count, A* fallback count, and copied path-node count for weighted batch
  planning.
- `PathScratch` owns reusable vectors for open nodes, visited records, and the
  returned path. `reserve_nodes(count)` prepares storage for allocation-free
  repeated queries when capacity is sufficient.
- `DistanceFieldScratch` owns reusable vectors for reverse shared-goal fields
  and reconstructed paths. `reserve_nodes(count)` also prepares weighted
  bucket storage for allocation-free bounded weighted field rebuilds after
  warmup.
- `RouteCacheScratch` owns reusable route-cache entries and cached path nodes
  for exact route and same-goal suffix reuse. `invalidate()` drops cached route
  data while preserving hit/miss counters; `clear()` drops routes and resets
  counters. `capture_world_versions(world)` and
  `invalidate_if_world_changed(world)` provide coarse whole-cache invalidation
  from chunk version fingerprints.
- `ChunkVersionDependencies` records explicit chunk/version pairs and can
  validate whether those chunks are unchanged. It is supporting infrastructure
  for future route products; current route-cache hits still use conservative
  whole-cache invalidation.
- `WeightedRouteProduct` stores one verified weighted route plus the chunk
  versions touched by that route. Replaying it succeeds only while those chunk
  versions are unchanged.
- `WeightedPortalRouteProduct` stores a supplied-waypoint weighted route
  product. It stitches exact weighted A* segments through caller-provided
  portal waypoints, stores the resulting path, and validates chunk versions on
  replay. It also supports an automatic chunk-boundary portal builder for a
  first topology MVP.
- `WeightedPathBatchScratch` owns reusable search scratch and stable copied
  result paths for weighted batch planning.
- `astar_path<World, PassableTag>(world, request, scratch)` runs optimized
  unit-cost deterministic pathfinding over the existing always-resident world
  storage. The passability field is treated as boolean-like.
- `weighted_astar_path<World, PassableTag, CostTag>(world, request, scratch)`
  runs deterministic weighted A* over passability plus an integral entry-cost
  field. It includes exact unit-cost direct and blocked-axis detour fast paths
  when their local optimality proofs apply.
- `build_weighted_route_product<World, PassableTag, CostTag>(world, request,
  scratch, product)` builds and stores a weighted route product.
- `weighted_route_product_path(world, product)` replays a stored weighted
  route product if its chunk dependencies are still valid.
- `build_weighted_portal_route_product<World, PassableTag, CostTag>(world,
  request, waypoints, scratch, product)` builds a supplied-waypoint portal
  route product.
- `build_weighted_chunk_portal_route_product<World, PassableTag, CostTag>(
  world, request, scratch, product)` derives a simple axis-ordered route
  through adjacent chunk-boundary portals, then builds the same weighted portal
  route product.
- `weighted_portal_route_product_path(world, product)` replays a stored portal
  route product if its chunk dependencies are still valid.
- `weighted_path_batch<World, PassableTag, CostTag, MaxCost>(world, requests,
  scratch)` groups weighted requests by goal, builds bounded weighted fields
  for repeated goals, uses weighted A* for singleton goals, and returns stable
  result spans owned by `WeightedPathBatchScratch`.
- `cached_astar_path<World, PassableTag>(world, request, scratch, cache)`
  checks the route cache before falling back to `astar_path`.
- `build_distance_field<World, PassableTag>(world, goal, scratch)` builds a
  unit-cost reverse distance field from one passable goal.
- `distance_field_path<World, PassableTag>(world, start, goal, scratch)`
  reconstructs a start-to-goal path from the most recent matching field.
- `build_weighted_distance_field<World, PassableTag, CostTag>(world, goal,
  scratch)` builds a weighted reverse Dijkstra field for positive integral
  entry costs.
- `build_bounded_weighted_distance_field<World, PassableTag, CostTag,
  MaxCost>(world, goal, scratch)` builds the same exact weighted reverse field
  through a bounded bucket queue when all reached entry costs are between 1 and
  `MaxCost`. If it encounters a higher positive entry cost, it falls back to
  the general weighted field builder.
- `weighted_distance_field_path<World, PassableTag, CostTag>(world, start,
  goal, scratch)` reconstructs a weighted start-to-goal path from the most
  recent matching weighted field.

## Behavior

Movement uses six axis-adjacent candidates in fixed order:

```text
+x, -x, +y, -y, +z, -z
```

Candidates outside the compile-time shape are rejected through the existing
shape containment helpers. Degenerate axes naturally reject out-of-bounds
neighbors, so the same implementation covers top-down 2D, vertical 2D, and
small 3D worlds.

The default `astar_path` costs are unit-weighted. The heuristic is Manhattan
distance. Tie-breaking is deterministic by lower total score, then higher path
cost for equal-score nodes, then tile-key order. Preferring higher path cost on
equal-score nodes avoids open-grid wavefront expansion while preserving
shortest paths.

`weighted_astar_path` charges the destination tile's positive integral
entry cost for each move. The start tile's cost is not charged, but start and
goal costs must be positive. Zero-cost and negative signed-cost tiles are
treated as blocked, and oversized integral costs saturate to the public
32-bit path-cost range. Weighted A* uses a binary heap and Manhattan distance
with a minimum edge cost of one, so it preserves optimal weighted paths while
skipping the unit-cost-only bucket queue and route cache. It does include an
exact direct Manhattan fast path when every entered tile on a probed route has
cost 1; no positive-cost path can beat that Manhattan lower bound. For
axis-aligned routes where the straight line is blocked, it can also return a
one-tile parallel detour when every entered detour tile has cost 1; any
positive-cost path around the blocked line needs at least Manhattan+2 moves.

Before entering open-set A*, the implementation probes direct Manhattan
paths in the shape-relevant axis orders. If any route is fully passable, it
returns that direct shortest path immediately. If a direct probe hits a blocked
tile whose axis plane is fully blocked, it returns `NoPath` immediately because
the plane separates start from goal under the current axis-adjacent movement
model. For axis-aligned requests, a clear one-tile parallel detour is also
returned before A* because its Manhattan+2 cost is optimal when the straight
line is blocked. For top-down 2D requests blocked by a non-separating axis
plane, the implementation can scan that plane for the cheapest passable gap and
return a verified Manhattan route through it; the same logic applies to
vertical 2D layouts. It also handles 2D forced-gap sequences by walking toward
the goal, scanning a barrier line only when the next progress step is blocked,
and accepting only fully open lines or lines with exactly one passable gap. In
3D, a blocked direct route can scan the blocked axis plane for the cheapest
passable crossing and return a verified Manhattan route through it. Other
non-separating blockers fall back to normal A*.

The returned path span points into the supplied `PathScratch` and remains valid
until the next path query or scratch clear/reserve operation. Scratch keeps
dense per-tile state arrays and clears only nodes touched by the previous
query, so repeated queries avoid full-world scratch resets when the search
visits a small fraction of the world.

For many agents repeating unit-cost point-to-point routes, `RouteCacheScratch`
can amortize complete path searches. Exact `(start, goal)` hits return the
cached path without expanding nodes. Same-goal suffix hits are also supported
when the new start already appears inside a cached optimal path; with unit
positive edge costs, that suffix is also optimal. The cache assumes the caller
invalidates it when passability or movement rules change. The optional world
version fingerprint support is deliberately conservative: when any chunk
version changes, `invalidate_if_world_changed(world)` drops the whole cache and
preserves hit/miss counters. It does not attempt region-selective validation.

Weighted route products are narrower than the route cache: they store one
weighted path and the chunk versions for chunks touched by that path. They are
safe for replaying that exact product while those chunks are unchanged. They do
not prove that unrelated blocker removals could not create a shorter route, so
they are support for future topology/portal products rather than a general
optimality-preserving weighted route cache.

Weighted portal route products are also exact for the supplied waypoint route,
not for arbitrary routing. The caller provides portal waypoints from a topology
or room graph; the product verifies each segment with weighted A*, concatenates
the segment paths, and records chunk-version dependencies. This makes topology
evidence measurable before the repository owns a full portal graph builder.
The automatic chunk-boundary builder is a minimal topology MVP: it walks from
the start chunk to the goal chunk in x/y/z order, scans each adjacent chunk
boundary for passable crossings, chooses the crossing with the lowest
Manhattan score to the current point and final goal, then verifies every
resulting segment with weighted A*. It does not search alternate chunk routes
or prove global portal optimality.

For many agents sharing a goal, `DistanceFieldScratch` can amortize search
work. A unit-cost field build visits reachable passable tiles once from the
goal, and each path query follows decreasing distances back to that goal. A
weighted field uses reverse Dijkstra: when expanding backward from tile `c`,
the reverse edge to predecessor `n` costs `entry_cost(c)`, matching the
forward move from `n` into `c`. Weighted reconstruction follows neighbors
where `distance(current) == entry_cost(neighbor) + distance(neighbor)`. The
scratch remembers the field goal and rejects path reconstruction for a
different goal instead of returning a path to stale field data.

When weighted entry costs are known to be small bounded positive integers,
`build_bounded_weighted_distance_field` avoids binary heap traffic with a
Dial-style bucket queue. The result is still exact, because nodes are expanded
in nondecreasing distance order. The bounded builder is an optimization of
weighted field construction, not a different path model.

`weighted_path_batch` makes the current weighted reuse policy explicit for
callers. Repeated goals use one bounded weighted field per unique goal;
singleton goals use normal weighted A*. Returned paths are copied into batch
scratch so all result spans remain valid until the next batch call or scratch
clear.

## Deliberate Limits

This MVP slice does not implement movement classes, topology prechecks, portal
graphs, sparse residency, reservations, dynamic blockers, async tickets, or
rich path diagnostics. The implementation uses reusable dense per-tile scratch
arrays, a two-bucket monotone open set for the current unit-cost Manhattan A*
fallback, exact route/suffix caches, dense reverse distance fields for
shared-goal batches, weighted shared-goal fields with optional bounded-cost
bucket construction, weighted batch grouping, exact weighted route products,
supplied-waypoint and chunk-boundary portal route products, and weighted A*
for positive integral entry costs; it is still an MVP path core, not the final
topology-aware path system.

The unit-cost A* API is suitable for individual point-to-point queries and
regression coverage. Weighted A* is suitable for correctness-first weighted
terrain queries, and weighted distance fields are suitable for weighted
batches with substantial goal reuse. Shared-goal distance fields are suitable
for batches with substantial goal reuse. Route caches are suitable for stable
maps with repeated exact routes or starts that lie on cached same-goal paths.
The v1 plan still needs topology prechecks, richer field reuse, and hierarchy
to cover broad many-agent workloads.

## Current Profiling Notes

Large open-grid benchmarks currently expand one node per path coordinate, but
reach roughly 2.5x more nodes because neighbor candidates are discovered around
the corridor. On a 1024x1024 open 2D world from corner to corner, the Release
benchmark reports a 2,046 unit path, 2,047 expanded nodes, and 5,112 reached
nodes.

Sampling the 1024x1024 query shows time concentrated in neighbor processing:
open-set maintenance, passability/world lookup for each accepted neighbor
candidate, and fixed six-axis neighbor generation even for degenerate 2D
shapes. Those are the first optimization targets before treating this A* path
as suitable for hundreds of independent agents per tick.
