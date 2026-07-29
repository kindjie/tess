# Local movement resolution: a screening study

Status: **Screening study** (2026-07-28). This document records a rapid
elimination pass over ~17 candidate mechanisms for multi-agent movement
deadlock, run in a disposable harness against an unmodified `include/tess/`.
Its purpose was to decide what deserves serious investigation in the library
itself, not to be that investigation. Read the numbers as directional: the
harness was throwaway and is not preserved, several deciding cells rest on
single-digit sample counts (denominators are given with each table), and no
result here is authoritative until re-established by library tests and
benchmarks. The follow-up implementation plan is the joint-movement-commit
work described under *Design*.

## Why

The library's stated goal is to find paths whenever practically possible.
Today multi-agent movement can stop permanently, and until recently it did so
silently.

Two defects have shipped fixes. PR #61 made planning see settled colonists as
obstacles and made the terminal verdict a search result rather than a retry
clock. PR #63 made a stopped colony say so. Neither *resolves* the remaining
failure: two agents each standing on the tile the other needs. A randomised
geometry search found that in roughly 2% of colony wall layouts, and the three
reproducing seeds are recorded in the colony work for regression use.

## Failure taxonomy

| # | Class | Status |
| --- | --- | --- |
| 1 | Permanent obstacle (settled peer on the route) | fixed, PR #61 |
| 2 | Transient congestion | resolves by waiting |
| 3 | Head-on with lateral room | open |
| 4 | Head-on in a one-wide corridor | open; a swap, a passing place, or avoidance-in-time |
| 5 | Cyclic deadlock, N agents | open; case 4 is its 2-cycle |
| 6 | Goal tile held by a peer | **not a movement problem** — see *Assignment* below |
| 7 | Genuinely sealed | correctly reported terminal |

## What the literature constrains

Two facts from the MAPF literature bound the design, both confirmed against
primary sources before any code was written.

**A "swap" is a forbidden conflict, not an oversight.** Two agents traversing
the same edge in opposite directions in one timestep is a *swapping conflict*,
a standard MAPF conflict class. Standard formulations forbid it because
embodied agents cannot pass through each other. Permitting it is a deliberate
semantic choice, not a bug fix.

**Nobody guarantees the one-wide corridor.** PIBT's reachability guarantee
requires that every pair of adjacent nodes lie on a simple cycle of length ≥ 3
(biconnected). A one-wide corridor is a bridge, so the guarantee is silent
there — for any algorithm in this family. Push-and-Rotate is complete only
given at least two empty vertices, and resolves swaps by routing both agents to
a vertex of degree ≥ 3.

References: PIBT (IJCAI-19, AIJ-22, MIT reference implementation), Push and
Rotate (JAIR), the 2025 MAPF survey, and TP-SWAP for decentralised target
exchange.

## Evidence

All runs used seeded integer PRNGs, a state-hash wedge detector (see *Method
notes*), and — from the density sweeps onward — a joint-solvability filter.
Configurations and denominators are stated with each table.

### First bakeoff, unfiltered

60 seeded random instances per configuration (sparse 24×24 n=12, dense 24×24
n=32, cluttered 24×24 n=20 at 22% walls, tight 16×16 n=24). Instances where an
agent cannot reach its goal in isolation are excluded; joint solvability is
**not** filtered here, so these rates undercount every method (see the
filtered sweep below). Resolution / stretch (ticks ÷ unobstructed lower bound)
/ µs per tick:

| strategy | sparse | dense | cluttered | tight | stretch | µs/tick |
| --- | --- | --- | --- | --- | --- | --- |
| baseline | 25% | 0% | 0% | 0% | — | 1.6–3.2 |
| jitter *(control)* | 100% | 93% | 82% | 86% | 1.18–3.19 | 2.4–4.0 |
| wants-order | 27% | 0% | 0% | 0% | — | 1.8–4.3 |
| wants-rotate | 27% | 0% | 0% | 0% | — | 1.9–5.0 |
| swap-permit | 100% | 93% | 89% | 90% | 1.04–1.35 | 1.8–4.6 |
| pibt-route | 100% | 78% | 52% | 64% | 1.05–1.39 | 3.3–7.7 |
| pibt-dist | 100% | 87% | 59% | 72% | 1.01–1.29 | 17–27 |
| pibt-swap | 100% | 95% | 89% | 90% | 1.01–1.18 | 17–66 |
| pibt-rt-swap | 100% | 93% | 89% | 90% | 1.04–1.34 | 3.2–7.7 |
| swap-on-dl | 100% | 97% | 89% | 86% | 1.12–2.01 | 1.8–4.5 |
| greedy-claim | 27% | 0% | 0% | 0% | — | 2.5–7.3 |
| passing-place *(approx)* | 30% | 0% | 0% | 0% | — | 11–41 |
| whca | 100% | 93% | 84% | 91% | 1.02–1.34 | 150–402 |

