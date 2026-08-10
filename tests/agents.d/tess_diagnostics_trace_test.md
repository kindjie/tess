# tess_diagnostics_trace_test

- `tess_diagnostics_trace_test` (diagnostics-enabled, links the allocation
  hooks): covers the S4 trace layer. Warning sink -- `WarningSink` concept
  satisfaction, oldest-first ring semantics with overflow `dropped()` counts,
  `source_location` defaulting to the call site, and allocation-free `warn()`.
  Trace buffer -- ring insert/overflow with monotonic sequence gaps, empty-span
  drop, per-category `record_timing` accumulation (first-sample min/max, Count
  sentinel guarded on both record and record_timing), `clear()`, `ScopedTrace`
  nesting, the `ScopedTimer` capture-at-construction binding (a second buffer
  installed before the timer ends must not receive the sample), safe zero-byte
  attribution when a timer outlives its allocation-counter scope, and
  allocation-free recording. Planner trace -- a conflicting plan and a
  phase-assignment plan produce the expected `Planner` records. Snapshot export
  -- `capture_diagnostics`/`capture_timing` copy every category and counter,
  retain a bounded newest-record window, and timed spans attribute inclusive
  allocation/free byte deltas. Schedule coverage pins automatic total-tick and
  per-task spans.
