## 2026-08-14 - Prefer registered dirty bits for chunk maintenance

- Selected the preallocated `DirtyBitScheduler` as the experimental backend
  for future chunk-maintenance integration. Its explicit registration and
  seal phase publishes stable task identities, after which producers set
  atomic task bits without a producer lock. Scheduling is allocation-free
  after each participating thread's first successful post-seal schedule or
  first task execution; either first use may initialize platform thread-local
  runtime state.
- Kept the immediate, FIFO, and indexed queued-coalescing backends as semantic
  and performance comparisons. The queued membership index removes the
  prototype's quadratic sparse scan, but it still misses the sparse-overhead
  criterion and is materially slower than dirty bits in every measured chunk
  workload.
- Applied the TDD's conditional selection rule: dirty bits beat queued
  coalescing by more than 20% in sparse, dense, and mixed chunk scenarios while
  also satisfying determinism, dirty-generation, concurrency, shutdown,
  allocation, sanitizer, latency, amplification, and flush criteria.
- Kept ownership outside storage. This decision does not alter world
  construction, embed handles in `ChunkMeta`, or schedule maintenance from
  authoritative mutation paths. A future external adapter remains a separate
  integration change.
- Added the new benchmark cells as informational evidence. They require
  representative Linux main-tier calibration before any timing ceiling gains
  blocking authority.
- Retained thread-local run attribution so a task that schedules a zero-progress
  follow-up stops the drain without misclassifying concurrent producers. An
  explicit per-thread preparation API remains deferred until a consumer needs
  allocation-free first use.
