# Optimization Log Archive (2026-08-15 to 2026-08-17)

Entries moved from `optimization-log.md` when it approached the
24k-token file limit. Newer entries remain in the live log; older
archives are linked from there.

## 2026-08-17 - Lazy Mermaid runtime for documentation pages

- **Area:** documentation shell JavaScript loading.
- **Hypothesis:** the globally eager Mermaid runtime is the main avoidable
  cost on pages without diagrams, so deferring it should remove roughly
  1 MiB of transfer and most unused JavaScript without affecting diagrams.
- **Environment and method:** PageSpeed Insights with Lighthouse 13.4.1
  against the live site on 2026-08-17, followed by a local strict site build
  served through Chrome. The homepage and pathfinding guide represented the
  shared documentation shell; the standalone colony demo was the control.
- **Baseline:** mobile performance scored 64 on the homepage and 72 on the
  pathfinding guide, versus 100 for the colony demo. The shared-page audits
  attributed a 963 KiB transfer and about 911 KiB of unused JavaScript to
  `mermaid.min.js`; the same pages had no diagrams. Desktop scored 84 on both
  shared pages, with total blocking time of 370 ms and 360 ms respectively.
  Core Web Vitals field data was unavailable because the site lacked enough
  traffic in the preceding 90 days.
- **Controlled change:** replace the eager script element with a narrow proxy
  for Material's `initialize()` and `render()` calls. The proxy loads the
  existing pinned, self-hosted runtime once, only when Material renders a
  diagram; no dependency version, diagram source, or CDN policy changed.
- **Verification:** Chrome made no Mermaid request on direct visits to the
  homepage or pathfinding guide. Diagrams rendered with the local runtime on
  direct entry and after instant navigation, with no console errors or CDN
  requests. All 23 authored diagrams passed the pinned-runtime parser check,
  the branding contract tests passed, and the strict site build and generated
  link check passed.
- **Decision:** accept the lazy loader. It removes the measured 963 KiB
  request from ordinary documentation pages while preserving the existing
  diagram behavior and self-hosting boundary.
- **Limitations and follow-up:** lab scores vary and must be remeasured after
  deployment; no field-data claim is possible yet. Reconsider if the live
  audit still requests Mermaid on a page without diagrams. Treat the remaining
  render-blocking theme CSS, short GitHub Pages cache lifetimes, and
  accessibility findings as separate experiments rather than broadening this
  change.

## 2026-08-17 - One-shot wide-merge detection permits a second seed

- Area and contract: the browser colony's optional equal-cost route spreading
  after the first bounded seeded wave. Any extension must preserve complete
  arrivals, the shared eight-query planning budget, canonical replanning after
  topology edits, and no scans or searches while the option is disabled.
- Reproduced symptom: a 1,024-agent wall from `(64, 32)` through `(64, 127)`
  forced canonical routes into a wall-tip line. Canonical planning reached only
  502 agents after 5,000 ticks and accumulated 3,128,251 routed waits. One
  seeded wave completed all agents in 1,772 ticks with 645,866 waits, but a
  large stationary merge remained visible.
- Rejected congestion costs: penalizing the full stationary queue reached only
  640-645 agents after 5,000 ticks with about 2.47 million waits. Penalizing
  only multiply claimed next tiles improved the wall-tip fixture to 1,630 ticks
  and 318,981 waits, but ended with 1,021 arrivals and three crowd-blocked
  agents. Dynamic costs therefore violated the terminal-outcome contract.
- Rejected broad second-wave trigger: repeatedly observing blocked next-tile
  claims improved the target, but also triggered ordinary two-gate queues. It
  produced one crowd-blocked agent at 512 agents, four at 640, and three at
  896. A later queue can be large without representing the broad merge that
  benefits from another route distribution.
- Accepted policy: after the first seeded queue and canonical queue both drain,
  wait for the existing congestion threshold, then inspect exactly one
  snapshot. Count distinct next tiles claimed by at least two blocked agents.
  If at least 64 such tiles exist, request one additional bounded wave using a
  different deterministic seed. Do not alter costs, passability, goals, or
  movement authority, and never inspect another snapshot during that leg.
- Scale evidence: the second seed activated on the wall-tip fixture at 640,
  768, 896, and 1,024 agents. It reduced ticks respectively from 1,083 to 826,
  1,191 to 929, 1,414 to 1,126, and 1,772 to 1,305. Routed waits fell from
  163,195 to 99,719; 235,284 to 146,830; 439,291 to 217,134; and 645,866 to
  309,363. Every run ended with all agents arrived and none crowd-blocked or
  unreachable.
