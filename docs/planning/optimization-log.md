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
