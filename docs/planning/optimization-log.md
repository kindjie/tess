# Optimization Log

This document records performance experiments that should remain separate from
architecture docs. Architecture docs describe current behavior; this log
captures hypotheses, benchmark evidence, accepted changes, rejected changes,
and deferred ideas.

Use this log when an optimization is benchmarked, profiled, rejected, or
deferred for scope reasons. Keep entries short and concrete:

- area and date
- hypothesis
- benchmark or profile evidence
- decision
- follow-up conditions, if any

Entries are recorded as one fragment per experiment in
[`optimization-log.d/`](optimization-log.d/) and assembled into this file at
release by [`tools/assemble_changelog.py`](../../tools/assemble_changelog.py).
Do not append here directly: every branch that does conflicts with every
other, and this file is subject to the repository's 24,000-token limit --
which it has now exceeded twice, forcing an archive split each time.

Entries from 2026-08-15 through 2026-08-17 are in
[`optimization-log-archive-2026-08-17.md`](optimization-log-archive-2026-08-17.md);
entries from 2026-08-10 through 2026-08-14 are in
[`optimization-log-archive-2026-08-14.md`](optimization-log-archive-2026-08-14.md);
entries from 2026-08-01 through 2026-08-09 are in
[`optimization-log-archive-2026-08-09.md`](optimization-log-archive-2026-08-09.md);
entries from 2026-07-13 through 2026-07-31 are in
[`optimization-log-archive-2026-07-31.md`](optimization-log-archive-2026-07-31.md);
entries from 2026-07-12 and earlier are in
[`optimization-log-archive-2026-06-07.md`](optimization-log-archive-2026-06-07.md).

## 2026-08-20 - External maintenance adapter promotion campaign

- **Hypothesis:** the registered dirty-bit backend will materially beat the
  queued-coalescing primary control on at least one of Apple M3 and Steam Deck
  without a material primary or immediate-execution guardrail regression on
  either device.
- **Candidate and method:** source
  `b4a882bbdaa32a704109d5bdd773a1adfe45b492`, built separately with the
  recorded native Apple toolchain and pinned Steam Runtime image. Each device
  ran a separate 30-block queued-coalescing A/A calibration, then 30 paired,
  SHA-ranked blocks across dense, sparse, mixed, flush, budgeted, and
  16/64/256/1,024/4,096 registered-task cells. Every cell compared dirty bit
  with immediate, FIFO, and queued-coalescing backends. CPU time was the
  decision metric; absolute times were never compared across devices.
- **Correctness gate:** the exact source completed the normal suite with no
  failures (1,567 passed and one intentionally unsupported capability
  skipped). Adapter-focused ASan/UBSan, TSan, and warnings-as-errors runs each
  passed 21/21 cases, and campaign-tool tests passed 43/43. The adapter cases
  cover deterministic 1,000-run flush, archive-v2 and independent-rescan
  equivalence, dirty ownership, typed content/residency generations,
  generation-safe clear, retry, budget, exceptions, shutdown, concurrency,
  and warmed dense/sparse zero-allocation behavior across all backends.
- **Calibration:** every A/A workload was valid. The largest paired relative
  noise p95 was 2.21% on Steam Deck, below the frozen 10% invalidation ceiling.
  Candidate thresholds remained the predeclared maximum of the fixed floors
  and twice each device's measured A/A noise.
- **M3 result:** `flat` overall. It provides neither the material primary win
  required to graduate dirty bit nor a material regression.
- **Steam Deck result:** the aggregate primary comparison against queued
  coalescing is `flat` at +1.46%, with a 95% interval of +1.36% to +1.64%
  against the 8% relative and 4.98 us absolute thresholds. The overall device
  decision is nevertheless `material_regression`: dirty bit is materially
  slower than immediate execution in budgeted (+10.06%), flush (+10.16%),
  scaling-256 (+10.32%), and scaling-1,024 (+10.85%) cells. The scaling-4,096
  immediate interval crosses the regression boundary and is `inconclusive`.
- **Memory limitation:** isolated scaling-4,096 M3 processes recorded one-off
  roughly 1.3 MiB peak-RSS excursions under dirty bit, FIFO, and immediate.
  Exact work counters, non-monotonic repetitions, lower dirty-bit median than
  coalescing, and green sanitizer/allocation gates do not indicate a leak.
  The protocol declared no memory threshold, so this remains descriptive and
  no post-result gate was invented.
- **Decision:** `keep_experimental`. `DirtyBitScheduler` does not satisfy the
  portable performance rule. This performance-only result does not block the
  separately validated stable task, handle, result, adapter, and immediate-
  execution contract from promotion.
