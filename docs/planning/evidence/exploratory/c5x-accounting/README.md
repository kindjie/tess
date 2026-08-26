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
compute inflated severely -- the recorded case is the memory policy
at ~84 ms/tick vs ~1.6 ms scoped, about 53x; larger ratios were
observed on other policies in the same browser session but were not
captured).

## Productization note (post-screen)

The pricing engine measured here was subsequently relocated: the
colony demo reverted to a clean tutorial, and the engine now lives in
`examples/web_congestion/` (the congestion lab), wrapping the colony
simulation through its native seam and scoping replans through
`tess::experimental::request_replans_for_route_crossings`. The
captures in this record describe the in-model code path as measured
(recoverable from this branch's git history); the relocated path is a
new measurement surface, and the planned supported-population matrix
will drive it directly. Decision record:
`docs/decisions/changelog.d/2026-08-26-congestion-productization.md`.

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

## Amendment-7 round: the escalation family on the relocated lab
(captures `a7-*.txt`)

Run through the congestion lab's native runner (the relocated code
path) across the seven standard scenarios plus the corrected `maze`
scenario, 16 cells, canonical + cool + stallcool + queue2 + the two
new arms, per-arm replays bit-identical, every wall admission ok.

| arm | pooled gm (16 cells) | safe | maze/1024 |
|---|---|---|---|
| cool | 0.4119 | 16/16 | 2,828 ticks, all arrive |
| **escal** (duration-escalating stall price) | 0.4220 | 16/16 | **2,471 ticks, all arrive -- best recorded** |
| radiate (duration-escalating, radiating cone) | 0.4349 | 16/16 | 2,627 ticks, all arrive (best at 256: 1,375) |
| stallcool | 0.4585 | 16/16 | 2,758 ticks, all arrive |
| queue2 | 0.6753 | 16/16 | 5,000 ticks, 297 arrive -- no better than canonical |

Findings: (1) the maze separates policies as its forward registration
predicted -- canonical strands 727 of 1,024 at the cap, and the
stall-gated queue policy helps not at all there, confirming its
chain-not-blob blind spot live; (2) the maintainer-proposed
duration-escalation family wins the capacity-bound geometry outright,
with plain magnitude escalation (escal) beating the radiating cone at
1,024 agents and the cone winning at 256 -- the registered hypothesis
(radiate best on capacity-bound maps) held for the family, not for
the specific variant; (3) pooled across all 16 cells the escalation
family lands second and third behind cooling memory, ahead of every
other fixed-radius policy. Note the anchors' pooled values shift
slightly from earlier rounds: this round ran on the relocated lab
path and includes the maze cells; comparisons are within-round.

## Amendment-8 round: on-path variant + planning-budget arms
(captures `a8-*.txt`)

Same 16 cells and runner as amendment 7, per-arm replays
bit-identical. The policy arms reproduce amendment 7's pooled values
exactly (cool 0.4119, escal 0.4220, radiate 0.4349 -- an incidental
replication check on the relocated path) and add the maintainer-user's
on-path variant.

| arm | pooled gm (16 cells) | safe | total scoped replans | maze/1024 |
|---|---|---|---|---|
| cool | 0.4119 | 16/16 | 153,818 | 2,828 ticks |
| escal | 0.4220 | 16/16 | **100,494 (lowest)** | **2,471 ticks** |
| **onpath** (escalating price along own route) | 0.4225 | 16/16 | 107,586 | 2,554 ticks |
| radiate | 0.4349 | 16/16 | 102,454 | 2,627 ticks |

Findings: (1) onpath is value-indistinguishable from escal (0.4225 vs
0.4220) at ~7% more planning load -- restricting the escalating price
to the stalled agent's own remaining route neither helps nor hurts at
this scale, so the escalating magnitude, not the footprint, carries
the effect; (2) escal remains the efficiency point of the family:
within 2.6% of cool's pooled value at two-thirds of its planning
load.

Budget arms (policy 29 at {4, 16, 32, dynamic} vs default 8): static
4 is consistently harmful at 1,024 agents (browser-incremental 1,324
ticks vs 687 default); static 32 posts the best tick counts at 1,024
in most scenarios (two-gates 432 vs 747); the amendment-8 dynamic
rule lands within ~5% of static 32 while submitting ~24% fewer
replans (maze 44,474 vs 58,614). At 256 the backlog rarely exceeds
the default budget and the arms separate weakly. The registered
wall-time and backlog metrics were not captured this round (gap
admitted at registration); amendment 9 adds the columns and reruns
the budget arms.

## Amendment-9 round: planning-budget family, and a measurement
## correction (captures `a9-*.txt`)

