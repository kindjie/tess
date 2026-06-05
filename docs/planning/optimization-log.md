# Optimization Log

This document records performance experiments that should remain separate from
architecture docs. Architecture docs describe current behavior; this log
captures hypotheses, benchmark evidence, accepted changes, rejected changes,
and deferred ideas.

Use this log when an optimization is benchmarked, profiled, rejected, or
deferred for scope reasons. Keep entries short and concrete:

- area and date
- hypothesis
- benchmark or profile evidence
- decision
- follow-up conditions, if any

## 2026-06-05 - Route Cache Invalidation Hook

- Area: Route-cache lifecycle support.
- Hypothesis: Cached route data needs an explicit invalidation hook so callers
  can respond to passability or movement-rule changes without also resetting
  hit/miss counters used by benchmarks and diagnostics.
- Evidence: New unit coverage verifies that `invalidate()` drops cached route
  entries, forces the next repeated query to recompute, and preserves prior
  hit/miss stats. This deliberately avoids region-selective invalidation,
  because removing a blocker outside a cached path can still create a shorter
  optimal route.
- Decision: Accepted as a supporting API. Keep conservative whole-cache
  invalidation until route products have stronger dependency tracking.
- Retry conditions: Revisit region-selective invalidation when topology,
  chunk-version dependencies, or route-product ownership are designed.

## 2026-06-05 - Weighted A* Entry Costs

- Area: Weighted terrain support for the path API.
- Hypothesis: Weighted costs should use a separate general A* API so
  unit-cost-only fast paths, bucket queues, distance fields, and route caches
  keep their existing optimality assumptions.
- Evidence: New unit tests cover avoiding expensive direct tiles, treating
  zero-cost tiles as blocked, and rejecting zero-cost endpoints. Local Release
  benchmarks report `path/weighted_astar_open_512x512` around 60 us and
  `path/weighted_astar_axis_detour_512x512` around 22 us. The unit-cost
  `path/astar_open_2d_512x512` benchmark remained around 2.2 us in the same
  local run.
- Decision: Accepted. Add `weighted_astar_path` for positive integral entry
  costs, keep weighted reuse/fast paths out of scope, and add weighted
  threshold cases below the 1 ms investigation trigger.
- Retry conditions: Revisit weighted open-set or weighted reuse only after
  profiles show weighted workloads are a bottleneck; do not reuse unit-cost
  shortcuts without a weighted optimality proof.

## 2026-06-05 - A* Fallback Passability Checks

- Area: A* fallback search in `include/tess/path/path.h`.
- Hypothesis: Many neighbor passability reads are unnecessary because closed
  nodes can be rejected before reading world storage, and already-open nodes
  were proven passable when first discovered.
- Evidence: Diagnostic fallback benchmarks showed `diag.passability_checks`
  near `diag.neighbor_candidates`, including hundreds of thousands to millions
  of checks against already-closed or already-open nodes.
- Decision: Accepted in commit `a6d0c52`. A* now rejects closed neighbors
  before passability lookup, skips repeat passability lookup for open nodes,
  and reads neighbor passability by known tile index.
- Result: Release fallback slice improved modestly, with mixed 100-request
  512x512 wall-gap workload dropping to about 129 ms on the local run.

## 2026-06-05 - A* Heap Nodes Carry Coordinates

- Area: A* open-set node payload.
- Hypothesis: Storing `Coord3` in each `OpenNode` would avoid converting tile
  index back to coordinate on every expansion.
- Evidence: Focused release fallback benchmarks regressed across the tested
  cases. Examples from the local run: `wall_gap_512x512` rose to about 4.73 ms,
  `no_path_1024x1024` rose to about 122 ms, and
  `batch_100_mixed_512x512` rose to about 168 ms.
- Decision: Rejected. The larger heap element increased memory traffic more
  than it saved in coordinate conversion work.
- Retry conditions: Reconsider only if the open set representation changes or
  a profile shows coordinate conversion dominating after heap/memory costs are
  reduced.

