## 2026-08-17 - Protected endpoint aisles enable guarded route spreading

- Area and contract: the 128x128 browser colony's group endpoint layout and
  optional equal-cost replanning policy. Every maintained campaign must reach
  a quiescent, correctly classified outcome; the policy may reduce routed wait
  events but may not trade arrivals for crowd-blocked agents.
- Rejected staging probe: releasing one 128-agent destination column at a time
  reduced routed waits but did not provide a permanent path through the packed
  endpoint block. At 1,024 agents, two gates reached only 1,018 arrivals after
  5,000 ticks, the goal-wall case ended at 928 arrivals plus 96 crowd-blocked,
  and open travel regressed from 243 to 1,048 ticks.
- Group-level change: widen each protected endpoint band from 10 to 18 columns
  and alternate eight populated columns with goal-free access aisles. The
  paired home/away columns preserve equal route lengths. Canonical routing then
  completed the previous goal-wall seal with all 1,024 arrivals in 1,017 ticks
  instead of quiescing early at 374 arrivals plus 650 crowd-blocked.
- Guarded policy: when explicitly enabled, wait until at least 64 goals remain
  and last-tick movement waits reach `max(8, active / 16)`, then request one
  bounded replan wave with stable per-agent equal-cost seeds. Suppress seeded
  routes when at least half a column's worth of construction lies within eight
  columns of either endpoint approach; that substantial one-sided geometry was
  the seeded search's known adverse case in both travel directions. Sparse wall
  crossings in those bands do not suppress unrelated interior congestion. Open
  terrain therefore performs no extra searches.
- Evidence: at 128 agents, two gates fell from 650 to 185 ticks and from 9,025
  to 198 routed waits; four gates fell from 378 to 219 ticks and from 5,550 to
  1,086 waits. At 1,024 agents, two gates fell from 4,384 to 938 ticks and from
  794,332 to 100,973 waits; four gates fell from 2,637 to 1,065 ticks and from
  610,567 to 91,901 waits. All agents arrived in every case.
- Control cases: 1,024-agent open travel remained 236 ticks with zero routed
  waits whether spreading was enabled or disabled. The guarded goal-wall case
  reported the same aggregate result: 1,017 ticks, 1,024 arrivals, and zero
  crowd-blocked or unreachable agents. On the local Apple M3 Max
  `-O3 -mcpu=native` probe, two-gate elapsed work fell from about 1,976 to 269
  ms and four-gate work from 1,139 to 273 ms; timings are uncontrolled and the
  deterministic tick, wait, and outcome counts decide acceptance.
- Final profile check: the expanded optimized native self-check ran in 1.709 s
  mean with 0.008 s standard deviation over ten runs. Its new central-wall
  fixture and full goal-wall completions make that absolute time incomparable
  with the earlier, shorter self-check. A 2,000 Hz Samply capture collected
  3,599 samples; the four leading leaf PCs all resolved to joint movement and
  accounted for 1,202 samples (33.4%), so the original movement hot path
  remains the relevant optimization target rather than planning overhead.
- Decision: accept the aisled endpoint layout and expose congestion spreading
  as an explicit browser option, enabled by the page but disabled by default in
  the reusable C++ model. Retain the eight-query planning budget, settled-agent
  recovery classifier, and canonical fallback in guarded endpoint
  approaches. This satisfies and supersedes the prior experiment's browser
  retry condition without changing joint-movement authority.
- Limits and retry condition: the approach guard is deliberately conservative
  demo policy, not a proof for arbitrary obstacle geometry. A goal-free aisle
  beside a populated endpoint column is not a cross-cut through that column;
  a delayed cohort can still be sealed by a fully settled populated column,
  and the settled-agent recovery classifier remains authoritative for that
  case. Re-evaluate with additional adversarial wall and settlement-order
  fixtures before broadening automatic activation into guarded approaches or
  into core pathfinding APIs.
