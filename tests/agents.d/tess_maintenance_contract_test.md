# tess_maintenance_contract_test

- `tess_maintenance_contract_test`: pins the fixed-registration maintenance
  candidate's opaque handle, owner/epoch, checked stale lookup, explicit
  schedule/drain/release results, positive-Idle release gate, task lifetime,
  exception containment, reentrancy, custom structural and fixed-hook backends,
  concurrency, and warmed allocation contracts. The bounded custom backend is
  deliberately non-derived and lossless so its capacity, retry, pending, drain,
  exception-retention, and producer linearization checks exercise the public
  extension boundary. The Immediate exception case deliberately proves that
  an accepted follow-up in the throwing invocation's call-local frame is
  consumed, unlike independently retained work, and that an explicit retry
  clears the task's authoritative dirty state. Cross-owner metrics observation
  is the deliberate read-only exception to guarded nested operations. `Idle`
  observes only scheduler-reachable work; it is not evidence that an adapter or
  world is clean.