## 2026-06-05 - A* Indexed Heap / Decrease-Key

- Area: A* open-set duplicate entries.
- Hypothesis: An indexed heap or decrease-key open set would reduce duplicate
  heap entries and high `closed_pops` counts in no-path and maze fallback
  cases.
- Evidence: Diagnostic fallback benchmarks showed large `diag.closed_pops`
  counts, for example hundreds of thousands in 512x512 and 1024x1024 no-path
  or striped-maze cases.
- Decision: Deferred, not rejected. It likely needs an additional open-set
  indexing structure, which is outside the current local-A* optimization scope.
- Retry conditions: Revisit when additional pathfinding data structures are in
  scope, or when current fallback timings become the primary MVP blocker.

## 2026-06-05 - A* Shallow Equal-Score Tie-Break

- Area: A* open-set ordering.
- Hypothesis: Preferring lower `g` on equal `f` would reduce duplicate open-set
  improvements and high `closed_pops` counts in no-path and maze cases.
- Evidence: The experiment eliminated `diag.closed_pops` and improved local
  no-path and striped-maze timings substantially. However, it regressed
  successful wall-gap detours and mixed batches badly; the local
  `batch_100_mixed_512x512` release slice rose from about 126 ms to about
  431 ms.
- Decision: Rejected as the default tie-break. The current higher-`g`
  tie-break remains better for successful detours and mixed-agent workloads.
- Retry conditions: Reconsider as a selectable strategy only if future topology
  prechecks or request classification can identify exhaustive failure searches
  before A* runs.

## 2026-06-05 - A* Geometry-Gated Shallow Tie-Break

- Area: A* open-set ordering.
- Hypothesis: Use shallow equal-score tie-breaking only when the request spans
  multiple axes, while preserving the current deeper tie-break for axis-aligned
  detours.
- Evidence: The local run preserved optimal paths and kept direct open paths
  fast, but it still regressed successful wall-gap and mixed-agent workloads.
  The mixed 100-request 512x512 release slice rose from about 126 ms to about
  368 ms. The runtime comparator branch also added overhead.
- Decision: Rejected. Request geometry is not enough to distinguish diagonal
  no-path searches from diagonal successful detours.
- Retry conditions: Revisit only with topology/reachability classification or
  another zero-overhead way to select tie-break policy before entering A*.

## 2026-06-05 - A* Comparator Arguments by Reference

- Area: A* heap comparator.
- Hypothesis: Passing `OpenNode` comparator arguments by `const&` would avoid
  copies in frequent `std::push_heap` and `std::pop_heap` comparator calls.
- Evidence: Local release fallback runs improved no-path and striped-maze
  cases, but regressed wall-gap and mixed-agent batches. The
  `batch_100_mixed_512x512` release slice rose from about 126 ms to about
  144 ms in repeated local samples.
- Decision: Rejected. Mixed-agent throughput is more important than isolated
  no-path wins, and the by-value comparator remains the better default for the
  current open-set representation.
- Retry conditions: Reconsider if `OpenNode` grows larger or the open-set
  representation changes.

## 2026-06-05 - A* Full Separating Barrier Precheck

- Area: A* no-path precheck for uniform passability grids.
- Hypothesis: If the direct path probe hits a blocked tile and that tile's
  axis plane is fully blocked, the plane separates start from goal under
  axis-adjacent movement and A* can return `NoPath` immediately.
- Evidence: Local release benchmarks dropped `path/astar_no_path_512x512`
  from about 17.8 ms to about 0.8 us and `path/astar_no_path_1024x1024` from
  about 88 ms to about 1.6 us. Wall-gap, striped-maze, and mixed-batch release
  timings stayed in the same range because non-separating barriers fall back to
  normal A*.
- Decision: Accepted. The precheck is exact for the current passability model:
  it only returns `NoPath` when a fully blocked axis plane separates the query.
