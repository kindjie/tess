## 2026-08-23 - Conflict-local escalation: resolves all three C3 conflicts; global arming declined

**Question** (pre-registered in issue #253 with two recorded
amendments): can a conflict-local, completion-planning escalation --
detection, bounded component extraction, an exact joint-space solver,
ordered execution -- flip C3's three pinned production-tier failures
within C3's own bound while preserving canonical behavior elsewhere?

**The mechanism** (Phase A, harness-level, behind no public API):
trigger on K = 8 no-progress ticks OR an imminent seal (an arrival
whose settle would strand a live agent -- discovered necessary because
queued_yields' seal forms at tick ~7 and terminal-set monotonicity
makes repair impossible, only prevention); component closure with
region radius 2 growing once to 4 on unsolvable; A_max 6, T_max 32,
solver cap 250k states (amendment 2, reduced from 2M, verified
outcome-identical), over-bounds = counted skip, never truncation;
whole-plan invalidation; deterministic throughout.

**Fixture gates: all pass.** pocket_yield 24 ticks (bound 24, 1 fire),
junction_cross 20 (bound 27, 1 fire, 3,024 solver states),
queued_yields 22 (bound 45, 2 fires, zero seals, all four arrive --
the C3 self-seal exhibit resolved by ordering). The three C3 passes
are untouched: zero fires, tick-identical, digest-identical.

**Substrate gate: failed, and the failure is the finding.** Across the
132 C0 seeds: all 71 clean seeds strictly inert (zero fires,
digest-identical); 61 residual seeds -> 56 identical, 3 strictly
better (three sealed agents converted to arrived), 1 mixed, 1 worse;
aggregate +3 arrived, -2 wedged, -1 sealed. But per-agent severity
non-worsening fails on 2 seeds: a locally sound, oracle-exact
intervention still perturbs the global trajectory, and the divergence
can strand an agent far from any fired component. Per the
pre-registration's stop condition the gate was applied, failed, and
NOT amended a second time.

**Disposition: attempted-with-partial-success; Phase B not proposed.**
The mechanism, fixtures, and pinned substrate measurement merge as
test support; the production tier is unchanged and C3's pins still
describe it; no planning authority is granted. Three fixed mechanism
defects are recorded for any successor (settled-agents-in-region plan
invalidation, aborted-tick starvation, trigger cooldowns), plus the
open problem that decides promotion: bounding trajectory divergence --
an intervention protocol whose global effect is provably no worse per
agent, or an accepted quality-delta contract in place of Pareto
safety. Detector cost (per-tick reachability probes) is a second
promotion blocker, needing incremental reachability.
