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
  per-agent dumps are taken from the 0.125 ms cell); dev machine
  (mechanism attribution, not timing evidence). PIBT maintains its own
  adaptive priorities; an early arm double-maintained them externally and
  was rerun after the fix with identical results.
- **Attribution (each step measured):** 86/500 agents complete zero items —
  the same set every repetition, all phase Blocked with retries exhausted,
  goals free, 1-3 neighbors occupied; zero reserved-but-empty tiles
  (reservation-leak refuted); zero goal-parkers and zero mutual goal pairs
  (simple head-on refuted); all 148 blocking occupants are themselves
  ≤2-completion agents forming 92 adjacency clusters (one of 58 agents).
  Mechanism: planning is occupancy-blind by design, stepping admits only
  on-route moves, and blocked agents retry the same retained step
  indefinitely (retry exhaustion stops planning, not stepping), so mutual
  occupancy-wait chains never resolve — permanent livelock pockets, the
  documented baseline gap the PIBT tier exists to close.
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
  - PIBT+Forbid, boxed BFS field ranking: 47 (margin 16) / 31 (margin 48)
    — boxed fields miss doors beyond the margin; the field is flat inside
    the box and the trap reappears.
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
  0 / 20; p250/20: 0 / 1.000 / 0 / 30. Stability genuinely holds through
  p250 and degrades at p500 — a measurable capacity boundary instead of
  universal failure.
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
