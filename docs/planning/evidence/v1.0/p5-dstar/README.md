# P5 D* Lite screen: retained evidence

Pre-registration: issue #255, with amendment 1 (review-driven
conformance corrections, posted before the corrected run; see the
issue). Source under measurement: main `b87900a5` with NO library
change in either arm -- the candidate is a program-local goal-keyed
D* Lite (recorded in `programs.md`), the incumbent is
`tess::astar_path` through its public entry point.

**Verdict: rejected at stage 1 under the registration's own bar,
applied with the accounting the registration actually declared.** The
originally captured stage-1 "pass" (pooled ratio 1.913,
`stage1-v1-asymmetric.txt`) was a counter artifact: the program
counted every incumbent neighbor candidate while counting a D* Lite
`update_vertex` as one unit, omitting its per-neighbor rhs-scan
examinations. Review caught the asymmetry. With symmetric per-neighbor
accounting -- and the registration's other under-implemented terms
fixed (route-local offsets drawn from the true Manhattan-2 table, the
edit/start trace pregenerated from the INCUMBENT's routes so both arms
replay one identical workload, warm-allocation and memory gates
actually enforced, the whole sweep replayed twice) -- the pooled
median incumbent/candidate work ratio is **1.012** against the >= 1.5
bar (`stage1.txt`). Repair machinery that saves no abstract work
cannot win wall time against this incumbent; the registration's stop
condition rejects without a hardware campaign.

## The corrected stage-1 gates (all pass)

- Invalidation: cost equality with the independent BFS oracle AND the
  fresh incumbent after every edit batch, 15,360 answers, zero
  mismatches -- a stale or omitted repair in either arm would surface
  here.
- Warm-path allocation (registered "no warm-path allocation after the
  first cycle"): zero in both arms once the prototype used
  array-based neighbor iteration and a reserved heap (the v1
  prototype's per-call `std::vector` neighbors would have failed this
  gate; conformance required fixing it, which also REDUCED the
  candidate's true cost -- the symmetric ratio is not an artifact of a
  hobbled candidate).
- Memory (registered "figures go in the record"): peak D* Lite
  resident state 13,107,200 bytes at 256x256 (g + rhs + heap),
  recorded per the registration.
- Determinism: the full sweep replayed twice, outcome digests
  identical (`1050b34bcdbc7bca`).
- Route-local sampling: 13-offset Manhattan-2 table, zero uniform
  fallbacks after bounded redraws.

Per-cell symmetric medians range 0.294 (rubble uniform E=16 at 64x64)
to 5.212 (wall-gap uniform E=1 at 256x256): the candidate only
approaches its promised advantage on the sparse-edit wall-gap cells
and pays everywhere edits are frequent or the map is rubble-like.

## The prior stage-2 wall-time capture (context, superseded as gate)

`m3-calibration-aa.json` / `m3-screen-ab.json` are retained: 23 of 24
cells were confirmed material regressions on M3 (+30.9% to +2,846%),
the sole pass being wall-gap uniform E=1 at 256x256 (-20.0%). Two
caveats now attach: the 12 route-local cells did not time an identical
workload in both arms (the edit stream followed each arm's own routes
-- the defect amendment 1 fixes), and the trace's route-local sampling
overshot the declared Manhattan bound. The 12 uniform-locality cells
carry neither caveat and alone contain 11 confirmed material
regressions, so the wall-time picture corroborates the corrected
stage-1 rejection; it is no longer needed to carry it.

## What the correction rescinds and keeps

- RESCINDED: "the only P-stream candidate to clear its feasibility
  bar" and the inversion narrative built on it.
- KEPT (restated): the calibration lesson. The asymmetric 1.913 was
  measuring REAL asymmetry -- the incumbent's per-operation cost is
  far below the candidate's -- but op-count ratios only predict wall
  time when both sides count comparable primitives. The durable form:
  compare per-op-cost-weighted work, or measure per-op cost directly,
  before promising timing; and count both arms with the same ruler.
- KEPT: the consistent-pop discard defect (popping an
  already-consistent vertex through the underconsistent branch toggles
  g to infinity and back forever), recorded for any successor.

## Dispositions this record feeds

- P6 (priority-queue retry): unchanged -- P5 merges no new open-set
  structure, so P6's opening condition remains unmet from this stream.
- P2 reconsideration: unchanged -- rejected on cost, not semantics,
  now at the feasibility stage.
