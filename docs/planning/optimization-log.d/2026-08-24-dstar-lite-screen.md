## 2026-08-24 - D* Lite screen rejected at feasibility once both arms were counted with the same ruler

**Question** (pre-registered in issue #255; amendment 1 recorded
review-driven conformance corrections before the corrected run): does a
goal-keyed D* Lite that repairs search state across version-marked
edits do materially less work per edit-query cycle than fresh
`tess::astar_path`, and does the advantage survive into wall time?

**Answer: no, at stage 1.** The first capture reported a stage-1 pass
(pooled work ratio 1.913 against the >= 1.5 bar) and a wall-time
rejection -- an "inversion." Review found the pass was a counter
artifact: every incumbent neighbor candidate was counted while a D*
Lite `update_vertex` counted as one unit, hiding its per-neighbor rhs
re-scans. With symmetric accounting -- plus the registration's other
under-implemented terms fixed (true Manhattan-2 route-local offsets,
an incumbent-derived pregenerated edit trace so both arms replay one
identical workload, warm-allocation and memory gates enforced, the
sweep replayed twice with identical digests) -- the pooled median
ratio is **1.012**: the repair machinery does essentially the same
abstract work as searching fresh, and the registration's stop
condition rejects without a hardware campaign. The retained M3
wall-time capture corroborates as context: its 12 uniform-locality
cells (which the trace defects do not touch) hold 11 confirmed
material regressions, up to +1,846% relative.

**The durable lessons.** (1) Count both arms with the same ruler:
asymmetric counters manufactured a feasibility pass that two further
stages of measurement then had to un-earn. (2) The prior capture's
lesson survives in corrected form: op-count ratios predict wall time
only over comparable primitives; weight by per-op cost or measure it
directly before promising timing. (3) The consistent-pop discard
defect (a consistent vertex popped through the underconsistent branch
toggles g to infinity and back forever) is recorded for any successor.

**Consequences.** No library change existed in either arm; the record
is the disposition. P6's opening condition stays unmet (no new
open-set structure merges); P2 stays closed (cost, not semantics).

Evidence: `docs/planning/evidence/v1.0/p5-dstar/`.
