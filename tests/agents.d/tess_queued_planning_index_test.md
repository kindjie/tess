# tess_queued_planning_index_test

- `tess_queued_planning_index_test`: holds the audit-2026-08-07 P1 chunk
  index indistinguishable from the linear scans it replaced. Differential:
  randomized plans run through both `find_hazard` and `find_hazard_indexed`
  and must blame the identical operation, and through both phase-grouping
  branches (the index above `phase_index_min_operations`, all-pairs below)
  and must produce the identical phase layout. Also pins the index itself --
  `clear` leaves no stale entry, and growth relinks every node. The
  `{A}, {B}, {B}, {A}` case is constructed rather than sampled: it is the
  shortest plan where an operation overlaps a CLOSED phase but not the open
  one, and randomized plans reach it too rarely to rely on. Both generators
  emit whole-domain operations, which the planner keeps OUT of the index
  and compares separately; the world is sized so such an operation exceeds
  `index_max_chunks_per_operation`, without which that path never runs.
  What the bound does for COST rather than for answers is not testable
  here -- removing it still plans correctly -- and is gated by
  `queued/plan_frame_dense_64` instead.