What this pass ruled out:

- **Chains and rotations without swap resolve ~nothing** (`wants-order`,
  `wants-rotate`: 0% on every dense configuration). Almost every real deadlock
  is a 2-cycle. This killed the approach that looked strongest on paper.
- **Priority inheritance, not candidate ranking, is PIBT's active
  ingredient**: `greedy-claim` (best free-or-vacating tile, priority order, no
  recursion) is indistinguishable from doing nothing.
- **A cheap ranking oracle buys nothing** (`pibt-rt-swap` ties `swap-permit`
  at higher cost); PIBT's quality edge exists only with an exact oracle.
- **Planning in time is the only swap-free approach that works** (`whca`,
  84–93% with edge conflicts forbidden), at 30–90× the cheap resolver's cost.
- **Jitter is a serious control** — high resolution, terrible stretch
  (2.8–3.2× = visible thrashing). Every proposal must beat it on quality.
- The passing-place score condemns the one-step approximation tried here, not
  the published multi-tick algorithm. Untested, not disproven.

### Directional bias: negative on rubble, positive where lanes can form

A decaying per-(tile, direction) trail charging counterflow — the
highway/pheromone idea — showed **no effect on random rubble** (incidence
−0–3.4%, resolution ±1%) but reversed on bidirectional corridors:

| corridor | pibt-swap | pibt-swap-bias | Δ | head-on onsets/1k |
| --- | --- | --- | --- | --- |
| width 2, n=16 | 10% | **18%** | +8 | 70.2 → 57.3 |
| width 3, n=24 | 32% | **42%** | +10 | 78.2 → 63.7 |
| width 6, n=36 | 98% | **100%** | +2 | 65.3 → 47.2 |

Incidence falls 18–28% where lanes can form; stretch rises (1.30 → 1.65) —
agents walking further to stay in lane. Rubble at 8–22% wall density simply
has no corridors, so the first negative was a map artefact. The structural
limit stands, however: a bias in the *ranking* tier reorders local candidates
but cannot segregate *routes*, and routes come from A\* whose
`FieldCost<CostTag>` is per-tile — `entry_cost(page, id)` never sees the
direction of approach. A real lane mechanism needs per-(tile, direction) cost,
which the cost model cannot express today.

Per-tile congestion pricing — the version the cost model *can* express — was
tested paired with the cheap resolver and killed: identical resolution and
stretch to `swap-permit` everywhere at 4–6× the cost. Both streams avoid the
same crowded tiles and never segregate; the value is in the direction.

### Clearance: swap is geometrically inert for agents with extent

Same policies under 2×2 footprints, with clearance-aware planning (a
materialised per-anchor clear field, since the movement DSL tests one tile at
a time). The resolution layer reproduced every 1×1 number before the 2×2 run
was trusted. 32×32 rubble, 12 instances per cell:

| n (2×2) | jitter | swap-permit | pibt-swap |
| --- | --- | --- | --- |
| 6 | 100% | 42% | 83% |
| 10 | 80% | 20% | 60% |
| 14 | 90% | 10% | 40% |

Two 2×2 footprints cannot exchange anchors without overlapping: swap scores
exactly the no-resolver baseline. What generalises is PIBT's alternative-tile
selection. Caveat: none of these numbers is good, jitter's 87–100% says the
instances are loose, and the footprint model was self-checked only at 1×1 —
treat the ranking as a direction.

### Geometry dominates, and tree maps are unsolvable rather than unsolved

Nine map shapes under one placement policy showed geometry to be the largest
single variable (full matrix in the session record; headline: `pibt-swap`
never worst, `whca` wins where routes cross rather than oppose and loses at
bottlenecks its 8-tick horizon cannot see through).

