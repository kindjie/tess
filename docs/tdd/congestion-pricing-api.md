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

**Prices belong in their own field.** The shipped recipe writes the
price into the same field the movement class reads and restores it by
writing 1 everywhere. That is correct only on uniform terrain. A caller
whose field carries terrain weight would overwrite it when pricing
turns on and erase it when pricing turns off, and a passability term
derived from cost could let a price make impassable ground passable.
The correct shape is a separate price field summed with terrain by the
movement class, which keeps restoration to clearing the price field and
keeps passability reading terrain only. The caller's configured maximum
cost must then cover terrain plus the price cap. This is recorded in
the guide as a hazard.

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

One candidate stable-tier change was identified and is deferred pending
its own fail-before test: `PathAgentReplanQueue` tracks membership
internally but exposes no accessor, so under a bounded planning budget
agents already queued are rescanned in full at every repricing.

## The gate

The decision changes when both of the following exist, and not before:

1. **One settled stall definition, re-screened across both families.**
   Until the escalation and snapshot arms are measured against the same
   predicate, the config space does not describe the evidence. This is
   a prerequisite, not a refinement.
2. **Supported-population coverage on two platforms** for the candidate
   optima — the bar the validated nearby-agent recipe already cleared:
   seven scenarios, all sixty-four supported populations, terminal
   classification retained or improved in every cell.

Beyond those, before anything is discussed for the stable tier: a
scenario family with non-unit terrain, a non-slab three-dimensional
geometry preserving the measured ordering, and a sparse implementation
shown identical to the dense one across the capture set — or an
explicit statement that trajectories are not reproducible across
library versions, which would have to be reconciled with what a named
configuration is supposed to promise.

## Verification

The claims in this document are checkable against the repository:
the two stall definitions in `examples/web_congestion/congestion_model.cc`;
unit terrain in `examples/web_colony/colony_model.cc`; the decay fixed
point by inspection of its integer arithmetic; the cost shares in the
congestion guide and the amendment-9 profiling comments on issue #269;
and the direct-write hazard in `examples/congestion_pricing.cc`.