- Negative evidence: at 128, 256, and 512 agents the wall-tip snapshot remained
  below 64 merge tiles, so tick, wait, and outcome counts were identical to the
  one-wave policy. Across 128-1,024 agents, two-gate controls also scheduled no
  second wave and reported identical tick, wait, outcome, and wave counts. At
  1,024 agents, open, four-gate, and guarded goal-wall controls were likewise
  unchanged.
- Cost: the disabled path returns before any scan. An enabled, congested leg
  performs at most one linear scan over agents and the 16,384-tile counter
  buffer after its first wave. A qualifying leg adds at most one replan per
  active agent, still amortized at eight exact queries per tick. On the local
  uncontrolled optimized probe, the 1,024-agent wall-tip elapsed work fell from
  about 2,085 ms to 794 ms; deterministic counts and terminal outcomes decide
  acceptance.
- Profile confirmation: a 2,000 Hz Samply capture of the accepted 1,024-agent
  wall-tip run collected 1,671 samples. The three leading leaf PCs resolved to
  joint movement and accounted for 535 samples (32.0%); weighted A* appeared
  below them, while the one-shot merge detector did not appear among the top
  25 leaf PCs. The expanded optimized native self-check averaged 2.200 s over
  ten runs, with one outlier and a 2.142-2.648 s range, so it is retained as a
  smoke measurement rather than a calibrated threshold.
- Verification: native regressions cover the qualifying 640-agent wall tip, a
  scale-matched two-gate rejection, the eight-query limit, and topology edits
  that cancel a partially drained second-seed queue and return every active
  route to canonical planning.
- Decision: accept the one-shot wide-merge second wave as part of the optional
  browser policy. Keep the fixed threshold and fixture-specific detector in the
  demo rather than promoting it into the core pathfinding API. Re-evaluate only
  with broader obstacle campaigns and a terminal-outcome invariant; do not
  reintroduce dynamic congestion costs without solving their incomplete-arrival
  cases first.

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

## 2026-08-17 - Browser colony PIBT probe rejected; progress metrics added

- Area: the 128x128 browser colony at 1,024 agents, especially the bounded
  planning ramp and the natural goal-wall fixture used by the native
  self-check.
- Hypothesis: an optional PIBT pass with route-attachment ranking would let
  routed agents yield locally, reduce convoy waits, and remain within the
  demo's eight-query replan budget.
- Baseline profile: an Apple M3 Max, AppleClang 21, `-O3`, `-g`,
  `-fno-omit-frame-pointer`, `-mcpu=native` build ran the complete native
  self-check in 1.288 s mean (0.027 s standard deviation, ten runs). A
  2,000 Hz Samply capture collected 2,717 samples; 40.78% of leaf samples
  landed in the joint-movement advance. The aggregate self-check mixes
  scenarios, so it identifies a candidate hot path rather than a causal
  result for the reported screenshot.
- Microbenchmark context: on the existing 128x128, 1,024-agent lab cells,
  the median joint versus PIBT chain-reset pass was 0.890 versus 0.246 ms.
  Fully denied contention was nearly equal at 0.154 versus 0.164 ms. These
  cells rank movement-pass cost but do not model the demo lifecycle.
- Paired probe: one fixed tick per iteration recorded awaiting-plan
  agent-ticks, valid-route waits, off-route moves, queue depth, completion,
  and elapsed time. Joint movement completed the open leg in 243 ticks and
  quiesced the goal-wall leg in 537 ticks at 374 arrived plus 650
  crowd-blocked. A naive PIBT composition incorrectly moved routeless agents
  through its Manhattan fallback, bypassing the bounded planner; it completed
  the open leg in 116 ticks but failed to quiesce the wall fixture after
  5,000 ticks (401 arrived, no classified crowd-blocked agents, 26.6 s).
- Constrained probe: forcing agents without a valid retained route to stay
  restored the open result to the same 243 ticks, but raised elapsed work from
  about 49 to 121 ms. The wall fixture still failed to quiesce after 5,000
  ticks (550 arrived, no classified crowd-blocked agents, 18.0 s): reactive
  yields kept agents live instead of allowing the settled-only recovery
  classifier to finish the leg.
- Decision: rejected for the browser demo. Do not expose the PIBT toggle or
  weaken the bounded planning FIFO. Add O(1) page diagnostics for pending
  plans, last-tick advances, and last-tick movement waits so future captures
  distinguish planning backlog from routed contention. Joint movement and
  all core library semantics remain unchanged.