The all-zero maze row was the instance filter, not the methods: a
randomised-DFS maze is a spanning tree — every edge a bridge — which is the
pebble-motion regime where joint solvability is far stricter than per-agent
reachability. An agent-count sweep confirmed it (swap-capable methods solve
100% of *jointly solvable* maze instances at n=4; at n ≥ 8 no instance is
solvable at all), and on a tree `swap-permit` and `pibt-swap` are identical at
every density: with no lateral room, swap is the entire mechanism.

### Density sweep with a joint-solvability filter

Rates below count only instances **at least one method in the run solved** — a
consensus oracle, since proving joint solvability is the pebble-motion
problem. The `unsolvable` column is the exclusion, and the effective
denominator is `15 − unsolvable`; **cells with denominators ≤ 5 are marked †
and should not carry weight alone**. 24×24, 15 seeded instances per cell:

| geometry | n | jitter | swap-permit | swap-on-dl | pibt-swap | whca | unsolvable |
| --- | --- | --- | --- | --- | --- | --- | --- |
| open | 24 | 100% | 100% | 100% | 100% | 100% | 0/15 |
| rubble | 24 | 100% | 100% | 100% | 100% | 100% | 1/12 |
| corridor | 16 | 62% | 62% | 62% | 75% | 62% | 7/15 |
| corridor | 24 | 0% | 50%† | 50%† | 50%† | 0%† | 13/15 |
| rooms | 24 | 92% | 100% | 92% | 100% | 92% | 2/15 |
| warehouse | 24 | 86% | 93% | 100% | 100% | 86% | 1/15 |
| bottleneck | 24 | 87% | 87% | 93% | 93% | 40% | 0/15 |
| ring | 24 | 60%† | 20%† | 0%† | 60%† | 60%† | 10/15 |
| cross | 24 | 58% | 83% | 42% | 67% | 33% | 3/15 |

At n ≤ 8 every real method reaches 100% on essentially every geometry; the
differences appear only under real contention and are far narrower than the
unfiltered table suggested. Directional readings: `pibt-swap` is the only
method never worst; `swap-on-dl` is the least predictable (100% warehouse,
0%† ring, 42% cross — waiting several ticks is fatal when the whole cycle
locks meanwhile); `whca` degrades sharply with density. The `swap-on-dl` ring
cell that motivated demoting it as a default is **0 of 5 instances** — reason
for caution, not a verdict; the default question is settled conservatively in
*Design* instead.

### Target swapping: the strongest measurement, rejected on layering grounds

Exchanging two agents' goals when crossed (anonymous-MAPF in miniature;
labelled MAPF is NP-hard, unlabelled is polynomial) was best or tied-best on
eleven of twelve geometry cells — with the caveat that several were
thin-denominator cells† — including corridor 45→82%, ring 10→60%, cross
33→67%. The counters show it is *preventative*: peak `blocked_retries` on a
jamming cross run is 7 under `swap-permit` and **1** under target swapping;
crossed goals are exchanged before a conflict can form.

It also survives the clearance model, where it holds ~90% while swap is inert
and PIBT degrades — and it survives a greedy nearest-agent task-assignment
baseline largely intact, because a task layer assigns once before anyone
moves, while target swapping is continuous re-assignment correcting the
staleness. (A stronger assignment baseline — optimal matching, re-solved
periodically — was not measured and would close more of the gap.)

Cost: within 10% of the PIBT tier (both are dominated by exact per-goal
distance tables, not the O(n²) scan), and it falls below the cheap resolver at
map saturation (82% vs 91% at n=64 — no uncontested goal left to trade).

**Rejected as a movement-layer mechanism regardless.** Assigning a goal
destroys fungibility: pairing agent A with goal X encodes information the
movement layer cannot see (cargo, ownership, type, role in a larger plan). A
movement layer that silently exchanges goals overrides a decision made with
strictly more information, and cannot detect when that is safe — fungibility
is a claim only the caller can make, and a caller who assigned individual
goals has generally asserted the opposite. The measurements stand as evidence
for the **caller's assignment layer**: continuous re-assignment is worth more
than most movement mechanisms here (greedy assignment alone lifted
`swap-permit` on a dense ring from 12% to 50%†). The one legitimate library
shape is a future explicitly **anonymous goal set** API — fungibility declared
at the call site, never inferred.

Per-agent escalation (cheap resolver, PIBT only for stuck agents) was a
verified null alongside these runs: it fires as designed but changes no
outcome on any cell; the trigger is rare and the cheap resolver arrives anyway.

### Consensus-oracle caveat

