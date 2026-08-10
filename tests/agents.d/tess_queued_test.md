# tess_queued_test

- `tess_queued_test`: verifies the M4 queued-operations scaffold, including
  empty-frame planning, stable handles and enqueue-order ids, explicit/dirty/
  active/resident chunk-domain expansion, enqueue-order plan preservation,
  diagnostic access metadata, untyped field access mask propagation,
  structured invalid write-policy, invalid field-access, and explicit-domain
  rejection, report lookup/count helpers, mixed valid/invalid report ordering,
  deterministic field-mask hazard validation, deterministic parallel phase
  grouping/rejection, plan-to-block adapters, policy mismatch rejection,
  allocation-free prebuilt planned block iteration, explicit planned execution
  with direct and deferred dirty propagation, dirty-record coalescing,
  phase-range deferred execution and rejection, backend-executor range dispatch,
  partitioned dirty phase execution and merge including test-only threaded
  mutable and read-only dispatch, scoped threaded executor equivalence and
  replay stress over shuffled legal phase plans, failure ordering, read-only
  const-view enforcement, partitioned threaded failure semantics,
  policy-mismatch execution rejection, allocation-free prebuilt planned
  execution, and the structural serial-executor guard (compile-time
  `static_assert`s that
  `execute_phase_deferred_dirty_with` accepts `tess::SerialExecutor`-tagged
  executors and rejects `ScopedThreadPhaseExecutor`, while the partitioned
  variant accepts both). Checked-plan coverage additionally pins non-aggregate
  construction, manual factory validation, cross-shape execution rejection,
  and checked dirty record/merge rejection without world or accumulator
  mutation, including rejection of incompatible empty-but-bound accumulators.
  Threaded replay stress compares every tile of every chunk between
  serial and threaded worlds via field spans, and the same replay harness
  drives one reused `WorkerPoolPhaseExecutor` across all stress seeds to pin
  persistent-pool equivalence with serial execution. Multi-worker rendezvous
  points use a bounded 30s spin (`await_rendezvous`) that fails with a clear
  message instead of hanging until the ctest timeout.
