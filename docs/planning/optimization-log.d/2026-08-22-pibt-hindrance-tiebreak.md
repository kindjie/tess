## 2026-08-22 - PIBT hindrance tie-break rejected on classification regression

- **Area:** the PIBT movement tier's resolution of candidates that tie on the
  route-attachment rank, currently decided by enumeration order.
- **Hypothesis:** choosing among equal-rank candidates by a hindrance measure
  improves movement quality on dense fixtures without regressing terminal
  outcomes, determinism, or tick cost. Pre-registered as issue #238 with one
  amendment recorded before implementation.
- **Definition, rewritten before implementing.** The first definition —
  "agents for which this tile is currently their best-ranked candidate" — was
  withdrawn as incoherent with the algorithm. PIBT computes ranks lazily, per
  agent, only when that agent begins deciding, and discards the frame after.
  Under the reading "ranks retained from earlier deciders" no such store
  exists, so the highest-priority decider always sees hindrance zero; under
  the reading "decided agents' chosen tiles", every decided destination is
  already claimed and vertex-rejected before a tie-break could see it. Both
  readings degenerate. The replacement counts other active agents whose
  retained route's next point is the candidate tile, which is fixed before any
  decision in the pass, independent of decision order, and adds no
  ranking-oracle calls.
- **Implementation:** no library change. The tie-break composes into the
  ranking oracle as `rank * 8 + min(hindrance, 7)`, which makes attachment the
  primary key, hindrance the secondary, and enumeration order the tertiary —
  the tier already breaks rank ties by enumeration order. Both arms are two
  oracle objects in one binary. The composition is piecewise because
  `RouteAttachmentRanking` returns disjoint attached and detached ranges and
  scaling the detached range would overflow; a test pins that the scaled
  attached maximum stays below the detached base for the committed shape.
- **A design flaw the tests caught.** The first draft rebuilt the hindrance
  index before each tick. Routes do not exist until the tier's planning pass
  runs, and planning happens inside the same call as movement, so that index
  was a pass stale and empty outright on the first tick. The rebuild moved to
  first use within a pass, which places it after planning and before any
  decision. It stays order-independent because the index is a pure function of
  the agent array and retained routes, neither of which a ranking call
  changes.
- **Method:** C0's committed fixtures and seed schedule — 132 seeds across
  seven families — run paired, arm against arm, on identical instances. Both
  arms replay bit-identically per seed, which is a gate rather than a result.
- **Result, classification:** on the colony family the candidate produced **22
  agent-level regressions against 13 improvements**, and sealed counts rose
  from 294 to 302. Every other family was byte-identical in classification.
- **Result, exclusion:** colony excluded **12 of 20 seeds** (60%) for
  classification change, far above the pre-registered 20% cap, so colony's
  tick metric is uninterpretable by the rule declared in advance and the
  decision there falls to residual counts — which regress.
- **Result, ticks:** pooled paired geometric mean over the 120 comparable
  seeds was 0.9948, a 0.52% improvement against an 8% material bar. On the two
  families with any tick headroom the effect is absent: warehouse was
  identical to the tick (2,246 against 2,246) and ring improved 0.11%.
- **Decision: reject.** The pre-registered stop condition fires on the first
  criterion — any seed regressing its terminal classification is a rejection —
  so the tick metric never needed to decide. It agrees anyway.
- **The null is not vacuous, but the first check offered for that was the
  wrong measurement.** A tie-break that never fires would produce the same
  flat result while testing nothing, so the claim needs evidence. Counting
  ranking calls with a nonzero hindrance term — 2.4% to 14.9% depending on
  family — does not supply it: the composition preserves every strict base
  ordering, so hindrance can only matter where two candidates in one decision
  frame tie on base rank and differ in hindrance. A nonzero term on a
  candidate that was never tied proves nothing.

  What does establish it is outcome divergence. Six of seven families produced
  different results under the two arms: ring 3,351 against 3,343 ticks,
  random_sparse 1,858 against 1,857, random_medium 1,854 against 1,855,
  random_dense 2,182 against 2,186, adversarial 372 against 369, and colony
  diverging on 12 of 20 seeds. The mechanism demonstrably fires and changes
  decisions; it simply does not improve them.

  Warehouse is the exception and is recorded as **inconclusive on engagement**
  rather than as the clearest case. Its two arms are identical — 2,246 ticks
  against 2,246, no classification change — so nothing distinguishes "the
  tie-break fired and did not matter" from "it never fired there".
- **Stopped early, not silently dropped.** The pre-registration also declared
  a 95% bootstrap interval over the seed set, swap and backtrack-depth
  secondaries, and an allocation gate. None was collected: the arm was
  rejected on the first stop condition, and the plan permits a correctness
  failure to stop an experiment before its remaining measurements. The
  backtrack-depth counter was never built. The allocation gate is unevidenced
  rather than passed — plausible, since no library code changed and the index
  reuses its capacity warm, but it was not measured.
- **Limitations:** the fixtures are one 64x64 shape and the tier is PIBT with
  `RouteAttachmentRanking`; a different oracle would tie differently and could
  give hindrance more to discriminate. No timing was measured, because the
  arm was rejected on correctness before tick cost could contribute, and the
  cross-hardware rule was therefore never reached. Colony's result is
  dominated by seals rather than live blocking, which this mechanism does not
  address, so colony was always a weak test of the hypothesis.
- **Reconsideration condition:** a ranking oracle whose ties are both frequent
  and load-bearing among top candidates, or a fixture family whose congestion
  is live blocking rather than sealing. A larger fixture shape would also
  require rechecking the piecewise composition before reuse.
