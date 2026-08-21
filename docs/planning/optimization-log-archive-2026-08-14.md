# Optimization Log Archive (2026-08-10 to 2026-08-14)

Entries moved from `optimization-log.md` when it approached the
24k-token file limit. Newer entries remain in the live log; older
archives are linked from there.

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

## 2026-08-14 - Colony settled-goal enclosure and turnaround

- Area: the 128x128 browser colony at up to 1,024 agents, especially its eight
  destination columns and wall edits made while a leg is active.
- Diagnosis: the reported 968-arrived/56-terminal state had no moving agents.
  A `Traveler` exact `NoPath` excluded settled teammates, but the demo promoted
  that snapshot result to durable `Unreachable` and then published the failed
  agent as another settled obstacle. The all-arrived relaunch gate could never
  open. Separately, the wall setter admitted occupied tiles even though the
  movement and path APIs correctly reject an impassable source.
- Accepted demo policy: after settled-aware failure, an independent exact
  terrain-only search distinguishes a durable wall failure from a teammate-only
  enclosure. The latter cancels the unfinished goal and is quiescent for the
  current leg. Once all agents have arrived or are crowd-blocked, the
  controller aborts that leg and rearms the synchronized wave in the opposite
  direction. Completed and crowd-aborted legs are counted separately. Wall
  requests are rejected synchronously when their tile is occupied, and
  topology updates now run in `Pathing` before `Movement`. No core-library
  semantics changed.
- Rejected wake-up experiment: rearming each arrived agent after a 20-tick
  dwell made settled occupancy temporary, but introduced mixed-direction
  traffic. In the 1,024-agent narrow-gap control it still had not completed a
  leg after 5,000 ticks; the debug run took 87.7 seconds. Temporary sidestep
  goals were not attempted because they also require restoration, reservation,
  chain arbitration, and starvation policy.
- Deferred dynamic-routing option: the existing PIBT movement tier already
  supports an active agent stepping off-route and priority-inheriting through
  active teammates. Its ring, detour, passability-consistency, allocation, and
  immovable-arrived tests pass, but the mechanism cannot move an arrived or
  unreachable agent and therefore cannot repair the reported quiescent state.
  Exact per-agent ranking for 1,024 distinct goals would hold about 64 MiB of
  distance cells before metadata and would rebuild as settled passability
  changes. It remains a separate library-policy option if classified live
  congestion persists after the demo lifecycle fix.
- Evidence: native regressions cover occupied-wall rejection and retry after
  vacancy; a four-neighbour enclosure under retained and all-agent-replan
  strategies; a synthetic exact 968+56 state whose formerly blocked agents
  complete the recovery leg; 1,024 agents naturally crowd-sealed by a mid-leg
  wall before all eight destination columns; retained and all-agent-replan wall
  seals; a 128-agent two-wall bottleneck; and the existing 48-agent three-wall
  case. The maximum-scale turnaround keeps the shared retained-route planning
  budget at eight exact queries per tick. Broader path-agent, PIBT, and
  colony-harness suites cover serial/pool execution, worker counts, chunk
  sizes, cache states, and incremental/fresh topology.
- Performance boundary: normal movement and core library code are unchanged;
  occupied-wall admission is one field read, while a failed settled-aware
  recovery may need a deferred terrain confirmation within the existing exact
  query cap. The final O3 native self-check took 1.27 seconds; the Wasm bundle
  compiled, loaded in a web-runtime smoke, exposed the new counters through
  `cwrap`, and rejected an occupied wall tile. The self-check now covers
  substantially more scenarios than the 96 ms baseline and is not a paired
  hot-tick comparison. Accepted-policy retained-versus-replan timing therefore
  remains unmeasured rather than being claimed as a non-regression.

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

## 2026-08-11 - Calibrate the five post-fix fields ceilings

- Area: the five fields gates deferred after the chunk-level capture fix.
- Evidence: ten distinct successful main-push baseline artifacts, enumerated
  in the benchmark calibration history, supplied 100 raw samples per benchmark
  under usable Ubuntu 24.04 runner fingerprints.
- Decision: accepted. Applying the standing 2x maximum-observed rule tightens
  `goalset_build_1` to 326,214 ns, `goalset_build_16` to 341,301 ns,
  `goalset_build_256` to 361,106 ns, `cache_miss_store` to 352,727 ns, and
  `cache_eviction` to 357,455 ns. These are calibrated gates rather than
  bootstrap evidence.
- Scope: other explicitly labeled fields bootstrap cells remain advisory.
  Their evidence windows and workloads are independent of this five-cell
  deferral and were not silently promoted by the recalibration.

## 2026-08-10 - Four-ary sift: rejected on both platforms