- Limitations and retry condition: timings are local and uncontrolled, and
  the paired fixture reproduces the maintained goal-wall case rather than the
  exact user-drawn wall coordinates. Revisit PIBT only if the new diagnostics
  show sustained partial occupied waits after the plan queue drains and a
  lifecycle design lets local yielding coexist with bounded replanning and
  settled-only terminal classification. Evaluate caller-owned lane or
  waypoint assignment separately because PIBT does not change the
  individual-shortest-path objective that creates aligned convoys.

## 2026-08-17 - Non-endpoint congestion follow-up screen

- Area and scope: follow-up mechanisms for the browser colony's visible
  wall-tip single-file progress. Endpoint parking, goal reassignment, and
  staging changes were deliberately deprioritized. The controlled runner
  recorded fixed ticks, retained-route waits, terminal outcomes, planning
  queue state, and streaks with exactly one advancing agent.
- Reproduction: build `tess_web_colony_model`, then run, for example,
  `tess_web_colony_model --scenario tip --agents 1024 --mode spread
  --max-ticks 5000 --require-complete`. With no arguments the executable
  retains its original native self-check. `--help` lists the fixed geometries,
  the batch and incremental browser replays, and both routing modes; one output
  line is suitable for a shell matrix.
- Current target result: canonical routing did not finish the 1,024-agent
  wall-tip fixture in 5,000 ticks and contained 842 one-agent-progress ticks,
  including a 20-tick streak while at least 559 agents remained. One seeded
  wave reduced that to five such ticks. The accepted guarded second wave
  completed all agents in 1,305 ticks with 309,363 waits and recorded no
  one-agent-progress ticks. At 640 agents it likewise completed in 826 ticks
  with 99,719 waits and no one-agent-progress ticks.
- Balanced interior waypoint assignment: built, corrected after independent
  review, and rejected separately. Exact gate-cell assignments created
  hotspots, regressed waits by more than an order of magnitude on two gates,
  and lost terminal arrivals.
- Adaptive PIBT: already exercised in the paired colony probe. The core tier
  already increments priority while unarrived and resets it on arrival. A
  route-constrained composition preserved the open tick count but more than
  doubled elapsed work and prevented the settled-only classifier from
  quiescing the wall fixture after 5,000 ticks. It remains rejected unless a
  future capture shows sustained routed local contention not removed by the
  bounded seed waves and provides a lifecycle-safe off-route replan contract.
- Conflict-cluster or adaptive-horizon space-time planning: not promoted into
  the demo. After the second wave, the target has no remaining one-at-a-time
  streak to escalate. Existing screening also found eight-step WHCA weak at
  bottlenecks and 30-90 times the cheap resolver's per-tick cost. A useful
  retry requires a new reciprocal conflict fixture and a bounded contract for
  off-route prefixes; retained-path scheduling alone cannot spatially spread
  identical wall-detour routes.
- Priority-consistent resource ordering and bridge direction mutex: not
  applicable to this colony leg. Every active agent travels in the same
  direction and swaps are permitted, so the wall-tip queue has no opposing
  resource acquisition cycle or direction reversal for either mechanism to
  break. The 640-agent two-gate control schedules no second wave; its eleven-
  tick one-agent tail occurs only after the active cohort falls to four,
  which is endpoint behavior outside this experiment's scope.
- Directional lane bias: not applicable to the synchronized one-directional
  fixture. A per-(tile, direction) penalty is identical for the whole cohort
  and cannot separate same-direction routes. Prior screening found benefits
  only under bidirectional corridor traffic and no effect on random rubble;
  adding the cost-model extension here would not test the reported symptom.
- Flow fields: useful as a shared route-cost computation when many agents have
  a common goal, but not a congestion policy by themselves. These agents have
  distinct goals, dynamic settled blockers, and equal-cost descent still
  needs the accepted diversification policy. The accepted profile remains
  movement-led, so replacing exact search with a field would target planning
  cost rather than the observed route convergence.
- Decision: retain the optional seeded equal-cost policy and its one guarded
  second wave. Reject the waypoint experiment; keep PIBT rejected for this
  lifecycle; defer space-time, resource-order, direction-mutex, lane-bias,
  and flow-field work until a fixture demonstrates their prerequisite traffic
  pattern. Do not add dormant toggles for mechanisms that cannot affect the
  current one-directional wall-tip case.
- Follow-up correction: the repository-owned runner exposed stale four-gate
  evidence after the endpoint layout changed. On the current 1,024-agent
  layout, canonical routing completed in 600 ticks with 29,262 waits, while
  an unguarded seed took 945 ticks and 40,621 waits. The accepted multigate
  topology guard now schedules no seed when a dominant interior barrier has
  more than two separate openings; four-gate results are canonical at 128,
  640, and 1,024 agents while wall-tip and two-gate wins remain intact.