The budget control gained an option family: static N, the amendment-8
dynamic rule, unbounded (drain the backlog), and a **work budget** --
the deterministic stand-in for a fixed time budget, fitting the request
count to a 16,384-expanded-node target from the previous tick's
measured cost per search. A true wall-clock budget is excluded by
design: it reads the host clock, so identical inputs would replan
different agents on different machines and replay identity would break.

Enabling library change: `PathAgentFrameStats::expanded_nodes`, the
deterministic work meter the node target reads.

**Measurement correction.** The first amendment-9 run measured its
wall-clock columns on `build/dev` (`CMAKE_BUILD_TYPE=Debug`). Those
columns were void and are withdrawn; the run was repeated on the
Release `examples` preset. Ticks, arrivals, expansions and backlog are
build-independent and verified identical across both builds, so only
timing was affected. Debug inflates the templated A* inner loop enough
to reverse the ordering: on open/1024 the unbounded budget moves from
worst on CPU to best. The captures retained here are the release run.

Geometric means vs the default budget (8 plans/tick) at 1,024 agents,
`walls=REFUSED` cells excluded:

| mode | ticks | wall | worst late tick | expansions |
|---|---|---|---|---|
| static 4 | 1.227 | 1.042 | 0.733 | 0.854 |
| static 16 | 0.854 | 1.221 | 1.417 | 1.407 |
| static 32 | 0.735 | 1.512 | 2.229 | 1.905 |
| dynamic | 0.782 | 1.406 | 2.088 | 1.718 |
| unbounded | 0.666 | 2.373 | 10.341 | 4.056 |
| work budget | 0.777 | 0.984 | 2.303 | 1.200 |

Findings: (1) **no mode dominates** -- fewest ticks is unbounded, at
2.37x the elapsed time, 4.06x the search and a 10.3x worse worst tick;
lowest latency tail is static 4, paying 1.227x in settle ticks; the
work budget has the best settle-per-elapsed-time. (2) The work budget's
0.984 must not be read as free: **5 of 8 scenarios are slower on
elapsed time**, the near-1.0 mean is carried by maze,
browser-incremental and open, and it costs 1.20x the search and 2.30x
the worst late tick. (3) The unbounded budget's cost is a thundering
herd measured directly in search work -- maze/1024 runs 1.82 billion
expansions against the default's 113 million -- and it appears only
under congestion; on open geometry expansions stay flat across modes.

Boundaries on this round: `wall_ms` is weak evidence at this resolution
(two back-to-back replays measure adjacent-run jitter rather than
run-to-run variance, cross-session drift is ~1.5%, `open` cells are
integer-quantized at 2-4 ms, and runs execute in fixed budget order so
thermal drift is confounded with mode). Ticks and expansions are exact
and replay-identical. The unbounded budget also invalidates the
browser-guard scenario at both populations (`walls=REFUSED`): draining
the full backlog shifts pre-wall trajectories so agents occupy scripted
wall tiles before the tick-4 batch. Matrix scenarios must place walls
at tick 0 or gate admission on acceptance rather than tick number.

## Amendment-10 round: adaptive budget switching -- NEGATIVE RESULT

Two signals were registered and both failed their pre-declared
separation test; no rule was built and no threshold was fitted
afterwards.

- **10, replan productivity** (routes changed / searches drained):
  0.97-1.00 in all eight scenarios, no separation. Pricing perturbs
  costs, so the optimal route differs on nearly every search; the
  measure detects difference, not value.
- **10b, rescue rate** (stalled agents that moved within 8 ticks of
  receiving a replaced route): informative at the extremes -- lowest in
  goal-wall (0.729) and maze (0.747), the two worst search blowups,
  highest in open (0.993) -- but the required grouping overlaps by
  0.004 (browser-incremental 0.892 vs tip 0.896). Confounded: an agent
  can unstick because the agent ahead moved, unrelated to its own
  replan.

The instrument is retained behind `--measure-productivity`, verified
non-perturbing (identical ticks, arrivals and scoped replans with it
on). Budget selection remains a documented caller choice.

## Files

- `<scenario>.txt` -- per-cell captures (arm rows: arrived,
  crowd-blocked, unreachable, ticks, turnaround, gate, planning-load
  integral).
- `programs.md` -- the recorded amendment-2..6 screen program
  (historical). From amendment 7 on, the program is the congestion
  lab's native runner (`examples/web_congestion/congestion_native.cc`)
  and the policies live in the lab model
  (`examples/web_congestion/congestion_model.cc`); the tutorial demo
  carries only the planning-budget seam.