- **Evidence and limitation:** the public sanitized bundle is retained under
  [`evidence/v0.13/maintenance/`](../evidence/v0.13/maintenance/), separately
  manifested by its inner `PUBLIC_EVIDENCE_SHA256SUMS`; the raw set stays
  external and unchanged under
  `CROSS_DEVICE_EVIDENCE_V4_SHA256SUMS`, SHA-256
  `407f6279aad3a27442ca4fb8673712baf1b7c4a152c20e74607ff0a10cd77cb0`, with
  the sanitized and omitted members pinned in the directory's redaction map.
  The Deck wrapper's aggregate console transcript/status was not separately
  captured; authoritative calibration and candidate phase statuses,
  inventories, logs, governor evidence, and replay outputs are retained.
- **Reconsideration:** retry only after a relevant implementation, adapter,
  benchmark, fixture, compiler, flag, or SDK change, then refreeze and rerun
  correctness plus both hardware legs. A mechanical move needs an explicit
  representativeness record rather than an assumed carry-forward.

## 2026-08-19 - Reduce CI setup and Traffic oracle latency

Issue 218 was a recovered infrastructure failure: two attempts were cancelled
while Ubuntu packages downloaded unusually slowly, then the same workflow run
succeeded on attempt three. Across 90 observed package-install steps, p50 was
16 seconds, p95 was 109 seconds, and the maximum was 1,306 seconds. Another run
continuously downloaded 50.4 MB for 21 minutes 34 seconds, confirming that APT
inactivity timeouts cannot impose a total duration bound.

The package sample came from completed setup-step timings queried through the
Actions API across 19 recent `main` push runs, from issue-218 run 32244691550
through run 32290830241.

Accepted changes remove APT from the common Linux ccache path by downloading
the upstream 4.13.6 static binary under a pinned SHA-256 digest. The remaining
libc++ and coverage package installs fail closed with retries and inactivity
timeouts. Runner-provided GCC 12/14, Clang 16, clang-tidy 18, and Ninja avoid
redundant installation and are checked explicitly so image drift is visible.
Successful same-run retries reconcile a bot-owned failure issue only while the
bot's unedited report remains the latest activity; ambiguous or human-owned
issues remain open.

The Traffic Lab's exact route oracle, rather than its crowd replay, dominated
Debug and sanitizer latency. A representative hosted PR previously spent
6 minutes 17 seconds in Dev CTest and another 11 minutes 38 seconds running two
generic Traffic example acceptances. After decomposition, a local Debug
Traffic slice retained all scenario checks and both 512/1,600-tick crowd
outcomes in 11.77 seconds; the 2,048 exact comparisons passed separately in
4.96 seconds under the optimized bench preset. These local and hosted figures
are not a paired benchmark. Required optimized PR and main gates now own the
exact comparisons, while Debug, GCC, ASan, Windows, and coverage retain the
long-run behavior checks.

No existing CI job was demoted. The prior failure classification still shows
independent signal from the required portability, sanitizer, static-analysis,
documentation, and benchmark gates, while existing benchmark sentinels and
coverage remain advisory. Reclassify only after post-change run history shows
a new low-signal critical path.

Migrating providers was deferred. Free public hosted runners avoid a second
control plane and currently offer a better cost boundary than an unmeasured
replacement. Reconsider a main-push-only, no-secrets shadow pilot after the
internal changes settle; require at least ten paired commits and a predeclared
25% improvement in both p50 and p95 end-to-end time without weaker reliability
or security before granting a provider authoritative work.

## 2026-08-18 - Traffic Lab planner investigation

- **Question:** explain the Traffic Lab's planning tail before changing
  behavior, then prefer a generally useful library change only when the
  measured problem falls within Tess's intended pathfinding contract.
- **Contract and workload:** Tess documents unit A* for unit-cost terrain,
  weighted A* for positive varying entry costs, shared-goal fields for repeated
  goals, and opt-in portal-first routes when bounded suboptimality is
  acceptable. The Traffic Lab has 1,024 distinct start/goal pairs, static
  terrain during the measured 128-tick startup, and an eight-request FIFO.
  Every terrain entry cost is initialized to one and no Traffic Lab operation
  changes it.
- **Root cause:** the baseline is not a retained-route or invalidation problem.
  The queue drains exactly the 1,024 initial requests, eight per tick, with no
  topology edit or replan. Across the 128 deterministic tick positions, median
  planning time correlates with touched nodes at 0.9979 for funnel and 0.9994
  for multi-gate. A statically unit-cost workload is routed through the
  weighted API: both planners use Manhattan distance, but barrier scenarios
  miss the unit planner's exact plane-gap shortcut and fall into weighted A*'s
  obstacle-blind wavefront expansion. Eight first searches can therefore touch
  about one million nodes in one tick. The request-count budget is not a work
  or wall-time bound, as the proposed budgeted-agent-replanning design already
  states.