- Retry conditions: If movement rules later permit jumping, teleporting, or
  non-axis transitions across the plane, gate or disable this precheck for
  those movement classes.

## 2026-06-05 - A* Alternate Direct Axis Orders

- Area: Uniform-cost direct-path fast path.
- Hypothesis: The direct Manhattan probe should try all shape-relevant axis
  orders before falling back to A*, because any passable Manhattan route is
  optimal under unit-cost axis-adjacent movement.
- Evidence: A new 512x512 benchmark with the X-first direct route blocked but
  the Y-first route clear runs in about 2.7 us with no heap work. The naive
  six-order version regressed fallback cases in 2D, so it was narrowed to the
  two meaningful 2D orders for degenerate-Z shapes. The focused release slice
  then kept wall-gap, no-path, striped-maze, and mixed-batch timings in the
  same range as baseline.
- Decision: Accepted. Direct probing now tries only shape-relevant axis-order
  permutations and falls back to A* when none are passable.
- Retry conditions: Gate or revise this fast path when non-unit movement costs
  or movement classes make arbitrary Manhattan axis orders non-equivalent.

## 2026-06-05 - A* Scan Then Build Direct Path

- Area: Uniform-cost direct-path fast path.
- Hypothesis: Direct probes should scan passability first and build the path
  only after a probe succeeds, avoiding path reconstruction work for failed
  probes that fall back to A*.
- Evidence: Diagnostics became cleaner for fallback cases, but release timings
  regressed common direct paths because successful paths walked coordinates
  twice. The local `open_512x512` slice rose from about 2.2 us to about 3.0 us,
  and `alternate_direct_512x512` rose from about 2.7 us to about 3.2 us.
- Decision: Rejected. Direct-path success latency matters more than reducing
  failed-probe reconstruction bookkeeping.
- Retry conditions: Reconsider only if direct-path reconstruction becomes
  materially more expensive or if probes can build a compact reusable route
  representation without a second coordinate walk.

## 2026-06-05 - A* Axis-Aligned One-Tile Detour

- Area: Uniform-cost fast path for simple blocked straight-line requests.
- Hypothesis: If an axis-aligned direct path is blocked but a one-tile
  parallel lane is clear, the detour path is optimal with Manhattan+2 cost and
  can return without fallback A*.
- Evidence: A new 512x512 benchmark with a single blocked tile on an
  axis-aligned route runs in about 2.3 us with no heap work. Open direct paths,
  alternate-direct paths, and no-path barrier rejection stayed fast. Wall-gap,
  striped-maze, and mixed-batch timings remained in the same range and still
  fall back to A* when the detour lane is blocked.
- Decision: Accepted for the current unit-cost axis-adjacent movement model.
- Retry conditions: Gate or revise this fast path when non-unit movement costs,
  movement classes, reservations, or dynamic blockers make Manhattan+2 detours
  non-equivalent.

## 2026-06-05 - A* Scan Then Build Axis Detour

- Area: Axis-aligned detour fast path.
- Hypothesis: Detour probes should scan passability first and build the path
  only after a detour succeeds, reducing reconstruction work for failed detour
  attempts that fall back to A*.
- Evidence: Failed-detour diagnostics became cleaner, but the successful
  `axis_detour_512x512` release case slowed from about 2.3 us to about 2.5 us,
  and fallback cases did not improve enough to offset the success-path cost.
- Decision: Rejected. The accepted detour path keeps one-pass build/probe
  behavior for low latency.
- Retry conditions: Reconsider if failed detour attempts become common enough
  in production workloads to outweigh successful-detour latency.

## 2026-06-05 - A* Top-Down 2D Single-Plane Gap

- Area: Uniform-cost fast path for simple wall-with-gap requests.
- Hypothesis: When a direct probe is blocked by a non-separating axis plane,
  scanning the plane for the cheapest passable crossing and verifying the two
  Manhattan legs can prove an optimal path without fallback A*.
