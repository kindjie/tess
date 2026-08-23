## 2026-08-23 - Bidirectional A*: rejected; both frontiers fail a misleading heuristic symmetrically

**Hypothesis** (pre-registered in issue #251, reusing P3's domain gate,
families, correctness gates, and decision rule verbatim): a whole-query
bidirectional unit search over the P3-eligible domain materially speeds
up at least one family x size on one platform without materially
regressing any on either.

**Prototype.** Behind `-DTESS_P4_BIDIR`, caller opt-in on `PathScratch`,
incumbent fast paths retained, only the exhaustive loop replaced. Two
Manhattan-guided frontiers alternating by open-list size; best meeting
cost mu; termination mu <= max(minf_forward, minf_backward), validated
empirically by the oracle on all 160 trials. The backward direction
carries one additional node-array set on the scratch -- the
pre-registered memory bound, met by construction.

**Correctness: all gates passed** -- cost equality with oracle and
incumbent, route validity, NoPath agreement, determinism, and
byte-identical flag-ignored behavior for hex, weighted,
provider-composed, sparse, and 3D callers (instantiated, per the gate
tightened in P3's deviation note).

**Performance: rejected -- five confirmed material regressions on M3.**
wall_gap +55.0%/+259.9%, maze +13.3%/+70.0%, rubble_256 +265.3%; the
sole win rubble_64 -15.8%; open cells immaterial (shared preamble). A/A
clean. Counters on the timed wall-gap workload: the bidirectional arm
did MORE work, not less -- 1.40x pushes (1.35M vs 0.97M), 1.31x pops,
1.38x touched nodes, equal reconstruct totals -- because a serpentine
misleads the backward Manhattan heuristic exactly as much as the
forward one, both directions flood, and the safe termination bound
keeps both floods alive nearly to completion, all paid at binary-heap
prices against the incumbent's near-free two-bucket dial. The Deck leg was unnecessary: the no-regression rule is
platform-existential and M3's regressions are confirmed
(pre-registered as the P3 precedent this time).

**Decision: reject; prototype removed from the branch.** Evidence in
`docs/planning/evidence/v1.0/p4-bidir/` (prototype diff including the
branch-only bench source -- the recording gap P3's addendum documents
is closed here by tracking the bench via intent-to-add). Whole-query
bidirectional search has no niche against this incumbent in this
domain: where the heuristic works the incumbent is near-minimal; where
it fails, it fails both directions symmetrically. Reconsideration: a
weak-heuristic or heuristic-free search surface would reopen the
question; P5's D* Lite screen does not.