- Bounded-detour follow-up: an isolated prototype split each seeded replan at
  a deterministic barrier-crossing waypoint, accepted at most six extra
  steps, and charged its two exact segments against the shared eight-query
  budget. At 1,024 agents the wall tip, two-gate, and replayed browser cases
  reached only 561, 575, and 417 arrivals after 5,000 ticks, with 31, 25, and
  15 crowd-blocked agents. A small premium exposed too few crossing rows, so
  the waypoints recreated the rejected capacity hotspots.
- Destination and axis follow-up: a deterministic one-to-one shuffle of all
  away-side goals lost 21 agents even on open terrain and 105 on the wall tip;
  the slots were already uniformly populated, so shuffling ownership added
  crossing traffic rather than demand diversity. A separate equal-cost
  prototype gave alternating agents persistent vertical-first or
  horizontal-first ties. It kept all arrivals in the two-gate and browser
  cases but regressed them from 867 to 3,138 ticks and from 455 to 2,440 ticks,
  respectively; the wall tip reached only 788 agents after 5,000 ticks.
  Straight-looking route families were less diverse than the accepted hash.
- Follow-up decision: reject all three prototypes without retaining code or a
  browser toggle. Preserve the per-agent hashed equal-cost ordering; revisit
  longer routes only with a mechanism that spreads capacity without shared
  intermediate targets and first proves terminal parity on open terrain.
- Route-portfolio follow-up: an isolated prototype generated two deterministic
  shortest paths per agent and greedily retained the one adding less peak and
  total spatial route load. The candidates differed for 702 of 955 wall-tip
  replans, 556 of 977 two-gate replans, and 810 of 1,024 browser-replay
  replans, proving that the tested candidate generator supplied real variety.
  Despite that variety, the wall tip regressed from 1,305 ticks and 309,363
  waits to 1,588 and 405,238; two gates regressed from 867 and 65,187 to 1,328
  and 175,367; and the browser replay ended at 1,023 arrivals plus one
  crowd-blocked agent. Reject this portfolio selector: its wave-local spatial
  overlap score ignores unreplanned routes, arrival time, mandatory cuts, and
  merge ordering, so lowering it is not a sound proxy for throughput. This
  does not reject future time-indexed or globally initialized load models.

## 2026-08-17 - Multigate topology guard prevents harmful spreading

- Hypothesis and trigger: a repository-owned scenario runner would expose
  policy regressions hidden by the hand-maintained experiment notes. The
  optional seeded policy must improve congested convergence without replacing
  natural route diversity already supplied by a broad interior barrier.
- Reproduction surface: `tess_web_colony_model --scenario <geometry>
  --agents <count> --mode <canonical|spread> --max-ticks 5000` prints fixed
  ticks, waits, outcomes, queue and query ceilings, seed-wave count, one-agent
  progress counts, and uncontrolled elapsed time. `--require-complete` makes
  an incomplete or adverse terminal outcome fail the command.
- Finding: the current 1,024-agent four-gate fixture completed canonically in
  600 ticks with 29,262 waits, but unguarded spreading took 945 ticks with
  40,621 waits. The result reproduced through both the new runner and the
  earlier temporary harness. The older improvement claim was stale after the
  protected endpoint layout changed.
- Controlled change: only after the existing option, endpoint, population,
  and wait gates pass, inspect the fullest interior construction column
  straddled by at least one quarter of the active start-to-goal traffic. If it
  spans at least half the map and exposes more than two contiguous open runs,
  mark the one-shot observation complete without scheduling a seed. One- and
  two-opening barriers retain the existing policy. Disabled and uncongested
  ticks perform no topology scan; a native regression proves an equally dense
  accumulated wall behind the cohort cannot suppress the active barrier.
- Compact campaign: spreading completed every tested case with at most eight
  exact planning queries per tick. At 1,024 agents, wall tip remained
  1,305 ticks / 309,363 waits and two gates remained 867 / 65,187, while four
  gates became the canonical 600 / 29,262. At 640 agents the corresponding
  results were 826 / 99,719, 653 / 31,823, and canonical 423 / 9,780. At 128
  agents they were 398 / 1,488, 185 / 199, and canonical 210 / 366. Open and
  guarded goal-wall controls remained unchanged at maximum population.
