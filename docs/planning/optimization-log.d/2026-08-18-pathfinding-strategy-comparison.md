## 2026-08-18 - Pathfinding strategy comparison

- Hypothesis: source-backed teaching examples plus paired benchmark evidence
  can explain when route caches, weighted batches, and distance fields repay
  their lifecycle cost without implying that any strategy is universally
  fastest.
- Method: Release Google Benchmark CPU time on one Apple M3 Max, single
  threaded, with ten repetitions and a minimum one-second sample per
  repetition. Each pair used the same world and request array. The run could
  not pin thread affinity, reported a load average around 4.0, and therefore
  remains informational rather than a portable threshold.
- Shared-goal result: 100 independent unit-cost A* requests took a median
  17.80 ms; one distance-field build plus 100 reconstructions took 2.78 ms,
  about 6.4x faster.
- Exact-repeat result: 100 independent A* requests took 48.88 ms; the exact
  route cache took 14.52 ms with 70 hits and 30 misses, about 3.4x faster.
- Suffix result: 100 independent A* requests took 113.08 us; the route cache
  took 17.41 us with one miss and 99 suffix hits, about 6.5x faster.
- Weighted-batch result: 100 independent weighted A* requests across eight
  goals took 441.16 ms; the planner built eight fields with no A* fallbacks
  and took 42.29 ms, about 10.4x faster.
- Decision: publish the measurements as machine-labelled workload evidence
  alongside the source-synchronized comparison. Keep API selection conditional
  on measured reuse, and make no benchmark threshold or implementation change.
