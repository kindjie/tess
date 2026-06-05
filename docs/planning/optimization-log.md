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
