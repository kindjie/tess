## 2026-08-19 - Give maintenance registrations explicit identity and lifecycle

- Added a fixed registered-task facade over the experimental maintenance
  backends. Opaque handles carry scheduler-owner, slot, and generation identity;
  expected stale uncertainty uses checked operations, while wrong-owner and
  unsafe lifecycle use fail-fast diagnostics in every build.
- Defined backend customization structurally at compile time instead of
  freezing the raw experimental virtual scheduler as stable ABI. Custom
  backends implement the small schedule/drain/metrics/pending contract,
  linearize concurrent producers against pending observations, and preserve
  every accepted offer. Pending observations linearize against scheduling and
  drains, while metrics are thread-safe monotonic diagnostic snapshots. The
  facade serializes drains. Backends may optionally provide no-throw fixed
  registration and seal hooks as a required pair.
- Made capacity, idle, drained, budget-exhausted, and stalled states explicit.
  Preserved verbatim callback exception propagation rather than translating an
  arbitrary exception to a lossy task-failure status. The throwing invocation
  and follow-ups coalesced into its synchronous call-local frame are consumed,
  while independently retained accepted offers remain reachable.
- Required a fresh positive `Idle` observation for post-seal release and made
  release retire rather than cancel a slot. In-flight schedules prevent an
  `Idle` result. `Idle` covers only scheduler-reachable work; adapters must
  close and join producers, then separately coordinate dirty state and
  residency mutation at their quiescent boundary.
- Allowed callback scheduling through the same registered owner, including
  self-scheduling, while rejecting nested identity, scheduling, drain, and
  lifecycle operations on another registered scheduler across backend types
  and rejecting reentrant drains. Read-only metrics remain callable across
  owners. This avoids implicit lock orders and their cycle hazards.
- Kept immediate, FIFO, queued-coalescing, and dirty-bit implementations under
  the experimental namespace. The facade grants no authority over exact events
  or authoritative simulation mutation and does not decide backend promotion.
