# tess_execution_phase_safety_test

- `tess_execution_phase_safety_test`: verifies executable phases are
  planner-issued capabilities bound to the exact `ExecutionPlan` that produced
  them. A phase from a disjoint plan cannot be rebound to dispatch same-chunk
  mutable operations through serial, partitioned, or result-bearing helpers;
  phases also expire when a reusable report is replanned or replaced. A wrong
  execution world or foreign caller-owned dirty accumulator is rejected before
  callbacks, dirty scratch, or result slots change.