- Evidence: The local release `path/astar_wall_gap_512x512` case dropped from
  about 4.1 ms to about 5.6 us with no heap work. The
  `path/astar_batch_100_mixed_512x512` case dropped from about 138 ms to about
  0.32 ms because its repeated wall-gap requests now use the precheck. Open
  512x512 and 1024x1024 direct paths stayed in the same range. The striped-maze
  case still falls back to A* and stayed around 11-12 ms.
- Decision: Accepted for top-down 2D, unit-cost, axis-adjacent movement. The
  route is returned only after passability of the concrete path through the
  chosen gap is verified.
- Retry conditions: Revisit if weighted movement, reservations, dynamic
  blockers, or movement classes make the cheapest passable plane crossing
  insufficient to prove optimality.

## 2026-06-05 - A* Top-Down 2D Forced-Gap Sequences

- Area: Uniform-cost fast path for repeated single-gap vertical barriers.
- Hypothesis: In top-down 2D, when progress toward the goal hits a blocked
  x-plane with exactly one passable gap, that crossing is forced. Repeating
  this scan only when the next x step is blocked can build and verify an
  optimal route through striped barrier layouts without fallback A*.
- Evidence: Local release `path/astar_striped_maze_512x512` dropped from about
  11-12 ms to about 0.18 ms, and `path/astar_striped_maze_1024x1024` dropped
  from about 52 ms to about 0.72 ms. Diagnostics show zero heap pushes/pops in
  both cases. Open direct paths and the wall-gap fast path stayed in the same
  range.
- Decision: Accepted for top-down 2D, unit-cost, axis-adjacent movement when
  encountered x-planes are fully open or have exactly one passable gap. The
  concrete route is still passability-checked tile by tile.
- Retry conditions: Revisit if weighted movement, reservations, dynamic
  blockers, horizontal forced-gap sequences, or multi-gap barrier choices enter
  the current pathfinding scope.

## 2026-06-05 - A* Scan Every X Plane For Forced Gaps

- Area: Forced-gap sequence detection.
- Hypothesis: Scanning every x-plane between start and goal would distinguish
  fully open planes from single-gap forced planes and keep the proof simple.
- Evidence: The stricter full scan kept correctness but pushed local
  `path/astar_striped_maze_1024x1024` to about 1.2 ms, crossing the current
  1 ms investigation trigger. Most time was spent scanning fully open columns
  that did not affect the route.
- Decision: Rejected in favor of scanning a plane only when the next x step is
  blocked.
- Retry conditions: Reconsider only if path queries gain cached topology or
  compact per-plane metadata outside the current local-A* scope.

## 2026-06-05 - A* Vertical 2D Gap Generalization

- Area: Degenerate-axis 2D fast paths and benchmark coverage.
- Hypothesis: The accepted top-down single-plane and forced-gap fast paths
  should apply to any 2D shape, including vertical YZ layouts, because the
  proof depends on the two active axes rather than named x/y axes.
- Evidence: New vertical 512x512 benchmarks match top-down behavior. Local
  release runs reported `path/astar_vertical_wall_gap_512x512` around 5.9 us
  and `path/astar_vertical_striped_maze_512x512` around 0.18 ms, both with zero
  heap pushes/pops in diagnostics.
- Decision: Accepted. The 2D gap fast paths now select active axes from shape
  traits instead of assuming top-down XY layout.
- Retry conditions: Revisit if non-axis movement, movement classes, weighted
  costs, or reservations make the current unit-cost proof insufficient.

## 2026-06-05 - A* 3D Single-Plane Gap

- Area: Uniform-cost fast path for simple 3D slab-with-gap requests.
- Hypothesis: If a direct 3D route is blocked by an axis plane, scanning that
  plane for the cheapest passable crossing and verifying a concrete Manhattan
  route through it can prove an optimal path without fallback A*.
