## 2026-08-26 - Congestion pricing stays out of the library API

A parameterized congestion-signal type — named immutable configurations
over four axes, defaults set to the best screened values — was designed
and reviewed by two independent agents with opposite mandates. Both
found the same defects, and the design is declined at current evidence.
See [the TDD](../tdd/congestion-pricing-api.md).

The deciding defect is that the laboratory carries two incompatible
definitions of a stalled agent: the snapshot family counts an agent
stalled when its position is unchanged since the previous repricing,
while the escalation family counts consecutive ticks unmoved. A config
space with one "stalled" axis cannot express both, so a preset named
for an escalation arm would not reproduce the result it cites — fatal
for a design whose premise is that the formula is the contract and the
formula was measured.

Two further defects: every screened world is unit terrain, so the price
cap is denominated in units no experiment varied; and the `decay`
memory has a fixed point at 1 under integer division, so its active set
can only grow, which is incompatible with sparse price application.

Profiling also showed a signal abstraction would abstract the cheap
part. Applying prices is proportional to tiles written and dominates
uncongested workloads; selecting who replans is proportional to total
remaining route length and dominates congested ones. Signal computation
is already cheap.

Two structural findings are retained independently of any API. Prices
belong in a separate field summed with terrain rather than written over
the field the planner reads — the shipped recipe is safe only on
uniform terrain, and this is now recorded in the guide as a hazard.
And `TileKey<Shape>` already packs chunk and local identity, so
ordering by it gives chunk-major traversal for a future sparse price
map.

The decision changes when one settled stall definition has been
re-screened across both families and the candidate optima have
supported-population coverage on two platforms.
