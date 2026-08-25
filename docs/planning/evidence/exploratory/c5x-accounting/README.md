# C5x congestion-accounting screen: retained evidence

Pre-registration: issue #269 with amendments 1-3, each posted before
the run it governs. Exploratory and explicitly may-or-may-not-merge:
this screen produces an ORDERING plus per-scenario boundaries, not an
accept/reject verdict; a merge case would need the full
supported-population matrix per the C5 precedent. Single platform
(M3): tick counts, classifications, and the planning-load proxy are
deterministic simulation outputs.

Protocol: the demo's own model driven through its PUBLIC pricing mode
(`ColonyModel::set_congestion_pricing`) -- the browser demo and this
evidence run the identical code path -- across seven scenarios x
populations {256, 1024}, canonical plus seven policies plus a
per-policy replay (all bit-identical; all wall admissions asserted).
Amendment 3 governs the recorded run: repricing publishes versioned
content marks but requests replans ONLY from agents whose remaining
retained route crosses a tile whose price increased (price never
invalidates a route; decreases trigger nothing). The amendment-2
capture -- global replan on every repricing -- is superseded and
recoverable from git history; live browser observation exposed its
defect (replan-queue saturation, far-field oscillation, and per-tick
compute inflated up to ~500x, e.g. the memory policy at ~84 ms/tick
vs ~1.6 ms scoped).

## Ranking (amendment-4 unified table: 13 policies, one protocol;
safe cells; gm of policy/canonical ticks; planning load as gm of
policy/canonical pending-plan integrals)

| policy | tick gm | load | safe | note |
|---|---|---|---|---|
| cool (prox1 halo + TRUE-cooling memory) | **0.3947** | 5.3x | 14/14 | overall best; fixing the ceil-residue turned memory from mid-pack into first |
| peak1 (peaked kernel) | 0.4052 | 5.4x | 14/14 | beats its flat counterpart (prox1 0.4118) -- per-agent gradients help |
| peakcool (peak + cooling) | 0.4111 | 5.5x | 14/14 | combo does NOT stack: no better than prox1 |
| prox1 (validated C5 recipe) | 0.4118 | 5.4x | 14/14 | the anchor |
| stallcool (stalled halo + cooling) | 0.4209 | **2.0x** | 14/14 | new efficiency frontier: better value than stalled at the same load |
| decay (ceil memory, permanent residue) | 0.4263 | 5.5x | 14/14 | superseded by cool |
| stalled | 0.4288 | 2.0x | 14/14 | |
| self | 0.4291 | 5.4x | 14/14 | |
| prox2 | 0.4306 | 5.4x | 14/14 | |
| stallpeak (stalled + peak) | 0.4325 | 2.1x | 14/14 | combo does not stack |
| demand | 0.5557 | 5.7x | 12/14 | STILL DISQUALIFIED (structural feedback) |
| queue (v1, ungated uniform) | 0.6348 | 3.9x | 14/14 | manufactures queues: convoy-blind detection + uniform thin-line pricing herds escapees into one lane |
| queue2 (stall-gated, graded) | 0.6906 | **1.09x** | 13/14 | minimal-intervention specialist: leaves healthy flow untouched (open 1.000), near-zero planning cost, kills v1's goal-wall pathology (1.12 vs 2.18); narrowly fails no-worse on tip/1024 (503 vs 505 at the cap) |

## The findings that matter (amendment-4 round)

1. **True cooling wins outright.** Removing the memory policy's
   permanent +1 residue (floor instead of ceil halving) moved it from
   mid-pack to the best pooled value measured in this stream.
2. **Peaked kernels beat flat ones** (0.405 vs 0.412): giving every
   agent a local gradient instead of a plateau differentiates escape
   routes, as the maintainer's kernel question predicted.
3. **Most combinations do not stack.** peak+cooling and stall+peak
   add nothing over their better component; the exception is
   stallcool, which improves the efficiency frontier (0.421 at 2.0x
   load vs stalled's 0.429).
4. **Stall-gating transforms the queue policy from harmful to a
   cheap specialist.** queue2 prices only real jams: healthy maps see
   literally no intervention (open gm 1.000, planning load 1.09x),
   the v1 queue-manufacturing artifact is gone from the demo, and the
   corridor pathology drops 2.18 -> 1.12 -- but it remains worst
   pooled among safe-ish arms and repeats a two-arrival no-worse
   failure on the one cell canonical itself cannot complete.
5. Amendment-2/3 findings stand: scoped replanning is the real
   optimization; demand pricing is structurally unsafe.

## Files

- `<scenario>.txt` -- per-cell captures (arm rows: arrived,
  crowd-blocked, unreachable, ticks, turnaround, gate, planning-load
  integral).
- `programs.md` -- the recorded screen program; policy and scoping
  implementations live in the demo model
  (`examples/web_colony/colony_model_internal.h`).
