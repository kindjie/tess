## 2026-08-14 - Keep crowd recovery in the browser colony controller

- Treat settled-aware `NoPath` as a snapshot outcome until an independent
  terrain-only search agrees. A terrain failure remains durably unreachable;
  a teammate-only enclosure cancels the unfinished goal and records a
  crowd-blocked outcome for the current leg.
- When every agent has arrived or is crowd-blocked, abort the incomplete leg
  and rearm the entire synchronized wave in the opposite direction. Count and
  display completed and crowd-aborted legs separately. This preserves one-way
  convoy traffic and avoids temporary sidestep goals, teleports, or a new
  movement authority.
- Keep the policy in the browser demo. Core path results, terminal lifecycle
  phases, joint movement, PIBT, and the rule that arrived agents are immovable
  are unchanged. The existing PIBT tier remains an optional experiment for
  classified live congestion, but cannot recover a state with no active
  agents.
- Rejected per-agent wakeups after a bounded dwell because they produced
  mixed-direction congestion at 1,024 agents. Deferred goal-column staging
  because it guarantees destination order by reducing the scale demo to one
  128-agent column at a time. Repeated crowd turnarounds remain visible rather
  than being mislabeled as successful trips.
