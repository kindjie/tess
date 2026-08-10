# tess_maintenance_test

- `tess_maintenance_test`: verifies the experimental immediate, FIFO, and
  coalescing maintenance backends, including duplicate scheduling, budgeted
  continuation, concurrent scheduling, deterministic flush, partial dirty
  clearing, allocation-free constant-stack immediate self-scheduling,
  synchronous concurrent immediate calls, duplicate self-request preservation,
  direct and cross-task zero-progress detection, concurrent producer
  distinction, exception consumption with caller-controlled explicit retry,
  shutdown, capacity failure, and steady-state allocation behavior.
