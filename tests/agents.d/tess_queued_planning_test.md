# tess_queued_planning_test

- `tess_queued_planning_test`: pins the planner-reuse
  contract -- the `plan_operations` overload that plans into a caller-owned
  `ExecutionReport` recycles report rows, planned operations, and pooled
  chunk lists, so warm steady-state planning performs zero allocations
  (counter-backed).
