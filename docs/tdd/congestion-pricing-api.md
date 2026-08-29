# Congestion pricing as library API

Status: **proposed — evaluated and declined at current evidence.** This
document records what was considered, the defects that decided it, and
the conditions under which the decision changes. The shipped material
is the [congestion guide](../guide/congestion.md), the compile-checked
`examples/congestion_pricing.cc`, the browser laboratory under
`examples/web_congestion/`, and one experimental primitive,
`tess::experimental::request_replans_for_route_crossings`.

## Problem

An exploratory stream (issue #269, ten pre-registered amendment rounds)
screened thirty-one congestion pricing policies. The findings were
productized as documentation plus one primitive, deliberately keeping
policies out of the library because they are trajectory-sensitive
formulas that a 1.x compatibility promise would freeze.

The question this document answers is whether those policies share
enough structure to ship instead as a **parameterized type with narrow
contracts** — named immutable configurations whose defaults are the
best screened values and which a caller can trivially customize. The
argument for it: shipping immutable *named configurations* rather than
a mutable best-effort policy turns trajectory-sensitivity from an
objection into the contract, because the contract becomes the formula
and the formula is a value.

## What was evaluated

A config space over four axes — contributors, kernel, magnitude scale,
memory — above the shared price envelope (`price = 1 + min(cap,
signal)`, cap 3, repricing every four fixed ticks), with each screened
policy becoming a named point in that space.

Two independent reviews were commissioned with opposite mandates: one
to build the strongest possible version, one to make the strongest case
against. They returned opposite verdicts and the same defects.

## Findings that decided it

### The config space does not describe the evidence

The laboratory carries **two incompatible definitions of a stalled
agent**. The snapshot family (`stalled_now`) counts an agent stalled
when its position is unchanged since the previous repricing — a
four-tick window, re-sampled while prices are written. The escalation
family (`stall_duration`) counts consecutive ticks unmoved, updated
every tick, so an agent stuck for one tick qualifies. These are
different contributor sets.

A config space with one "stalled" axis value therefore cannot express
both families, and a preset named for the escalation arm but defined
with the snapshot predicate would not reproduce the result it cites.
For a design whose premise is that the formula is the contract and the
formula was measured, that is disqualifying until re-screened. The
distinction was also absent from the guide and has since been added
there.

### The envelope is denominated in unscreened units

Every screened world is unit terrain (`world.fill_field<CostTag>(1)`).
The cap of 3 is a three-unit detour budget on flat ground; on a map
whose terrain spans a wider range the same cap buys a different amount
of detour, and no arm in the stream varied cost units. The config space
contained no axis for this because the evidence contains no answer.

### One proposed configuration cannot be applied sparsely

The `decay` memory, `heat = (heat + 1) / 2 + signal`, has a fixed point
at 1 under integer division: a tile that ever heats never returns to
zero. Its active set can only grow, which is incompatible with the
sparse application the cost profile requires. It also lost the screen.

### A signal abstraction abstracts the cheap part

Profiling established two costs proportional to problem size, neither
of which is the signal formula. Applying prices is O(tiles written),
independent of agent count. Selecting who replans is O(total remaining
route length) and dominated congested workloads — 89% of pricing
samples on the maze scenario. Signal computation is
O(contributors x kernel) and is already cheap.

On an uncongested 1,024-agent run, enabling the escalating-stall policy
cost about a fifth of total run time, and about half at 256 agents.
That figure covers the whole mechanism — sweep, the policy's per-tick
bookkeeping, selection, and the additional searching repricing provokes
— and a finer attribution would need per-phase timers rather than the
end-to-end A/B and sampling profile taken here. It is quoted as
evidence that the fixed cost is not negligible where search is cheap,
not as a measurement of the sweep alone.

An API taking per-tile deltas into a caller-owned buffer would encode
the O(tiles) sweep into a public contract rather than remove it, and
would not touch the selection scan at all.

## Two structural findings worth keeping

These are independent of whether any policy API is ever built.

**Prices belong in their own field.** The recipe originally written for
this stream wrote the price into the same field the movement class
reads and restored it by writing 1 everywhere -- correct only on
uniform terrain, and destructive on any other map, since the restoring
code knows the uniform value rather than the terrain. That has since
been corrected: `examples/congestion_pricing.cc` now keeps terrain and
surcharge in separate fields summed by
`tess::movement::OverlayCost`, so disarming clears one field and
terrain is never touched. The caller's configured maximum cost must
cover terrain plus the surcharge cap, and passability keeps reading its
own field. `OverlayCost` is zero exactly when terrain is zero, so a
surcharge cannot make impassable ground enterable.

**Sparse application has a natural key.** `TileKey<Shape>` already
packs `chunk_key << local_bits | local_tile_id`, so ordering by the
packed value is chunk-major then chunk-local — one page resolution and
one content mark per chunk, with cache-coherent writes inside it. A
retained sparse price map would emit candidate writes over the union of
the previous and current active sets, so prices left behind still
decay and a rematerialized chunk self-heals. Cooling is provably
identical to the dense pass, because an out-of-set tile has heat 0 and
`0 / 2 + 0 == 0`.

Neither finding requires a policy abstraction to be useful, and the
queue-detection recipe excluded from the config space could use the
same map.

## Decision

No new congestion API at any tier on current evidence. The guide,
example, laboratory, and the single experimental primitive remain the
delivery mechanism.

The smallest changes that are justified now are documentation and
contract clarifications rather than surface:

- the two stall definitions, stated in the guide;
- both size-proportional costs, stated in the guide;
- the terrain-clobbering hazard and the separate-price-field
  alternative, stated in the guide;
- the open contract detail on
  `request_replans_for_route_crossings` — whether the scan starts at
  the agent's current tile — settled before any promotion, and its
  cost characteristic documented, since it is the term that dominates
  congested workloads.

One candidate stable-tier change was identified here and has since
shipped: `PathAgentReplanQueue::contains(index)`. Membership is what
makes `request` idempotent, and without an accessor a caller writing
its own selection pass had to guess; under a bounded planning budget
agents already queued were rescanned in full at every repricing. The
scoped-replan helper now skips them.

Its equivalence is established by asymmetry rather than assertion:
removing the skip fails only the fixture's consultation assertion,
while the returned count, queue contents and drain order hold either
way. That is what makes the change invisible to a caller. The
membership set is the PENDING set, cleared at `pop_front`, and a
partial-drain fixture pins that — a sticky "ever requested" bit would
silently drop agents that had been drained and needed queueing again.

## The gate, and its first firing

The original gate named two conditions. **Both have since been met, the
question was re-evaluated, and the decision is unchanged.**

1. ~~One settled stall definition, re-screened across both families.~~
   Met by amendment 11: the duration counter is the single definition
   and the snapshot families threshold it. The measured ordering
   survived.
2. ~~Supported-population coverage on two platforms.~~ Met by the
   matrix: 8 scenarios x 64 populations x 5 arms on two platforms,
   4,608 runs a side, with zero replay mismatches, zero cross-platform
   divergence across 2,560 cells, and zero construction refusals.
   Cooling, escalating-stall and stalled-cooling clear retention and
   no-worse in every cell. Queue detection with spreading is rejected:
   it strands more agents than canonical on six maze cells whose
   populations interleave with passing ones.

**Re-evaluated verdict: still no API.** The config-space defect that
originally decided it is fixable, so it is no longer the deciding
reason, and leading with it would have been leading with the weaker
argument. The deciding reason is the one coverage cannot touch: the
costs that scale are price application, proportional to tiles written,
and replan selection, proportional to total remaining route length.
More passing cells add cells, not a different cost shape. Every
screened world is also unit terrain, so the price cap remains
denominated in units no experiment has varied, and further cells on
those worlds cannot vary them.

## The replacement gate

A met gate left standing is stale, so it is replaced here by the
conditions this document previously listed as further requirements:

1. **A scenario family with non-unit terrain**, so the envelope's cap
   is expressed in units something has measured.
2. **A non-slab three-dimensional geometry** preserving the measured
   ordering under a different branching factor and a much lower areal
   density.
3. **A sparse implementation shown identical to the dense one** across
   the capture set -- or an explicit statement that trajectories are
   not reproducible across library versions, which would then have to
   be reconciled with what a named configuration promises.

Two candidates fall outside this decision and should not be treated as
settled by it. A saturating **cost-composition expression** summing two
cost fields is movement vocabulary rather than a pricing policy, and any
caller combining terrain with an overlay needs it. A **per-tick
expansion bound** on search would turn a work budget into an actual
bound rather than a lagged target, and only the search itself can
enforce one.

## Verification

The claims in this document are checkable against the repository:
the two stall definitions in `examples/web_congestion/congestion_model.cc`;
unit terrain in `examples/web_colony/colony_model.cc`; the decay fixed
point by inspection of its integer arithmetic; the cost shares in the
congestion guide and the amendment-9 profiling comments on issue #269;
and the two-field shape now demonstrated in
`examples/congestion_pricing.cc` (the direct-write hazard this document
originally recorded is corrected there).
