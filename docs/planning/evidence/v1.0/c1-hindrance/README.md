# C1 hindrance tie-break: retained evidence

Source under measurement: the C1 branch at the commit this directory lands
in, over the C0 fixture substrate merged as `43aadf41`. No hardware campaign
ran: the arm was rejected on a correctness criterion, and the plan's rules
allow a correctness failure to stop an experiment before timing both
platforms.

## Programs

The three programs are recorded in `programs.md` as source, matching the
precedent set by the P1 seam stand-in, with their captured output alongside.
Each is standalone and reads only committed fixtures, so any figure quoted in
the optimization-log fragment can be regenerated rather than taken on trust.

- `decide` / `decide.txt` — the decision. Per-family agent-level
  classification regressions and improvements, seed exclusion counts against
  the pre-registered 20% cap, and the pooled paired geometric mean of
  ticks-to-settle over comparable seeds.
- `engagement` / `engagement.txt` — the check that the null is not vacuous.
  Counts ranking calls whose hindrance term is nonzero, per family. A
  tie-break that never fires would produce the same flat result while testing
  nothing.
- `per_family` / `per_family.txt` — per-family tick totals, geometric means,
  and wedged/sealed counts for both arms, which is where the colony seal
  increase (294 to 302) is visible.

## What decided the experiment

`decide.txt` line "AGENT-LEVEL regressions=22 improvements=13" is the
rejection. The pre-registered stop condition is that any seed regressing its
terminal classification rejects the arm, so the tick metric never had to
decide. Colony's 60% exclusion rate independently made its tick metric
uninterpretable by the rule declared in advance.

## Two predicates, and why they agree here

`decide` excludes a seed when any agent's classification changes severity;
`per_family` excludes on exact category inequality plus nonpositive ticks.
They agree on this data — colony 12 seeds under both — but would diverge on a
severity-neutral flip between wedged and goal-occupied. The decision rests on
`decide`, whose predicate matches the pre-registered rule.

`engagement.txt` covers five of the seven families. Its metric is retained
because it was run, not because it decides anything: the non-vacuity argument
in the optimization-log fragment rests on outcome divergence across six
families, not on this count. Counting calls with a nonzero hindrance term
cannot establish that hindrance ever discriminated among *tied* candidates,
which is the only place the composition can change a decision.

## Limits of these artifacts

The programs record aggregate counts, not per-seed tables, so a reader
checking an individual seed must rerun rather than re-analyse. The
determinism evidence retained here is narrower than a full per-arm
replay matrix (a pre-RC audit correction): the retained prototype's
`BothArmsReplayIdentically` test -- prototype source only, never a
registered target -- replayed the CANDIDATE arm on two sampled seeds
(warehouse and colony trial 0), and the decision program ran each arm
once. The canonical arm's determinism rests on the substrate's own
rebuild-reproducibility pinning, not on a C1 artifact; a reader
requiring stronger per-seed determinism evidence should rerun the
recorded programs twice and compare.
