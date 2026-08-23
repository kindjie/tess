## 2026-08-23 - Reciprocal conflicts: production tier fails three fixtures; C4 opens

**Question** (pre-registered in issue #247 plus one amendment recorded
before the evidence): do the current movement tiers resolve canonical
reciprocal conflicts within a pre-registered bound -- arrival at the
no-progress fixpoint AND ticks <= max(3 x optimal, optimal + 8), optimal
from an exhaustive joint-space BFS oracle -- or does PR C4
(conflict-local temporal escalation) open on a proven failing fixture?

**Answer: C4 opens.** The production PIBT tier
(`tick_weighted_path_agents_with_pibt` under `RouteAttachmentRanking`,
the C0-pinned configuration) fails three of six hand-built fixtures,
deterministically: the mid-corridor pocket-yield corridor (both agents
wedge), the four-agent junction cross (all four wedge), and the
queued-yields corridor. Where it passes, it is exactly optimal (Permit
head-on swap in 5 ticks; the 2x2 rotation cycle in 1 tick under both
swap policies).

**The failure is myopia, not ranking.** A diagnostic arm ran the same
machinery under per-agent exact BFS ranking -- the configuration the
pinned pocket-yield regression proves can yield when the yielder is
cornered adjacent to the pocket -- and it fails the same three fixtures
identically. A mid-corridor pocket is enterable from exactly one tile;
the retreating yielder passes it while retreat and pocket rank equally,
and beyond that tile no single-step decision recovers. One step of
foresight separates the pinned regression's pass from these failures.

**The queued-yields self-seal is the sharpest exhibit.** The tier's
partial success creates the unsolvable instance: one opposing pair
arrives and settles, and the settled tiles wall the one-wide corridor,
structurally sealing the other pair's goals on terrain that is fully
open (`structural_seals = 0`). Order-of-arrival is the difference
between the oracle's full 15-tick resolution and a permanent seal, which
gives C4 a concrete case beyond any bounded look-at-the-live-conflict
horizon -- the plan's WHCA-failure-mode clause made concrete.

**Everything merged.** Fixtures, oracle (anchored on hand-computed
cases: rotation 1; Permit head-on 5, since parity makes 4 impossible;
Forbid bare corridor unsolvable), digest table with `SwapPolicy` mixed
in, and verdicts pinned at observed values -- three of them failures
that C4 must flip. Context arms recorded: the joint movement tick
passes swaps and rotations and fails the three foresight fixtures; the
sequential mover fails everything, as the library documents (no swap or
cycle capability). Every arm replays bit-identically. Tick counts
decide; no timing, so no two-platform campaign.

Evidence: `docs/planning/evidence/v1.0/c3-reciprocal/`; regression
suite: `tests/tess_reciprocal_conflict_test.cc`.
