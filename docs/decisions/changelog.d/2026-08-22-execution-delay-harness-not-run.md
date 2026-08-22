## 2026-08-22 - Execution-delay harness dispositioned without a run

The v0.13-to-v1.0 queue listed a private harness that would inject delayed and
uncertain action completion, compare three execution models, and decide whether
a new execution contract was justified. It is closed without being built.

Two findings closed it, and both are about what the harness could measure
rather than about the idea being uninteresting.

The library does not assume same-tick completion in any way a caller is bound
by. Occupancy and reservation are caller-owned world fields, and both movement
tiers treat an externally set occupancy bit as an immovable do-not-enter, so a
caller that wants to animate a move across several ticks already has the
primitive: commit, then hold the tiles. That recipe uses only documented
fields. The execution layer is already the caller's, on the same boundary the
earlier local-movement study drew for assignment.

The one capability an in-tier mechanism could add is suppressing same-tick
chain admission into a held origin. Both tiers admit a follower into a vacated
origin inside the advance call and expose no option to prevent it, while a
harness can only re-mark occupancy after the call returns. The arm representing
held tiles is therefore permanently one tick more permissive than the model it
names. That bias is not neutral: it points toward "held tiles cost little",
which is the conclusion the experiment would have reported.

A second defect made the intended comparison empty. The movement tier
reconsiders every active agent every pass, so an agent blocked on a held tile
already retries and is admitted on the first tick after release. An explicit
dependency-edge arm would have differed from a held-tile arm only in whether
the harness applied releases before or after the advance call — a loop-ordering
convention rather than a mechanism. A flat result would have confirmed the
pre-registered expectation without testing it, which is the failure mode the
screening study named when it warned that an exact zero across variants is a
wiring defect until instrumentation proves otherwise.

Related and worth recording: permanent deadlock cannot be caused by held tiles,
because at a fixpoint nothing is in flight and no tiles are held. The category
the harness was built to populate is structurally near-empty.

The pre-RC gate is a recorded disposition rather than a forced production
change, and this is one. Reconsider if a tier option ever suppresses chain
admission into a designated tile, or if a consumer reports a real failure under
the commit-at-decision recipe.