- Evidence: A new `path/astar_slab_gap_3d_64x64x16` benchmark initially took
  about 1.3 ms and expanded about 32.8k A* nodes. After the fast path it runs
  around 5.3 us with zero heap pushes/pops. The open 3D benchmark stays below
  1 us. Added no-gap, multi-gap, and carved-corridor 3D cases also stay below
  the 1 ms investigation trigger; the corridor case still uses fallback A*
  but expands only about 142 nodes.
- Decision: Accepted for the current unit-cost axis-adjacent movement model.
  The concrete route through the chosen plane crossing is still checked tile by
  tile; failures fall back to A*.
- Retry conditions: Revisit when 3D multi-plane portals, stairs, movement
  classes, reservations, dynamic blockers, or weighted costs enter scope.

## 2026-06-05 - A* Remaining Fallback Profile

- Area: Current post-fast-path fallback cases.
- Hypothesis: After direct, gap, forced-gap, and slab-gap fast paths, remaining
  fallback A* cases should be small enough to stay below the 1 ms
  investigation trigger.
- Evidence: Diagnostic runs show `path/astar_corridor_3d_64x64x16` still uses
  fallback A*, but only expands 142 nodes with 142 open-set pushes and 142
  open-set pops, running around 9 us locally. The 100-agent open and mixed
  batches now report zero open-set pushes/pops because all sampled requests hit
  verified fast paths.
- Decision: No additional A* optimization accepted in this iteration. The
  current fallback profile does not justify an indexed heap or additional
  pathfinding data structures inside the current scope.
- Retry conditions: Revisit if new benchmarks or production traces show a
  remaining fallback case over 1 ms or high heap churn in realistic workloads.

## 2026-06-05 - A* Fallback-Stress Benchmarks

- Area: Heap-backed A* under maps that defeat current uniform-cost fast paths.
- Hypothesis: Sparse blockers, room/portal partitions, branch-heavy lattices,
  and repeated shared-destination requests will expose the next bottleneck more
  clearly than open grids or single-wall fast-path cases.
- Evidence: Sparse blockers run around 0.82 ms locally with about 11.8k heap
  pops and 19.1k heap pushes. Room/portal partitions run around 0.35 ms with
  about 5.4k expanded nodes. The 100-request shared-room/portal batch runs
  around 35 ms because it repeats the same fallback search shape 100 times;
  there is no route cache, hierarchy, or shared batch planner in current scope.
  Diagnostics report zero allocations and zero stale pops, so the current
  bottleneck is graph expansion plus heap maintenance rather than allocation
  churn or duplicate-pop cleanup.
- Accepted: Moved z-only neighbor stride/local-coordinate work into the 3D-only
  branch of the indexed neighbor helper. This is a small flat-world supporting
  code cleanup and preserves behavior.
- Rejected: Reversing the final open-set index tie-break made room/portal and
  sparse-blocker cases slower by increasing heap pushes, pops, and expansions.
  Increasing room size from 32 to 64 tiles also made the room/portal case worse
  by roughly doubling expansions.
- Decision: Keep the fallback-stress benchmarks. Keep individual fallback
  searches under the 1 ms threshold and bound the investigated repeated
  100-request fallback batch explicitly instead of forcing it under 1 ms without
  the future data structures needed to share work.
- Retry conditions: Revisit indexed heaps, region graphs, route caches, or
  batch/shared-destination planning once those data structures enter scope.

## 2026-06-05 - A* Unit-Cost Bucket Open Set

- Area: Fallback A* open-set maintenance for the current unit-cost Manhattan
  path model.
- Hypothesis: Since unit-cost axis-adjacent movement with a consistent
  Manhattan heuristic generates fallback nodes at the current `f` score or
  `f + 2`, a two-bucket monotone queue can remove binary heap maintenance while
  preserving optimal path ordering.
- Evidence: Release threshold runs dropped `path/astar_sparse_blockers_512x512`
  from about 0.82 ms to about 0.35 ms, `path/astar_room_portals_512x512` from
  about 0.35 ms to about 0.15 ms, and
  `path/astar_batch_100_shared_room_portals_512x512` from about 35 ms to about
  15 ms. The bucket queue expands more nodes on the sparse and room/portal
  stress cases, but the removed heap maintenance more than offsets the extra
  graph work. Path unit tests and the path benchmark threshold target pass.
