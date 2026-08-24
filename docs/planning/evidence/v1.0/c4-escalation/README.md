# C4 conflict-local escalation, Phase A: retained evidence

Pre-registration: issue #253, with amendment 1 (region growth 2 -> 4,
the imminent-seal trigger, the split substrate gate) recorded after the
first gate run's diagnosis and before further tuning, and the closing
disposition comment recording the final measurement. Unlike the
rejected screens, the MECHANISM MERGES -- as harness-level test support
(`tests/escalation_harness.h`) with its gates
(`tests/tess_escalation_test.cc`) -- because its pinned tests are the
evidence and C4's fixtures gate any successor. No library code changed;
no public API exists; Phase B (the authority grant) is NOT proposed,
per the design decision fragment merged alongside.

**Disposition: attempted-with-partial-success.** All three motivating
conflict classes resolve within C3's own pre-registered bound; the
substrate gate failed as written and the failure is pinned rather than
weakened.

## The fixture gates (all pass, deterministic on replay)

| fixture | C3 plain verdict | escalated | bound | fires |
|---|---|---|---|---|
| pocket_yield/forbid | FAIL (2 wedged) | **24 ticks, all arrive** | 24 | 1 (region growth to radius 4 recovers the mid-corridor pocket) |
| junction_cross/forbid | FAIL (4 wedged) | **20 ticks, all arrive** | 27 | 1 (3,024 solver states) |
| queued_yields/forbid | FAIL (2 arrived + 2 sealed) | **22 ticks, all arrive, zero seals** | 45 | 2 (imminent-seal trigger) |
| head_on/permit, rotation x2 | PASS | identical, zero fires | — | 0 |

queued_yields is the decisive one: C3 proved the plain tier's partial
success creates the unsolvable instance (arrivals wall the corridor),
and terminal-set monotonicity means a formed seal can never be
repaired. The K = 8 no-progress trigger is structurally too late (the
seal forms at tick ~7); the amendment's imminent-seal trigger -- probe,
at arrival-adjacency, whether settling would strand any live agent, and
escalate with the arriver and its victims -- prevents the seal by
ordering, which is exactly what the joint solver's simultaneous-
objective semantics provide.

## The substrate measurement (the gate that failed)

132 C0 seeds, plain vs armed, all deterministic on replay:

- **71 clean seeds: strictly inert.** Zero fires, digest-identical.
- **61 residual seeds:** 56 identical; 3 strictly better (warehouse t4,
  adversarial t0 and t9 each convert a sealed agent to arrived); 1
  mixed (colony t10: two wedged resolve, one agent ends sealed); 1
  worse (warehouse t10: one arrived agent ends sealed). Aggregate: +3
  arrived, -2 wedged, -1 sealed, 91 fires.

Per-agent severity non-worsening therefore fails on 2 of 61 residual
seeds. The mechanism's local interventions are oracle-exact, but they
perturb the global trajectory, and the divergence can strand an agent
far from any fired component. Per the pre-registration's stop
condition, the gate was applied, failed, and not amended a second
time: always-on arming is declined, and the pinned substrate test
records the exact deltas so any successor must consciously flip them.

## Mechanism defects found, fixed, and recorded for successors

1. **Settled agents inside a region must not invalidate the plan they
   were modeled in.** The first executor aborted whenever any
   non-component agent stood on a region tile -- but settled agents are
   static obstacles the solver already accounted for. Fix: a
   planning-time position snapshot; only an outsider that MOVED into
   the region aborts.
2. **An aborted plan tick must fall through to a normal tier tick.**
   The first executor consumed the tick on abort, so a fire/abort
   cycle froze the population until the wedge rule classified everyone
   wedged (observed: 18 fires, 16 aborts, a 48-agent warehouse seed
   collapsing from 47 arrived to 21).
3. **Both triggers need cooldowns** after failed or aborted attempts,
   or detection thrashes; and the imminent-seal probe must be memoized
   (position, goal, terminal-count) or it re-runs a full-map
   reachability sweep per blocked agent per tick.
4. **Solver cost containment took three layers.** Packed `(u64 key,
   parent)` nodes instead of state vectors; a futility memo so an
   identical wedged configuration never re-burns the cap after a
   cooldown; and amendment 2's cap reduction (2M -> 250k), verified
   outcome-identical on the full sweep because every successful solve
   used at most ~71k states while 6-agent joint spaces (~10^9) cap out
   at either limit. Together: the armed sweep fell from 10+ CPU-minutes
   to 13 seconds (release).

The merged per-seed substrate tests pin a deterministic 18-seed subset
(trials 0 and 1 of every family plus every divergent seed), one test
per seed so the slowest sanitizer runner stays inside the repository's
60-second per-test contract; `substrate-sweep.txt` is the
captured FULL sweep and `programs.md` records the program that
reproduces it.

## What promotion would require (the two recorded blockers)

- **Bounding trajectory divergence**: an intervention protocol whose
  global per-agent effect is provably non-worsening, or an explicit
  quality-delta contract replacing Pareto safety.
- **Incremental reachability** in place of per-tick BFS probes for the
  imminent-seal trigger.

Tick counts and classifications decided everything; no wall-time claim
is made and no hardware campaign ran.
