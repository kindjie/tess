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

## Ranking (amendment-5 unified table: the full factorial, 22 arms,
one protocol; safe cells; gm of policy/canonical ticks; planning load
as gm of pending-plan integrals vs canonical)

The mechanism axes: signal source {all live agents | stalled only} x
kernel {flat | peaked} x memory {snapshot | true-cooling} x optional
stall-gated graded queue overlay. All sixteen factorial points plus
the six legacy arms ran under one roof.

| policy | tick gm | load | safe |
|---|---|---|---|
| **cool** (all+flat+cooling) | **0.3947** | 5.3x | 14/14 |
| coolq (+overlay) | 0.4020 | 5.3x | 14/14 |
| peak1 (all+peak+snap) | 0.4052 | 5.4x | 14/14 |
| peak1q | 0.4111 | 5.4x | 14/14 |
| peakcool | 0.4111 | 5.5x | 14/14 |
| prox1q | 0.4114 | 5.4x | 14/14 |
| prox1 (the validated anchor) | 0.4118 | 5.4x | 14/14 |
| **stallcool** (stall+flat+cooling) | 0.4209 | **2.0x** | 14/14 |
| peakcoolq | 0.4233 | 5.5x | 14/14 |
| decay (superseded ceil memory) | 0.4263 | 5.5x | 14/14 |
| stalled | 0.4288 | 2.0x | 14/14 |
| self | 0.4291 | 5.4x | 14/14 |
| prox2 | 0.4306 | 5.4x | 14/14 |
| stallpeak | 0.4325 | 2.1x | 14/14 |
| stalledq | 0.4354 | 2.0x | 14/14 |
| stallpeakcool | 0.4376 | 2.1x | 14/14 |
| stallpeakq | 0.4439 | 2.1x | 14/14 |
| stallpeakcoolq (all four) | 0.4446 | 2.0x | 14/14 |
| stallcoolq | 0.4477 | 2.0x | 14/14 |
| demand | 0.5557 | 5.7x | 12/14 DISQUALIFIED |
| queue (v1) | 0.6348 | 3.9x | 14/14 |
| queue2 (overlay alone) | 0.6906 | 1.09x | 13/14 (tip/1024: 503 vs 505) |

## The findings that matter (amendment-5 round)

1. **Composition is not additive; the registered hypotheses were
   REFUTED.** Both predictions (queue overlay composing well with
   stallcool and with cool) failed: every one of the nine higher-order
   arms is equal to or worse than its best component, and the queue
   overlay never improves any base field. The maintainer-requested
   triple (stalled+peaked+cooling, 0.4376) is worse than both
   stallcool (0.4209) and stalled (0.4288).
2. A plausible mechanism, stated as interpretation rather than
   measurement: combined signals add on the same tiles under the
   shared cap of 3, so the better component's gradient is clipped
   flat exactly where guidance matters most -- composition saturates
   rather than sharpens.
3. **The landscape has converged at this screening scale.** Three
   distinct optima stand: cool for value (0.395 at ~5.3x planning
   load), stallcool for efficiency (0.421 at 2.0x), queue2 for
   minimal intervention (near-zero cost, jams only, one narrow
   no-worse miss on the cell canonical itself cannot complete).
   Everything between them is a plateau, and further mechanism mixing
   at this scale is unpromising by the factorial's own evidence.
4. One incidental positive: the queue overlay INSIDE a base field is
   safe on all 14 cells everywhere (the base carries tip/1024), so
   overlay unsafety is specific to running it alone.
5. Amendment-2/3/4 findings stand: scoped replanning is the real
   optimization; true cooling beats residue memory; peaked beats
   flat; stall-gating makes queue detection convoy-safe; demand
   pricing is structurally unsafe.

## Amendment-6 round: the deferred axes (captures `a6-*.txt`)

Spread interaction, repricing period, and price-cap depth, applied to
the three optima with in-binary anchors. All recorded hypotheses were
again partially or fully refuted -- the round's value:

| arm | tick gm | load | safe |
|---|---|---|---|
| **coolS** (cool + spread) | **0.3876** | 5.5x | 14/14 |
| cool | 0.3947 | 5.3x | 14/14 |
| **stallcoolS** | 0.4013 | 2.3x | 14/14 |
| cool7 (cap 7) | 0.4036 | 5.4x | 14/14 |
| **queue2S** (queue2 + spread) | **0.4091** | **1.32x** | **14/14** |
| stallcool | 0.4209 | 2.0x | 14/14 |
| cool8 (period 8) | 0.4220 | 5.1x | 14/14 |
| stallcool7 | 0.4377 | 2.0x | 14/14 |
| cool16 | 0.4445 | 4.6x | 14/14 |
| spreadonly | 0.4543 | 1.29x | 14/14 |
| stallcool8 | 0.4671 | 1.9x | 14/14 |
| stallcool16 | 0.6497 | 1.7x | 14/14 |
| queue2 (anchor) | 0.6906 | 1.09x | 13/14 |

1. **Spread composes with pricing everywhere** (hypothesis of
   tie-fighting: refuted). It improves all three optima -- and it
   REPAIRS queue2's sole safety miss: tip/1024 goes from 503 arrivals
   at the cap to all 1024 in 1,043 ticks, and two-gates/1024 from
   3,802 ticks to 612. Mechanistically coherent: queue2 prices only
   real jams and touches nothing else; spread diversifies the
   equal-cost ties everywhere pricing is silent -- each covers the
   other's blind spot. queue2S is a new frontier point: near-champion
   value at 1.32x planning load, fully safe.
2. **Faster repricing wins; the period hypothesis inverted for
   stallcool.** Value degrades monotonically with period (cool 0.395
   -> 0.422 -> 0.445; stallcool 0.421 -> 0.467 -> 0.650) for modest
   load savings; stallcool degrades FASTEST, not least -- stall
   detection loses meaning when the window stretches.
3. **Deeper prices do not help** (cap 7 slightly worse for both
   bases): cap 3's clipped gradient is apparently already enough, and
   deeper wells overshoot into detours.
4. The overall leaderboard after six amendments: coolS for value
   (0.388), queue2S for value-per-cost (0.409 at 1.32x), stallcoolS
   between (0.401 at 2.3x). Every top arm now includes the demo's
   shipped spread mechanism -- the two congestion answers are better
   together than either alone.

## Files

- `<scenario>.txt` -- per-cell captures (arm rows: arrived,
  crowd-blocked, unreachable, ticks, turnaround, gate, planning-load
  integral).
- `programs.md` -- the recorded screen program; policy and scoping
  implementations live in the demo model
  (`examples/web_colony/colony_model_internal.h`).
