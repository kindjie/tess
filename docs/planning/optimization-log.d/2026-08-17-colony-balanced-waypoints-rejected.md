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
