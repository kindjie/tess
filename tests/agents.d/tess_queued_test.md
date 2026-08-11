# tess_queued_test

- `tess_queued_test`: broad scaffold coverage for queued planning, hazards,
  phases, domains, checked plans, dirty propagation, serial and threaded
  execution, failure ordering, and allocation-free warm paths. Compile-time
  guards distinguish serial-only deferred-dirty execution from partitioned
  executors. Replay stress compares every world tile and reuses one worker pool
  across seeds; multi-worker rendezvous is bounded at 30 seconds so failure is
  diagnostic rather than a CTest timeout.
