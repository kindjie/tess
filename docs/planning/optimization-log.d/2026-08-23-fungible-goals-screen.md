## 2026-08-23 - Fungible goals: rejected; the value is dispatch quality, not reassignment

**Hypothesis** (pre-registered in issue #241, revision 2 plus amendment 1
recorded between the fixture merge and any arm code): with an anonymous
goal pool of M >= N interchangeable goals, reassigning targets during
movement beats the best assignment a caller can make at dispatch, without
changing terminal outcomes or breaking accounting. Acceptance bar: paired
geometric mean of ticks-to-fixpoint against the OPTIMAL one-shot dispatch,
at least 8% in the candidate's favour with the bootstrap CI excluding 1.0.

**Design.** The pool fixture merged first (#245) as its own arm-neutral
commit: agents placed goal-less, 21-entry digest table, M = N pool equal
to the paired C0 instance's goal sequence, larger pools prefix-coherent.
Three arms in one binary on identical instances: greedy dispatch, optimal
dispatch (exact rectangular assignment), and the candidate -- optimal
dispatch plus the amendment's policy (single best strictly-improving move
per tick over unheld-goal moves and pairwise exchanges, improvement > 1
tile, applied only through `set_path_agent_goal`). One shared distance
(BFS on bare terrain) for dispatch, reassignment, and the contention
metric. 7 families x 3 pool sizes x pre-registered trials = 396
seed-cells; every arm replayed per seed with bit-identical outcomes
required.

**Result: reject.** Pooled gm(C/B) = 0.9797, CI [0.9580, 0.9986] over 382
included seeds -- a real but 2% improvement against an 8% bar. At M = N
the candidate shows no benefit and trends harmful (gm 1.0154, CI
[0.9960, 1.0457], includes 1.0); the ring cell (gm 1.0924) shows the
mechanism: with no goal surplus the policy can only exchange, and
exchange churn slows settling. The
per-surplus cells (M = 1.25N: 0.9577, M = 2N: 0.9684) are the candidate's
best and still miss the bar. The rejection is not a vacuous null: Control
B still saw an improvable matching on 11.8% of ticks pooled, headroom the
policy could not convert into settling speed. Residual (non-arrived)
counts never favoured the candidate in any cell, and colony M = N
exceeded the 20% exclusion cap, falling to residual counts that also
favour Control B; dropping the
uninterpretable cell's surviving seeds moves the pooled statistic only
from 0.9797 to 0.9789.

**The retained finding is the pre-named alternative.** Greedy dispatch is
47% slower than optimal dispatch on the same pools (gm(A/B) = 1.4739, CI
[1.4103, 1.5411], over the 278 of 396 seeds surviving the same severity
rule): the anonymous-pool advantage overwhelmingly lives in
the one-shot matching made at dispatch, which is fully expressible today
through `set_path_agent_goal` with no library change. The caller recipe:
solve the assignment well once (optimal is cheap at 48 x 96), and do not
bother reassigning mid-movement on distance grounds -- with no surplus it
showed no benefit and trended harmful.

**Gates all passed**: `GoalOccupied` zero in every arm (its reclassification
as a defect gate held), flow admission and retention identities at
quiescence, `superseded` exactly equal to reassignment calls so no
invisible goal mutation, `completed` equal to arrivals, per-tick
assignment validity, bit-identical replay of every arm on every seed.

**Decision: reject; no arm code merges.** Evidence (program source and
captured output) in `docs/planning/evidence/v1.0/c2-fungible/`.
Reconsideration: an in-tier mechanism using congestion-aware costs where
the caller layer cannot reach would be a new experiment; a distance-based
caller-layer policy is answered here.
