---
description: >-
  Congestion pricing over the tess cost-field surface: the validated
  two-field recipe, scoped replanning, and its measured boundary.
---

# Congestion pricing

Congestion pricing makes crowded tiles more expensive to weighted
planners, using surfaces that tess already ships. The
[congestion lab](../../demo/congestion/) exposes
the retained recipe and selected experimental policies; the numbers
below come from 28 pre-registered experiment arms whose captures live
in the repository's evidence records.

!!! info "API used"

    [`tess::movement::OverlayCost`][api-overlay-cost],
    [`tess::PathAgentState`][api-path-agent-state],
    [`tess::PathAgentRoutes`][api-path-agent-routes], and
    [`tess::PathAgentReplanQueue`][api-path-agent-replan-queue].

**Validated caller recipe.** Nearby-agent pricing was tested across
seven scenarios, all 64 supported populations, and two platforms.

**Screened, not promoted.** The cooling, stalled-agent, queue,
scoped-replanning, and route-spreading variants were compared across
the same seven scenarios at 256 and 1,024 agents on one platform,
across pre-registered rounds. These results rank the tested
configurations; they do not predict other maps. The retained captures
predate the pricing engine's relocation into the current lab model,
which is the successor measurement surface.

## The mechanism (all policies share it)

1. Give congestion its own field, separate from terrain, and have the
   movement class read their sum:
   `OverlayCost<FieldCost<TerrainTag>, FieldCost<SurchargeTag>>`.
   Pricing then owns one field outright and never writes the caller's
   terrain.
2. Every four fixed ticks, compute a per-tile **signal** and write
   `min(3, signal)` into the surcharge field. In the single-platform
   screen, four ticks produced a lower aggregate settle-tick ratio than
   eight or sixteen for both cooling policies, and a cap of 3 likewise
   produced a lower aggregate ratio than a cap of 7.
3. Publish the writes as versioned edits: `mark_content_changed` on
   every chunk you touched. Do **not** raise the global pathing-dirty
   flag; request replans only for affected retained routes, as
   described below.
4. When pricing turns off, clear the surcharge field and only then
   force one full replan: every retained route was planned against
   prices, so this single global replan is correct.

Pricing changes route cost, not passability. An existing route
remains valid, although it may no longer be the lowest-cost route.

Two consequences of the separate field. The maximum cost your planner
is configured for must cover terrain plus the surcharge cap, not the
cap alone. And if you encode impassable terrain as cost zero, say so in the
passability term as well — `AllOf<Field<PassableTag>,
NotZero<TerrainTag>>`. `OverlayCost` is zero exactly when terrain is
zero, so a surcharge cannot make impassable ground enterable in the
weighted search, but that absorption is a backstop rather than the
whole guard: region labelling and the minimum-step APIs consult the
passability predicate alone, and would otherwise disagree with the
search about the same tile.

A single cost field, with the price written as `1 + min(3, signal)` and
restored by writing 1 everywhere, is a valid shortcut **only** on
uniformly unit terrain — which is what every experiment in this stream
was screened on. On any other map it destroys terrain when pricing
turns on and cannot restore it when pricing turns off, because the
restoring code knows only the uniform value, not the map.

Note also that the cap and the price magnitudes are expressed in your
cost units. All the measured results here come from unit terrain, so
`min(3, signal)` was a detour budget of three unit steps. On a map
whose terrain spans a wider range, the same cap buys a different
amount of detour, and no experiment in this stream has measured that.

## Scoped replanning

Global replanning dominated the cost of the initial implementation.
With 1,024 agents and a bounded planning budget, the browser run
showed a saturated planning queue, replanning far from any changed
price, and oscillation between near-equal routes. One recorded policy
fell from about 84 ms to 1.6 ms per tick when replanning was scoped —
about 53x.

Request a replan only when an agent's remaining route crosses a tile
whose price **increased**. Price decreases do not request replanning;
agents consider them during their next ordinary replan. The
experimental helper encodes this:

<!-- tess-snippet: congestion-scope source=examples/congestion_pricing.cc -->
```cpp
// A price change never invalidates a retained route, so only agents
// whose remaining route crosses a price increase are asked to replan.
// Price decreases request nothing; agents consider them during their
// next ordinary replan.
std::size_t scope_replans(std::span<const tess::PathAgentState> agents,
                          const tess::PathAgentRoutes& routes,
                          const PricingState& state,
                          tess::PathAgentReplanQueue& queue) {
  return tess::experimental::request_replans_for_route_crossings(
      agents, routes,
      [&](tess::Coord3 coord) {
        return state.increased[static_cast<std::size_t>(coord.y) * kWidth +
                               static_cast<std::size_t>(coord.x)] != 0;
      },
      queue);
}
```
<!-- /tess-snippet -->

The scan starts at the agent's next step, not the tile it occupies: a
tile's cost is charged on entering it, so the occupied tile is already
paid for and a price rise there cannot change what the remaining route
costs. A route that revisits that tile later is still caught, at the
later index.

The helper is experimental and its name may still change. Its
documentation comment specifies the equivalent caller-owned scan.

Its cost grows with the total length of the routes it scans rather than
with the number of agents, which is why long routes with distant price
changes are its worst case.

## The signal menu

All signals reprice every four ticks and share the price formula;
they differ only in what they count.

**Define "stalled" as consecutive ticks unmoved, and derive the rest
from it.** Every policy below that reacts to stalling reads one
per-agent counter: ticks since the agent last changed position, reset
on any move. Policies wanting a window rather than a duration threshold
that counter (`stall_duration >= reprice_period`) instead of comparing
positions across repricings.

Comparing positions only at repricing boundaries looks equivalent and
is not: an agent oscillating between two tiles returns to where it
started and reads as stalled, so the price lands on agents that are
moving. Screening both definitions across sixteen cells left every
duration-based policy bit-identical and improved the two
window-based ones by 5-7%, without changing which policy ranks first.

Admit only agents with an outstanding goal. An agent whose goal has
become unreachable never moves, so its counter grows without bound and
pins a permanent hotspot on a tile nothing can use. Ratios are policy/canonical
settle ticks over the screen's safe cells (geometric mean; lower is
better; 1.0 = no effect), with planning load relative to canonical.

| policy | signal | settle-tick ratio | planning-load ratio | screening result and boundary |
|---|---|---|---|---|
| **Cooling memory** | each live agent's tile and four orthogonal neighbours; the stored signal halves during each idle repricing period | 0.39 | ~5x | lowest aggregate ratio among unspread pricing policies |
| **Stalled + cooling** | same, but only agents whose stall duration reaches the repricing period contribute | 0.43 | **~2x** | similar aggregate ratio at about 2x planning load rather than about 5x |
| **Stall-gated queues** | single-file chains of more than four agents, at least half stalled; prices the chain, graded into its free side lanes; corridor chains also price their approach ends | 0.69 | **~1.1x** | emits no signal without a qualifying stalled chain; safe in 13 of 14 cells without spreading |
| Nearby agents (base recipe) | each live agent +1 on its tile and four neighbours | 0.41 | ~5x | screening ratio shown here; separately validated at supported-population coverage |
| Stalled agents | stalled tiles and neighbours, no memory | 0.43 | ~2x | superseded by stalled + cooling |
| Peaked (own tile +2, ring +1) | each agent a small gradient rather than a plateau | 0.41 | ~5x | lower aggregate ratio than the flat nearby-agent signal in this screen |
| Escalating stall price | contribution grows with consecutive ticks unmoved (+1 magnitude per 8 such ticks, capped) | 0.42 | ~2x | best recorded result on the capacity-bound maze scenario; screened on the relocated lab path |
| Radiating stall price | same, and the priced region widens with stall duration (a cone sloping from the agent outward) | 0.43 | ~2x | best maze result at 256 agents; the wider cone did not add value at 1,024 |
| On-path stall price | the escalating magnitude applied only along the stalled agent's own remaining route, fading with distance | 0.42 | ~2x | value-indistinguishable from plain escalation in the screen; restricting the footprint to the route neither helped nor hurt |