- Area: the packed open-list heap (all weighted searches). Follow-up
  to the Deck verification entry, which measured ~20% of the mixed
  batch cell still in libstdc++ sift machinery and named a d-ary sift
  the justified next experiment.
- Hypothesis: halving sift levels (arity 4 over the same strict total
  order — pop sequence provably unchanged, pinned by the differential
  test driving the new functions against a std-heap oracle plus the
  per-consumer goldens) would recover part of the Deck's ~20%.
- Method: measurement-first under a reviewer-validated protocol
  (predeclared endpoints: primary = the profiled mixed batch cell,
  secondary = geomean of the three A* batch cells; ship threshold
  Deck >= 5%, M3 non-regression within 2%; balanced ABBA/BAAB blocks,
  8 invocations per binary per platform; binary SHAs recorded; commits
  9d0d6a3 and the bottom-first follow-up on branch perf/quad-heap,
  never merged). Two variants: the classic early-exit hole sift, then
  the bottom-first sift (libstdc++'s strategy at arity 4) after the
  first variant's flood regressions were diagnosed as early-exit
  overhead on pops that sink a recent push to the bottom.
- Evidence: REJECTED, decisively and symmetrically.
  - Variant 1 (early-exit): Deck A* batches -4.8/-5.1% but mixed only
    -1.3%, flood batch +4.8%, product-build flood +30.3%; M3 +9.2 to
    +14.4% on every weighted cell.
  - Variant 2 (bottom-first): Deck A* -5.3/-5.3/-3.1% (secondary
    geomean -4.6%), flood batch +2.1%, product-build flood +21.8%; M3
    +7.4 to +16.1% on every weighted cell.
  - Both variants fail the primary threshold, regress a covered
    consumer badly on the Deck, and fail M3 non-regression outright.
    The standard-library heaps beat this hand-rolled arity-4
    implementation on every flood workload on both platforms and on
    everything on M3; the only sustained gain is ~5% on two Deck A*
    batch cells.
- Interpretation, scoped per the protocol review: this rejects "the
  4-ary implementation", not arity as a concept — the comparison
  bundles arity with sift strategy, inlining, and codegen. The
  libstdc++ ~20% sift share on the Deck is real but is evidently near
  the cost floor for this access pattern; share is not headroom.
- Retry conditions: only with a structurally different open set (the
  2026-06-05 indexed-heap/decrease-key deferral remains the recorded
  candidate), or a per-consumer split keeping std heaps for floods —
  and only if a future profile shows the A*-side sift share grown
  enough that ~5% on the Deck justifies the added surface. The
  packed-node equivalence harness (differential oracle + goldens)
  carries over to any such attempt.

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
- Epoch rule: pre-#98 and post-#98 `parallel/` observations measure
  different harness codegen around an unchanged library call and must
  not be pooled for baselines, change-point analysis, or threshold
  calibration. This is recorded, not enforced. `tile_touch_pool_w4` is
  the only cell that could be re-flagged over the boundary — the other
  nine either shift under the 10% relative floor (6.1% at most) or, in
  `tile_touch_serial`'s case, cannot clear the 2,000 ns absolute floor
  with a 1,260 ns delta — and this entry is the answer if it is.
  It was not, at the first opportunity. This entry first predicted the
  re-flag outright; that was too strong. Run 31444691634 read the whole
  60-artifact window with the corrected metric selection, 48 of those
  artifacts predating the boundary, and returned `clean`. One run does
  not make it impossible: only the newest runner stratum is evaluated,
  the rule needs all three candidates elevated, and one of the three
  was a 28,579 ns reading from an unrelated commit — a low candidate
  defeats the rule by itself. The exposure is transient either way,
  because baseline artifacts have 30-day retention, so pre-boundary
  readings age out by roughly 2026-09-01 and each main push pushes them
  further out.
  Boundary: last old-harness commit
  `6e67d3843b8d9ab5a8c51c593f7a7dc1c077f352` (#97), first
  shared-harness commit `73cf59ca93144dd2b6091d31748091fa14573730`
  (#98), both dated 2026-08-02. The retained comparison binary is
  `c92accd48ca5aff10f96e3cfdbba5b83c80b8b33` (#96); it precedes the
  boundary only by #97's documentation.
- Why it is not enforced: a mechanism that drops pre-boundary readings
  was built and then withdrawn on review. Suppression is the wrong
  default for an advisory signal. Its failure mode is a silently
  missed regression — a mistyped boundary mutes a benchmark entirely
  while the run still reports `clean`, provenance fields are prose the
  loader discards rather than enforced, per-benchmark "not evaluated"
  is indistinguishable from "clean" in the verdict, and the ordering
  depends on run identifiers whose chronological comparison GitHub
  does not actually guarantee. Weighed against it, the cost of not
  enforcing is one duplicate advisory issue that a reader closes by
  citing this entry. A duplicate alert is cheaper than a missed
  regression, so the alert stays. Automating this is now recorded as
  not planned, the clean first run having removed what payoff it had;
  should a boundary ever justify revisiting it, the design must
  annotate an alert with its recorded epoch rather than suppress the
  reading, which cannot hide anything. What #164 keeps is the defect
  the review turned up on the way: a benchmark the detector skipped for
  want of history is reported inside a `clean` verdict, so "not
  evaluated" and "passed" are indistinguishable — which is what made
  suppression hard to reason about in the first place.
- Second alert, refuted 2026-08-13: change-point run 31669132367 reported
  `tile_touch_pool_w4` at 39,997 ns against a 32,734 ns baseline (+22.2%)
  over suspect range b193d0ba ... 2131b279. An exact local paired
  confirmation on Apple M3 Max/AppleClang 21 measured 13,628 ns at the base
  and 13,692 ns at the head, +0.1% with a 95% interval of [-1.1%, +0.7%],
  verdict `immaterial-scale`. The range changes no parallel source or harness;
  its only library edits add exception-free assertions to queued-operation
  headers. This refutes a regression attributable to the reported range, not
  every older level in the hosted series. Decision: close the duplicate alert,
  keep the 61,000 ns ceiling unchanged, and reconsider only if a widened
  paired comparison or a later same-stratum alert identifies a reproducible
  range.
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
  context rather than as corroboration. This entry rests on the
  controlled interleaved paired runs, which are designed for exactly
  this comparison. Whether the
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

## 2026-08-10 - One compare orders the weighted open lists

- Area: every weighted open list — the weighted A* loop (astar.h), the
  weighted distance-field flood (path.h), the boxed flood
  (distance_field_box.h), and both field-product flood loops
  (field_product_cache.h). Follow-up to the banked Deck main-suite
  baseline (2026-08-08), whose largest absolute costs are the weighted
  A* batch cells (1.21 s / 1.08 s / 0.55 s on device), and to the
  DWARF srcline finding of ~18.5% stl_heap push/pop plus ~19%
  comparator/iterator inside exact weighted A* — measured on the
  goal-churn singleton profile; transfer to the batch cells was a
  hypothesis this change's A/B tested.
- Hypothesis: the three-field comparator (f asc, g desc, index asc)
  costs up to three compare-branches per heap step; concatenating f
  and UINT32_MAX - g into one 64-bit key decides almost every step
  with one compare, without changing which node pops.
- Method: `detail::PackedOpenNode{key, index}` replaces the element of
  the private open-list vectors in place (same 16-byte size, same
  reserve/clear lifecycle, no public type changed). The ordering is a
  strict total order (index breaks every remaining tie; same-index entries
  differ in g by the strict-improvement push guard), and the key is
  injective and order-isomorphic to the old comparator's first two
  fields over their FULL range — UINT32_MAX - g is defined and
  invertible for every g — so any correct heap pops the identical
  sequence and behavior is preserved by construction. The unit-cost
  two-bucket loop keeps its dial semantics through f()/g() accessors.
  Prior art honored: the 2026-06-05 comparator/OpenNode experiments
  (by-const& comparator, Coord3 payload — both rejected) recorded
  "reconsider if the open-set representation changes"; this is that
  change, and the tie-break ORDER those experiments settled is
  preserved bit-exactly.
- Evidence: accepted on M3 (interleaved A/B/A/B, 2 repetitions per
  round, rounds within 1.3%): weighted_astar_batch mixed 568.4 to
  491.9 ms (-13.5%), shared_sparse 271.6 to 233.5 ms (-14.0%),
  multigoal_sparse 515.4 to 446.3 ms (-13.4%),
  weighted_distance_field batch 135.5 to 120.1 ms (-11.3%) — the
  flood gain arriving despite the review's expectation that floods
  (which push f == g) would benefit less. Bounded flood and
  goal_churn_portal watch cells flat. The bench counters double as an
  equivalence check: expanded_total and cost_total are identical
  before and after. Steam Deck verification pending device access;
  the platform baselines differ (libc++ heap vs libstdc++), so the M3
  result does not transfer either direction.
- Equivalence: comparator-isomorphism tests over corner and seeded
  triples, pack round trips at all corner values, a differential heap
  test pinning identical pop sequences on adversarial insertions
  (same-index stale chains, equal-(f,g) index ties, boundary words),
  and tie-heavy goldens captured from the pre-change loops for every
  packed consumer: weighted A* (cost, path length and walk validity,
  expanded/reached literals), the general flood (expanded/reached and
  replay-cost literals), the boxed flood, the weighted goal-set
  product, and the unit-product weighted branch forced by a
  stair-transitions provider (expanded/reached literals each). The
  originally drafted bounded-flood case was dropped after review
  showed that consumer routes to the bucket implementation and never
  touches the packed node. Scratch-reuse pins across
  weighted/unit/weighted searches and warm allocation-freedom close
  the lifecycle. Fail-before mutants against the final suite: an
  inverted key order fails all four consumer goldens plus the
  comparator and differential tests (8 total — every golden provably
  exercises the packed comparator); dropping the index tie-break fails
  the 3 order-sensitive tests (isomorphism x2, differential heap) —
  flood expanded/reached literals are order-insensitive under lazy
  deletion, so pop order is carried by those three and the identical
  bench counters.
- Follow-ups: a d-ary sift over the same packed element, only if the
  next profile still shows the heap hot; Deck on-device verification
  when access returns.

## 2026-08-10 - Packed open node on the Deck: flat, and why that gates d-ary

- Area: on-device verification of the packed open-list key (merged
  2026-08-10), which the M3 A/B accepted at -13.5/-14.0/-13.4% on the
  weighted A* batch cells and -11.3% on the flood batch.
- Method: interleaved A/B/A/B on the Steam Deck (performance governor,
  2 repetitions x 2 rounds), pre-packed (4a22164) against merged main
  (bda7afa) — library code between those commits differs only by the
  packed node. Same five cells as the M3 run plus the watch cells.
- Evidence: FLAT on every cell — mixed 1025.3 to 1029.8 ms, shared
  472.9 to 474.0 ms, multigoal 901.0 to 899.3 ms, flood 263.2 to
  262.9 ms, watch cells unmoved. All deltas within round-to-round
  noise. No regression; the M3 result simply does not transfer, the
  mirror image of the seam-scan hoist (Deck-only, M3-flat). The
  recorded non-transfer caveat (libc++ versus libstdc++ heap
  baselines) is now measured fact in both directions.
- Attribution: a same-session srcline profile of the mixed batch cell
  on the packed binary shows where the Deck's cost actually sits:
  libstdc++ sift machinery (stl_heap.h lines) ~20% of the cell, the
  packed key compare (path.h:675) ~8%, accessors/comparator wrappers
  ~3% — about 30% of the cell still in open-list work after packing.
  The packed key cheapened the compare, which is what M3 was paying;
  the Deck pays for sift depth and its memory traffic, which the key
  does not change.
- Decision: the d-ary sift follow-up is now JUSTIFIED by direct
  evidence on the platform that did not benefit — halving sift levels
  attacks the ~20% the Deck still spends there. It remains a separate
  experiment with its own A/B on both platforms; the pop-sequence
  invariance argument and the differential/golden test harness from
  the packed-node change carry over unchanged.

## 2026-08-10 - Baseline collection split out of the gates job

- Area: CI benchmark pipeline, not library code. Recorded here because
  it creates a data gap trend readers must know about: no baseline
  artifacts reached the history/change-point pipeline between
  2026-08-07 (the last completed collection, #107's push run) and this
  change — three days of merged perf work, including the seam-scan
  hoist (#140) and the packed open node (#150), have no CI baseline
  points. Treat change-point verdicts spanning that window
  accordingly.
- Hypothesis: Benchmark Gates' main-push variant was cancelled at the
  45-minute ceiling #109 added — measured step timings put its real
  runtime at ~51 minutes (build 13-21, test ~1, threshold gates 9-10,
  baseline collection ~27), and the long steps are push-only, so
  pull-request CI validated only the ~25-minute short variant. The
  kill always landed in the final, non-gating baseline step with the
  threshold gates green.
- Method: a dedicated main-push-only `bench-baselines` job (own ccache
  key family, gates family as warm fallback) collects and uploads the
  baselines; it stays outside `ci-gate` by design and reports through
  the ci-failure issue via step-level timeouts (a job-ceiling kill
  concludes `cancelled`, which the reporter ignores because benign
  concurrency supersedes conclude the same way). Both artifact
  consumers re-point at the new job. Policy tests pin the job guard,
  the overrun-reporting contract, and the artifact wiring.
- Evidence: accepted. Three timeout-cancelled runs with the identical
  signature (#148, #149, #150 pushes) against the ~51-minute measured
  total; the gates job's worst path is now ~32 minutes under its
  45-minute ceiling, and the baselines job's ~49-minute worst sits
  under step budgets of 30 + 35 with a 75-minute backstop.
- Follow-ups: the post-merge main-push run is the real verification —
  first expected green full CI since 2026-08-07 — and the first
  baseline artifacts close the data gap from that run onward.
