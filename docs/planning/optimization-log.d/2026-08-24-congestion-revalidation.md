## 2026-08-24 - Congestion pricing revalidated under the demo classifier; retained as a caller recipe

**Question** (pre-registered in issue #256; amendment 2 added the
plan-mandated demo-classifier leg after review flagged that the
original registration mis-placed its gate on the C0 substrate): does
one bounded dynamic price policy -- per-tile cost `1 + min(3, live
agents within Manhattan 1)`, recomputed every 4 ticks, written through
the versioned edit channel -- preserve terminal classification and buy
anything, or does the colony-era rejection reconfirm?

**Answer: the rejection does not reconfirm under the plan's own
gate.** On the web_colony demo's recovery classifier (5 scenarios x
populations {256, 1024}), pricing keeps zero crowd-blocked and zero
durably-unreachable on every cell, rescues the one canonical failure
(tip/1024: canonical strands 519 agents at the 5000-tick cap; priced
delivers all 1024 in 1085 ticks), and wins congested cells outright
(tip/256 3362 -> 566; two-gates/1024 4431 -> 531), beating even the
demo's shipped spread mode on the two heaviest cells. The boundary:
detour-shaped maps pay modestly (goal-wall 1004 -> 1146), and the
originally-registered substrate parity gate fails on 17 of 132 C0
seeds in both directions -- the substrate's fixpoint classifier is
trajectory-sensitive on marginal wedge/seal seeds and a price signal
perturbs trajectories (C4's divergence mechanism), while the demo's
recovery loop absorbs trajectory changes and measures delivered
outcomes. Tick counts and classifications decided everything; no
wall-time claim, so no hardware campaign.

**Consequences.** Pricing is retained as a documented caller recipe
with its boundary (use under gate/corridor congestion; avoid where
fixpoint-style per-seed classification stability is required; expect
small detour-map regressions). No library change was ever involved --
an ordinary cost field plus versioned edits express the whole policy
-- so nothing merges beyond the record, and the demo's default spread
mechanism is unchanged. C6's opening condition is answered precisely:
a capacity-contention premise IS isolated (gate throughput), but it is
served by this recipe with zero library authority, so the
capacity-aware crossing reservation dispositions without a run in X3
on no-UNSERVED-premise grounds.

Evidence: `docs/planning/evidence/v1.0/c5-congestion/`.