- Verification authority: native CTest smoke coverage proves the CLI's
  terminal outcome and a 512-agent four-gate run proves zero seed waves. The
  existing native checks continue to pin the two-gate activation, wall-tip
  second wave, topology cancellation, and eight-query ceiling.
- Decision and limit: accept the demo-local topology guard. It represents the
  material distinction demonstrated by the fixture without introducing a
  general portal abstraction or capacity model. Reconsider only if a barrier
  with more than two openings demonstrably benefits from seeding on a broader
  deterministic corpus; terminal outcomes and comparative tick/wait counts
  remain authoritative over the fixed opening threshold.

## 2026-08-17 - Balanced interior waypoints rejected

- Area and contract: the browser colony's optional congestion response after
  a seeded equal-cost replan wave. A candidate must preserve complete arrivals
  and the shared eight-query planning ceiling before performance can justify
  it. Endpoint parking and goal reassignment were excluded from this probe.
- Hypothesis: when an interior wall has several openings, assigning the active
  cohort evenly across every open gate cell and joining two exact path
  segments would use the available width more deliberately than per-agent
  equal-cost tie breaking.
- Guardrails: the corrected probe considered only a construction column
  straddled by active start-to-goal pairs, assigned only agents that had not
  crossed it, preserved retained direct-goal routes when an advisory segment
  failed, and charged both exact searches against the existing eight-query
  tick budget. It changed no passability, movement authority, or goal.
- Result: reject. At 1,024 agents, the then-current unguarded seeded policy
  completed the two-gate fixture in 867 ticks with 65,187 routed waits and all
  agents arrived. The corrected waypoint probe required 2,704 ticks, accumulated
  908,527 waits, and ended with 1,022 arrivals plus two crowd-blocked agents.
  On four gates, the seeded policy completed in 945 ticks with 40,621 waits;
  the waypoint probe required 1,191 ticks, 154,240 waits, and ended with one
  crowd-blocked agent. The wall-tip and goal-wall controls did not activate
  waypoint assignment.
- Earlier implementation audit: a first draft independently selected each
  agent's nearest opening rather than balancing the cohort, granted a failed
  advisory segment NoPath authority, could select an unrelated accumulated
  wall, and queued agents without assignments. These were correctness defects,
  not explanations for the corrected probe's failure, and were fixed before
  the decisive matrix above.
- Interpretation: exact gate-cell waypoints synchronized large cohorts onto
  capacity hotspots. Equal assignment counts did not imply balanced crossing
  throughput, and route stretching increased downstream contention. The
  experiment failed the terminal-outcome gate, so profiler attribution would
  not change the decision.
- Decision: remove the experimental implementation and its small-scale native
  test. Do not expose a waypoint toggle. A future retry needs capacity-aware
  crossing reservations or a flow formulation, plus maximum-scale outcome and
  actual-crossing-distribution tests; assignment balance alone is insufficient.

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

## 2026-08-16 - Tiered and 1024-capacity mixed campaigns on controlled hardware

- **Area:** budgeted-progress mixed-colony benchmark; movement tiers
  (baseline vs PIBT with the route-attachment ranking); capacity ladder.
- **Setup:** Steam Deck LCD (Zen 2), process pinned to an isolated
  physical core, performance governor, sleep inhibited for the full
  window; serial executor; seed the canonical colony seed; ten
  repetitions per cell, all seven frame budgets, fidelity view. Two runs:
  the 512 tiered matrix (both tiers × 20/60 TPS × populations
  100/250/500, 84 cells, at the movement-tier commit) and the 1024
  capacity ladder (both tiers × 20 TPS × populations
  100/250/500/1000/2000, 70 cells, at the 1024-cell commit). Device
  temperature spot checks: 42-51 °C across launch/end boundaries; no
  continuous thermal trace. All 154 artifacts pass the fail-closed
  validator with no matrix holes; artifacts stamp
  `machine_fingerprint: local-uncontrolled`, and the campaign manifest
  kept with the archived bundle is the hardware-provenance record.
- **Results (512 world):** the PIBT tier flips `flow_stable` to true at
  all four p100/p250 rungs on device (deadline success 0.994-0.999,
  zero starved, oldest outstanding age 24-28 ticks versus baseline's
  window-bound 240/720); p500 is unstable on both tiers at both rates,
  so the closed-loop capacity boundary sits between 250 and 500 agents
  on this map at these densities. PIBT useful throughput reaches
  2.3-3.1× baseline at p500. Baseline cells reproduce the earlier
  single-tier campaign byte-for-byte across two commits, so the tier and
  world-shape refactors left baseline dynamics untouched.
