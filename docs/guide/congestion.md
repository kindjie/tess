# Congestion pricing

How to make crowded routes cost more so planners spread traffic --
using only surfaces the library already ships -- and which accounting
policy to choose. Everything here was measured on the
[congestion lab](https://tess.owx.dev/latest/demo/congestion/); the numbers come from
28 pre-registered experiment arms whose captures live in the
repository's evidence records.

**Evidence tiers, stated up front.** The base recipe (nearby-agent
pricing at supported-population coverage on two platforms) is the only
promotion-grade result. Every ranking below it -- the cooling, stalled,
and queue policies, the scoped-replanning protocol, and the spread
interaction -- comes from a screening matrix: seven scenario
geometries at two populations on one platform. Treat the rankings as
strong guidance with recorded boundaries, not as guarantees for your
map.

## The mechanism (all policies share it)

1. Give your movement class a cost term over an ordinary cost field
   (`tess::movement::FieldCost<CostTag>`).
2. Every repricing period (4 fixed ticks -- measured better than 8 or
   16), compute a per-tile **signal** and write
   `price = 1 + min(3, signal)` into the field. The cap of 3 (prices
   1..4) measured better than a deeper cap of 7.
3. Publish the writes as versioned edits: `mark_content_changed` on
   every chunk you touched. Do **not** raise the global pathing-dirty
   flag -- see the replanning section, which is where naive
   implementations lose two orders of magnitude.
4. When pricing turns off, restore every tile to unit cost and only
   then force one full replan: every retained route was planned
   against prices, so this single global replan is correct.

A price is advice, not law: it never makes a tile impassable, and it
never invalidates a route that was already planned.

## Scoped replanning -- the part that actually matters

The single largest measured effect in the whole experiment stream was
not any signal choice; it was **who replans when prices change**.
Raising `mark_pathing_dirty` on every repricing replans every agent:
at 1,024 agents with a bounded planning budget the queue saturates for
hundreds of ticks, agents far from any congestion oscillate between
near-equal routes as tie-breaks flip, and per-tick compute inflates up
to ~500x (the worst measured policy fell from ~84 ms to ~1.6 ms per
tick when scoped).

The discipline: a cost change never invalidates a retained route, so
ask only the agents whose own remaining route crosses a tile whose
price **increased** to replan -- and let decreases trigger nothing,
because chasing newly cheap ground is precisely the oscillation. The
experimental helper encodes this:

<!-- tess-snippet: congestion-scope source=examples/congestion_pricing.cc -->
```cpp
// A price change never invalidates a retained route, so only agents
// whose remaining route crosses a rise are asked to replan; decreases
// deliberately trigger nothing (chasing newly cheap ground is the
// oscillation the scoping removes).
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

It is experimental: the spelling and one contract detail (whether the
scan starts at the agent's current tile) may still change. The twelve
lines it replaces are recorded in its documentation comment's
contract if you prefer to own them.

## The signal menu

All signals reprice every 4 ticks and share the price formula; they
differ only in what they count. Screening geometric means are
policy/canonical settle ticks over the safe cells (lower is better;
1.0 = no effect), with planning load relative to canonical.

| policy | signal | ticks gm | planning load | character |
|---|---|---|---|---|
| **Cooling memory** | halo of live agents, halved (floor) each period before re-adding | 0.39 | ~5x | best value measured; smooths transient noise; fully evaporates ~2 idle periods after a crowd leaves |
| **Stalled + cooling** | same, but only agents that failed to move since the last repricing contribute | 0.42 | **~2x** | the efficiency frontier: failure-to-move is already local to real trouble |
| **Stall-gated queues** | detects single-file chains (>4 agents, at least half stalled); prices the chain, graded into its free side lanes; corridor chains also price their approach ends | 0.69 | **~1.1x** | minimal intervention: healthy maps see literally no pricing; a specialist for jam-only response |
| Nearby agents (base recipe) | each live agent +1 on its tile and 4 neighbours | 0.41 | ~5x | the promotion-grade anchor; what the architecture notes document |
| Stalled agents | stalled halo, snapshot | 0.43 | ~2x | superseded by stalled + cooling |
| Peaked kernel | own tile +2, ring +1 | 0.41 | ~5x | every agent a gradient, not a plateau; beats the flat halo slightly |

Two teaching negatives, selectable in the lab so you can watch them
fail: **ungated queue detection** breaks up healthy convoys and herds
escapees into a new single-file lane one over (it manufactures
queues); **route-demand pricing** (pricing your agents' planned
tiles) chases its own replans and never settles -- it is the one
signal that failed safety gates outright, under every protocol tried.

Composition advice, measured: combining signal mechanisms does not
stack -- signals add under the shared price cap, which clips the
better component's gradient exactly where guidance matters. Pick one.

## Route spreading composes with pricing

The library's equal-cost tie-break seed
(`PathAgentReplanOptions::equal_cost_tie_seed` on
`process_weighted_path_agent_replans`) distributes agents across
routes the planner scores identically. Pricing creates gradients;
spreading distributes the ties that remain -- and the screening
found them complementary everywhere, never conflicting. Notably,
spreading repaired the queue specialist's one recorded miss. If you
adopt one congestion answer, consider both: they address different
halves of the same problem. Gate the seeded wave on an observed
congestion signal (the lab's model fires it once per journey leg,
only above a waits threshold, and skips topologies that already offer
several openings -- that gating is application policy, not library
behaviour).

## Boundaries recorded with the evidence

- Detour-shaped maps whose walls are never contended pay for pricing
  without benefit (the worst recorded geometry regressed ~1.5x under
  the value champion). If your map has no real contention, leave
  pricing off.
- Fixpoint-style consumers needing per-seed classification stability
  should not arm pricing: 17 of 132 marginal seeds reclassified
  chaotically in the substrate screen.
- All screening beyond the base recipe is single-platform,
  two-population evidence; a supported-population matrix for the
  leading policies is planned and this page will be updated from it.

## See it, then copy it

- The [congestion lab](https://tess.owx.dev/latest/demo/congestion/) runs every
  policy in this table (plus the period and cap variants) live in the
  browser over the same simulation the tutorial colony demo uses,
  with a price-heat overlay and wall painting.
- `examples/congestion_pricing.cc` is the compile-checked, copyable
  implementation of the full protocol -- signal, publish, scoped
  replan, disarm -- against public APIs only.