- Decision: Accepted for the current unit-cost fallback. Keep the binary heap
  idea rejected for this slice unless weighted costs or non-Manhattan movement
  require a more general open-set policy.
- Retry conditions: Revisit when weighted terrain, non-unit movement costs,
  non-axis movement, or movement classes enter the public path API.

## 2026-06-05 - Shared-Goal Distance Fields

- Area: Many-agent path batches with repeated goals or goal chunks.
- Hypothesis: For 100 agents sharing one goal, a reverse distance field can
  build one goal-rooted search tree and reconstruct each path more cheaply than
  running 100 independent point-to-point A* searches.
- Evidence: Release threshold runs report
  `path/astar_batch_100_shared_room_portals_512x512` around 15.6 ms versus
  `path/distance_field_batch_100_shared_room_portals_512x512` around 2.8 ms.
  The sparse shared-goal batch drops from about 38.9 ms to about 3.5 ms. The
  8-goal room/portal batch drops from about 56.8 ms with independent A* to
  about 17.7 ms by building one field per goal. Benchmark counters report
  unique starts, unique goals, unique start/goal chunks, and average expanded
  nodes to make reuse opportunities visible.
- Accepted: Add `DistanceFieldScratch`, `build_distance_field`, and
  `distance_field_path` for the current unit-cost passability model. The
  scratch records the goal used to build the field and rejects mismatched
  reconstruction requests.
- Rejected: Applying distance fields blindly to mixed routes with many unique
  goals is not accepted in this iteration. The mixed repeated room/portal batch
  has 21 unique goals, so broad route caching, hierarchy, or selective field
  reuse needs a separate design.
- Decision: Use reverse distance fields for shared-goal and small common-goal
  batches. Keep point-to-point A* for one-off routes until route caches,
  hierarchy, or weighted path APIs enter scope.
- Retry conditions: Revisit field storage/reuse policies when topology,
  invalidation, chunk residency, weighted costs, or path product caching are
  introduced.

## 2026-06-05 - Exact Route and Suffix Cache

- Area: Many-agent path batches with repeated point-to-point routes or starts
  that lie on an already-computed route to the same goal.
- Hypothesis: A small caller-owned route cache can avoid rerunning A* for
  repeated stable-map routes. Exact route hits can return the stored path
  directly, and same-goal suffix hits are optimal for unit positive edge costs
  when the new start is already on a cached optimal path.
- Evidence: Targeted Release benchmarks report
  `path/astar_batch_100_mixed_repeated_room_portals_512x512` around 41.1 ms
  versus
  `path/cached_astar_batch_100_mixed_repeated_room_portals_512x512` around
  12.5 ms with 30 misses and 70 exact hits. The suffix-specific open batch
  reports `path/astar_batch_100_suffix_open_512x512` around 95.9 us versus
  `path/cached_astar_batch_100_suffix_open_512x512` around 4.0 us with
  1 miss and 99 suffix hits.
- Accepted: Add `RouteCacheScratch` and `cached_astar_path` for explicit
  caller-managed route reuse. The cache keeps whole path nodes, reports
  entries, exact hits, suffix hits, misses, and stored path nodes, and assumes
  the caller clears it when passability or movement rules change.
- Deferred: Room/portal hierarchy remains a larger topology feature. It needs
  persistent portal graph ownership, invalidation, and tests over general
  map structure; adding benchmark-only room knowledge would not be a usable
  library optimization.
- Decision: Keep exact route and same-goal suffix reuse. Continue using
  independent A* or distance fields when there is no route/suffix reuse.
- Retry conditions: Revisit cache indexing when route counts become large
  enough for linear lookup to show up in profiles, and revisit hierarchy when
  topology graph ownership is designed.