- **Controlled strategy probe:** an untracked C++ probe at commit
  `8dda47f6a7980e4e348b66b888e81ea95b77e129` used the exact 1024x512 shape,
  fields, barriers, openings, and 1,024 request pairs. It was compiled `-O3
  -DNDEBUG` with Apple Clang 21.0.0 on arm64 macOS 26.5.1. Exact weighted A*
  supplied the cost oracle, using the demo's compound movement class rather
  than a legacy tag approximation. The probe source SHA-256 was
  `44057de133582810405a202fd1212ab5ac2491fe0193785111a5110fb3af392e`.
  Deterministic result and counter totals are the primary evidence; the single
  ordered process's per-request timing is exploratory and does not meet the
  repository's 2,000-sample p99 publication floor.

  | Scenario and strategy | Exact-cost routes | Reported expanded | p50/p95 |
  | --- | ---: | ---: | ---: |
  | funnel, weighted exact | 1,024 | 91,701,010 | 5,259 / 5,793 us |
  | funnel, unit exact | 1,024 | 1,271,312 | 7.3 / 8.2 us |
  | funnel, supplied gates | 1,024 | 1,274,336 | 9.3 / 13.6 us |
  | multi-gate, weighted exact | 1,024 | 12,935,564 | 587 / 1,227 us |
  | multi-gate, unit exact | 1,024 | 1,058,176 | 6.8 / 7.3 us |
  | multi-gate, supplied gates | 1,024 | 1,061,120 | 7.5 / 10.2 us |

  Unit and supplied-gate results matched the weighted status and optimal cost
  for every request. The reported expanded-node field fell 72.1x for funnel
  and 12.2x for multi-gate under unit search; on the unit shortcut this field
  is constructed path length, so it is not presented as a total-work ratio.
  Supplied-gate routes use the existing weighted portal-route product with
  exact weighted segments and no segment cache. The scenario supplies a
  nearest opening because its static barriers make crossing an opening
  mandatory and every entry cost is one. Each request's start and goal have
  the same row, so a route crossing at row `g` has the fixed horizontal cost
  plus `2 * abs(start_row - g)` vertical cost; choosing the nearest opening is
  therefore optimal. The portal builder reads `PassableTag` and `CostTag`,
  while the movement class additionally rejects `ConstructionTag`. Scenario
  initialization maintains the stronger invariant that every construction
  tile is impassable and every non-construction tile is passable; final tests
  must either pin that equivalence or use one movement predicate throughout.
- **Route and crowd parity:** equal optimal cost is insufficient for a traffic
  experiment. Only 52 funnel and 160 multi-gate unit routes were byte-identical
  to weighted A*. Unit routes often turn vertically at the spawn edge rather
  than approach the barrier first. In a 1,600-tick deterministic replay this
  changed congestion materially:

  | Scenario and strategy | Arrived | Accumulated waits |
  | --- | ---: | ---: |
  | funnel, weighted exact | 438 | 296,604 |
  | funnel, unit exact | 48 | 516,792 |
  | funnel, supplied gates | 789 | 115,579 |
  | multi-gate, weighted exact | 776 | 127,736 |
  | multi-gate, unit exact | 128 | 474,432 |
  | multi-gate, supplied gates | 1,024 | 768 |

  The unit substitution is rejected. Supplied-gate routes deliberately are
  not byte-identical either (534 funnel and 560 multi-gate matches), but retain
  the intended barrier-first approach: at tick 512 their blocked/wait counts
  exactly match the weighted baseline (funnel 80/441, multi-gate 16/73). Their
  later improvement removes equal-cost route-shape gridlock rather than the
  choke point, so the changed long-run congestion outcome is an intentional
  scenario behavior change, not parity.
- **Alternatives evaluated:** lowering the FIFO request count would trade a
  smaller tick for a longer initial planning drain and would still not bound
  one search. Existing goal-monotone chunk portals found only 128 of 1,024
  funnel routes, so their exact fallback retains the tail. A prototype using
  the existing region graph's non-monotone coarse path found all routes and
  reduced funnel segment expansions to 97,587 with the default segment-cache
  budget, but its routes were 5.0% more costly in aggregate and up to 12.3%
  more costly than exact; the current 4/3 Manhattan quality cap accepted only
  607 of 1,024 funnel routes. Generalizing this tier to compound movement
  classes and runtime graph guidance would add useful weighted capability, but
  the static gate scenarios do not justify that behavioral and API scope.
  Resumable A* is the only candidate that could make one arbitrary search a
  cooperative work quantum; the existing budgeted-progress plan correctly
  keeps it contingent on broader stage-5 evidence, persistent-state semantics,
  and resumed-versus-contiguous correctness tests.
- **Recommendation before revised review:** keep weighted movement semantics
  and use scenario-supplied gate waypoints only for funnel and multi-gate;
  aligned and shuffled-crossing remain direct exact searches. Expose the
  existing generic queued-replan lifecycle as a public callback-based wrapper
  so callers can combine the FIFO with any `PathResult`-returning Tess strategy
  without duplicating agent-state transitions. The current exact unit and
  weighted helpers remain convenience wrappers over it. The callback is
  invoked synchronously as `(agent_index, request)`. Its returned path may be
  borrowed but must remain valid through the immediate copy into retained
  route storage. It must not mutate or reenter the supplied agents, routes, or
  queue, or alias the destination route. The generic mechanism manages queue
  and lifecycle state but neither validates nor certifies the callback's path
  legality, cost, or optimality; the exact helper APIs retain their stronger
  guarantees. If the callback throws, the front item, agent lifecycle, and
  retained route remain unchanged, but callback-owned side effects are not
  rolled back. This is a small, generally useful library boundary; gate
  selection itself remains correctly local to the demo. Do not add a unit
  joint-tick wrapper, region-graph portal extension, or resumable search for
  this fix.
