## 2026-08-14 - Colony large-agent recovery and replan stutter

- Area: the 128x128 browser colony with 1,024 agents and the reproduced
  239-wall layout, built with Emscripten 6.0.6.
- Baseline: 600 post-edit ticks measured p50 6.6 ms, p95 29.8 ms, p99 94.8
  ms, and max 310 ms. Native tracing attributed recurring 75-89 ms ticks to
  317-344 weighted A* recovery probes every 16 ticks (roughly 1.5-1.6 million
  expansions); movement itself was below 1 ms. The one-time wall edit also
  triggered a synchronous all-agent replan.
- Accepted recovery change: honest `RemainBlocked` exhaustion plus a bounded
  deterministic recovery schedule, exponential backoff/equal jitter, and unit
  exact reachability probes for this uniform-cost demo removed the recurring
  herd. Five matched browser runs then measured p50 1.1-1.2 ms, p95 2.0 ms,
  p99 2.2-2.3 ms, with the separate wall replan still 297-300 ms.
- Rejected: selective invalidation found that all 1,024 retained routes crossed
  at least one new wall, so it changed no work and left the wall tick near
  300 ms. Portal-first replanning measured 615 ms because rejected candidates
  paid portal work plus exact fallback, matching that policy's documented
  worst-case shape. Neither experiment was retained.
- Accepted topology change: a caller-owned exact FIFO replan queue shares an
  eight-query budget with recovery. Three matched browser runs measured p50
  1.5 ms, p95 3.3-3.4 ms, p99 4.3-4.4 ms, and max 4.5-4.9 ms. The queue makes
  all-agent convergence gradual and relies on validated movement to reject an
  old route's newly illegal step; it does not bound one A* query's expansions.
- Scheduler overhead (O3, five repetitions): a fully blocked scan cost roughly
  12 ns for one agent, 55-57 ns for 32, 1.33-1.39 us for 1,024, and 16.3-16.6
  us for 10,000. Warm scans and replan drains allocated nothing.
- Population sweep (native O3, three 600-tick runs with trip relaunches):
  retained-route p50 stayed within noise through 256 agents, improved from
  roughly 52 to 48 us at 512 and 201 to 160 us at 1,024. Median-run p99 fell
  from 7.7/31.5/62.5/132/302/695 us at 16/64/128/256/512/1,024 agents to
  3.0/6.4/6.7/22.5/64.5/254 us. The explicit all-agent-replan diagnostic
  remained near 37/193/483 us p50 at 128/512/1,024 after its redundant queue
  drain was removed.
- Post-review population sweep (native O3, five paired 600-tick runs) found no
  material regression from observing blocked-agent position changes. Median
  retained-route p50 before/after was 0.3/0.3, 1.2/1.3, 2.5/2.5, 12.5/12.7,
  49.5/49.3, and 164.5/165.2 us at 16/64/128/256/512/1,024 agents. Explicit
  replan p50 was 38.0/38.0, 195.3/196.5, and 494.8/497.4 us at 128/512/1,024.
  P95 and p99 moved in both directions; the largest absolute median-run p99
  increase was 22 us at 1,024 explicit-replan agents.
