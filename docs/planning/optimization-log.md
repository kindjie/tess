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

## 2026-06-07 - Concurrent Phase Backend Library Spike

- Area: Tile-world queued operation phase execution.
- Hypothesis: A small external concurrency primitive might reduce risk for
  future production worker backends if it maps cleanly to Tess's current
  phase contract: execute one contiguous planned-operation index range,
  complete or join before returning, keep per-operation dirty/result scratch
  caller-owned, and avoid hidden cancellation or lifecycle semantics.
- Evidence: Reviewed current upstream state for
  `buildingcpp/work_contract` at commit
  `3f56a17e36db57846a086e20d8788478287f3c86` and
  `buildingcpp/signal_tree` at commit
  `f7b59510e117bc6156af86a6b8689ca4a3832e3c`. `signal_tree` is a concurrent
  readiness set: it selects ready signal ids, is idempotent, does not store
  work payloads, and leaves worker waiting/join/result handling to the caller.
  That is useful scheduler infrastructure, but it is not a phase executor.
  `work_contract` is closer to a scheduler because it manages recurrent
  schedulable contracts with non-blocking and blocking groups, explicit async
  release, thread-local reschedule/release controls, and exception callbacks.
  Those lifecycle features are stronger than Tess needs for the current phase
  adapter and would introduce semantics not yet covered by Tess tests.
- Decision: Deferred. Keep the internal `ExecutorPhaseRange` /
  `for_each_operation(first, count, fn)` adapter as the production-facing
  contract for now, with `SerialPhaseExecutor` and test-only threaded
  executors covering the memory and ordering rules. Do not add either
  dependency in this slice.
- Follow-up: Revisit `work_contract` only after Tess has a production worker
  backend prototype that needs recurrent work handles or blocking wakeups, and
  first prove scoped phase completion, deterministic result reduction, and
  shutdown behavior in Tess-owned tests. Revisit `signal_tree` only if a later
  scheduler has many stable ready lanes where idempotent readiness and
  non-FIFO fairness are the real bottleneck.

## 2026-06-05 - Weighted Shared-Goal Distance Field

- Area: Weighted many-agent shared-goal pathfinding.
- Hypothesis: Reverse Dijkstra over positive entry costs should amortize
  weighted pathfinding for many starts sharing one goal, similar to the
  unit-cost distance-field win.
- Evidence: Added `build_weighted_distance_field` and
  `weighted_distance_field_path`, plus correctness and allocation tests against
  weighted A*. On the 512x512 weighted sparse shared-goal batch,
  independent weighted A* runs around 236-238 ms for 100 agents, while the
  weighted field batch runs around 15.5 ms. Diagnostics drop from about
  10.3M neighbor candidates, 5.0M passability checks, and 4.0M heap pushes to
  about 859k neighbor candidates, 367k passability checks, and 243k heap
  pushes.
- Decision: Accepted. Weighted shared-goal fields are the first choice for
  weighted batches with substantial goal reuse.
- Follow-up: Add weighted multi-goal field grouping when weighted workloads
  have a small set of repeated goals.

## 2026-06-05 - Weighted Batch Planner API

- Area: Weighted many-agent request dispatch.
- Hypothesis: A public batch helper can make the accepted weighted reuse
  policy explicit: use one bounded weighted field per repeated goal and use
  weighted A* for singleton goals.
- Evidence: Added `weighted_path_batch` and stable-result batch scratch. Unit
  coverage verifies grouped repeated goals, singleton fallback, stable result
  spans, and cost agreement with weighted A*. The 100-agent eight-goal sparse
  benchmark runs around 46.5 ms, matching the hand-grouped bounded field path.
- Decision: Accepted. This is the default API for current weighted batches
  with repeated goals.
- Follow-up: Add hierarchy/topology when many unique far goals make one field
  per goal too expensive.

## 2026-06-05 - Supplied-Waypoint Portal Route Product

