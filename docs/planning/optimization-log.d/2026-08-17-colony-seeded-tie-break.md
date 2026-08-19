## 2026-08-17 - Seeded equal-cost routing primitive accepted; colony policy rejected

- Area: exact weighted A* tertiary ordering and the 128x128 browser colony's
  obstacle-heavy, maximum-population movement campaign.
- Hypothesis: deterministic per-agent pseudo-random ordering among nodes with
  identical `(f, g)` values can spread agents across equal-cost wall detours
  without the longer routes and high planning cost of congestion weights.
- Controlled change: keep A*'s optimal `(f ascending, g descending)` order and
  replace only the final tile-index comparison with a seeded hash. Seed zero
  preserves canonical ordering. The browser probe enabled seeded replans only
  after at least 64 active goals and last-tick movement waits reached one
  sixteenth of the active population (minimum eight).
- Evidence: on the local Apple M3 Max `-O3 -mcpu=native` probe, 128-agent
  two-gate completion fell from 657 to 192 ticks and routed wait events from
  9,025 to 198; the four-gate fixture fell from 385 to 226 ticks and 5,550 to
  1,086 waits. Open terrain reported the same tick, wait, and outcome counts
  and completed in 131 ticks under both policies.
- Disabled-path cost: the final reversible permutation retains the 16-byte
  frontier node. The complete zero-seed native self-check ran in 1.273 s mean
  with 0.004 s standard deviation over ten runs, versus the matched 1.288 s
  mean and 0.027 s deviation captured before the change; no default-path
  regression was measurable.
- Scale result: at 1,024 agents the two-gate fixture fell from 4,531 to 1,344
  ticks and from 828,963 to 154,901 routed waits, but ended with 938 arrivals
  and 86 crowd-blocked agents instead of 1,024 arrivals. Four gates similarly
  ended at 949 arrivals plus 75 crowd-blocked. The one-sided goal-wall fixture
  regressed from 537 to 875 ticks and increased crowd-blocked outcomes from
  650 to 717.
- Rejected mitigation: reserving packed destination tiles in farthest-first
  order prevented premature settling but formed a cyclic hold with paths
  approaching from different sides; the 1,024-agent two-gate fixture had zero
  arrivals after 5,000 ticks.
- Decision: accept only the opt-in exact-search `PathTieBreak` primitive. Do
  not expose automatic or browser-demo seeded routing: the demo's densely
  packed eight-column destinations make route diversity and terminal ordering
  one coupled lifecycle problem. The zero-seed/default path retains its
  canonical ordering and storage footprint.
- Retry condition and limits: reconsider a colony policy only with a
  group-level endpoint/staging design that cannot form reservation cycles,
  then repeat the open, multi-gate, and goal-wall campaigns. Timings are local
  and uncontrolled; stable fixed-tick, wait-event, and terminal-outcome counts
  decide the current rejection.
- Follow-up: the endpoint-access retry condition was subsequently satisfied by
  the protected-aisle experiment recorded in
  `2026-08-17-colony-aisled-route-spreading.md`; that record supersedes only
  this experiment's browser-policy rejection.
