# tess_budgeted_progress_test

- `tess_budgeted_progress_test`: the budgeted-progress benchmark
  harness's deterministic fake-clock battery
  (docs/planning/budgeted-progress-benchmarks.md section 13, cases
  numbered in test comments). Drives `FrameBudgetController`
  (`tests/budgeted_progress_controller.h`) with the scripted
  integer-nanosecond clock: zero/positive budget admission, exact-
  deadline and one-nanosecond overshoot boundaries, indivisible-
  quantum completion, frame-scope allowance sharing across multi-tick
  frames versus per-tick reset, mandatory-first ordering with
  quantum-tail versus mandatory overshoot bucket attribution, paced
  frame-start lag with fresh next-frame allowance, zero-tick-frame
  entitlement and last-tick attribution, unsigned no-underflow
  arithmetic, and the canonical 20/30/60/120 TPS grant patterns
  dropping no simulation time. Also pins the per-item record layer
  (`tests/budgeted_progress_records.h`): a real `ResumableWorkQueue`
  resuming across frames at `AsyncWorkBudget{1}` without duplicates,
  tick-released demand and inclusive deadline boundaries, admission
  and retention identities after every transition, oldest-age
  tracking, dependency-ready-only starvation, sealed-at-settlement
  verdicts that drain and post-seal reclassification cannot change,
  completed-to-stale attribution back to the admission window,
  per-class-to-total aggregation on both counting bases, and the
  section 4.1 service-order tie-break chain. The artifact layer
  (`tests/budgeted_progress_artifact.h`) is pinned too: nearest-rank
  percentiles publishing only at the section 11.4 sample minimums
  with named sample bases, the embedded SHA-256 against FIPS 180-4
  vectors, and v1 JSON emission omitting inapplicable groups
  (saturated cells carry no deadline fields; unpaced cells no lag
  family) with escaped strings and null-suppressed percentiles; the
  fail-closed reader side lives in
  `tools/check_budgeted_artifacts.py` under
  `tests/test_budgeted_artifacts.py`. The arrival layer
  (`tests/budgeted_progress_arrival.h`) pins the deterministic
  Bresenham rate accumulator's exact release pattern and the
  single-class FIFO tracker's O(1) oldest-age bookkeeping, inclusive
  deadlines, window scoping, and seal-time starvation/lateness
  derivation. The capacity-search layer
  (`tests/budgeted_progress_search.h`) pins the section 9.3 policy
  against scripted boundaries: monotone convergence within the
  terminal resolution, confirmation-failure step-down with failed
  points retained, non-inverting bands under deterministic flapping,
  unstable-seed halving, and the hopeless-workload case. Milliseconds
  in Debug; no sleeps.
