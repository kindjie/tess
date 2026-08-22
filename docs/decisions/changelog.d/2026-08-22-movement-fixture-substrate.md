## 2026-08-22 - Movement experiments share one fixture substrate

The v0.13-to-v1.0 execution plan's movement stream named five fixture
families — warehouse, ring, colony, random, adversarial — as if they were
existing infrastructure. They were not in the tree. `warehouse` and
`adversarial` appeared in no source file, the PIBT tier's in-repo evidence was
one pinned ring seed with a test-local breadth-first helper, and the screening
harness that produced that tier's gate evidence lived outside the repository
and no longer exists, as its own study records under limitations.

Three queued experiments name those families: the PIBT hindrance tie-break,
the reciprocal-conflict screen, and the execution-delay harness. Each could
have narrowed itself to whatever happened to exist, but three separately
narrowed fixture sets produce three results that cannot be compared, which is
the one thing a serialized stream exists to prevent.

The substrate is therefore built once, before any arm exists, and the plan
gains a preceding PR. Four decisions inside it are load-bearing:

Seeds are a closed formula over a trial index with pre-registered trial
counts, not a curated list. A list would prove commit order rather than
knowledge order, since the same author controls when the fixtures land and
could have run an arm privately first. A formula leaves no per-seed freedom,
and any later exclusion must be recorded with its cause.

Every option that can censor a result is owned by the fixture rather than the
experiment. `SwapPolicy` changes what "no mover could succeed" means. The knob
that can manufacture seals outright is `blocked_exhaustion_policy`: under
`MarkUnreachable` an exhausted agent settles, and a settled agent can cut
another agent's goal off. It is pinned to `RemainBlocked` rather than
inherited, so exhaustion never settles anyone. Under that policy
`max_blocked_retries` still matters, because exhaustion stops an agent
replanning, so it is pinned large as well.

A fourth option turned out to belong in the same category and was missed at
first. Settling changes passability for the movement class, so retained routes
crossing a newly settled tile go stale, and a stale-routed agent parks rather
than replanning. Left unhandled, the harness manufactured its own deadlocks:
354 of 960 agents on the ring family classified as wedged, on the lattice
where this tier's own pinned regression solves the entire population. The
substrate therefore invalidates routes on every settled-set change, and that
choice is fixture-owned for the same reason the others are. A regression test
pins the ring family at zero wedges.

Runs terminate on a no-progress fixpoint, with the tick cap demoted to a
safety bound. Classifying at a cap is invalid because the terminal set grows
monotonically and is final only at quiescence; under a shared cap an agent
still making progress would be recorded as a mover failure when in fact the
experiment stopped, which biases residual counts against slower arms.

Classification carries five categories rather than the three the prior
evidence used. A live mutual wedge is invisible to a reachability test under
the terminal set — neither wedged agent is terminal, so both read as ordinary
residuals — and that category is precisely the deadlock signal the
execution-delay harness needs. A goal tile held by a terminal agent is an
assignment failure that fungible-goal work would fix by reassignment, and is
kept separate from a corridor seal, which no reassignment helps.

Two claims made while drafting are recorded as withdrawn rather than dropped.
Reusing the ring lattice shares the terrain but does not make results
numerically comparable to the pinned ring regression, which ranks with
per-agent exact breadth-first tables rather than the route-attachment oracle
the substrate pins. And the pinned ring seed does not demonstrate neutrality:
it was selected because it discriminates one movement tier from another. It
remains admissible for the hindrance axis only because it was not selected on
hindrance.

The congestion revalidation experiment is explicitly not a consumer. Its
terminal-classification requirement is judged by the web_colony demo's own
recovery classifier, whose vocabulary is distinct and stays distinct.
