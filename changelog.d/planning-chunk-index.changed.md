- Queued-operation planning no longer scales quadratically with the
  operation count. Hazard detection and parallel-phase grouping both
  compared every candidate against every operation accepted so far, which
  cost the most on the ordinary per-chunk-edit shape where the operations
  are pairwise disjoint and every comparison fails. Both now consult a
  chunk-keyed index of accepted operations and examine only the operations
  that actually share a chunk. Planning results are unchanged, including
  which operation a hazard blames; `plan_operations` into a caller-owned
  report stays allocation-free in the steady state, because the index is
  cleared rather than freed on reuse.
  `plan_parallel_execution_phases` takes a plan rather than a report, so
  it has to build its index per call — two allocations a short plan would
  never repay. Plans under sixteen operations therefore keep the
  all-pairs comparison, and a differential test holds both paths to the
  same phase layout.
