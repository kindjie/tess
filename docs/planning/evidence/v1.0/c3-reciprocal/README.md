# C3 reciprocal conflicts: retained evidence

Pre-registration: issue #247 (plus amendment 1, recorded during
implementation and before this record: the plan's second configuration is
the joint movement tick, every arm is replayed, and the BFS-ranked
diagnostic arm is context that can neither open nor veto C4). Unlike the
rejected screens, everything here MERGED: the fixtures, the oracle, and
the pinned verdicts live in `tests/tess_reciprocal_conflict_test.cc` as a
permanent regression suite, and `screen.txt` is the captured run.

**Verdict: the production PIBT tier fails three of six fixtures under the
pre-registered bound. PR C4 opens.** The bound was arrival-at-fixpoint
AND ticks <= max(3 x optimal, optimal + 8); all three failures are
structural (wedges or self-seals at the no-progress fixpoint), not tick
overruns, and all are deterministic (every arm bit-identical on replay).

## The result table

From `screen.txt` (optimal makespans are exact, from the joint-space BFS
oracle; PASS/FAIL is the pre-registered bound):

| fixture | optimal | pibt | joint | sequential | pibt-bfs |
|---|---|---|---|---|---|
| pocket_yield/forbid | 8 | FAIL (2 wedged) | FAIL | FAIL | FAIL (2 wedged) |
| head_on/permit | 5 | PASS (5 ticks) | PASS (5) | FAIL | PASS (5) |
| rotation/forbid | 1 | PASS (1 tick) | PASS (1) | FAIL | PASS (1) |
| rotation/permit | 1 | PASS (1 tick) | PASS (1) | FAIL | PASS (1) |
| junction_cross/forbid | 9 | FAIL (4 wedged) | FAIL | FAIL | FAIL (4 wedged) |
| queued_yields/forbid | 15 | FAIL (2 arrived, 2 sealed) | FAIL (4 wedged) | FAIL | FAIL (2 sealed) |

Where the tier passes, it is exactly optimal: the swap resolves in the
oracle's 5 ticks and the 4-cycle rotates in 1. The sequential mover fails
everything as expected -- it has no swap or cycle capability, which the
joint arm's passes confirm is a mover property, not a fixture defect.

## The failure mechanism, and why the ranking is exonerated

The diagnostic arm matters because it fails identically: PIBT under
per-agent EXACT BFS ranking wedges the same three fixtures. The pinned
regression (`tess_pibt_movement_test.cc`,
`InheritanceYieldsAnOffRouteDetourForbidJointCannot`) proves the same
machinery pocket-yields when the yielder is cornered ADJACENT to the
pocket. These fixtures move the pocket one step out of reach of that
reflex: a mid-corridor pocket is enterable only from one tile, the
retreating yielder passes that tile while retreat and pocket rank
equally, and once pushed beyond it no single-step decision can recover
-- the pair wedges at the corridor end. The failure is the policy's
one-step horizon, not ranking inexactness. That is precisely the
boundary PR C4's "conflict-local temporal escalation" names, and it is
why the C4 candidate must plan the conflict component rather than
re-rank it.

## The queued-yields self-seal deserves its own paragraph

`queued_yields/forbid` is the strongest exhibit. The production tier
does not merely wedge: one opposing pair ARRIVES, settles, and the
settled tiles wall the one-wide corridor, structurally sealing the other
pair's goals (`structural_seals = 0` -- the terrain is open; the seal is
dynamically formed by the tier's own partial success). Arrival itself
created the unsolvable instance. A bounded-horizon candidate that only
looks at the live conflict in front of it would still admit those
arrivals; this fixture is why the plan's C4 section demands "fixtures
whose conflict queues exceed any bounded candidate horizon so the prior
WHCA failure mode cannot be hidden", and it hands C4 a concrete
acceptance case where order-of-arrival is the difference between full
resolution (optimal 15 ticks, oracle-proven) and a permanent seal.

## Gates and mechanics

Oracle anchored on hand-computed cases (rotation = 1; Permit head-on = 5,
where the naive 4 is impossible by parity; Forbid bare corridor
unsolvable) so a wrong conflict model cannot silently pass fixtures.
Fixture digests are committed, with the declared `SwapPolicy` mixed in
(the two rotation instances differ only by policy). Every arm replayed
bit-identically. The tick metric decides; no timing was read, so no
two-platform campaign. C0 settle-loop options pinned unchanged
(`RemainBlocked`, large retry budget, wedge rule 16, cap 3000 as safety
bound only).

## What follows

PR C4 opens, with these fixtures as its acceptance gates: the three
pinned failures must flip to passes within the same pre-registered bound,
the three passes must not regress, and `queued_yields` must resolve
without the dynamically-formed seal. Escalation must stay conflict-local
per the plan (no global fixed-horizon planner, WHCA, CBS, or LaCAM).