- Area: Weighted room/portal route products.
- Hypothesis: If topology supplies portal waypoints, stitching exact weighted
  A* segments through those waypoints should reduce tile search volume while
  keeping the general weighted A* fallback unchanged.
- Evidence: Added `WeightedPortalRouteProduct`, build/replay tests, dependency
  invalidation tests, and room-portal benchmarks. On the 512x512 weighted
  room-portal case, normal weighted A* runs around 10.8 ms with about 139k
  expanded nodes. The supplied-waypoint product build runs around 1.0 ms with
  about 17k expanded nodes, and replay is around 10 ns.
- Decision: Accepted as a product primitive. It is exact for the supplied
  waypoint route but not a general optimal portal query.
- Follow-up: Add a topology-owned portal graph builder that chooses waypoints
  and proves when the waypoint route is globally optimal or acceptable.

## 2026-06-05 - Chunk-Boundary Portal Route Product

- Area: Weighted room/portal route products.
- Hypothesis: Deriving waypoints from adjacent chunk-boundary portals should
  make the topology MVP measurable without requiring callers to supply every
  portal coordinate.
- Evidence: Added `build_weighted_chunk_portal_route_product`, unit coverage
  for automatic boundary selection, and 512x512 weighted room-portal build and
  replay benchmarks.
- Decision: Accepted as a minimal topology product. It verifies each segment
  with weighted A*, but chooses one axis-ordered chunk route and therefore is
  not a full shortest-path portal graph.
- Follow-up: Add a persistent graph with alternate chunk/portal routes only
  after profiling shows this boundary-derived MVP is insufficient.

## 2026-06-05 - Portal Candidate Selection And Segment Reuse

- Area: Weighted room/portal route products.
- Hypothesis: Comparing a small set of chunk-boundary portal candidates should
  improve route choice without adding meaningful overhead, and warmed segment
  reuse should avoid repeated A* work for stable supplied-waypoint portal
  routes.
- Evidence: Added six-order chunk-boundary candidate selection, a greedy
  monotone candidate, candidate and scan counters, route cost-ratio counters,
  an isolated candidate-selection benchmark, and warmed portal segment-cache
  benchmarks. On the 512x512 weighted room-portal case, candidate selection
  scans 7,456 boundary tiles in about 7.4 us. The greedy candidate drops the
  full chunk-boundary product from around 1.0 ms, 17.4k expanded nodes, and a
  1.37 route-cost ratio to around 0.72 ms, 12.6k expanded nodes, and a 1.12
  route-cost ratio. Warmed single-route portal segment-cache rebuilds run
  around 11.3 us with zero expanded nodes. A 100-agent shared portal-leg batch
  with exact segment reuse runs around 4.4 ms with about 566 expanded nodes per
  agent. Isolating the endpoint segments shows the unique start-to-first-portal
  A* searches alone run around 2.3 ms with about 398 expanded nodes per agent,
  while the shared last segment expands about 45 nodes. Replacing those unique
  first segments with one exact local-domain weighted field to the first portal
  plus cached shared portal legs drops the 100-agent batch to about 2.1 ms; the
  local field expands about 1,025 nodes once and reconstruction averages about
  39 nodes per agent.
- Decision: Accepted. Candidate selection is cheap enough to keep, and segment
  reuse is useful for repeated stable portal routes. The high cost ratio means
  this is still a product/throughput primitive, not a global optimal portal
  planner.
- Follow-up: Improve waypoint quality with a real portal graph or weighted
  portal-edge summaries before optimizing the remaining segment A* cost.
  Promote local-domain portal-entry fields into a product API only after the
  topology layer owns room/region domains.

## 2026-06-05 - Exact Weighted Route Products

- Area: Route-product dependency support.
- Hypothesis: Storing a verified weighted path plus chunk-version
  dependencies gives a safe route product primitive without claiming
  region-selective optimality.
- Evidence: Added `WeightedRouteProduct`, build/replay helpers, and tests that
  replay succeeds across unrelated chunk edits and fails when a captured chunk
  changes.
