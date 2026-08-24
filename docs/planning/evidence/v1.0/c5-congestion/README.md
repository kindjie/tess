# C5 dynamic congestion revalidation: retained evidence

Pre-registration: issue #256, with amendment 2 (the first
demo-classifier leg; review-driven, posted after data, so its run is
recorded as exploratory) and amendment 3 plus its addendum (the full
supported-population matrix with exact gates and a pre-declared value
rule, then browser-incremental as a seventh scenario and a
turnaround-complete replay gate -- each posted BEFORE its run). Source under measurement: main `b87900a5` plus the recorded
programs in `programs.md` -- no library change exists in any arm, by
design: the priced arm writes an ordinary cost field through the
versioned edit channel (`mark_content_changed` + a pathing-dirty
mark), and the planners already compose `FieldCost`.

**Verdict: the historical rejection does not reconfirm under the
plan's own gate at full supported coverage; pricing is retained as a
documented caller recipe with its measured boundary.** No library
authority; the demo's default congestion mechanism (route spreading)
is unchanged.

## The amendment-3 matrix (primary evidence, `matrix/`)

Seven scenarios -- the native CLI's full set: open, tip, two-gates,
four-gates, goal-wall, browser-guard, and browser-incremental (the
demo's progressive wall-admission flow, replicated verbatim: from tick
4, up to 4 walls per tick, refusals retried, turnaround gated on full
admission) -- times all 64 supported populations (16..1024 step 16),
canonical vs priced vs priced-replay, programs and captures
recorded. The plan's gate ("every supported
population retains the same terminal classification and failure
counts no worse than canonical") was operationalized in amendment 3
before the run:

- **G1 retention**: wherever canonical completes, priced completes
  with zero crowd-blocked and zero durably-unreachable. PASS on all
  applicable cells.
- **G2 no-worse**: wherever canonical does NOT complete, priced is
  no worse on every count. PASS -- and in fact the priced arm
  completes ALL 448 cells; canonical itself strands agents at the
  5000-tick cap on 41 cells (tip, every population >= 384).
- **G3 re-rejection trigger** (priced incomplete arrivals or
  crowd-blocked where canonical completes): never fires.
- **G4 determinism**: priced replay bit-identical on all 448 cells,
  turnaround-readiness included (the addendum's completeness fix).
- **G5 wall admission**: every scenario wall assertion-checked
  (accepted == attempted; for browser-incremental, every wall
  eventually admitted -- refusals are that scenario's retry mechanism)
  in every arm of every cell -- the amendment-2 capture had reported
  this gate green without checking it; it is now enforced in the
  program.
- **G6 platforms**: the identical source ran the full matrix on M3
  (clang, ARM64) and Steam Deck (gcc 14, x86-64 Linux). Registered
  gate -- classification-table identity -- PASSES; beyond the gate,
  tick counts are also byte-identical on all 448 cells
  (`matrix/g6-result.md`), so the demo is fully deterministic across
  the two platforms and no per-platform value analysis is needed.

**Value rule (pre-declared)**: pooled geometric mean of
priced/canonical ticks over all 448 cells = **0.4180**, bootstrap 95%
CI [0.3859, 0.4522] -- against the bar gm <= 0.95 with CI high < 1.0.
PASS. Canonical cap-censored cells enter at the cap value,
understating the priced win. Computation recorded in
`matrix/value-rule.md`.

## The boundary, in exact numbers

Per-scenario geometric means: tip 0.20, browser-incremental 0.21,
two-gates 0.22, browser-guard 0.23, open 0.75, four-gates 0.90,
**goal-wall 1.49**. Pricing
regresses goal-wall at every population that matters -- up to +89%
ticks (N in {432, 496, 512}) -- and the amendment-2 sample's
"+14.1%..+39.7%" understated that; classification is retained
everywhere regardless. Callers on detour-shaped maps with uncontended
walls pay real tick cost for no classification benefit.

## What this record does and does not establish

It establishes, at full supported coverage: classification safety
under the demo's own recovery classifier, determinism, composition
with the demo's real topology-invalidation channel (scenario walls
land at tick 4 through `set_wall`, which rebuilds the graph while
pricing runs), and large measured tick improvements on six of seven
geometries including every historically hard cell (canonical's 41
stranded tip cells all complete priced; browser-guard, the scenario
whose 896-960 non-monotonic failures motivated the 64-population
sweep, passes at every population with gm 0.23; browser-incremental,
the demo's progressive topology-edit flow, passes at every population
with gm 0.21 -- pricing composes with walls landing DURING the run,
not only with the batch queue).

It does NOT establish mechanism. Ticks and terminal classifications
are the only measured quantities; waits, gate utilization, queue flow,
and route distribution were not instrumented. Statements of the form
"pricing relieves gate capacity contention" or "the recovery loop
absorbs trajectory perturbation" are plausible readings, not measured
findings, and the maintained documentation confines itself to the
tested geometries and populations.

## The amendment-2 leg and the substrate leg (retained context)

The amendment-2 run (`colony-leg.txt`, 5 scenarios x {256, 1024} plus
shipped-spread context) is superseded by the matrix but retained: its
spread-mode comparison (priced beats the shipped mechanism on the two
heaviest cells, loses on two lighter ones) was not re-run at full
width and remains context-only. The substrate parity screen
(`arms.txt`) stands unchanged as the sensitivity boundary: 17 of 132
C0 fixpoint seeds reclassify chaotically under pricing (both
directions), so consumers needing seed-stable terminal classification
under a fixpoint-style settle should not arm it. The two legs measure
different harnesses; the plan pins C5's verdict to the demo
classifier.

## What follows for C6

C6's opening condition ("only if C3-C5 isolate a capacity-contention
premise that a bounded crossing reservation or flow rule can
represent") is answered on the evidence actually measured: no
experiment in this stream isolated a MECHANISM-level capacity premise
-- the matrix's wins are outcome-level, with contention never
instrumented -- so there is no represented premise for a crossing
reservation to target, and C6 dispositions without a run in X3. (The
earlier "served premise" phrasing overclaimed; this is the corrected
basis, and it reaches the same disposition.)