- **Fix acceptance evidence:** after revised review, require every planned path
  to be legal, deterministic, and equal in status and optimal cost to direct
  weighted A*. Byte-identical routes and crowd states are explicitly not the
  contract. Pin gate crossing, 128-tick queue drain, representative 512-tick
  choke-point behavior, the existing eight-request ceiling, the full native
  percentile campaign with at least 2,048 fixed-tick samples per scenario, the
  separate counter pass, browser capture, and the complete test suite. Timing
  remains advisory. Also repeat the 1,600-tick replay and record its arrivals,
  accumulated waits, progress, and state hashes: the long-run behavior change
  is part of the chosen scenario strategy, rather than an incidental result
  hidden by the 512-tick check. Reconsider a general weighted topology-guided
  route only for a measured varying-cost, distinct-goal workload where current
  goal-monotone portals fail; reconsider resumable A* only under the existing
  stage-5 evidence requirements.
- **Accepted implementation and native result:** the reviewed public callback
  drain now owns only FIFO and agent lifecycle, while the Traffic Lab supplies
  gate waypoints locally. The Release timing pass repeated every 128-tick
  scenario in 16 fresh processes (2,048 samples each). It used nearest-rank
  percentiles and the existing publication floors; timing remains advisory.

  | Scenario | Update p50/p95/p99 us | Planning p50/p95/p99 us |
  | --- | ---: | ---: |
  | aligned | 132.50 / 159.54 / 170.08 | 34.62 / 49.46 / 57.83 |
  | shuffled-crossing | 125.00 / 159.25 / 173.25 | 42.83 / 58.08 / 68.29 |
  | funnel | 186.42 / 238.33 / 258.75 | 101.42 / 138.21 / 155.25 |
  | multi-gate | 160.62 / 195.17 / 210.21 | 78.79 / 92.92 / 106.21 |

  Funnel planning p99 fell from 47,532.71 us to 155.25 us (306x), and
  multi-gate fell from 9,617.96 us to 106.21 us (90.6x). The separate
  diagnostic pass reported zero touched nodes, heap pops, and neighbor
  candidates in all scenarios. Funnel performed 1,270,288 passability checks
  and reconstructed 1,274,336 nodes; multi-gate performed 1,057,152 and
  1,061,120 respectively. This is direct-segment work, not a displaced heap
  tail.
- **Browser and behavior result:** Chrome 151.0.7922.138 captured one fresh
  headless page per requested initial scenario at 1920x1080. The corrected
  frame endpoint covers synchronous measurement bookkeeping, the metrics DOM
  update, and final `requestAnimationFrame` scheduling; it inherently excludes
  its own timestamp and bounded-sample commit, and does not include
  asynchronous paint. Every scenario filled 4,096 frame samples and recorded
  zero catch-up frames:

  | Scenario | Render p99/max ms | Frame p50/p95/p99 ms | Frame max ms | Wasm linear memory |
  | --- | ---: | ---: | ---: | ---: |
  | aligned | 0.2 / 0.4 | 0.1 / 0.4 / 0.5 | 0.8 | 71.5 MiB |
  | shuffled-crossing | 0.3 / 1.2 | 0.2 / 0.6 / 0.8 | 1.7 | 71.5 MiB |
  | funnel | 0.3 / 0.4 | 0.2 / 0.7 / 2.0 | 4.7 | 71.5 MiB |
  | multi-gate | 0.2 / 0.4 | 0.1 / 0.5 / 0.6 | 0.8 | 71.5 MiB |

  The 74,973,184-byte value is the complete Wasm linear-memory allocation,
  not native RSS or model-owned heap. Removing three unused
  `PathRequestRuntime` reservations reduced the aligned fresh-page allocation
  from the separately observed 464,715,776 bytes (443.2 MiB) by 83.9%. The
  canvas remained exactly 2:1, fit horizontally, and caused no horizontal
  overflow at both 1366x768 and 1920x1080. Fixed-tick browser families had
  only 682--683 samples, so browser update p99 remained suppressed and the
  native campaign retains authority. The browser artifact preserves summarized
  output and rebuilt Wasm, loader, and app hashes, but not raw browser samples;
  its percentiles are therefore not independently recomputable. Exhaustive
  native validation matched direct compound-class
  weighted status and optimal cost for all 2,048 guided requests, checked
  every node and edge, selected-gate crossing, repeat determinism, and
  whole-map legacy/compound predicate equivalence. The 512- and 1,600-tick
  arrivals, waits, progress, and state hashes matched the reviewed acceptance
  values.
