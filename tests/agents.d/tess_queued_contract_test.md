# tess_queued_contract_test

- `tess_queued_contract_test`: verifies queued-operation lifecycle and boundary
  contracts, including source-location capture, allocation-free inspection of
  already-built queue/report/plan spans, typed path/nearest/field-product/
  movement/topology/residency/dirty/render intent envelopes with type-checked
  non-owning batch payloads and planner-preserved version, invalidation,
  backend, and exactness metadata, allocation-free planned block
  iteration, a tagged custom serial executor end to end,
  `FrameOps::clear()` id restart and allocation-free warm re-enqueue,
  zero-value default-constructed `OpId`/`OpHandle`, write-then-read hazard
  rejection and same-chunk write-then-read phase splitting, planner-issued
  full-phase execution and executor-range conversion, contiguous raw range
  dispatch including empty/end-anchored ranges, empty-plan phase planning,
  explicit-domain duplicate-key deduplication, empty explicit domains,
  `ScopedThreadPhaseExecutor` worker-count clamping and zero-count early return,
  dirty-before-callback ordering for direct, deferred, and phase execution,
  coalescing of overlapping read-only dirty records, and compile-time proof
  that explicit `noexcept` callbacks remain no-throw through partitioned and
  result-channel adapter layers.
