## 2026-08-10 - The parallel/ step was the harness, not the pool

- Area: the gated `parallel/` benchmark family and the alerting leg
  behind it. Closes the change-point issue raised on
  `parallel/tile_touch_pool_w4` (run 31061625127, filed 2026-08-06).
- Alert: the detector reported a sustained shift, baseline median
  17,947 ns to newest 21,369 ns (+19.1%), suspect range c92accd
  (#96) ... b193d0ba.
- Bisect: paired sentinel confirmations, hosted, interleaved,
  Bonferroni-adjusted. The full range c92accd -> b193d0ba confirmed
  +20.1% [+19.2, +20.3]; the slice c92accd -> 73cf59c (#97 + #98)
  confirmed +25.1% [+24.0, +26.2]; #99 was clean (+0.1%), #100/#101
  passed (+2.8%), and b193d0ba -> main measured -0.1%, so the level
  was still live and unaddressed.
- Mechanism: #98 moved the workload bodies out of
  `bench/tess_parallel_bench.cc` (-184 lines) into the new shared
  `bench/parallel_phase_support.h` (+258) so the thread-scaling sweep
  could run byte-identical workloads. `run_tile_touch` and
  `run_parallel_phase` went from anonymous-namespace internal-linkage
  functions, which the compiler fully specialized per translation
  unit, to weak `tess_bench::` templates parameterized on
  `PhaseWorldTraits` and `WarmUp`. Symbol tables of the two paired
  binaries differ in exactly that code. #97 is two documentation
  files, and #98 touches nothing under `include/` at all, so the
  measured difference is the benchmark's own codegen. The library's
  pool is innocent; the cell is simply not comparable across #98.
- Decision: accepted as a measurement epoch, not fixed. Two
  independent reviews of the same brief reached this verdict
  separately. Restoring the old number was
  rejected on three grounds: `[[gnu::flatten]]` promises recursive
  inlining, not the previous code shape, and would restage both
  families' codegen including the published sweep curves; an
  anonymous-namespace forwarding wrapper leaves the weak template
  instantiation as the inliner's root, so it does not address what
  changed; and including the header inside an anonymous namespace
  would fork types per translation unit for a number with no adopter
  meaning. The pre-#98 shape was an emergent property of source that
  no longer exists, so any restoration would be a third shape agreeing
  with the old one by coincidence — and a near miss would leave three
  epochs in the series instead of two. Preserving an accidental
  optimizer decision would make the cell a test of compiler
  heuristics.
- Epoch rule, enforced rather than asserted: pre-#98 and post-#98
  `parallel/` observations measure different harness codegen around an
  unchanged library call and must not be pooled for baselines,
  change-point analysis, or threshold calibration. Recording that only
  in prose would have left the detector free to seed a post-#98
  baseline with pre-#98 readings and re-raise this settled
  investigation as a fresh suspicion, so the boundary is data:
  `bench/benchmark-epochs.json` names the `parallel/` prefix and the
  first post-#98 artifact run, and the detector drops older readings
  for those benchmarks. That defers alerting on them until the new
  epoch has its own eight baselines, which is the honest state.
  Boundary: last old-harness commit
  `6e67d3843b8d9ab5a8c51c593f7a7dc1c077f352` (#97), first
  shared-harness commit `73cf59ca93144dd2b6091d31748091fa14573730`
  (#98), both dated 2026-08-02. The retained comparison binary is
  `c92accd48ca5aff10f96e3cfdbba5b83c80b8b33` (#96); it precedes the
  boundary only by #97's documentation.
- Scope, measured rather than assumed (paired run 31438907252,
  6e67d38 -> 73cf59c, all ten gated registrations, real time, 99.5%
  intervals). Only the two `tile_touch` cells moved:

  | Benchmark | Base | Head | Δ | Verdict |
  | --- | --- | --- | --- | --- |
  | tile_touch_pool_w4 | 27,391 ns | 34,511 ns | +26.1% [+22.3, +27.0] | regression |
  | tile_touch_serial | 2,023 ns | 3,283 ns | +31.5% [+0.3, +85.2] | immaterial-scale |
  | tile_touch_scoped_threads_w4 | 130,324 ns | 131,037 ns | +0.8% | pass |
  | chunk_fill_pool_w2 | 62,920 ns | 67,151 ns | +6.1% | pass |
  | chunk_fill_pool_w4 | 60,724 ns | 62,901 ns | +3.5% | pass |
  | chunk_fill_serial | 47,380 ns | 47,577 ns | +0.4% | pass |
  | chunk_fill_scoped_threads_w4 | 147,931 ns | 146,708 ns | -0.7% | pass |
  | chunk_compute_pool_w2 | 1,145,935 ns | 1,150,465 ns | +0.4% | pass |
  | chunk_compute_pool_w4 | 668,629 ns | 670,972 ns | +4.9% | pass |
  | chunk_compute_serial | 2,214,703 ns | 2,215,682 ns | +0.0% | pass |

  The pattern corroborates the mechanism rather than merely restating
  it. `tile_touch` writes one tile per chunk, so its per-operation
  work is near zero and the harness's own code is most of what the
  cell measures; `chunk_fill` and `chunk_compute` do real work per
  chunk and absorb the same codegen change into noise. The one
  `tile_touch` cell that did not move, `scoped_threads_w4`, spends
  130 us in thread creation per iteration, which swamps the
  difference. An effect that appears only where the measured quantity
  is small is a property of the measurement, not of the pool.
- Ceilings: unchanged. `parallel/tile_touch_pool_w4` stays at 61,000
  ns and the post-#98 level sits well inside it; the standing S11.3
  policy (2x the maximum observed across 10 CI baseline artifacts)
  governs recalibration once ten post-epoch baselines exist, and
  mixing pre- and post-#98 samples into that calculation is exactly
  what the epoch rule forbids. Until then the shifted cells run with
  less effective headroom than the nominal 2x, which is conservative
  and deliberate.
- Coupled defect, fixed in the same change: the detector and the trend
  renderer read `cpu_time` unconditionally, while
  `bench/thresholds/parallel.json` sets `max_cpu_time_ns` to null for
  all ten cells on purpose — pool work happens on worker threads, so
  the dispatching thread's CPU time understates the operation. This
  issue is the visible consequence: it was raised on cpu_time while
  every confirmation that answered it measured real_time, because
  `tools/paired_bench.py` already selected the gated metric. The two
  numbers happened to agree in direction, which is why it went
  unnoticed. The selection rule now lives in
  `tools/benchmark_thresholds.py` and is shared by all three tools.
- Caveat on the published series: the eleven baselines on the
  `benchmark-data` branch do not show a clean step at the boundary.
  The #98 artifact is the lowest reading in its stratum (26,785 ns
  real time), the elevated plateau begins with the next artifact on
  2026-08-05, and the spread within a single stratum (26.8k to 36.9k
  on the 9V74 runs, 30.5k to 40.3k on the 7763 runs) is the same
  magnitude as the effect attributed to codegen. Two runner models
  alternate across that history (EPYC 7763 and 9V74, seven and four
  artifacts), and each stratum holds fewer artifacts than the detector's
  own eleven-artifact minimum, so this series cannot resolve an effect
  this size — running the detector over it today returns
  insufficient-history. It is not the detector's input: alerting reads
  trailing Actions artifacts, a denser, retention-limited series that
  is not published, which is why it had enough in-stratum history to
  fire. The published levels do straddle the boundary by about the
  right amount — 26,785 ns times the measured +26.1% lands on the
  34,588 ns plateau, and `tile_touch_serial` steps from ~2,000 to
  ~3,000 ns as its +31.5% predicts — but they step one artifact later
  than the source boundary, which cannot be reconciled from what is
  archived. The published baselines are therefore recorded here as
  context rather than as corroboration. This entry rests on the controlled interleaved paired
  runs, which are designed for exactly this comparison. Whether the
  alerting series is stable enough for a cell with this much
  run-to-run spread is a live question for the alerting leg, and is
  left open rather than settled here.
- Deferred, deliberately: the gated family still measures with
  `WarmUp::kNo`, whose cold-start bias `parallel_phase_support.h`
  documents as an open item because warming it would step every
  baseline and trend series at once. This epoch is the cheapest moment
  to absorb that second step, but it is a semantic change to what the
  cell measures, whereas the #98 step is pure codegen; bundling them
  would leave neither attributable afterwards. Recorded here so the
  alignment is a choice rather than an oversight.