- **Advisory artifact integrity:** the timing, counter, and browser artifact
  basenames are `tess-traffic-lab-percentiles-guided.json`,
  `tess-traffic-lab-counters-guided.json`, and
  `tess-traffic-lab-browser-guided.json`. Their SHA-256 values are respectively
  `15e42cbeda92e881f0fce37ca00924c988cbef3d7946a8d80a605864f428883d`,
  `a79fa7a9057e3e09df21dc7aec9405293876c3ae97d500017644b3f8efe926ff`, and
  `a3d2937226085afd24ad37a38e3b30a2e1f190bdad78d58cf54a86050fb61bea`.
  These remain untracked machine-local evidence; the maintained record keeps
  the portable method and accepted conclusions.

## 2026-08-18 - Traffic Lab 1024×512 baseline and tail attribution

- **Hypothesis:** a 1024×512 full-map overview with 1,024 agents is a useful
  first large-grid congestion lab, while 1024² should wait for tail-latency
  evidence and a rendering design that justifies four times as many tiles.
- **Controlled workload:** the compile-time model uses an eight-search-per-
  tick FIFO. Aligned and shuffled-crossing use open terrain; funnel and
  multi-gate use the same four-column central barrier with different
  deterministic openings.
- **Timing method:** Apple Silicon macOS, Apple Clang 21.0.0, Release. Each
  scenario ran 128 fixed ticks in each of 16 fresh processes, producing 2,048
  samples. The native executable preallocated its sample buffer and serialized
  only after the measured loop. Percentiles use nearest rank; repository
  publication floors are 20/200/2,000 samples for p50/p95/p99. These numbers
  are advisory and have no CI authority.

  Update time:

  | Scenario | p50 µs | p95 µs | p99 µs | max µs |
  | --- | ---: | ---: | ---: | ---: |
  | aligned | 135.83 | 172.25 | 240.46 | 567.21 |
  | shuffled-crossing | 127.29 | 159.25 | 179.17 | 249.17 |
  | funnel | 41,607.00 | 45,918.62 | 47,724.38 | 50,977.17 |
  | multi-gate | 4,705.25 | 9,412.33 | 9,783.46 | 12,769.88 |

  The original table's 28.6 MiB "conservative resident-memory estimate" is
  withdrawn. It was a synthetic model-storage allowance that omitted reusable
  path scratch and runtime capacities, so neither "resident" nor
  "conservative" was supported. A later fresh-page capture records exact Wasm
  linear-memory allocation instead.

  Planning time:

  | Scenario | p50 µs | p95 µs | p99 µs |
  | --- | ---: | ---: | ---: |
  | aligned | 35.38 | 50.08 | 76.29 |
  | shuffled-crossing | 42.46 | 56.33 | 67.83 |
  | funnel | 41,510.04 | 45,803.42 | 47,532.71 |
  | multi-gate | 4,558.25 | 9,287.38 | 9,617.96 |

- **Counter pass:** the separately compiled `TESS_ENABLE_DIAGNOSTICS` binary
  ran one deterministic 128-tick repetition. Its wall times are deliberately
  unpublished. Aligned and shuffled-crossing stayed on the direct corridor
  path; funnel and multi-gate invoked full heap search:

  | Scenario | Touched nodes | Heap pops | Neighbor candidates |
  | --- | ---: | ---: | ---: |
  | aligned | 0 | 0 | 0 |
  | shuffled-crossing | 0 | 0 | 0 |
  | funnel | 93,545,738 | 91,648,586 | 365,931,036 |
  | multi-gate | 14,511,504 | 12,774,220 | 51,016,440 |

  | Scenario | Passability checks | Reconstructed nodes |
  | --- | ---: | ---: |
  | aligned | 1,027,072 | 1,028,096 |
  | shuffled-crossing | 1,202,572 | 1,203,596 |
  | funnel | 2,009,102 | 3,228,042 |
  | multi-gate | 1,935,988 | 2,832,980 |

- **Sampling profiles:** 2 kHz Samply captures used the `bench-profile`
  configuration (`-O3 -g -fno-omit-frame-pointer`) and repeated the aligned,
  multi-gate, and funnel scenarios 200, 20, and 3 times respectively. The
  captures contained 6,741, 27,503, and 25,949 samples. CLI symbol summaries
  attributed 97.6% of multi-gate and 99.5% of funnel leaf samples to the
  weighted-A* and regular-neighbor expansion family. Aligned attributed 19.5%
  there and 70.8% to the enclosing, partly inlined fixed-tick body. The latter
  bucket cannot separate movement from bookkeeping, so no narrower aligned
  cause is claimed.