- Decision: Accepted as an exact product primitive. It is not a general
  weighted route cache because an unrelated edit could create a shorter route.
- Follow-up: Use this dependency shape in future topology/portal products,
  where the product can record enough evidence to prove optimal reuse.

## 2026-06-05 - Weighted Unit-Cost Axis Detour Fast Path

- Area: Weighted A* local fast paths.
- Hypothesis: If an axis-aligned straight line is blocked and a one-tile
  parallel detour has only unit entry costs, returning that detour is optimal
  under positive axis-adjacent movement.
- Evidence: Added a weighted detour fast path and unit coverage. The existing
  weighted axis-detour benchmark is unchanged because it covers expensive
  direct terrain, not a blocked unit-cost line.
- Decision: Accepted with a narrow guard. Do not apply this to merely
  expensive direct terrain.
- Follow-up: Add a dedicated blocked weighted detour benchmark only if this
  path appears in production-style workloads.

## 2026-06-05 - Weighted Multi-Goal Field Grouping

- Area: Weighted many-agent pathfinding with a small set of repeated goals.
- Hypothesis: Building one weighted field per unique goal should still beat
  independent weighted A* when a batch has several repeated goals.
- Evidence: Added 100-agent sparse weighted benchmarks with eight unique
  goals. Independent weighted A* runs around 470-504 ms locally; grouped
  weighted fields run around 119-133 ms with the same reconstructed paths.
  The grouped field path still performs one full weighted field build per
  unique goal, so cost scales with unique goals rather than agents.
- Decision: Accepted. Group requests by goal and build one weighted field per
  goal when weighted batches have repeated destinations.
- Follow-up: Use bounded weighted field construction for small integral costs;
  use topology or hierarchy for batches with many unique far goals.

## 2026-06-05 - Bounded Weighted Distance-Field Buckets

- Area: Weighted shared-goal field construction for small positive costs.
- Hypothesis: A Dial-style bucket queue should reduce binary heap traffic when
  weighted entry costs are bounded small integers.
- Evidence: Added `build_bounded_weighted_distance_field` and correctness,
  fallback, allocation, benchmark, and threshold coverage. On the 512x512
  sparse weighted shared-goal batch, general weighted field construction runs
  around 15.1 ms while the bounded field runs around 6.3 ms. On the eight-goal
  grouped sparse batch, general weighted fields run around 118.8 ms while
  bounded fields run around 46.6 ms.
- Decision: Accepted for exact bounded-cost weighted field builds. It falls
  back to the general weighted builder if a reached tile exceeds `MaxCost`.
- Follow-up: Profile bucket occupancy and modulo-collision churn only if
  bounded weighted field benchmarks regress or weighted costs grow beyond the
  current small-cost assumption.

## 2026-06-05 - Explicit Chunk Version Dependencies

- Area: Route-product support.
- Hypothesis: A small public helper for chunk/version dependencies can support
  future route products without changing current route-cache semantics.
- Evidence: Added `ChunkVersionDependencies` with explicit chunk capture,
  whole-world capture, and validation tests. The helper correctly remains
  valid across unrelated chunk edits and invalidates when a captured chunk
  version changes.
- Decision: Accepted as supporting code only. Current route-cache hits still
  rely on conservative caller invalidation or whole-world fingerprints.
- Follow-up: Wire dependencies into cached route products only after the
  product records enough route/topology evidence to prove reuse remains
  optimal.

## 2026-06-05 - Weighted Unit-Cost Direct Fast Path

- Area: Weighted A* common-case latency.
- Hypothesis: If a direct Manhattan route is passable and every entered tile
  has entry cost 1, returning it is optimal because no positive-cost
  axis-adjacent path can beat Manhattan distance.
- Evidence: `path/weighted_astar_open_512x512` dropped from about 60 us to
  about 2.9 us. Expensive-axis and weighted obstacle cases still fall back to
  general weighted A* and keep their previous behavior.
- Decision: Accepted. This protects unit-cost maps using the weighted API
  without weakening the general weighted optimality path.
