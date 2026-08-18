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