- **Browser pass:** one Chrome session at a 1,665×984 CSS-pixel viewport
  captured the first samples after each reset; buffers stop at 4,096 rather
  than overwriting startup work. The historical frame endpoint preceded
  measurement bookkeeping, the metrics DOM update, and final animation-frame
  scheduling. These values therefore cover only partial synchronous callback
  work and are retained as baseline history, not as full-callback evidence.
  They do not include asynchronous paint completion. Render and partial-frame
  timing are milliseconds:

  | Scenario | Frames | Render p99/max | Partial frame p50/p95/p99 | Partial frame max | Catch-up frames |
  | --- | ---: | ---: | ---: | ---: | ---: |
  | aligned | 4,096 | 1.0 / 7.3 | 0.5 / 1.2 / 1.7 | 7.5 | 0 |
  | shuffled-crossing | 3,269 | 0.8 / 4.1 | 0.4 / 1.2 / 1.6 | 4.3 | 0 |
  | funnel | 2,885 | 0.9 / 5.5 | 0.5 / 1.4 / 49.4 | 1,287.3 | 26 |
  | multi-gate | 2,778 | 1.3 / 6.1 | 0.6 / 3.0 / 43.1 | 73.4 | 0 |

  Each browser frame family clears the 2,000-sample p99 floor. Browser update
  families contain only 358–514 single-fixed-tick calls, so their p99 values
  remain suppressed; the native campaign owns fixed-tick tail claims. Funnel's
  26 catch-up frames explain why its callback maximum is far above p99.
- **Result:** funnel tail cost is algorithmic search work, not rendering or
  movement bookkeeping: planning accounts for almost its entire update, its
  p99 is 47.5 ms, and its observed maximum update slightly exceeds the 50 ms
  simulation period. Multi-gate has a 9.8 ms update p99. All timing runs kept
  the eight-search ceiling, and deterministic counter runs explain the
  scenario difference without mixing instrumented wall time into the result.
  Browser rendering itself stays below 1.3 ms at p99. The historical partial
  frame values are insufficient to decide full animation-callback budget.
  Funnel entered a catch-up cascade with an observed 1.29 s partial sample.
- **Decision:** retain 1024×512 and the full-map overview for version one. Do
  not introduce 1024², zoom/pan, or a more complex renderer. Keep timing
  advisory; deterministic scenario and search-budget checks remain blocking.
  The browser exposes a bounded capture only under `?measure=1`, while normal
  previews retain live exponential averages.
- **Limitations and reconsideration:** native samples pool deterministic tick
  positions across fresh processes on one host; they are not a calibrated
  cross-machine threshold. Samply identifies hot call paths but not why the
  CPU executes them at a given rate, and its measurements are not benchmark
  timings. Browser figures come from one foreground session and are not paint
  completion or a cross-device calibration; timer resolution produces visible
  quantization. Their partial frame endpoint is superseded by the corrected
  guided capture. Reconsider 1024² only after a concrete experiment needs the
  extra area and both native planning tails and browser frame tails fit their
  stated budgets.

## 2026-08-18 - The portal-tick cache premise held; its ceiling did not

- Area: `detail::best_chunk_portal` in the portal steady-state tick,
  and the deferred per-tick cache experiment sized against it. No code
  changed; this records a re-measurement that keeps a deferred
  experiment honest.
- Why: the deferred cache rested on two figures measured on 2026-08-11
  at an older `main` - a 65.32% profile share and a 67.13% within-tick
  call-redundancy rate. `main` has since advanced ~40 commits,
  including the budgeted-replanning and bounded-recovery rewrite of
  `include/tess/sim/path_agent_tick.h`, so both inputs were stale and
  every figure derived from them inherited that.
- Profile: `path/agent_tick_100_weighted_goal_churn_portal_512x512` on
  controlled hardware, three `perf record -F 499 -g --call-graph fp`
  captures of a `-O3 -DNDEBUG -g -fno-omit-frame-pointer` build,
  18,470 pooled samples, zero lost. Sampled user-cycle self share:
  `best_chunk_portal` 68.42/69.14/68.90%,
  `process_weighted_batch_impl` 16.91/16.46/15.92%,
  `vector<Coord3>::_M_range_insert` 5.52/5.68/6.28%. That three-way
  ordering of report entries repeats in all three captures and matches
  the 2026-08-11 ordering. Timing, from retained benchmark JSON at a
  fixed iteration count with five repetitions: median 30,451 ns.
- Census: the measurement-only redundancy scaffold was re-applied to
  the current tree and both cells re-run at the same fixed iteration
  counts. Every published figure reproduces - 456,750 calls, 217.5 per
  tick, 0.12% cold, 67.13% within-tick duplicates, 32.75% cross-tick,
  95.24% replayed ticks for the repeated cell; 66.68% within-tick and
  zero cross-tick for the fresh one. The decompressed dumps are
  byte-identical to the 2026-08-11 dumps: every tick stamp, semantic
  key and scan count is unchanged. The analysis script was validated by
  reproducing the older published figures from the older dumps before
  it was pointed at the new ones.