- Follow-up: Consider exact weighted detour or gap fast paths only when the
  optimality proof is as local and cheap as the direct unit-cost proof.

## 2026-06-05 - Route Cache World-Version Fingerprint

- Area: Route-cache dependency support.
- Hypothesis: A coarse whole-world chunk-version fingerprint gives callers a
  correct invalidation hook without pretending region-selective route
  validation is solved.
- Evidence: Added `capture_world_versions(world)` and
  `invalidate_if_world_changed(world)` on `RouteCacheScratch`. Unit coverage
  verifies that a chunk version change drops cached route entries while
  preserving hit/miss counters.
- Decision: Accepted as conservative support. It is opt-in and does not change
  existing route-cache hit behavior.
- Follow-up: Replace whole-world fingerprints with explicit route-product
  chunk dependencies only after topology/portal products are designed.

## 2026-06-05 - Weighted Portal Topology

- Area: Weighted room/portal single-query performance.
- Hypothesis: The weighted room-portal case needs a graph or portal product
  rather than more local A* bookkeeping.
- Evidence: The weighted room-portal single path remains around 11 ms and is
  dominated by search volume. The accepted weighted field helps shared-goal
  batches, but a single weighted route through many rooms still expands a
  large tile search.
- Decision: Deferred. Implementing weighted portal topology would add the
  extra data structures that are intentionally outside the current A* and
  supporting-code scope.
- Follow-up: Design chunk/room portal products with weighted edge summaries,
  movement class keys, and version dependencies before adding a topology-aware
  weighted query.

## 2026-06-05 - Weighted A* Stress Profiling

- Area: Weighted A* benchmarks and diagnostics.
- Hypothesis: Weighted entry costs need stress cases beyond open-grid and
  single-axis detour paths before choosing another open-set optimization.
- Evidence: Added weighted sparse-blocker, room-portal, and 100-request mixed
  batch benchmarks. Corrected diagnostics show no allocations in warm runs.
  The weighted sparse case runs around 2.0 ms and performs about 98k neighbor
  candidates, 49k passability checks, 50k cost reads, and 41k heap pushes. The
  weighted room-portal case runs around 10.7-12.4 ms and performs about 554k
  neighbor candidates, 157k passability checks, 269k cost reads, 224k heap
  pushes, and 219k heap pops. The weighted 100-request mixed batch runs around
  509-588 ms with about 20.8M neighbor candidates and 7.5M heap pushes.
- Decision: Accepted the benchmark and diagnostic coverage. The specific
  bottleneck is search volume plus memory-heavy world/cost reads and binary
  heap traffic, not allocations or floating-point arithmetic.
- Follow-up: Any single-query path benchmark above 1 ms remains an
  investigated bottleneck. Next useful work is reducing weighted search volume
  through exact domain-specific fast paths or weighted shared-goal products,
  while preserving the current unit-cost regression gates.

## 2026-06-05 - Weighted A* Indexed Heap

- Area: Weighted A* open-set duplicate entries.
- Hypothesis: Updating open nodes in place with an indexed heap would remove
  duplicate closed pops and improve weighted stress cases.
- Evidence: The experiment eliminated weighted `diag.closed_pops` and improved
  `path/weighted_astar_room_portals_512x512` from about 10.8 ms to about
  9.0 ms. It regressed common weighted cases: open 512x512 rose from about
  60 us to 79 us, axis detour from about 22 us to 40 us, sparse blockers from
  about 2.0 ms to 2.6 ms, and the 100-request mixed batch from about 513 ms to
  613 ms.
- Decision: Rejected. The extra indexed-heap bookkeeping costs more than it
  saves for most current weighted workloads.
- Retry conditions: Reconsider only if a future profile is dominated by
  duplicate heap pops rather than neighbor/cost reads, or if a lower-overhead
  indexed open set is introduced.

## 2026-06-05 - Route Product Dependency Direction

