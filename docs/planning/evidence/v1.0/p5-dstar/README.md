# P5 D* Lite screen: retained evidence

Pre-registration: issue #255, applied without amendment. Source under
measurement: main `b87900a5` with NO library change in either arm -- the
candidate is a program-local goal-keyed D* Lite (recorded in
`programs.md`), the incumbent is `tess::astar_path` through its public
entry point, and the stage-2 bench arms differ only by `-DTESS_P5_DSTAR`
selecting which one answers each cycle (base binary SHA-256 `bcd9f40e…9c73e8da`, head `cb98b625…3efe5ed5`; both binaries build from one worktree).

**Verdict: stage 1 passed -- the only P-stream candidate to clear its
feasibility bar -- and stage 2 rejected it with 23 of 24 cells as
confirmed material regressions on M3.** The inversion is the finding.

## Stage 1 (work ratio, `stage1.txt`)

Pooled median incumbent/candidate primitive-operation ratio **1.913**
against the pre-registered >= 1.5 bar, across the full pinned churn
trace (wall-gap + rubble x 64/256 x locality x edit rate x 10 trials x
64 cycles). Per-cell medians ranged from 0.62 (rubble uniform E=16 at
64x64) to 15.4 (wall-gap uniform E=1 at 256x256). Every correctness
gate passed on EVERY cycle of every trial: cost equality with the BFS
oracle AND the fresh incumbent, oracle-checked route validity,
Found/NoPath agreement, deterministic replay. One implementation defect
was caught on the way and is recorded for reuse: popping an
already-consistent vertex through the underconsistent branch toggles g
to infinity and back forever -- consistent pops must be discarded.

## Stage 2 (wall time, M3: `m3-calibration-aa.json`, `m3-screen-ab.json`)

The stage-2 arms are UNVERIFIED BY CONSTRUCTION -- a timing harness
strips the oracle -- so correctness coverage for the timed workload
comes from stage 1 running the identical trace construction (same
generators, same seeds, same cycle structure), where every answer was
checked. A/A clean. The A/B is one-sided: the sole pass is wall_gap uniform E=1
at 256x256 (-20.0%, CI [-21.5%, -18.7%]); every other cell is a
confirmed material regression, from +30.9% to +2,846%, with the worst
cells exactly where edits are frequent or route-local. Per the
pre-registered platform-existential rule, the Steam Deck leg was not
run: acceptance requires no material regression on either platform, and
M3 supplies twenty-three.

## Why a 1.9x op advantage became a 10x-28x time loss

The stage-1 ratio counts primitive operations as equals, but the
incumbent's operations are a two-bucket dial frontier over packed
arrays behind a fast-path preamble, while the candidate's are
pair-keyed lazy-deletion heap pushes, per-vertex rhs re-scans over
neighbor g-values, and per-edit repair cascades that run whether or not
any query needs the repaired region. The pre-registered stage-1 bar of
1.5x was calibrated as if operations cost the same; against THIS
incumbent the bar would need to be roughly an order of magnitude to
predict wall-time survival. That calibration lesson is the reusable
output: for any future incremental-search candidate, stage-1 feasibility
should demand a work ratio comparable to the measured per-op cost gap
(>= 10x), or measure per-op cost directly before promising timing.

## Dispositions this record feeds

- P6 (priority-queue retry): P5 merges no new open-set structure, so
  P6's opening condition remains unmet from this stream (X3 records it).
- P2 reconsideration: not reopened -- this screen rejected D* Lite's
  continuation shape on cost, not semantics.
