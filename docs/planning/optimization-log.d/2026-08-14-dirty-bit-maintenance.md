## 2026-08-14 - Registered dirty-bit maintenance

- Hypothesis: replacing queue-depth membership scans with stable registered
  identities and atomic pending bits will make coalesced chunk maintenance
  competitive with immediate sparse work while retaining dense collapse.
- Area: experimental derived-state maintenance scheduling, using synthetic
  sparse/dense tasks plus sparse, dense, and mixed `AlwaysResidentWorld`
  dirty-chunk rebuilds, sparse flush, budgeted continuation, and a 16-to-4,096
  distinct-task scaling sweep.
- Baseline: queued coalescing performed a linear pending scan for each
  admission. The 256-task sparse median was about 25.3 us and the 4,096-task
  scaling cell reached 5.17 ms. Sampling attributed roughly 78% of the sparse
  run to the inlined schedule loop rather than waiting or allocation.
- Accepted queue change: a preallocated pending-membership index tied to ring
  slots reduced the 256-task coalescing median to about 6.42 us and removed the
  quadratic scaling shape. It remains useful as a general non-registered
  backend, but still missed the no-more-than-10% sparse overhead criterion
  against the 3.52 us immediate baseline.
- Accepted candidate: a pre-registered scheduler publishes stable task indexes
  at `seal()` and uses atomic pending bits thereafter. The 256-task synthetic
  sparse median was about 1.06 us, the 512-repeat dense median was 1.22 us, and
  steady-state scheduling allocated nothing.
- Dirty-chunk medians for dirty bits versus queued coalescing were about
  1.47/6.88 us sparse, 2.70/4.96 us dense, and 22.39/41.75 us mixed. Thirty
  repetitions measured dirty-bit versus FIFO p95 latency at 1.49/6.28 us
  sparse, 2.70/12.92 us dense, and 22.72/102.99 us mixed: reductions of 76.3%,
  79.1%, and 77.9%.
- Sparse flush measured about 1.02 us for dirty bits versus 4.68 us FIFO and
  4.83 us queued coalescing. The 256-task, ten-unit, 64-unit-budget case
  measured about 3.48 us versus 8.65 us FIFO and 9.77 us queued coalescing.
- Profiling the representative dirty-bit workload attributed roughly 50% of
  samples to admission, 41% to drain scanning, and 9% to the rebuild task;
  mutex samples were negligible. User plus system CPU tracked elapsed time, so
  the result does not suggest a hidden wait bottleneck.
- Method and limits: release-with-debug-info Google Benchmark CPU time on one
  local arm64 macOS system, ten repetitions for medians and coefficients of
  variation and 30 repetitions for the reported p95 comparison. Sampling was
  on-CPU. Aggregate evidence is retained here; raw local captures were
  diagnostic rather than release artifacts, so the new cells remain
  informational until repeated on the Linux main-tier environment.
- Correctness evidence: 1,000-run cross-backend deterministic hashes,
  byte-identical canonical archives and a load round trip after flush,
  generation-safe intervening marks, concurrent producer and drain cases,
  serialized task execution, budget exhaustion, zero-progress stopping,
  exception retention, shutdown, and allocation contracts all passed. The
  complete maintenance suite also passed ASan/UBSan and TSan.
- Decision: accept registered dirty bits as the preferred experimental chunk
  backend and retain the indexed queue implementation as a general comparison.
  Keep new timing entries non-gating until calibrated on representative Linux
  main-tier artifacts.