**Experiment rejected — ungated queue detection.** In the screen it
disrupted flowing convoys and created adjacent single-file queues.

**Experiment rejected — planned-route demand.** It failed the safety
gate in 2 of 14 cells under scoped replanning; coupling prices to
planned routes also creates feedback between pricing and replanning.
Both remain selectable in the lab as rejected controls.

None of the nine combined-signal arms improved on its better
component in the 14-cell screen. A shared cap clipping the combined
signal is one plausible explanation; the experiment did not
instrument the cause.

## What pricing costs

Two costs scale with problem size rather than with crowding, and a
caller should size both before adopting any policy here.

**Applying prices scales with the tiles you write, not with the number
of agents.** The recipe below writes every tile every repricing. That
is fine for a demo-sized world and wrong for a large one: the sweep
visits every tile regardless of how many agents exist or how crowded
they are, so a world with sixty-four times the area writes sixty-four
times as many tiles per repricing, for a signal that is usually local
to a few congested regions. That is a statement about work, not a
measured elapsed time on any particular world. Write only the tiles whose price changed, tracking the union
of the previous and current footprints so prices left behind still
decay. Cooling memory — the halving variant, whose heat reaches zero
once crowds move on — can still legitimately spread across most of the
map; that is a reason to bound its extent, not to sweep the world
unconditionally.

Turning pricing on cost roughly a fifth of total run time on an
uncongested 1,024-agent run of the escalating-stall policy, and about
half at 256 agents. Read that as the price of the **whole mechanism**,
not of the sweep alone: it also contains the policy's own per-tick
bookkeeping, the replan selection, and the extra searching that
repricing provokes. Those shares move with policy and geometry, and the measurements here
do not separate the parts, so size your own configuration rather than
transferring these numbers.

**Selecting who replans is proportional to total remaining route
length.** `request_replans_for_route_crossings` walks each eligible
agent's remaining route until it finds a price increase. On a congested
map this dominates: it accounted for 89% of pricing samples in a maze
scenario at 1,024 agents. Its cost therefore grows with how long routes
are, not just how many agents exist, and long routes with distant
price changes are its worst case.

Sparse price application addresses only the first of these.

## Choosing a planning budget

Scoping decides *which* agents replan; the planning budget decides *how
many* of them get served each tick. The two interact: pricing creates
replan demand, and the budget decides how fast that demand drains.

Every option below reads only simulation state, so replay stays exact.
A wall-clock budget is deliberately absent — it would read the host
clock, and identical inputs would then replan different agents on
different machines.

| budget rule | settle ticks | elapsed time | worst tick | search |
|---|---|---|---|---|
| Small fixed (4/tick) | 1.23 | 1.04 | **0.73** | 0.85 |
| Larger fixed (32/tick) | 0.74 | 1.51 | 2.23 | 1.91 |
| Scale with backlog | 0.78 | 1.41 | 2.09 | 1.72 |
| Drain the backlog | **0.67** | 2.37 | 10.34 | 4.06 |
| Fit a search-work target | 0.78 | 0.98 | 2.30 | 1.20 |

Ratios are against a fixed budget of 8 per tick, at 1,024 agents,
geometric means over the screened scenarios; lower is better. Every row
covers eight scenarios except *drain the backlog*, which covers seven:
it changes trajectories before the scripted walls land in one scenario,
so that scenario is not comparable for that rule and is excluded.

**No rule dominates, so choose by the constraint you actually have.**

- **A frame budget you must not miss**: the small fixed budget is the
  only rule that improves the worst tick, and it pays for that with
  23% more settle ticks. Nothing else here lowers the tail.