- What that settles: the redundancy input is current, not stale. It
  also shows only that these two synthetic cells do not exercise what
  the tick rewrite changed - not that the tick at large is invariant.
- What it does not settle: the ceiling. Multiplying share by redundancy
  by tick time gives ~14.1 us/tick and a ~65 ns per-call cache budget,
  but that product assumes a duplicate call costs what an average call
  costs, and multiplies a sampled CPU-cycle share by a wall-clock
  total measured from a differently-sized run. Duplicates within a
  tick touch data the original just warmed, so they are plausibly
  cheaper than average and the product likely over-estimates removable
  time. The equal 32-tile scan per call fixes the scanned-tile count,
  which is not the same as equal work and further still from equal
  cost. Two adversarial review passes rejected the ceiling framing;
  the figure is recorded as a scenario.
- Also observed, not claimed: the 2026-08-11 fourth entry
  `build_bounded_weighted_distance_field_core` was 3.34% there and is
  0.67% in these captures. Symbolization and inlining are not
  established as comparable across the two binaries, so this is a
  thread to pull, not a measured change.
- Ranking limit: each capture holds a contiguous cluster of
  unsymbolized `libc.so.6` addresses totalling ~3.2%, more than the
  fourth resolved entry. Nothing below ~3% in these captures is a
  ranking of code regions.
- Status: the per-tick cache stays deferred. With cost weighting
  measured, what still separates the scenario from a ceiling is the
  second premise - share and total come from different runs and
  different clocks. Closing that needs both measured together, or a
  prototype timed end to end against an interleaved control, in the
  shape this repository already used to reject the four-ary heap. A
  seam-only `(from, to)` index remains unruled-out and unmeasured; this
  census counts calls, not scoring operations.

## 2026-08-18 - Request-scoped memo for chunk-portal seam queries

- Area: `detail::select_chunk_portal_waypoints` and the two
  `best_chunk_portal` call sites beneath it. Accepted.
- Evidence for the target: a Steam Deck profile of
  `path/agent_tick_100_weighted_goal_churn_portal_512x512` put
  `best_chunk_portal` at 68.4-69.1% of sampled user-cycle self share
  across three captures, and an offline census of every call in two
  portal cells measured 67.13% and 66.68% of calls repeating a key
  already answered in the same tick. Cost-weighted rather than
  call-counted, those rates are 66.73% and 65.14% net of instrument, so
  duplicates are marginally cheaper than average but not materially.
- Shape of the duplication: every `(tick, seam)` pair carries exactly
  one distinct `(current, goal)` query, and each is visited by 3.04
  candidate routes with zero same-route repeats. The redundancy is
  therefore cross-candidate reuse of the same seam under the same
  query, never the same seam under a different query.
- Result, paired on device, main against branch, alternating rounds
  with bootstrap intervals: the repeated-goal portal cell falls 30,415
  to 19,045 ns (-37.3% [-37.5, -37.1]) and the fresh-goal portal cell
  134,296 to 125,585 ns (-6.4% [-7.2, -4.8]).
- Capacity: 128 and 256 entries measure the same within overlapping
  intervals (-36.8% and -37.3% on the repeated cell). 256 was chosen for
  headroom rather than speed: the measured maximum live entry count is
  74 per selection, and entry count scales with chunk distance, so the
  larger table saturates only on routes about three times longer. The
  packed entry is 32 bytes, so the thread-local table is 8 KB.
- Design: the key is `(tile index of current, signed six-way step)`. The
  goal is omitted because it is invariant across one selection; `from`
  is omitted because it is the chunk containing `current`, and a caller
  passing an inconsistent pair bypasses the memo rather than colliding.
  A generation stamp retires every entry when a selection begins, and an
  RAII scope makes a nested selection — reachable through a
  user-supplied passability predicate — bypass the memo instead of
  sharing its generation. Saturation falls back to the uncached call and
  is sticky, so a saturated selection does not re-walk the table on
  every later miss.
- Rejected along the way: a compile-time flag, because a macro that
  changes inline definitions in a header-only library is an ODR hazard
  and would leave the gated path untested; a `PathRuntimeCachePolicy`
  field, because the direct portal builders never receive one and the
  memo retains nothing across calls for a caller to reason about; and a
  hit-rate guard, because it would disable the memo during exactly the
  cold phase whose entries later candidates reuse.
- Mechanism confirmed on the merged binary, not inferred from the
  prototype. Scratch counters compiled in for one run reported 456,750
  memoized calls and 306,600 hits on the profiled cell - 67.13%, the
  census figure to the digit - and zero calls taking the non-keyable
  bypass across 46.6k calls in a second cell, so the
  `from == chunk_coord(current)` invariant holds everywhere reachable
  and nothing silently skips the memo. Independently, the public
  `portal_scan_tiles()` counter on
  `path/weighted_chunk_portal_product_room_portals_512x512` falls 7,456
  to 3,328 (-55.4%) between main and the branch; that cell's topology
  differs from the profiled cells, so its rate differs from 67% as
  expected. The scratch counters were removed before commit.
