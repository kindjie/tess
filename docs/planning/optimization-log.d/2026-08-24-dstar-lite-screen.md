## 2026-08-24 - D* Lite: passes the work-ratio bar, rejected 23-to-1 on wall time

**Hypothesis** (pre-registered in issue #255, applied without
amendment): for a caller repeatedly querying one goal under
version-marked churn, D* Lite's incremental repair beats fresh
canonical search -- gated in two stages, work ratio first (>= 1.5x
median across the pinned trace) and wall time on hardware second.

**Stage 1 passed -- the only P-stream candidate to clear its
feasibility bar.** Pooled median incumbent/candidate primitive-op ratio
1.913 across wall-gap and rubble at 64/256 with swept edit locality and
frequency; per-cell medians 0.62 to 15.4. Every correctness gate held
on every cycle: cost equality with the BFS oracle and the fresh
incumbent, oracle-checked route validity, NoPath agreement,
deterministic replay. No library change existed in either arm; the
candidate is recorded program source. One D* Lite implementation trap
recorded for reuse: consistent vertices popped through the
underconsistent branch toggle g between infinity and rhs forever --
duplicates must be discarded on pop.

**Stage 2 rejected it 23 cells to 1 on M3** (timing arms unverified by
construction; correctness coverage is stage 1's, which ran the
identical trace construction with every answer checked). Sole pass: wall-gap,
uniform edits, E=1, 256x256 (-20.0%). Everything else confirmed
material regressions from +30.9% to +2,846%, worst where edits are
frequent or route-local. Per the pre-registered platform-existential
rule the Deck leg was unnecessary. The inversion mechanism: stage 1
counts a dial-frontier bump over packed arrays as equal to a
pair-keyed lazy-heap push plus rhs re-scans plus per-edit repair
cascades that run whether or not any query needs them. Against this
incumbent the 1.5x bar was an order of magnitude too generous.

**The reusable output is the calibration rule**: a stage-1 work-ratio
bar must be set against the measured per-op cost gap versus the
incumbent (here ~10x), not against parity. Recorded for any future
incremental-search candidate alongside the reconsideration boundaries:
P6's opening condition remains unmet (no new open-set structure
merges), and P2 is not reopened (cost, not semantics, decided this).

Evidence: `docs/planning/evidence/v1.0/p5-dstar/` (stage-1 capture,
A/A and A/B JSONs, programs as source including the branch-only bench
diff, binary hashes).