- Area: Route-cache invalidation and future route products.
- Hypothesis: Route reuse should eventually track dependencies so stable route
  products can survive unrelated world edits without risking stale optimality.
- Evidence: Whole-cache invalidation is correct but coarse. The weighted batch
  benchmark shows independent weighted A* is too expensive for repeated agent
  workloads, while current exact/suffix route caches only reuse already-known
  paths and do not know which world edits affect them.
- Decision: Deferred as additional product data structure work. Keep current
  conservative invalidation for now.
- Follow-up: Design route products around public, non-private dependencies:
  movement class, cost/passability field identity, goal or exact request,
  touched tile/chunk keys, and chunk/version stamps. Revalidate or invalidate
  products when any dependent chunk version changes; do not attempt
  region-selective invalidation without an optimality proof.

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

## 2026-06-06 - Unit-Cost Distance-Field Products

- Area: Reusable unit-cost distance fields for stable maps with repeated goal
  sets.
- Hypothesis: Copying a built reverse field into a reusable product can make
  repeated nearest-target and replay queries much cheaper than rebuilding the
  field, while chunk-version dependencies conservatively reject stale products.
- Evidence: Targeted local benchmark runs report
  `path/distance_field_product_build_8_goal_room_portals_512x512` around
  13 ms while visiting about 247k reachable tiles and copying about 1 MiB of
  dense field data. The corresponding
  `path/nearest_target_product_100_starts_room_portals_512x512` batch is about
  0.32 ms, `path/field_product_cache_hit_replay_room_portals_512x512` is about
  4 us, stale rejection is about 0.8 us, and constrained LRU eviction is about
  27 us.
- Accepted: Add `GoalSet`, `DistanceFieldProduct`, nearest-target replay, and
  a caller-owned byte-budgeted `FieldProductCache` for unit-cost fields. Keep
  replay and cache lookup exact and conservative: products carry reached
  chunk-version dependencies, and stale products return `NoPath` or are
  rejected from the cache.
- Deferred: Weighted field products and `PathRequestRuntime` integration remain
  out of this slice. Runtime policy needs a separate decision once product
  ownership, invalidation cadence, and topology interaction are clearer.
- Decision: Keep dense field products for now. The 8-goal product build is a
  batch/product-construction workload, so its threshold is explicitly above
  the 1 ms single-query investigation line; nearest-target and cache replay
  thresholds stay under 1 ms.
- Retry conditions: Revisit sparse product storage or differential cache
  invalidation if dense product byte size or build-copy time starts dominating
  real workloads.

## 2026-06-06 - Runtime Field-Product Reuse Policy

- Area: Integrating unit-cost distance-field products into
  `PathRequestRuntime`.
- Hypothesis: Repeated-goal runtime batches can opt into field-product reuse
  while preserving the existing exact route/suffix cache as the default unit
  pathing behavior.
- Evidence: Runtime tests cover opt-in repeated-goal reuse, stale product
  rejection after world edits, world-change cache clearing, and warm
  allocation-free agent processing. Local benchmark comparison on a 100-agent
  shared wall-gap workload reports the default route/suffix cache around
  0.31 ms and the opt-in field-product cache around 0.68 ms; the route cache
  wins there because many starts are suffix hits on already-cached paths. A
  start-chunk policy gate makes that same field-product opt-in path skip the
  suffix-friendly group and match route-cache timing around 0.31 ms. A
  scattered-start wall-gap workload uses the product, but still measures
  around 0.41 ms versus route/suffix around 0.21 ms because suffix reuse
  remains strong in the current map.
- Accepted: Add `PathRuntimeCachePolicy::use_unit_field_product_cache`,
  `unit_field_product_min_goal_reuse`,
  `unit_field_product_min_start_chunks`, and a byte budget for the
  runtime-owned field-product cache. Only repeated single-goal groups with
  enough distinct start chunks use products; singleton and suffix-friendly
  single-chunk groups keep the route/suffix path. Runtime counters report
  candidate, used, and skipped field-product groups.
