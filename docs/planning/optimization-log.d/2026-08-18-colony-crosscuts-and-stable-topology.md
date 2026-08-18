## 2026-08-18 - Endpoint cross-cuts and stable-topology seeding accepted

- Area and contract: the browser colony's tutorial-owned endpoint placement
  and optional congestion response. Every supported slider population must
  retain a correctly classified terminal outcome, and an incrementally drawn
  wall must receive the same kind of bounded response as its batch equivalent.
- Root cause: the eight goal-free vertical aisles were not cross-cuts through
  their adjacent populated columns. At 896 agents, the 128-agent cohort for
  away column 113 settled first and sealed that full height at tick 409. Three
  spread-routed agents targeting column 115 remained at column 112 and were
  classified crowd-blocked. At 864 agents, column 113 held only 96 goals and
  never sealed. At 1,024, the added column-111 cohort delayed column-113
  closure to tick 439; the last observed affected agent crossed by tick 430.
  Canonical routing failed differently at 896: two agents became trapped in
  one-column pockets between already settled populated columns.
- Scale evidence before the change: the checked-in `browser-guard` replay
  reached 894 plus two crowd-blocked agents canonically and 893 plus three
  crowd-blocked with spreading at 896. At 928 the outcomes were 927+1 and
  925+3; at 960 they were 959+1 and 958+2. Both modes happened to reach all
  agents at 1,024, so a maximum-only test concealed the non-monotonic defect.
- Endpoint change: relocate only the row-64 agent from each of the eight dense
  endpoint columns into the unused sparse outer column at rows 56 through 63.
  Row 64 is then a shared horizontal cross-cut through every dense column and
  through the sparse column. Goals remain unique and every open-terrain leg
  remains 109 steps. A native structural oracle settles every other goal and
  proves that a delayed agent can still reach either endpoint.
- Endpoint alternatives rejected: alternating 16-agent rows made endpoint
  closure impossible but regressed the browser replay to 731 ticks. Placing
  the eight sparse goals at rows 64 through 71 blocked the cross-cut's outer
  continuation and lost two agents from populations 336 through 448. Spacing
  those goals across the full height passed the terminal sweep but regressed
  the 1,024-agent wall tip to 2,040 ticks. Rows 56 through 63 preserved the
  cross-cut without scattering the convoy lanes.
- Interactive root cause and change: topology edits already canceled an
  in-flight seed but left the leg's one-shot eligibility consumed. The first
  incremental probe exposed this but silently skipped six occupied wall tiles,
  so its 2,042-tick spread and 2,083-tick canonical counts describe only 291
  accepted walls and are not acceptance evidence. The corrected runner admits
  up to four walls per tick and retries an occupied coordinate in order. With
  all 297 walls and the cross-cut layout, the old one-shot behavior took 2,986
  ticks and 535,127 waits, close to the 3,172-tick, 564,929-wait canonical
  control. Each topology edit now resets seed eligibility and records its
  schedule tick; a new congestion seed may start after eight edit-free ticks.
  Waiting for all canonical work to drain was rejected because it regressed
  the two-gate control to 1,469 ticks. The idle-only gate keeps work bounded by
  the existing eight-query budget.
- Results after both changes, 1,024 agents: open travel completed in 236 ticks
  and 1,317 waits with no seed; wall tip in 1,323 ticks and 290,749 waits; two
  gates in 792 ticks and 62,849 waits; four gates in 600 ticks and 26,165
  waits with no seed; and the batch browser replay in 471 ticks and 28,079
  waits. The guarded goal wall completed canonically in 1,004 ticks and
  229,359 waits. The exact-topology incremental replay completed in 801 ticks
  and 78,557 waits with one seed and all 297 walls accepted. Every case reached
  all agents with no crowd-blocked or unreachable outcome.
- Scale verification: run `tess_web_colony_model --scenario browser-guard
  --agents N --mode spread --max-ticks 1000 --require-complete` for every
  `N` from 16 through 1,024 in steps of 16. All 64 supported populations
  completed. The same 64-population sweep completed canonically. CI retains
  canonical and spread 896 controls, the structural delayed-agent oracle, and
  a 1,024-agent incremental replay that requires all 297 wall admissions.
- Decision and limits: accept both demo-local changes. The cross-cut fixes a
  proven endpoint-layout defect; it is not a general multi-agent pathfinding
  policy. The incremental fixture preserves the final coordinate set and
  coordinate order with up to four acceptances per tick; occupied coordinates
  delay later admissions, so it does not reproduce original pointer timing. A
  user who pauses longer than the idle window and resumes drawing can
  legitimately cause another bounded wave; topology cancellation and the
  per-tick query cap remain authoritative.
  This supersedes only the known full-column limitation in the earlier aisled
  endpoint record; its routing-policy limits still apply.
