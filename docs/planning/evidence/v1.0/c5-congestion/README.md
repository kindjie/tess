# C5 dynamic congestion revalidation: retained evidence

Pre-registration: issue #256, with amendment 2 recording the
demo-classifier leg. Source under measurement: main `b87900a5` plus the
recorded programs in `programs.md` -- no library change exists in any
arm, by design: the priced arm writes an ordinary cost field through
the versioned edit channel (`mark_content_changed` +
`mark_pathing_dirty`), and the planners already compose `FieldCost`,
so the canonical machinery simply reads the new prices.

The experiment has TWO legs with opposite verdicts, and the execution
plan decides which one governs. The pre-registration as originally
filed put its gates on the C0 substrate; review correctly flagged that
the plan's C5 section pins the gate to the web_colony demo's own
recovery classifier ("arrived, crowd-blocked, durably unreachable").
Amendment 2 declares the demo leg the plan-mandated primary gate. It
was posted after the substrate results were in hand and after the
demo leg ran -- review-driven, like C4's amendment, and flagged with
the same caveat: not the default ordering.

**Verdict: the historical rejection does NOT reconfirm under the
plan's own gate.** Pricing is retained as a documented caller recipe
with explicit boundary conditions. No library change; the demo's
default congestion mechanism (route spreading) is unchanged.

## Leg 1 -- the plan-mandated demo-classifier gate (PASS)

`c5_colony_leg.cc` runs the shipped `wc::ColonyModel` through its own
native access seam: 5 scenarios (open, tip, two-gates, four-gates,
goal-wall) x populations {256, 1024}, canonical vs priced arms, plus
the demo's shipped spread mode as context. Pricing uses the
pre-registered policy (per-tile cost `1 + min(3, live agents within
Manhattan 1)`, every 4 ticks, versioned edits). Full capture:
`colony-leg.txt`.

- **Classification: retained or improved on every cell.** Zero
  crowd-blocked and zero durably-unreachable in BOTH arms on all 10
  cells. The one divergence favors pricing: on tip/1024 the canonical
  arm strands 519 agents at the demo's 5000-tick cap (arrival
  incomplete, turnaround never reached) while the priced arm delivers
  all 1024 in 1085 ticks. The historical failure signature (incomplete
  arrivals, crowd-blocked recurrence) appears in NEITHER priced cell.
- **Congestion value is large.** tip/256: 3362 -> 566 ticks.
  two-gates/1024: 4431 -> 531. two-gates/256: 1099 -> 358. open/1024:
  236 -> 158. On the two heaviest cells pricing also beats the
  shipped spread mechanism (tip/1024: 1085 vs 1323; two-gates/1024:
  531 vs 792).
- **The boundary.** Detour-shaped maps pay: goal-wall 408 -> 570
  (256) and 1004 -> 1146 (1024); four-gates/256 252 -> 269. Spread
  mode wins two-gates/256 (213) and four-gates/256 (211).
- Priced-arm replay is bit-identical per cell, and the scenario walls
  land at tick 4 through the demo's own `set_wall` channel -- full
  topology invalidation and graph rebuild compose with active pricing.

Tick counts and classifications decided everything; no wall-time claim
is made, so the cross-hardware acceptance rule is not triggered and no
hardware campaign ran.

## Leg 2 -- the substrate parity screen (fails as registered)

The originally-registered gate: identical per-seed terminal
classification multisets over 132 C0 seeds. It fails on **17 seeds**
(warehouse t3/t4/t17; colony t0/t1/t2/t3/t4/t7/t9/t10/t11/t13/t15
plus t17/t19; random_dense t19), in BOTH directions -- e.g. warehouse t3
loses an arrival to a seal while colony t3 converts four failures to
arrivals. Aggregate failure counts barely move (warehouse 10 -> 11,
colony 330 -> 328, random_dense 31 -> 30) and the tick metric is flat
where classification agrees (pooled gm 0.9977, CI [0.9810, 1.0180]
over 115 seeds). Full capture: `arms.txt` (the program exits nonzero
on this leg by design; its final line covers only mechanical checks).

Read together, the legs are not in conflict -- they measure different
things. The substrate's no-progress-fixpoint classifier scores
marginal wedge/seal seeds whose outcome is trajectory-sensitive, and a
price signal perturbs trajectories, so parity breaks chaotically --
the same divergence mechanism C4's escalation measurement documented.
The demo's recovery loop (replan queues, recovery scheduling) absorbs
trajectory changes and measures delivered outcomes. The recipe's
boundary follows directly:

- **Use where congestion is the binding constraint** (gate/corridor
  throughput): large tick wins, and it can rescue arrival-incomplete
  populations.
- **Do not use where per-seed terminal classification must be stable**
  under a fixpoint-style settle (C0-substrate-like consumers): 17/132
  seeds reclassify chaotically.
- **Expect modest regressions on detour-shaped maps** where pricing
  pushes agents around walls that were never contended.

## Gates, dispositions

| gate | leg | result |
|---|---|---|
| classification retained, failures no worse (demo classifier) | demo | PASS (one divergence, in pricing's favor) |
| replay determinism, priced arm | both | PASS (canonical-arm replay letter-gap noted in the history; canonical determinism is pinned by the substrate rebuild test and C4's replays) |
| edit/topology composition | demo (`set_wall`) + substrate (content-invalidation replay) | PASS |
| writes within declared bound (cost in [1, 4]) | both | PASS |
| per-seed classification parity (as originally registered) | substrate | FAIL, 17/132 -- retained as the sensitivity boundary, superseded as the verdict gate by amendment 2 |

**What follows for C6.** The demo leg DOES isolate a
capacity-contention premise -- gate throughput at tip and two-gates is
exactly capacity contention, and it is what pricing relieves. But the
premise is served by this caller recipe with zero library authority; a
bounded crossing reservation would target the same contention while
requiring new mechanism, and no evidence suggests the recipe is
insufficient there. C6 therefore dispositions without a run in X3 on
grounds of no UNSERVED premise, not no premise.