- Why the merged shape wins less than the prototype did (-37.3% against
  -43.2%): the hit rate is identical, so the difference is the
  consistency guard and the tile-index conversions the merged version
  adds, not fewer hits. The guard costs a `chunk_coord` per call and
  has never once rejected; it is kept because it converts a
  load-bearing assumption into an enforced property.
- The consistency guard was measured rather than argued about. Three
  shapes ran paired on device against each other: the guard as written,
  the guard moved onto the hit path with `from` stored in the entry and
  compared there, and the guard compiled out of release builds.
  Moving it to the hit path is 1.9% SLOWER on the repeated cell
  ([+1.7, +2.3]) - hits are two thirds of calls, so the extra compare
  lands on the majority path and the entry grows 32 to 40 bytes -
  and compiling it out of release measures -0.0% ([-0.3, +0.4]) and
  +0.1% ([-1.7, +0.9]), which is nothing. So the guard is free on the
  target hardware and stays in release: there is no cost to recover by
  weakening it. An earlier 2.3% figure from an unpaired host run was
  noise, as its overlapping variation suggested.
- Worth recording about the measurement: the exact-strategy control cell
  moves -1.2%, and the memo provably never runs there. That residue is a
  codegen and layout confound from the added code, so the portal figures
  contain an unquantified component of the same effect.
- Artifacts: `portal-tick-profile-2026-08-18`,
  `portal-redundancy-census-2026-08-18`,
  `portal-cost-weighted-2026-08-18`, `portal-cache-prototype`.

## 2026-08-18 - Pathfinding strategy comparison

- Hypothesis: source-backed teaching examples plus paired benchmark evidence
  can explain when route caches, weighted batches, and distance fields repay
  their lifecycle cost without implying that any strategy is universally
  fastest.
- Method: Release Google Benchmark CPU time on one Apple M3 Max, single
  threaded, with ten repetitions and a minimum one-second sample per
  repetition. Each pair used the same world and request array. The run could
  not pin thread affinity, reported a load average around 4.0, and therefore
  remains informational rather than a portable threshold.
- Shared-goal result: 100 independent unit-cost A* requests took a median
  17.80 ms; one distance-field build plus 100 reconstructions took 2.78 ms,
  about 6.4x faster.
- Exact-repeat result: 100 independent A* requests took 48.88 ms; the exact
  route cache took 14.52 ms with 70 hits and 30 misses, about 3.4x faster.
- Suffix result: 100 independent A* requests took 113.08 us; the route cache
  took 17.41 us with one miss and 99 suffix hits, about 6.5x faster.
- Weighted-batch result: 100 independent weighted A* requests across eight
  goals took 441.16 ms; the planner built eight fields with no A* fallbacks
  and took 42.29 ms, about 10.4x faster.
- Decision: publish the measurements as machine-labelled workload evidence
  alongside the source-synchronized comparison. Keep API selection conditional
  on measured reuse, and make no benchmark threshold or implementation change.

## 2026-08-18 - Endpoint guard narrowed to substantial barriers

- Premise: the optional browser policy should spread routes around interior
  congestion while retaining the known safety fallback for a dense one-sided
  barrier immediately before an endpoint band.
- Reproduction: replay the checked-in `browser-guard` native scenario, whose
  297 wall coordinates preserve the user-drawn browser fixture, with 1,024
  agents in canonical and spread modes. Seven wall tiles crossed the protected
  approach zones: five along one horizontal wall on the left and two along
  another on the right.
- Finding: the original any-tile guard silently disabled the option for the
  entire leg. Canonical and checked spread modes were identical at 3,277 ticks,
  388,436 routed waits, 1,458 low-progress ticks, and zero seed waves. Both
  still reached all 1,024 goals, so terminal outcome alone hid the policy
  suppression.
- Controlled probe: retain every wall but bypass only the global endpoint
  veto. One normal seed wave then completed all 1,024 agents in 455 ticks with
  25,166 waits, no crowd-blocked or unreachable agents, and no low-progress
  ticks. The existing one-shot merge detector observed 22 merge tiles and
  correctly scheduled no second wave.
- Change: count accepted construction tiles in each eight-column approach zone
  and suppress spreading only when either zone contains at least 64 tiles,
  half the map height. This keeps the mechanism demo-local and additive; it
  does not infer portals, change passability, or alter movement authority.
- Verification: the central two-gate native fixture now includes sparse wall
  touches in both approach zones and must still schedule exactly one seed wave.
  Direct 63/64 boundary checks include duplicate submissions. The existing
  96-tile goal-wall control remains canonical with zero seed waves, and
  maximum-scale terminal checks remain authoritative.
- Decision and limit: accept the narrower guard. The 64-tile threshold
  distinguishes the two measured geometries without claiming a general
  endpoint-capacity proof. Reconsider it only with a failing deterministic
  endpoint fixture; compare terminal outcomes before optimizing tick or wait
  counts.

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