- **Throughput above all, offline or amortized**: draining the whole
  backlog settles fastest. Under congestion it is very expensive —
  four times the search overall, and on a dense-maze scenario 1.82
  billion node expansions against 113 million for the fixed budget of
  8. The likely mechanism is agents repeatedly replanning against a
  price field that is still moving, though the runs did not isolate it.
  On the open-geometry cells measured, expansions stayed flat across
  every budget rule.
- **General use**: fitting the request count to a search-work target
  (a fixed expanded-node budget per tick, divided by the previous
  tick's measured cost per search) settles about as fast as scaling
  with the backlog while spending noticeably less time.

**What this does not claim.** The elapsed-time column is the weakest
evidence in the table: five of the eight scenarios are individually
slower under the work-target rule even though its aggregate is ~1.0,
and the aggregate sits inside measurement drift. The settle-tick and
search columns are exact and replay-identical; treat elapsed time as
indicative only. A search-work target is a *lagged* target, not a
bound: it sets this tick's request count from the previous tick's
average cost per search, so a single expensive search still overshoots
and the count reacts only afterwards. That is why its worst tick is
2.3x rather than lower. A genuine per-tick bound would need a search
that can be paused or cancelled mid-expansion, which this library's
synchronous drain does not offer.

**Screened, not promoted.** Eight scenarios, two populations, one
platform.

## Route spreading composes with pricing

The library's equal-cost tie-break seed
(`PathAgentReplanOptions::equal_cost_tie_seed` on
`process_weighted_path_agent_replans`) distributes agents across
routes the planner scores identically. Adding it lowered the
aggregate settle-tick ratio for each of the three screened pricing
policies, and changed the stall-gated queue policy from 13 of 14 safe
cells to 14 of 14. **This does not claim universal benefit**: the
comparison covers seven scenarios at 256 and 1,024 agents on one
platform.

The lab permits one seeded replan wave per journey leg after its
waits threshold, and suppresses the wave when the topology already
offers several openings. Those gates are application policy, not
library behaviour.

## Boundaries recorded with the evidence

- The supported nearby-agent recipe regressed the goal-wall geometry
  by a geometric mean of 1.49x across 64 populations. In the
  cooling-memory screen, the 256- and 1,024-agent ratios were 0.99x
  and 1.30x, for a geometric mean of about 1.13x. The experiments
  measured outcomes, not wall contention: if your map has no real
  contention, leave pricing off.
- On the fixpoint substrate, 17 of 132 marginal seeds changed
  terminal classification in both directions under pricing. Consumers
  requiring stable per-seed classification under that settle rule
  should leave pricing disabled.
- On a dense-maze scenario (single-file corridors nearly everywhere),
  the stall-gated queue policy does not help at all: large waiting
  blocks fail its single-file chain test by design, and by the time an
  agent is inside a corridor no alternative remains to price toward.
  The duration-escalating policies were proposed for exactly that
  case and hold the best maze results.
- **Screened, not promoted.** No supported-population, two-platform
  result exists for the cooling, stalled-agent, queue, or escalation
  policies.

## Lab and example

- The [congestion lab](../../demo/congestion/)
  runs the recipe, five screened policies, two rejected controls, and
  three period or price-cap variants live in the browser over the
  same simulation the colony tutorial uses, with a price overlay and
  wall painting.
- `examples/congestion_pricing.cc` is the compile-checked, copyable
  implementation of the full protocol — signal, publish, replan
  scoping, and restore — against public APIs only.

[api-overlay-cost]: https://tess.owx.dev/api/structtess_1_1movement_1_1OverlayCost.html
[api-path-agent-state]: https://tess.owx.dev/api/structtess_1_1PathAgentState.html
[api-path-agent-routes]: https://tess.owx.dev/api/structtess_1_1PathAgentRoutes.html
[api-path-agent-replan-queue]: https://tess.owx.dev/api/classtess_1_1PathAgentReplanQueue.html
