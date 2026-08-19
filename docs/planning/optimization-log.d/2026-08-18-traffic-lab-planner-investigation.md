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
- **Browser and behavior result:** a fresh-origin Chrome capture of the rebuilt
  Wasm used a 1920x1080 viewport. Every scenario recorded zero catch-up frames;
  render p99 was 1.2--1.3 ms and frame-callback p99 was 2.0--2.1 ms. The canvas
  remained exactly 2:1, fit horizontally, and caused no horizontal overflow at
  both 1366x768 and 1920x1080. Fixed-tick browser families had only 260--585
  samples, so browser update p99 remained suppressed and the native campaign
  retains authority. The browser artifact preserves summarized output and
  rebuilt Wasm, loader, and app hashes, but not raw browser samples or the
  browser version; its percentiles are therefore not independently
  recomputable. Exhaustive native validation matched direct compound-class
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
  `147f2a53ba4179cabde2aff3b6d599e1e054b550e7e005237bb0c0198c23e4b1`.
  These remain untracked machine-local evidence; the maintained record keeps
  the portable method and accepted conclusions.
