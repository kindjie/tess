# tess_phase_executor_test

- `tess_phase_executor_test`: pins the public phase-executor concepts,
  dispatch, ordering, failure, exception, lifecycle, and no-throw contracts for
  serial, scoped-thread, worker-pool, and custom executors. The allocation
  counter is process-global while pool workers live, so several warm dispatches
  precede the one required to allocate nothing. After a throwing callback, all
  in-flight work is joined and the pool must remain reusable. Nested dispatch
  and reserve-during-dispatch misuse must fail fast in every build mode.
