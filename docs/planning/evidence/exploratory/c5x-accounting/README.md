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

## Ranking (safe cells; gm of policy/canonical ticks; planning load
as gm of policy/canonical pending-plan integrals)

| policy | tick gm | planning load | safe | note |
|---|---|---|---|---|
| prox1 (validated C5 recipe) | 0.4118 | 5.4x | 14/14 | best pure ticks |
| decay (prox1 + memory) | 0.4263 | 5.5x | 14/14 | |
| stalled (unmoved since last reprice) | 0.4288 | **2.0x** | 14/14 | best value per unit of planning -- prices only where movement actually failed, so replans stay local to real trouble |
| self (own tile, rescaled) | 0.4291 | 5.4x | 14/14 | |
| prox2 (Manhattan-2 halo) | 0.4306 | 5.4x | 14/14 | |
| queue (geometry-aware chains) | 0.6348 | 3.9x | 14/14 | fully safe under scoping (its amendment-2 tip/1024 failure is gone); still best-in-class four-gates, weakest elsewhere |
| demand (planned-route tiles) | 0.5557 | 5.7x | 12/14 | STILL DISQUALIFIED (two-gates/1024, browser-incremental/1024 arrival-incomplete): the route-chasing feedback survives scoping -- a robust negative result |

Every safe policy except queue completes canonical's stranded
tip/1024 (canonical: 505 of 1024 at the cap); queue completes 13 of
14 cells' populations and passes no-worse everywhere.

## The findings that matter

1. **Scoped replanning is the real optimization.** All policies keep
   80-90% of their tick value while planning load drops from
   queue-saturating (hundreds pending for hundreds of ticks) to
   bounded, and browser per-tick compute falls up to ~500x. The
   far-field oscillation a maintainer observed live -- agents far
   from any congestion zigzagging -- was the global-replan protocol,
   not the pricing signals.
2. **`stalled` is the efficiency frontier**: within noise of the best
   tick value at 2.0x canonical planning load (others ~5.4x),
   because failure-to-move is already a spatially tight signal.
3. **Under the corrected protocol the halo matters again** (prox1
   best at 0.412): with fewer forced replans, the halo's early
   warning does useful work that the flood protocol had drowned.
4. **Geometry-aware queue pricing becomes fully safe when it stops
   forcing global replans**, and keeps its four-gates best-in-class
   result -- a per-scenario tool.
5. **Forward-looking demand pricing fails its safety gate under both
   protocols** -- the feedback loop is structural, not an artifact.

## Files

- `<scenario>.txt` -- per-cell captures (arm rows: arrived,
  crowd-blocked, unreachable, ticks, turnaround, gate, planning-load
  integral).
- `programs.md` -- the recorded screen program; policy and scoping
  implementations live in the demo model
  (`examples/web_colony/colony_model_internal.h`).