The filter makes rates relative to the method pool *in that run*: adding a
strong method promotes instances from "nobody solved" into the denominator and
mechanically lowers every other method's score. Comparisons within a table are
sound; across tables with different pools they are not.

## Findings

Stated per regime, because no single sentence survived all the evidence:

1. **Point agents, cheap tier:** a joint (batch) commit that admits
   moves-into-vacated-tiles, with 2-cycle swap permitted, resolves 89–100% of
   solvable instances at 2–11 µs/tick. Swap is the semantic price; refusing it
   reactively costs almost everything (top-out ~27% among reactive swap-free
   strategies).
2. **Swap-free by requirement:** only planning in time works (`whca`,
   84–93%), at 30–90× the cost and with a horizon that fails at dense
   bottlenecks. The choice is which currency to pay: semantics or CPU.
3. **Agents with extent:** swap is geometrically impossible; priority
   inheritance (the PIBT tier) carries the entire load.
4. **Any ranking tier's oracle must share the agent's movement-class
   passability**, or agents park beside obstructions forever.
5. **Assignment quality dominates movement mechanics and belongs to the
   caller.**

## Design

What the screening supports building in the library, to be re-verified by
library tests and benchmarks rather than by more lab runs.

### Layer 1 — joint movement commit

Collect desired moves for the tick, admit a move when its destination is free
*or* vacated in the same tick (fixpoint resolves chains; the unadmitted
residue is exactly the cycle set), then admit cycles by policy. Cycles of
length ≥ 3 are always legal — every member vacates simultaneously.