- **Results (1024 world):** baseline stabilizes at no rung (success
  0.86-0.88 with starvation scaling to 7 500 items at p2000). PIBT holds
  success 0.990-0.995 with zero starvation through p2000 at ~2.4-2.5×
  baseline throughput. Per-agent throughput is flat across the entire
  ladder for both tiers (baseline ~34, PIBT ~82-83 useful per agent):
  at one quarter of the 512 world's agent density the map is
  contention-light and scaling stays linear through 2000, so PIBT's
  margin here is wedge resolution and ranking quality rather than
  congestion relief.
- **Stability-flag inversion, quantified:** under PIBT at 1024 the
  aggregate miss rate is 1.03 % at p100/p250 (per-rep 0.99 gate barely
  missed → flag false) versus 0.52-0.72 % at p500-p2000 (flag true).
  Every miss is a late completion bounded at 25 ticks of lateness
  (lateness p99 is 0-2 over full-cohort samples); none starve. The
  working hypothesis is structurally long wall detours — scale 16
  doubles detour lengths against the fixed 32-tick allowance — but the
  causal association (miss ↔ admission-time static route distance) is
  not yet instrumented, so this stays a hypothesis; the artifacts lack
  per-repetition sidecars to settle it.
- **Flow stability is not frame safety:** at 1024/p2000 the mandatory
  tick body reaches ~75 ms (baseline) / ~96 ms (PIBT) p99 against the
  50 ms frame period, with frame-start lag p99 of ~59 ms / ~79 ms. A
  rung can be flow-stable while overrunning every frame; the two
  properties must be read from different columns.
- **Realized churn:** applied-edit hashes diverge across tiers at
  512/p500 (both rates) and 512/p250/60 TPS, and agree at 512/p100,
  512/p250/20 TPS, and every 1024 rung. Agreement proves an identical
  applied-edit sequence, nothing more.
- **Limitations:** peak-RSS values pool across process order (RSS
  high-water never shrinks), so no per-cell memory claims are made; no
  continuous thermal/frequency trace; the 60 TPS axis was not run at
  1024 (population is that cell's axis; deferred unless needed).
- **Decision:** `flow_stable` stays advisory — no gate. Reconsider a
  PIBT-tier-scoped gate only after allowance semantics are calibrated:
  any scaled allowance must derive from an immutable admission-time
  static shortest-path distance, never realized route length (which
  would reward congestion and pathological rerouting).
- **Follow-up:** instrument the miss ↔ static-route-distance
  association to settle the detour hypothesis; calibrate allowance
  semantics; consider per-repetition cohort sidecars in the artifact
  schema; the 1024/60 TPS axis if a consumer needs it.

## 2026-08-15 - Mixed-colony starvation attribution and movement-tier arms

- **Area:** budgeted-progress mixed-colony cell (bench mirror of the colony
  harness); PIBT movement tier; ranking oracles.
- **Hypothesis:** the campaign's universal `flow_stable=false` (deadline
  success 0.89-0.98, oldest age at the window bound) is caused by a
  movement-level pathology rather than budget physics.
- **Method:** throwaway per-agent instrumentation on a scratch branch
  (never merged), on top of the merged mixed cell at main 87275c9 with the
  canonical seed: per-agent completion counts, active ping-pong goal,
  positions, phase/retries, neighbor and goal tile states
  (passable/occupied/reserved), route length and cursor; plus an
  env-selected movement-tier switch (baseline / joint / PIBT × swap policy
  × ranking oracle) in the bench mixed cell. Arms ran the full 7-budget
  cell at one repetition (dynamics are identical across budgets, so the
  per-agent dumps are taken from the 0.125 ms cell; the baseline arm ran
  two repetitions and the stuck set was identical in both); dev machine
  (mechanism attribution, not timing evidence). PIBT maintains its own
  adaptive priorities; an early arm double-maintained them externally and
  was rerun after the fix with identical results.
- **Attribution (each step measured):** 86/500 agents complete zero items —
  the identical set in both of two baseline repetitions, all phase Blocked
  with retries exhausted, goals free, 1-3 neighbors occupied; zero
  reserved-but-empty tiles (reservation-leak refuted); zero goal-parkers
  and zero mutual goal pairs. The retained-route next steps complete the
  picture: every one of the 86 next steps is occupied by an agent in the
  ≤2-completion set (57 by fully stuck agents), and 29 of the 86 form
  mutual next-step 2-cycles — true head-ons — with the rest in longer
  chains; the 148 neighboring occupants are all themselves ≤2-completion
  agents forming 92 adjacency clusters (one of 58 agents). Mechanism:
  planning is occupancy-blind by design, stepping admits only on-route
  moves, and blocked agents retry the same retained step indefinitely
  (retry exhaustion stops planning, not stepping). The wait-for graph is
  closed over the low-completion set, so it contains cycles, and the
  baseline has no off-route step that could break them: livelock pockets
  with no internal resolution mechanism, persisting across the full
  observed windows — the documented baseline gap the PIBT tier exists to
  close. (Terrain churn could in principle perturb a pocket; none
  resolved across any observed window.)
