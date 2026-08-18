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