- Decision: Keep runtime field products opt-in. They are useful for workloads
  where callers know field reuse should beat suffix reuse, but the existing
  route cache remains the safer default for converging paths.
- Retry conditions: Revisit automatic runtime selection after more workload
  benchmarks identify when field products consistently beat route/suffix
  caching.

## 2026-06-07 - CI Path Threshold Calibration

- Area: Path benchmark thresholds on GitHub-hosted Ubuntu runners.
- Evidence: PR CI on the `ubuntu-24.04` runner completed the product
  benchmarks within their thresholds, but exceeded several existing path
  thresholds: weighted sparse and portal A*, weighted portal segment batches,
  existing 100-agent A* batches, and
  `path/agent_runtime_100_weighted_mixed_512x512`. The failing values were
  broad runner calibration misses rather than a product-specific regression.
  A follow-up run narrowed the remaining misses to two multigoal weighted
  batch thresholds at about 81-82 ms against 80 ms, plus the existing
  `path/astar_batch_100_mixed_512x512` batch at about 1.08 ms against 1 ms.
- Accepted: Raise only the path thresholds exceeded by that CI run, using the
  observed runner timings with headroom. Keep single-query product replay,
  nearest-target, stale rejection, and cache lookup thresholds below the 1 ms
  investigation line.
- Deferred: No optimization work was started because the failed gates covered
  pre-existing weighted and batch workloads, and the new product benchmarks
  passed their gate on CI.
- Follow-up: A later hosted-runner pass measured the existing
  `path/astar_sparse_blockers_512x512` single-query benchmark at about
  1.002 ms against its 1.000 ms gate. Accepted a narrow threshold adjustment
  to 1.1 ms as runner jitter; this does not change the 1 ms investigation
  rule for new single-query benchmarks.
- Retry conditions: Profile the affected weighted and batch workloads before
  further threshold changes if future PRs exceed these calibrated runner
  bounds.

## 2026-07-06 - Pre-A* Scan Cost Model Accepted; Grouping Rescans Removed

- Area: `astar_path` pre-A* fast-path scans;
  `PathRequestRuntime::process_repeated_goal_fields` and
  `weighted_path_batch` goal grouping.
- Hypothesis: The plane-gap/forced-gap/barrier fast paths carry an
  O(world-slice) worst case when they all miss, and the repeated-goal
  grouping passes carried O(n^2)/O(n^3) request rescans that a flat-hash
  grouping pass removes without behavior change.
- Evidence: Two new worst-case benchmarks pin the scan-miss cost:
  `path/astar_plane_gap_miss_512x512` measured about 1.76 ms (sealed wall
  gap, every 2D scan fails, full-flood NoPath A* of about 131k nodes) and
  `path/astar_plane_gap_miss_3d_64x64x16` about 9.0 us (sealed best 3D
  gap, A* reroutes through a second gap). Seeded randomized equivalence
  tests (fixed `std::mt19937` seeds) pin grouped statuses, costs, and all
  `field_product_*`/batch stats counters against per-request A* oracles
  before and after the grouping rewrite, and warm reruns of both grouping
  passes are allocation-free under `ScopedAllocationCounter`.
- Accepted: Rewrite both grouping passes as single O(n) flat-hash passes
  (goal -> group id, counting-sort member buckets, sort+unique distinct
  start chunks) with runtime/scratch-owned reusable storage. Add the two
  scan-miss benchmarks to `bench/thresholds/path.json` with deliberately
  generous 10x-measured ceilings as documentation gates.
- Deferred: No structural change to the pre-A* scans themselves. The miss
  cost is bounded by one world slice per failed scan and the fast paths
  win on real layouts; the accepted evidence is the benchmark pair plus
  the cost-model section in `docs/architecture/path.md`.
- Retry conditions: Revisit the scan ordering (or gate the plane scans
  behind a cheap occupancy summary) if the miss benchmarks regress past
  their generous ceilings or profiling shows scan overhead dominating
  realistic mixed workloads.
