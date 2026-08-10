# tess_phase_executor_test

- `tess_phase_executor_test`: verifies the public `tess/ops/phase_executor.h`
  contract in isolation: compile-time `PhaseExecutor` concept conformance
  (satisfied by `SerialPhaseExecutor`, `ScopedThreadPhaseExecutor`,
  `WorkerPoolPhaseExecutor`, and a minimal custom executor; rejected for
  wrong return types, non-const `for_each_operation`, and non-executors),
  `SerialExecutor` tag relationships, exact-range single-visit dispatch for
  serial, threaded, pool, and custom executors, serial first-failure
  short-circuit ordering, threaded failure reporting after join,
  concept-constrained `execute_operation_index_range` dispatch, and
  allocation-free warm serial dispatch. Worker-pool coverage adds
  worker-count clamping, empty-range early return, worker reuse across many
  phases, first-failure reporting in operation order, allocation-free
  steady-state warm dispatch after `reserve_operations` (several warm
  dispatches run and only the last must not allocate, since the counter is
  process-global while pool workers are live), repeated create/run/stop
  lifecycle cycles, and destruction without ever running a phase.
  Scoped-thread and worker-pool exception tests require callbacks that throw to
  cancel work that has not started, join callbacks already in flight, and
  rethrow on the dispatching thread; the pool must remain usable for a
  subsequent successful phase.
  The explicit no-throw aliases are representation-checked and execute a
  no-throw callback without exception coordination.