- **Arms (p500/20 TPS: zero-completion agents / useful / cohort success /
  starved):**
  - baseline: 86 / 1 611 / 0.893 / 161
  - joint+Forbid: 85 / 1 604 / 0.891 / 164 — rotations alone do nothing;
    the wedges need off-route yields.
  - PIBT+Forbid, Manhattan ranking: 55 / 4 005 / 1.000 / 0 — livelock
    solved, but a wall-adjacent column strands at Manhattan local minima
    (all at distance 18, pressed against one room wall; their pre-window
    items are invisible to the cohort but caught by the age criterion).
    Swap policy made no difference (PermitOnDeadlock identical), so Forbid
    suffices on this map.
  - PIBT+Forbid, boxed BFS field ranking, margin 16: 47 / 4 028 / 0.992 /
    1; margin 48: 31 / 4 048 / 0.985 / 9 — boxed fields miss doors beyond
    the margin; the field is flat inside the box and the trap reappears.
  - PIBT+Forbid, whole-map static field (limit case): 2 / 4 065 / 0.980 /
    21 — an exact gradient collapses the stuck set.
  - PIBT+Forbid, route-local-attachment ranking: 3 / 4 021 / 0.980 / 28 —
    matches the limit case using only the agent's retained A* route:
    candidates attach to route points within radius 2 (Manhattan
    attachment is wall-blind; an unrestricted first draft reproduced the
    Manhattan trap exactly), scored attachment + remaining route length,
    detached candidates steered back to the corridor.
- **Ladder under PIBT + route ranking (zero-stuck / success / starved /
  oldest-age ticks):** p100/20: 0 / 1.000 / 0 / 15; p100/60: 0 / 1.000 /
  0 / 20; p250/20: 0 / 1.000 / 0 / 30; p250/60: 0 / 1.000 / 0 / 45.
  Stability genuinely holds through p250 at both simulation rates and
  degrades at p500 — a measurable capacity boundary instead of universal
  failure.
- **Cost:** PIBT arms raise mandatory frame cost: p95 ~1.9 ms (baseline) →
  ~5.0 ms (Manhattan) / ~8.9 ms (route ranking, unoptimized O(route)
  scan). Movement remediation shifts cost into the mandatory tick;
  budgeted-progress artifacts will price it precisely.
- **Residuals at p500:** 3 stuck agents (undiagnosed) plus success 0.98:
  the remaining misses are consistent with allowance-limited detours
  (wall-crossing routes reach 25-269 tiles against an allowance sized for
  the 24-tile direct goal), which is a demand-model property; the three
  residual stuck agents still need their own diagnosis.
- **Decision:** proceed with a movement-tier axis for the mixed cell
  (baseline + PIBT with a route-attachment ranking) rather than changing
  the baseline or the demand model; the baseline rows document the cost of
  no remedy. No stability gate on baseline (nothing is stable to gate).
- **Follow-up:** production route-attachment ranking (library-side, with
  tests, replacing the scratch oracle); harness `movement_tier` config +
  bench mirror + artifact schema/tooling; churn trace must hash realized
  edits (occupancy-dependent skips make realized terrain tier-dependent);
  comparator cell-identity hardening; diagnose the 3 residual stuck agents
  and the allowance semantics for structurally long detours before any
  PIBT-tier stability gate.

## 2026-08-15 - Budgeted-progress stage-4 campaign on controlled hardware

- **Area:** budgeted-progress benchmark suite (frame-budget controller,
  isolated cells, capacity search, counter pass, mixed colony).
- **Setup:** Steam Deck LCD (Zen 2, 4c/8t), SteamOS, process pinned to an
  isolated physical core, performance governor, steamrt4 container build,
  serial executor. Timing pass and counter pass at one commit; the mixed
  matrix at the seating-fix commit that followed (placement stride 2), with
  the two cohorts labeled and never pooled. Artifacts validate fail-closed
  and curves regenerate from artifacts alone; artifacts stamp
  `machine_fingerprint: local-uncontrolled`, so the campaign manifest kept
  with the archived artifacts is the hardware-provenance record. Device
  temperature 44-53 °C across pass boundaries with rotated budget order;
  no continuous thermal trace was captured, so drift is mitigated and spot
  checked rather than conclusively excluded.
