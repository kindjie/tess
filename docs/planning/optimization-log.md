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
  can return without heap-backed A*.
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
  Manhattan legs can prove an optimal path without heap-backed A*.
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