This is the primitive `commit_movement_intent` cannot express: it validates
one destination against current state, so "move into a tile being vacated this
tick" is unreachable by construction. `docs/architecture/spatial-coordination.md`
already records the gap ("also rejects swaps and move-through cycles under the
existing movement commit contract").

Invariants to test: destinations pairwise distinct; every source in the moving
set or already vacated; reservation and dirty-mask semantics identical to the
per-agent commit; a move off the retained route, or onto a tile turned
impassable, invalidates the route so scoped resubmission replans.

### Layer 2 — swap policy

`Forbid` / `Permit` / `PermitOnDeadlock`, and it is a semantic choice, not a
tuning knob: `Permit` means agents pass through one another for one tick.

**API default: `Forbid`.** Zero behavioural change for existing callers, and
it matches the standard MAPF constraint — the surprising behaviour is the one
adopters opt into. The screening's attempt to rank `Permit` against
`PermitOnDeadlock` produced cells too thin to decide (the demotion evidence
was 0-of-5 on one geometry); all three ship, tests cover each, and the colony
demo opts into `Permit`, which is what resolves its recorded wedge class.

### Layer 3 — PIBT tier (opt-in, gated)

Priority inheritance with backtracking over each agent's ≤ 5 candidates,
ranked by a caller-suppliable oracle that **must** share the agent's movement
class. Screening says it is the only mechanism that holds up on cycle-rich
maps and for agents with extent, at 4–6× the cheap tier. Built only if
library-scale evidence shows the cheap resolver leaving real gaps; the
screening's thin cells do not justify it on their own.

### Assignment is the caller's layer

No movement strategy resolved duplicate goals, and none should: that is an
assignment failure. `assign_tactical_candidates_greedy`
(`include/tess/spatial/tactical_assignment.h`) exists for it, and the target
swapping evidence argues for *continuous* re-assignment there. The movement
layer documents that it will not fix duplicate goals. A future anonymous
goal-set API is the explicit-fungibility entry point.

## What adopters control

| Knob | Rationale |
| --- | --- |
| **Swap policy** (`Forbid` default / `Permit` / `PermitOnDeadlock`) | Whether units may interpenetrate is a semantic decision only the adopter can make; the default preserves current semantics. |
| **Priority policy** | Who yields is game design — unit importance, ownership, blocked duration. Deterministic given the caller's span order, matching the existing movement contract (ECS adapters already require replay-stable order). |
| **Effort tier and per-tick budget** | 50 agents can afford a ranking tier; 5,000 cannot. Degrade gracefully at the budget rather than blowing the frame. |
| **Participation opt-out** | Scripted, cutscene, and immovable agents must be excludable; the colony's `SettledTag` generalises to this. |
| **Ranking oracle** (future tier) | Adopters often have a flow field or influence map already; accept a caller-supplied ranking, and document that it must agree with the movement class. |
| **Observability** | Blocked-by-whom, cycle detected, swap denied, ticks without progress. Without this, adopters file their map bugs against the library. |
| **No-progress signal** | The colony's stall counter belongs in the library as a first-class statistic. |

## Limitations

- **No guarantee.** PIBT's proof needs biconnectivity and its own adaptive
  priority rule; the screening substituted `blocked_retries` throughout, so
  the guarantee was never even under test. Roughly 10% of dense solvable
  instances stayed unresolved under every reactive strategy.
- **With swap forbidden, a one-wide head-on is unsolvable** by anything
  proposed here; passing-place planners are out of scope for a reactive tick.
- **Scale unverified.** Screening ran at 16×16–40×40 with n ≤ 64. The colony
  is 128×128 with up to 1,024 agents; costs get re-measured there by the
  library benchmarks before any tier default is set.
- **Stretch floors at 1.0–1.35×** the unobstructed optimum under contention,
  and the screening's stretch is a mean over resolved instances only.
- **The screening harness is gone.** It lived in a session scratchpad by
  design; these tables are not regenerable. Anything that matters gets
  re-established in-library.

## Method notes

The wedge detector is a state-hash fixpoint — a repeat under a deterministic
strategy proves no further progress — but only counts when no agent arrived
inside the repeat period, and "all agents terminal on a genuinely sealed map"
is correct behaviour, not a wedge. Getting both wrong produced rounds of false
positives before either was right.

The screening produced roughly **eight** confidently wrong intermediate
results before the numbers above stabilised, and the correction pattern is the
study's most transferable lesson: **an exact tie or an exact zero across
variants is a wiring defect until instrumentation proves otherwise.** The
instances: a ranking oracle built on terrain while agents moved under a
settled-aware class (agents park beside obstructions — this is design
constraint #4); a batch apply that failed to invalidate routes whose next tile
became impassable (agents never replan — a library test now); the two detector
errors above; a corridor generator that crammed goals into three columns (a
jam by construction); congestion pricing first tested without the resolver it
composes with; a directional bias whose penalty rounded to zero at every
evaluated strength (three sweeps bit-identical) plus a dispatch no-op that
sent the biased strategy down the wrong code path; and a LaCAM prototype whose
budget never found a plan, so its sweep column silently measured its PIBT
fallback. Separately: naming a scratch binary `compare` with `-I .` shadows
the C++20 `<compare>` header.

## Candidate mechanisms (not built; revisit on demonstrated need)

- **LaCAM / Real-Time LaCAM** — *attempted, unresolved.* A from-scratch
  prototype searching for the full goal configuration found nothing at 1,500
  through 40,000 expansions (52 ms) on a 16-agent corridor; node growth badly
  trailed expansions, so the constraint tree was not diversifying, and
  discarding the search tree every tick contradicts Real-Time LaCAM's central
  idea. Any retry should start from the MIT reference (`Kei18/pylacam`) with a
  persistent tree, not from the prototype.
- **Conflict-cluster escalation** — per-agent escalation was a verified null;
  the right granularity is connected space-time conflict components, solved as
  units.
- **Adaptive horizon for space-time planning** — `whca` fails at density
  purely because eight ticks cannot see through a queue; scale the horizon
  with observed contention.
- **Priority-consistent resource ordering** (AGV literature) — rank tiles or
  regions globally; wait only on higher-ranked resources; circular wait
  becomes impossible by construction. The one *prevention* mechanism not yet
  tried.
- **True adaptive PIBT priority** — increment while unarrived, reset on
  arrival; required by the reachability proof and never tested here.
- **Bridge direction mutex** — lock articulation edges to one direction via
  the region graph the library already builds; targets corridor and maze.
- **Anonymous goal-set API** — the explicit-fungibility home for the target
  swapping evidence.
- **Per-(tile, direction) cost** — the extension a real lane/highway mechanism
  needs; a cost-model change, so a separate design conversation.

## Open questions

- Cost of the joint commit at 128×128 with 1,024 agents against the 50 ms
  tick (answered by the library benchmarks, not by more screening).
- Whether an adaptive priority rule recovers PIBT's liveness guarantee on the
  biconnected subset of a map, and whether that is worth stating publicly.
- Where the joint commit sits relative to `resolve_local_moves`
  (`include/tess/spatial/local_coordination.h`) — shipped, unwired, and a
  one-shot destination allocator; its priority/stable-ID vocabulary is the
  house style the new primitive should match.