- **Results (timing pass, isolated):** the A* unit cell is clearly
  sub-linear in budget: each 4× budget step (0.125 → 0.5 → 2 → 8 ms) yields
  1.41× / 2.21× / 3.12× useful completions/frame (1.56 → 15.1 end to end),
  with work per completion in a 4.3 % band (7 527-7 847 units) and identical
  trace hashes. Cell shapes differ by design: queued and resumable cells
  scale near-linearly with budget while the field-product cell is flat.
  Overshoot stays at small per-frame rates with the quantum-tail bucket
  dominant.
- **Results (capacity):** paced-arrival capacity bands confirm stable rates
  69 / 83 / 184 / 780 items/s at 0.125 / 0.5 / 2 / 8 ms — monotone,
  non-inverting, 1.2-1.9 % wide, zero flapping. The 0.5 ms confirmation is
  borderline: 3 of 5 confirmation repetitions passed and pooled deadline
  success is 98.96 %, just under the 99 % target, accepted by the documented
  majority policy (the table's rounded 0.990 hides this).
- **Results (counter pass):** all 20 timing/counter pairs passed the
  designed trace-identity, conservation, and applicable comparison gates.
  By design several statistical comparisons do not apply (saturated A*
  cells complete no full pool wrap per repetition; overloaded arrival pairs
  and saturated throughput carry no tolerance), and counter-build resumable
  throughput runs 31-37 % above timing — instrumentation cost shifts wall
  figures, which is why counter wall numbers are never published.
- **Results (mixed colony, populations 100/250/500 at 20/60 TPS):** within
  every (TPS, population) point the flow counters, class records, useful
  work, deadline success, and age metrics are exactly identical across all
  seven budgets — structural, since every mixed task is tick-coupled
  mandatory work with no defer-capable quantum; the budget changes only the
  overshoot classification. Frame-start lag is likewise population-driven,
  not budget-driven (~90 ns at p100; 28.35-28.40 ms at p500/20 TPS across
  all budgets, the tick body outrunning the 50 ms frame period). Population
  scaling is monotone but sub-linear: 1 : 2.5 : 5 population gives
  1 : 2.18 : 3.59 useful/frame at 20 TPS (per-capita throughput falls ~28 %
  from p100 to p500).
- **Finding:** `flow_stable` is false at every mixed point, and on two
  criteria at once: interactive-class deadline success runs 0.893-0.979
  against the 0.99 gate, and final oldest outstanding age reaches 240 ticks
  (20 TPS) / 720 ticks (60 TPS) against the 128-tick threshold. Misses are
  starvation-dominated — items that never complete through settlement
  (310 → 1 610 of ~4.4 k → 15.9 k admitted as population grows at 20 TPS) —
  rather than a lateness tail. This is a property of closed-loop ping-pong
  demand with churn at stride-2 placement density, not a budget effect; the
  stride-2 interference explanation is plausible but untested (no controlled
  cross-stride comparison exists, and the earlier stride-8 cells were
  superseded by the seating fix).
- **Artifact bug found in review:** the mixed cell records only positive
  lateness samples while labeling the family `completed_cohort_items`, so
  published lateness percentiles describe only the 0-140 late completions,
  not the full cohort. Deadline-success rates are unaffected. Lateness
  percentiles from this campaign are excluded from conclusions until the
  sample base is fixed or relabeled.
- **Incident:** the first timing pass aborted silently partway into the
  mixed matrix — stride-8 placement seats at most 227 agents, and the abort
  message was lost to stdout block buffering in the redirected log. Fixed
  (stride parameterization, up-front seating preflight, counted error
  message, line-buffered stdout, `--mixed-only` resume flag) with a
  bench-path seating test at the ladder maximum.
- **Decision:** accept the campaign as stage-4 acceptance evidence for the
  suite; record the mixed-stability shortfall as a workload finding, not a
  regression (no gate exists at these matrix points). The 512² /
  {100, 250, 500} ladder is a campaign subset of the design's canonical
  1024² / 1000-agent ladder.
- **Follow-up:** fix the mixed lateness sample base (record zero-lateness
  completions or relabel the family); starvation attribution in the mixed
  cell (churn-unreachability versus contention) before any stability gate
  is proposed; the canonical 1024² / 1000-agent rung needs the larger world
  and a fresh seating-capacity check.
