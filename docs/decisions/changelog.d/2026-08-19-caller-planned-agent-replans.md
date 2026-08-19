## 2026-08-19 - Separate replan scheduling from route authority

- `process_path_agent_replans` owns bounded FIFO scheduling and agent
  lifecycle transitions, while its synchronous callback owns the legality and
  optimality of the returned route. The callback's borrowed path must remain
  valid through the immediate retained-route copy and must not reenter or
  mutate the queue, agents, or routes. A thrown callback leaves the current
  queue item and lifecycle state unchanged, but callback side effects are not
  rolled back.
- This qualifies the exact-only queue rationale recorded on 2026-08-14 and in
  the historical budgeted-replanning TDD. The generic drain permits callers to
  compose domain-specific route construction without moving that policy into
  Tess. `process_unit_path_agent_replans` and
  `process_weighted_path_agent_replans` remain the exact helpers and retain
  their legality and optimality guarantees.
