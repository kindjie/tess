# tess_flow_accounting_test

- `tess_flow_accounting_test`
  (`tests/tess_flow_accounting_test.cc`): per-transition coverage for
  the diagnostics flow-accounting layer — FlowCounters conservation
  identities, delta-weighted tick observation, snapshot verdicts, and
  the four instrumented flows (resumable work queue including stale
  reclassification, clear-drops, move/copy attachment semantics, and
  oldest-age tracking; event streams including overflow rejection,
  consume versus discard, and residence; maintenance schedulers
  including coalescing, capacity rejection, throwing tasks, and
  immediate-backend self-schedules; path-agent goal lifecycles
  including supersede, cancel, arrival, retry-exhaustion failure, and
  terminal re-arming).
