# tess_diagnostics_trace_test

- `tess_diagnostics_trace_test` (diagnostics-enabled, links the allocation
  hooks): pins warning, trace, planner, snapshot, timing, and schedule
  instrumentation, including bounded-ring and allocation-free warm behavior.
  `ScopedTimer` binds its destination at construction, so replacing the active
  buffer before destruction must not redirect the sample. A timer that outlives
  its allocation-counter scope safely attributes zero bytes.
