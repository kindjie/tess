# Optimization Log

This document records performance experiments that should remain separate from
architecture docs. Architecture docs describe current behavior; this log
captures hypotheses, benchmark evidence, accepted changes, rejected changes,
and deferred ideas.

Use this log when an optimization is benchmarked, profiled, rejected, or
deferred for scope reasons. Keep entries short and concrete:

- area and date
- hypothesis
- benchmark or profile evidence
- decision
- follow-up conditions, if any

Entries from 2026-07-12 and earlier are in
[`optimization-log-archive-2026-06-07.md`](optimization-log-archive-2026-06-07.md).

## 2026-08-06 - Steam Deck controlled baseline

- Area: complete on-device timing, thread scaling, and fields PMU attribution.
- Method: commit `4a919fbd99a2` was built with Clang 19.1.7 in steamrt4,
  then run on external power with the `performance` governor. The unrestricted
  main and diagnostics suites used 10 repetitions and a 0.2 s minimum. Seven
  scaling workloads used 20 repetitions at widths 1, 2, 4, and 8; widths up
  to four were pinned to distinct physical cores and width eight used all
  logical CPUs. Counter runs used a 1 s minimum and were separate from timing.
- Timing evidence: all 198 main and 192 diagnostics registrations completed
  without benchmark errors. Main real-time CV was 0.20% at the median and
  1.58% at p95; diagnostics was 0.17% at the median and 1.27% at p95. The
  two largest outliers were manual-time cache-maintenance cases at 11.7% and
  20.3% CV, so they should be repeated before using small differences as
  evidence. External power remained present and sampled APU temperature stayed
  at or below 66 C.
- Scaling evidence: the compute-heavy chunk workload reached 1.96x, 3.42x,
  and 5.97x at widths 2, 4, and 8. Chunk fill peaked at 1.48x at four physical
  cores and fell to 1.43x with SMT. Tile touch lost at every width, confirming
  that dispatch overhead dominates extremely small work. Partial-fill results
  varied with granularity; the 192-unit serial control reached about 7% CV,
  so its near-break-even width-two result is not a stable crossover claim.
- Counter evidence: all eight fields runs produced numeric cycles,
  instructions, cache misses, branch misses, and task-clock values plus their
  matching iteration counts. IPC ranged from 3.06 to 4.34. `perf` emitted
  user-space-qualified event names such as `cycles:u`; a one-off validator
  that required literal `cycles` falsely marked the otherwise complete PMU
  artifacts as failed. Raw process totals are retained and must be normalized
  by each run's own iteration count before comparing benchmarks.
- Decision: accept this campaign as the first controlled handheld baseline,
  not as a new cross-machine threshold calibration. For game-like parallel
  work, retain physical-core-first scheduling and let SMT participate only
  when tasks are compute-heavy enough; use four workers as the conservative
  default for mixed work. Repeat the two noisy cache-maintenance cases and any
  near-crossover partial-fill point before drawing optimization conclusions.

## 2026-08-04 - Exception-free execution paths

- Area: compiler exception mode, phase dispatch, and schedule type erasure.
- Hypothesis: removing exception-only pool state and catch-based paths should
  primarily reduce generated code and compilation work; runtime improvement
  should be expected only where the removed coordination is material.
- Method: one AppleClang 21 C++20 `-O3 -DNDEBUG` consumer instantiated a
  four-worker pool and one every-tick schedule task in three variants: normal
  callback, explicitly `noexcept` callback with exceptions enabled, and an
  ordinary callback compiled with `-fno-exceptions`. Hyperfine ran 8 pool and
  6 schedule repetitions after warmup. Six clean object compilations measured
  compile time; macOS `time -l` measured peak RSS; Mach-O section inspection
  measured executable code and exception metadata.
- Evidence: compile means were 632 ms enabled, 621 ms explicitly no-throw,
  and 565 ms exception-free. Peak compiler RSS was 158.0 MB, 157.1 MB, and
  151.7 MB respectively. Executable `__text` was 8,308 bytes in both enabled
  variants and 5,376 bytes exception-free; the enabled executables carried
  540 bytes of `__gcc_except_tab` plus 328 bytes of `__unwind_info`, while the
  exception-free executable had neither section. The implementation does not
  add `-fno-unwind-tables`; this section difference is the compiler's result
  for `-fno-exceptions`, not a Tess policy to discard stack metadata.
- Runtime evidence: the pool harness means were 173.9 ms enabled, 180.7 ms
  explicitly no-throw, and 183.0 ms exception-free, with overlapping noise;
  no pool speedup is claimed. Schedule means were 49.8 ms, 49.3 ms, and
  34.9 ms respectively. Representative pool peak RSS was identical at
  1,605,632 bytes in all three variants.
- Regression control: the repository's 12-sentinel paired base/head run used
  10 interleaved repetitions and passed. Storage and field sentinels were
  within -0.1%; the four main path sentinels ranged from -6.6% to +0.3%; the
  scoped-thread parallel sentinel was +1.4% with a 95% interval of
  [-0.8%, +5.2%]. No sentinel crossed the 5% regression budget at material
  scale.
- Decision: accept the policy-specialized representation and no-throw adapter
  preservation for code-size and compile-cost value. Treat the schedule result
  as a promising local measurement, not a general runtime claim. Reject a
  claim that no-throw pool dispatch is faster; measured differences were noisy
  and slightly favored the ordinary enabled baseline.
- Follow-up: profile instruction-level scheduler differences only if schedule
  dispatch becomes material in a representative application trace. Preserve
  the existing sentinel names and thread-scaling baselines.

## 2026-07-31 - Direct Directory For Fully Covered Sparse Worlds

- Area: `SparseResidentWorld` directory lookup and sparse weighted batch path
  planning.
- Hypothesis: a fully resident sparse world still paid the open-addressed
  chunk-directory hash and probe for each residency, page, and cost access,
  accounting for most of its 2.07x time versus the dense equivalent.
- Evidence: equal-work 512x512 baselines were 88.5 ms sparse-resident versus
  42.8 ms dense (10 repetitions, 0.2 s minimum). A 2 kHz Samply profile
  collected 27,168 samples; the dominant leaf was the bounded weighted-field
  neighbor loop, whose disassembly showed repeated inlined hash/probe
  sequences. A post-change profile removed those executed hash sequences and
  shifted the hot samples to generation and distance-array reads. Formal
  alternating A/B confirmation against `ccb1c30` measured 90,677,330 ns base
  versus 57,568,792 ns head, -35.8% with a 99% confidence interval of
  [-37.8%, -34.8%]. The dense-equivalent gap fell to about 1.35x.
- Memory effect: at the profiled 256-chunk capacity, the directory changes from
  512 24-byte hash buckets (12 KiB) to 256 8-byte slot entries (2 KiB). Each
  successful lookup reads one slot entry instead of one or more buckets, and
  insert/erase writes one slot entry. The 10 KiB peak heap-payload reduction
  is below process-RSS measurement granularity but exact from the selected
  layouts.
- Decision: accepted. Use the direct array only when capacity covers the
  complete bounded key space; genuinely sparse worlds retain bounded hash
  storage. Tests cover lookup, erase, slot reuse, out-of-range keys, and the
  large-key hashed representation.
- Tradeoff and follow-up: the five-suspect paired control passed. Hash-mode
  eviction changed by +0.9% to +2.0%, ensure-hit by -0.2%, and the 1-2 ns raw
  lookup by +25.0%; all are below the configured absolute materiality floor.
  Profile slot-direct page/cost access only if a hash-mode end-to-end workload
  shows a material regression or the remaining 1.35x sparse/dense gap becomes
  a priority. Do not specialize that API from the raw nanosecond lookup alone.

## 2026-08-04 - Third Campaign (fixed mask): the published bracket was wrong

Re-ran the full seven-workload sweep at `813dc9d` with the N+1 mask. 46
minutes, ~$8, exit 0. Verified from `sweep-cpu-masks.tsv` that all 77
points ran with N+1 CPUs -- a plan is not evidence of what ran.

**The fix holds at scale.** `chunk_compute` against the pool's own
ceiling, exactly-N to N+1: width 2 64%->99%, width 4 77%->99%, width 8
82%->98%, width 16 79%->96%. Width 24 was already at 95% and is
unchanged, as the diagnostic predicted -- the dispatcher's penalty is
about 1/N of the mask.

**The published crossover was wrong, and wrong in the direction that
understates the library.** docs/performance.md said the pool loses below
about 45 ns of work per chunk at four workers. Under the fixed mask,
44.8 ns wins at 1.17x and 46.7 ns at 1.16x, both Holm-significant. The observed crossover is bracketed between 11.5 ns (loses, 0.34x) and
44.8 ns (wins); nothing between them, or below 11.5 ns, was measured.
The degraded mask had been costing the pool roughly a third of its
throughput at low widths, and that loss was published as a property of
the library.

Corrected on the page, and the chart regenerated from this campaign
alone. The earlier "both campaigns agree" support is withdrawn: both of
those campaigns were measured under the defect, so their agreement
reflected a shared artifact rather than independent confirmation.

**Beyond 24 workers nothing improved, and that appears to be real.**
`chunk_compute` plateaus near 34x from width 64 onward under either
mask; `chunk_fill` peaks around 7x at width 24 and then declines. Those
are saturation, not harness defects.

**The curve is still not publishable**: 31 points over the 5% CV limit,
against 24 before. High widths remain noisy (14-24% CV at 64 and above).

**And the fix made width 2 worse for light workloads**, which review
caught and my first explanation got wrong. I blamed run length -- the
diagnostic ran ten points in two minutes against the campaign's 77 over
35 -- but the diagnostic only ever measured `chunk_compute`, and
`chunk_compute` at width 2 is *cleaner* in the campaign than before.
Width-2 CV, exactly-N to N+1:

| workload | exactly-N | N+1 |
| --- | ---: | ---: |
| `chunk_compute` | 4.28% | 0.27% |
| `chunk_fill` | 1.21% | 0.45% |
| `partial_fill_1536` | 2.29% | 16.41% |
| `partial_fill_640` | 0.83% | 26.50% |
| `partial_fill_192` | 1.46% | 17.85% |
| `partial_fill_64` | 1.00% | 13.35% |
| `tile_touch` | 1.04% | 6.62% |

It splits by workload weight, not by run length. The same points were
low-CV under the old mask -- and pinned in the slow mode, which is what
the fix removed. The artifact-supported reading is that a slow mode
still exists inside the 3-CPU mask at width 2 and is entered per
repetition: `taskset` constrains the process, not thread placement
within it, so two workers can land on the `{0,96}` SMT pair instead of
`{0,1}`. Light workloads have short phases, so a placement flip costs
proportionally more.

The published bracket is unaffected: it rests on width 4, where the
bracketing points measure 2.33% and 2.17% CV.

The direct test and likely fix is per-thread affinity -- each worker
bound to its own CPU and the dispatcher to the extra one -- rather than
a process-wide mask. Not attempted here.

**An analysis error worth recording.** My first comparison took the
median efficiency across all seven workloads and produced nonsense --
1% at width 190 -- because `tile_touch` and the light fills legitimately
never scale. Their low efficiency is the crossover, not a defect.
Efficiency has to be read per workload.

## 2026-08-04 - The Width-2 Anomaly Was the Harness (resolved)

The open anomaly from the 2026-08-03 campaign is closed, and it was a
defect in the measurement setup rather than in the executor. Two
diagnostic runs on `c3-standard-192-metal` plus one on a
`c3-standard-4`, about $3.20 in total.

**Cause.** `sweep_cpu_plan.py` pinned each point to exactly N CPUs. The
pool runs N worker threads *and* the benchmark's dispatching thread, so
N+1 threads shared N CPUs and the measurement dropped into a distinct
slow mode. Varying only the mask, holding everything else fixed:

| mask at width 2 | CPUs | reps in fast mode | efficiency |
| --- | ---: | ---: | ---: |
| `{0,1}` two adjacent cores | 2 | 0/10 | 65% |
| `{0,24}` across NUMA nodes | 2 | 7/10 | 99% |
| `{0,48}` across sockets | 2 | 8/10 | 98% |
| `{0,96}` one core, both SMT threads | 2 | 10/10 | 93% |
| `{0,1,2}` | 3 | 10/10 | 100% |
| `{0,1,2,3}` | 4 | 10/10 | 98% |

It is a mode mixture, not a level shift: adjacent cores were slow on
every repetition, node- and socket-spanning masks only sometimes, and
any mask with a spare CPU never. A median alone hides that, which is why
the pass criterion below counts modes.

**Fix.** `mask_for_width()` allocates N+1 CPUs. The extra one is an SMT
sibling of a worker's core rather than the next physical core, so the
mask stays inside the same NUMA node and the widths keep their
topological meaning -- 24 is still exactly one node, 48 one socket.
Verified on hardware: the dispatcher lands on CPU 96, whose node and
socket sets match the workers' at every width.

**Validation** (paired arms, one run, masks from the production planner):

| width | before | after | ceiling |
| ---: | ---: | ---: | ---: |
| 2 | 65% | 99% | 2.0 |
| 4 | 69% | 99% | 4.0 |
| 8 | 80% | 98% | 8.0 |
| 16 | 79% | 96% | 16.0 |
| 24 | 97% | 96% | 19.5 |

The fixed arm reached the fast mode on 10 of 10 repetitions at every
width, with CV 0.40% against 5.12% at width 2. The degraded arm still
failed in the same run, which is the control that matters: had both arms
looked clean it would have meant the mask never reached `taskset`.

**This also closes the uniform 77-82% loss** recorded on 2026-08-03 as a
separate question. It was the same defect: the dispatcher's penalty is
roughly 1/N of the mask, so it is catastrophic at width 2 and fades by
width 24.

**A correction to that entry's arithmetic.** It reported width 24 at 77%
by dividing speedup by width. The pool's own quantization ceiling at 24
workers is 19.5, not 24, so the campaign's 18.52 was 95% of what is
achievable -- width 24 was never degraded. The report tool prints an "of
ceiling" column for exactly this reason; the ad-hoc analysis ignored it.

**Refuted along the way**, each against data rather than argument: that
the collapse was SMT co-location (the live topology shows sibling(0) =
96, so `{0,1}` is two distinct cores, and a real sibling pair was
*faster* than the campaign's mask); that it was dispatcher CPU cost
(22 us against a 20,557 us wall); that it was an Amdahl serial floor
(fitted at 114-164 us, ~1.3% of T(4)); and that it was a fixed extra
cost in the pool path (pool w1 equals serial to 0.015%).

**Consequence for the published crossover.** The bracket on
docs/performance.md was derived from sweeps run under the degraded mask.
Speedups move -- `chunk_compute` at width 4 goes 2.78x to 3.96x -- so
while that workload's verdict is unchanged, the bracket itself has to be
re-measured under the fixed mask before it can be relied on.

## 2026-08-03 - Second Bare-Metal Campaign (pinned, clock-controlled)

Re-ran the sweep with each point pinned via `taskset` to a planned CPU
set and the `performance` governor set and verified across all CPUs. 46
minutes, ~$7.70, exit 0, all 77 points measured.

**Pinning plus clock control cut variance sharply at low and mid
widths.** Median CV by width, first campaign -> second: w4 7.12% ->
0.65%, w8 6.12% -> 0.54%, w16 8.04% -> 0.55%, w24 9.16% -> 0.54%. Within
socket 0 the median CV is 0.69%. The two changes cannot be separated:
the first campaign had neither pinning nor clock control.

**It did not help across sockets.** w64 18.06% -> 14.70%, w96 16.88% ->
17.69%, w190 21.95% -> 21.48%; median 17.46% for widths >= 64. The curve
is still not publishable, and 24 points fail the gate.

**The crossover replicated, and that is the result worth having.** At
four workers the sign agrees for every workload across both campaigns
despite the different regimes: below ~47 ns per chunk the pool loses,
above ~94 ns it wins. The first campaign's four-worker bracket was
47.5-90.1 ns and the second's is 46.9-93.6 ns. Published on
docs/performance.md as ~45-95 ns with a recommendation to measure
locally.

### What review refuted

Three causal explanations I proposed do not survive:

- *"Residual noise above 64 workers is memory-path contention."*
  `tile_touch` touches one tile per chunk and has essentially no memory
  traffic, yet its CV goes 1.80% at w48 to 11.06% at w64 to 22.54% at
  w96. Interleaving was on in BOTH campaigns and at every width, so the
  memory configuration does not change at w64. Thread placement does.
  The noise also is not monotone in load: `chunk_fill` is 10.83% at w32
  and 3.19% at w48.
- *"Two workers are handicapped because the dispatcher does per-iteration
  work inside the timed loop."* It does not: it wakes workers, blocks in
  `done_cv_.wait`, then scans results. And at w1 -- where the dispatcher
  shares one CPU with the only worker -- the total overhead is ~4 us per
  iteration, 0.015%. A dispatcher stealing CPU would hurt w1 most; w1 is
  unaffected and only w2 is hurt.
- *"w32/w48 are slower pinned because of worse memory locality."* Under
  uniform interleave across four nodes the expected access mix is
  placement-invariant: 25% local / 25% same-socket / 50% cross-socket
  either way. Better candidates, unmeasured: per-socket turbo budget
  concentrating 32-48 active cores on one socket, and mesh/UPI
  concentration. No per-point frequency telemetry exists to decide it.

### The open anomaly

Pinned w2 is uniformly ~1.5x slower than unpinned across every
substantial workload, and its throughput matches two workers sharing one
physical core's SMT threads -- which the CPU plan should have made
impossible. Pinned w4 likewise matches the *slow* mode of the unpinned
campaign's bimodal w4 rather than its fast mode. This cannot be
adjudicated from the artifacts, because the masks that were actually
applied were recorded nowhere. Both are now captured
(`sweep-cpu-masks.tsv`, `lscpu-topology.csv`); a plan is not evidence of
what ran. Two-worker results are withheld from the adopter page until
this is resolved.

The cheap next step is a targeted diagnostic rather than another sweep:
A/B the masks {0,1} vs {0,2} vs {0,96} vs {0,1,2} at w2/w4 with
per-thread placement sampling and aperf/mperf capture.

### The persistence anomaly reverted

`persistence/save_dense_512x512_2_fields`: 6.833 ms -> 11.821 ms (+73%)
-> **6.839 ms**, within 0.09% of the original, while the median change
across all 184 main-pass benchmarks was +0.04%. It was not a library
regression and not the code-layout effect proposed for it. The governor
changed between the campaigns so attribution is not definitive, but the
ratio 11.82/6.83 = 1.73 matches the 3.79/2.2 GHz clock range the first
campaign recorded, which fits frequency better than layout.

### Publishable, and not

Published: the four-worker crossover bracket, as a range, with the
machine stated. Withheld: any two-worker bracket, the full scaling
curve, and every causal narrative above that review did not support.
A methodology note worth carrying forward -- single-socket pinned points
measure at ~0.7% median CV on this class of machine; cross-socket points
do not get below ~15% even pinned and clock-controlled, so cross-socket
speedup claims need interval reporting rather than point estimates.

## 2026-08-03 - Thread-Scaling Sweep (first attempt; curve not publishable)

A worker-count sweep from 1 to 190 workers over seven workloads on a
4096-chunk world, run on `c3-standard-192-metal` under
`numactl --interleave=all`, 20 repetitions, no thread pinning and no
governor control. 39 minutes, ~$6.57, exit 0.

**The curve could not be published, and the analysis gate said so.**
`tools/thread_scaling_report.py` flagged 57 points; CV reached 16-33%
above 32 workers.

**The noise was thread placement, not jitter.** Repetitions split into
discrete modes rather than scattering. `chunk_compute/4` sat at either
~6.73 ms or ~8.81 ms, a 31% gap with almost nothing between; the fast
mode is 3.94x against a 4.0 quantization ceiling, and the slow mode's
3.01x matches two of the four workers sharing one core's SMT threads at
sibling efficiency ~0.53. `chunk_compute/8` shows three levels in the
same ratios that 0, 1 and 2 colocated pairs predict, with the same
efficiency. Two alternatives were ruled out from the artifact: Google
Benchmark's `iterations` is constant across all 20 repetitions of all 84
points, and the pool's `job_stride` is deterministic per width.

Attribution is weaker at 96 and 190 workers, where 191 threads on 192
CPUs leaves almost no placement freedom and oversubscription and
all-core frequency licensing are co-suspects. "Unusable" holds either
way.

**The crossover did survive, and it was the point of the exercise.**
Corrected for multiplicity across all 77 pool comparisons, at two workers
the pool loses at 42.3 ns of work per chunk and wins at 90.1 ns. The
uncorrected reading was tighter and wrong: `partial_fill_64` at two
workers has a marginal interval of 0.91-0.99, which looks decisive, and
an adjusted p of 0.090, which is not.

The bracket is conditional on this machine, this width, and unpinned
placement. Under good placement it likely sits lower: the two fast-mode
repetitions of `partial_fill_64/2` beat serial outright.

**Frequency was uncontrolled and demonstrably wandered.** `machine.txt`
recorded `CPU(s) scaling MHz: 21%` against an 800-3800 MHz range, and the
counter pass measured single-thread effective clocks from 2.35 to 3.79
GHz across benchmarks minutes apart on an idle machine.

**An unrelated anomaly in the main pass.** All seven `fields/*` held
within 0.3% of the previous campaign -- the historical regression did not
recur -- and all 184 benchmarks stayed under their ceilings, the closest
at 65%. But `persistence/save_dense_512x512_2_fields` measured +73%
(6.83 -> 11.82 ms) with 0.1% CV in both campaigns, while `load_dense` in
the same binary was unchanged at 1.000x and the same benchmark in
`tess_bench_diagnostics` was unchanged at 1.003x. Frequency cannot
produce that pattern. No library code changed between the campaigns, and
an arm64 A/B of the two commits reproduces nothing (10.506 vs 10.508 ms),
so the leading explanation is x86 code layout shifted by the
`parallel_phase_support.h` extraction. Unresolved; it is a bench-binary
artifact at 19% of its ceiling, invisible to library consumers, and the
next campaign re-measures it.

**Changed as a result:** each sweep point now runs in its own process
pinned with `taskset` to a CPU set from `tools/cloud/sweep_cpu_plan.py`
(one thread per physical core, filling NUMA nodes in order, SMT siblings
last); the `performance` governor is set before measuring and the
achieved state recorded; and verdicts are Holm-corrected across the whole
artifact rather than read off marginal intervals.

**A caveat on the persistence re-measurement.** The governor change
applies to the main timing pass too, so the next campaign's
`persistence/save_dense` number will not be a clean A/B against either
earlier campaign: a difference could be the governor rather than layout.
Distinguishing them needs the two binaries run under the same governor,
not two campaigns run under different ones.

**Still uncontrolled:** benchmark order is not randomised against the
worker axis, so a smooth drift could still imitate a worker-count trend.
Registration is workload-major, so the axis is traversed seven times and
drift aliasing should show as knee positions disagreeing between
workloads -- a cross-check, not a fix.

## 2026-08-02 - First Bare-Metal Campaign (post-fix baseline)

- Area: section 8's cloud bare-metal tier, first execution.
- Machine: `c3-standard-192-metal`, Xeon Platinum 8481C, Ubuntu 24.04.4,
  clang 18.1.3, kernel 6.17.0-1021-gcp. Commit `3a7b12d`
  (`v0.4.0-86-g3a7b12d`), source archive verified by SHA-256. 28 minutes,
  about $4.70.
- Timing evidence: 184 and 177 benchmarks at 10 repetitions, zero errors.
  **Median CV 0.12% against 2.03% on the shared-VM validation run** -- a
  roughly seventeen-fold reduction. All eight fields benchmarks at
  CV <= 0.91%. Residual noise is concentrated in three intrinsically
  jittery groups (`queued/execute_resident_update`, the `parallel/*_pool`
  family, and the manual-time LRU eviction benchmark) and is workload
  behaviour rather than machine noise.
- Counter evidence: the PMU is exposed on metal and returned usable
  values for all eight fields benchmarks. Legitimate conclusions are
  RATES only -- IPC 3.3-5.1, branch mispredicts around 1.0-1.6 per
  thousand instructions on the build paths versus about 2 per million on
  the lookup paths, and LLC misses at 0.001-0.007 MPKI, meaning the
  fields working set is cache-resident and memory traffic is not the
  bottleneck.

### What this does NOT support

Recorded because the first analysis of this data got it wrong twice.

- **Cross-benchmark comparison of the raw counter columns.** `perf`
  wraps the whole process, so a cheaper benchmark runs more iterations
  in the fixed min-time and accumulates more of everything.
  `fields/cache_hit` shows the highest cycle count purely because it ran
  about 6.7M iterations; it is the cheapest operation measured.
- **The per-iteration normalisation attempted during analysis.** It
  divided counter-run totals by TIMING-run iteration counts, which have
  different min-times, and inverted the true ordering: it implied
  `goalset_build_1` costs twice `goalset_build_16` when the timings show
  it is 13% cheaper. Per-operation cycles should come from
  `median_ns x measured_frequency`, not from that division.
- **Production-binary microarchitectural claims.** The counter pass runs
  the diagnostics binary, whose fields kernels are 12-21% slower because
  of allocation hooks.
- **"The regression is fixed."** `90b61ef` is an ancestor of every
  commit measured here, so there is no pre-fix arm. The hosted
  alternating paired confirmation remains the evidence that closes it;
  this campaign corroborates without independently proving the delta.
- **Metal-versus-VM speedup.** The two runs differ in both machine and
  commit.
- **Threshold recalibration.** One snapshot on a different machine.

- Follow-up: publish the counter run's own iteration count and
  task-clock (done, this commit) so future rows can be normalised; a
  paired pre/post-`90b61ef` run on this recipe if the fix is to be
  quantified on metal; pinning plus a performance governor as the next
  controlled experiment, since the counter pass drifted 2.5-3.8 GHz;
  dedicated handling for the three noisy groups before any of them gate.

## 2026-08-01 - Hosted Confirmation Of The Field Product Fix

- Area: follow-up to the 2026-07-31 chunk-level capture restoration.
- Evidence: alternating paired confirmation on the hosted ubuntu-24.04
  runner, `c300560` base against `7f25018` head, five suspects in
  `tess_bench_diagnostics` (run 30732908152). All five pass:

| Sentinel | Base | Head | Delta | 99% CI |
| --- | ---: | ---: | ---: | ---: |
| `fields/cache_eviction` | 146,831 ns | 143,247 ns | -2.1% | [-3.6%, -0.6%] |
| `fields/cache_miss_store` | 140,100 ns | 139,427 ns | -0.6% | [-3.2%, +0.4%] |
| `fields/goalset_build_1` | 129,593 ns | 133,187 ns | +2.8% | [+1.3%, +3.7%] |
| `fields/goalset_build_16` | 124,269 ns | 128,306 ns | +3.0% | [+2.6%, +4.3%] |
| `fields/goalset_build_256` | 151,161 ns | 144,416 ns | -4.6% | [-5.5%, -4.0%] |

  Every interval sits inside the 8% effect floor, and the hosted base
  medians (124-151 us) match the pre-regression range. The 2.3x-2.8x
  hosted amplification of the original slowdown is gone.
- Decision: the 2026-07-31 remediation is confirmed on the runner family
  the gates are calibrated against. The regression is closed as a defect
  with a root cause, not absorbed by recalibration.
- Ceilings: **not recalibrated, deliberately.** The fields family still
  carries bootstrap ceilings (850 us - 1.1 ms against ~124-151 us
  observed, roughly 7x headroom), which is why a 2.3x-2.8x regression
  passed the gate. Recalibrating needs the documented 10-artifact rule
  at 2x maximum observed, and only **2** unexpired baseline artifacts
  post-date the fix. A window spanning the regression would bake the
  inflated numbers in, which is precisely the section 2.3 loophole.
- Follow-up conditions: recalibrate the five fields ceilings once ten
  post-fix main-run baselines exist. The data branch landing alongside
  this entry makes that window assemblable without racing the 30-day
  artifact expiry that would otherwise keep resetting the count.

## 2026-07-31 - Restore Chunk-Level Capture For Default Field Products

- Area: unit and weighted distance-field product dependency capture.
- Hypothesis: the v0.12 regression came from replacing the default orthogonal
  builder's reached-chunk frontier expansion with exact transition enumeration
  for every reached tile, even though axis steps cross at most one chunk face.
- Evidence: the exact `c300560`/current local diagnostic-binary A/B reproduced
  a 42-51% regression on Apple Silicon while expanded and reached counts stayed
  fixed at 4,096. A 2 kHz Samply profile attributed 8,799 of 33,043 leaf
  samples (26.6%) directly to `for_each_dependency_chunk`. Restoring the
  chunk-level path changed the five build/store medians as follows (10
  repetitions, 0.2 s minimum, CPU time):

| Benchmark | Base | Regressed | Fixed |
| --- | ---: | ---: | ---: |
| `fields/goalset_build_1` | 80,356 ns | 121,461 ns | 82,198 ns |
| `fields/goalset_build_16` | 94,896 ns | 136,305 ns | 96,805 ns |
| `fields/goalset_build_256` | 94,745 ns | 134,643 ns | 94,406 ns |
| `fields/cache_miss_store` | 95,509 ns | 136,633 ns | 95,056 ns |
| `fields/cache_eviction` | 94,140 ns | 135,961 ns | 94,324 ns |

The formal alternating paired confirmation against `c300560` then passed all
five suspects: fixed deltas ranged from +0.2% to +2.6%, with every 99%
confidence interval below the 8% effect floor.

- Decision: accepted locally. Default orthogonal adjacency now expands the
  reached chunk set by one face, reducing dependency capture from
  O(reached tiles x transitions) to O(reached chunks x faces). Diagonal, hex,
  and custom-provider models retain exact per-transition capture. The same
  helper covers unit and weighted products, and a weighted blocked-frontier
  test pins the exact reached-plus-face-neighbor invalidation contract.
- Follow-up conditions: confirm on the hosted Clang 18 runner, where the
  original slowdown amplified to 2.3x-2.8x. Do not recalibrate the elevated
  v0.12 ceilings before that confirmation.

## The v0.12 Fields Regression Is Confirmed: 2.3x To 2.8x, Not Noise

- Date: 2026-07-31
- Outcome: **confirmed regression; root cause and local remediation above**
- Evidence: hosted paired confirmation, CI run 30599407227, base
  `c300560` versus head `61f8044` (the v0.12 merge), diagnostics
  binary, Bonferroni-adjusted to the seven-name suspect list.

The change-point detector's backtest flagged a sustained fields-family
step at the v0.12 merge, which recalibration had silently absorbed.
That suspicion is now confirmed against the exact commit pair, on
hosted runners, at 99.29% per-comparison confidence:

| Benchmark | Base | Head | Delta | Verdict |
| --- | ---: | ---: | ---: | --- |
| `fields/goalset_build_1` | 124,444 ns | 352,296 ns | +180.5% | regression |
| `fields/goalset_build_256` | 156,725 ns | 364,645 ns | +134.5% | regression |
| `fields/goalset_build_16` | 145,878 ns | 365,615 ns | +151.0% | regression |
| `fields/cache_miss_store` | 145,832 ns | 363,127 ns | +150.9% | regression |
| `fields/cache_eviction` | 147,513 ns | 357,825 ns | +142.6% | regression |
| `fields/cache_hit` | 35 ns | 47 ns | +36.3% | immaterial-scale |
| `fields/nearest_target` | 140 ns | 143 ns | +2.2% | immaterial-scale |

Five of seven confirm as regressions; the two that do not are
nanosecond-scale and fall below the materiality floor, which is the
verdict working as designed rather than a partial result.

The shape is informative. Every regressed benchmark lands at roughly
355,000-365,000 ns regardless of its base cost, which spanned
124,000-157,000 ns. A uniform ceiling rather than a proportional
slowdown points at a fixed cost added to every field-product build —
the `field_product_cache.h` rewrite in that merge is the obvious
suspect — rather than an algorithmic change that would scale with goal
count. Note that `goalset_build_1` and `goalset_build_256` now cost
almost the same, which is what a per-build constant looks like.

The follow-up profile established that this was not an unavoidable cost of the
new transition capability. Exact per-tile dependency enumeration had replaced
the sufficient chunk-level frontier expansion even for default axis steps. The
gate still did not catch the original step: recalibration absorbed it, which is
the section 2.3 loophole the redesign exists to close.

## Streaming To Certification Costs 3.5x The Rounds And Buys The Optimum

- Date: 2026-07-30
- Outcome: accepted (recorded as a scenario contract, not a code change)
- Evidence: `tests/tess_sparse_stream_test.cc` (S3 scenario), 256x256
  world in 32x32 chunks, 12 deterministic requests.

A sparse search returns `Found` as soon as it pops the goal, even when
it skipped non-resident chunks along the way. A stream-and-retry loop
that stops at the first definitive answer therefore reports an **upper
bound** on the cost, not the optimum — measured at 2 of 12 requests
even with a budget large enough to hold the entire world.

Streaming past the first answer until full residency can be certified
closes the gap: all 12 requests then match the dense reference exactly.
The price is streaming rounds — 28 versus 8 for the same twelve
requests, about 3.5x.

Consequences recorded in the scenario harness rather than the library:

- A latency-sensitive consumer that stops early must treat the cost as
  an upper bound; it is never below the true optimum, so it is safe for
  admission decisions but not for cost comparisons between routes.
- Certification requires the budget to hold what the search needs. At a
  quarter budget nothing certifies, and at 5% (three chunks) only one
  of twelve requests reaches any definitive answer at all — the loop
  stops cleanly rather than thrashing.
- Keeping the best answer seen across the sweep matters: a later
  resident set can support only a worse route, so a loop that reports
  the latest rather than the best would regress its own bound.

## 2026-07-30 - Full-Suite Paired Confirmation Rejected On Tail Validity

- Area: the design of section 4.2's on-demand confirmation
  (`tools/paired_bench.py` confirm mode, the paired-bench dispatch
  workflow).
- Hypothesis: a literal full-suite paired A/B mode (all ~193 gated
  benchmarks, five rounds for wall-clock reasons) could confirm
  change-point suspicions under the sentinel run's statistics.
- Evidence: at 193-way Bonferroni confidence (99.974%, tail 1.3e-4)
  a five-observation bootstrap percentile collapses to the sample
  minimum, so a benchmark at the 8% null boundary flags with
  probability 1/32 per pass; with re-run-once the illustrative
  suite-level false-confirm rate is ~17%, before shared-runner
  correlation makes it worse. Resample cost is also quadratic (~149M
  bootstrap medians per evaluation). Measured single full-suite
  invocation: ~208 s, so a five-round A/B pass is ~35 minutes and a
  flagged run ~70, per side of the statistics' validity problem.
- Decision: rejected. Formal confirmation is suspect-scoped (up to 64
  predeclared names; Bonferroni sizes to the list — at n=64 the tail
  is 3.9e-4 with 256k resamples, resolvable and ~0.6 s per suspect),
  metrics come from the threshold manifests, benchmarks whose scale
  cannot clear the materiality floor report "immaterial-scale", and a
  requested suspect unavailable in either revision fails the run. The
  broad sweep remains the change-point detector's job.
- Follow-up conditions: revisit only with a method calibrated for
  small-sample extreme tails (for example exact permutation bounds) or
  with enough rounds to resolve suite-wide tails, and re-measure the
  wall-clock budget then.

## 2026-07-29 - Change-Point Backtest Flags A 3-4x Fields Family Step At v0.12

- Area: first backtest of the new change-point detector
  (`tools/benchmark_changepoint.py`) against the repository's real
  trailing main artifacts (legacy single-stratum mode; 19 usable
  artifacts in the recent window).
- Evidence: 29 benchmarks — the fields family plus adjacent
  product/cache workloads — show a clean sustained step, for example
  `fields/goalset_build_16` from a 117-135 us band (11 consecutive
  artifacts) to a 364-475 us band (8 consecutive artifacts, still
  current). The step lands exactly at 61f8044 (the roadmap-completion
  merge, #59), whose diff rewrites `field_product_cache.h` (+922
  lines) and `distance_field_box.h` while `tess_fields_bench.cc` is
  unchanged — same workload, 3-4x slower. The post-merge threshold
  recalibration (2026-07-24, "Gate Previously Smoke-Only Benchmark
  Families") accepted the elevated levels as the new baseline, which
  is the section 2.3 self-referential-calibration loophole in action.
- Decision: recorded as a confirmed-sustained, unconfirmed-cause
  suspect. Not remediated in the change-point slice; the follow-up is
  a paired base-vs-head comparison of c300560 versus 61f8044 over the
  fields family and, if the regression is unintended, a targeted fix
  with the detector's commit range as the starting bisection window.
- Follow-up conditions: when the fields cost is intended (weighted
  product capability), the intent must be written down here and the
  ceilings' provenance annotated; silence is what let it pass.

## 2026-07-29 - Paired Sentinel Replay Reproduces The 2026-07-23 Catch

- Area: validation of the new paired base-vs-head sentinel runner
  (`tools/paired_bench.py`, redesign phase 2 slice 1) against the one
  confirmed genuine catch in the threshold gate's history.
- Method: local arm64 Release builds of the exact historical pair — base
  c300560 (main before the roadmap-completion regressions), head f92b5f5
  (the commit CI run 30041607406 measured) — compared over the four
  confirmed-catch sentinels with the shipped parameters (10 alternating
  rounds, paired per-round-ratio bootstrap, 8% effect floor, 2 us
  materiality floor, re-run-once confirmation).
- Evidence: verdict `regression` on all four. cached_astar_batch +395.2%
  [+390.6%, +397.1%], field_product_cache_hit_replay +156.0%, nearest_
  target_product +158.5%, weighted_batch_planner +9.3% [+8.6%, +10.1%] —
  the last clearing the 8% floor with a CI excluding it. Local deltas
  exceed the hosted +19-28% ceiling overshoots because ceilings measure
  excess over calibrated maxima, not over the pre-regression base, and
  the arm64 build amplifies the per-edge abstraction overhead.
- Decision: the detector demonstrably reproduces the historical catch
  end to end on the exact commit pair. This is local evidence only; the
  section 4.3 exit criterion still requires the hosted-runner replay
  during the phase 4 write-up, and the ceilings remain authoritative
  until every criterion is met.
- Follow-up conditions: if the shadow period shows a divergence in which
  the ceilings fire truthfully and the paired run stays silent, classify
  it against this replay's sensitivity before tuning any floor.

## 2026-07-28 - Phase 3 Gate Re-Evaluation: Sealing, Not Patience, Dominates

- Area: the PIBT-tier go/no-go evidence — width-2/3 ring and cross gate
  cells (64x64, n=48, 20 seeded instances, real weighted tick driver, full
  settled consumer recipe, retry patience effectively infinite).
- Hypothesis (from the original gate run): a width-2 ring is biconnected and
  therefore jointly solvable, so the joint commit's 1/20 solve rate under
  `Permit` is an algorithm gap the PIBT tier should close.
- Evidence: classifying every stranded agent's goal by a class-consistent
  BFS under the final settled set splits the residuals into
  stranded-but-reachable (an algorithm gap) versus sealed (settled arrivals
  cut the goal off — unsolvable for any movement tier; the pebble-motion
  solvability argument does not survive settle-on-arrival). Ring width 2:
  joint 1/20 solved with 30 reachable + 194 sealed residuals; PIBT 1/20 with
  5 reachable + 191 sealed. Ring width 3: joint 17/20 (2 reachable + 6
  sealed), PIBT 18/20 (0 reachable + 3 sealed), including one seed where the
  joint run seals and the PIBT run fully solves in 120 ticks. Control: with
  goals restricted to the outer lane (seal-proof by construction — the inner
  lane stays a free circuit), the joint commit solves 20/20 even at n=96,
  and faster than PIBT (125 versus 243 average ticks at n=96).
- Decision: ship the PIBT tier with an honest, narrower justification —
  live-congestion resolution (reachable residuals 30->5 and 2->0), dead-end
  yields under `Forbid` that the route-bound joint commit categorically
  cannot make (deterministic test), and secondarily fewer seals formed
  because populations keep moving. Sealing itself is re-scoped as a goal
  placement/lifecycle hazard documented in the settled-recipe consumer
  contract, not a movement-tier problem. The decisive regression is the
  width-3 seed the two tiers split on plus the deterministic pocket-yield
  scenario, not the width-2 cell (both tiers fail it identically for
  non-algorithmic reasons).
- Follow-up conditions: if a consumer needs thin-ring random-goal workloads
  to complete, the lever is goal placement (or unsettling on demand), not a
  stronger mover; revisit only with LaCAM-class search plus that evidence.

## 2026-07-28 - PIBT Decision Cost Against The Joint Baseline

- Area: `advance_path_agents_with_pibt` on the joint lab benches' 128x128
  world and layouts (`lab/pibt_*` mirrors `lab/joint_*` one for one).
- Hypothesis: PIBT decisions (priority order, candidate ranking,
  inheritance, backtracking) cost the screening study's expected 4-6x over
  joint admission.
- Evidence: three-run local arm64 medians, trivial Manhattan oracle (oracle
  maintenance is caller-side and excluded by design). Steady-state denial
  (`lab/pibt_headon_denied_128x128`, boxed pairs): 7.9 us at 128 agents,
  66 us at 512, 141 us at 1,024 versus joint 4.1/42/154 — about 2x at small
  counts, converging then crossing below joint at 1,024 (denial work per
  pair is constant; joint re-runs its whole pipeline). Chain drain including
  identical reset accounting (`lab/pibt_chain_reset_128x128`): 8.2 us at
  128, 200 us at 1,024 versus joint 59/903 — 4.5x FASTER, because one
  inheritance recursion admits a chain in linear time while the joint
  vacated-chain fixpoint re-scans the span per round (quadratic in chain
  length). The screening 4-6x expectation measured oracle upkeep, which
  lives with the caller here.
- Decision: no micro-optimisation; the tier's real recurring cost is the
  caller's ranking-oracle maintenance (`distance_at` product rebuilds on
  settled-set change), which the architecture docs call out.
- Retry conditions: profile if a consumer drives PIBT above roughly 4,096
  agents per tick; the joint chain fixpoint's quadratic sweep is now also a
  known candidate if joint-side chain workloads ever dominate a profile.

## 2026-07-28 - Joint Movement Admission Cost At Colony Scale

- Area: `advance_path_agents_with_joint_movement` on a 128x128 world (the
  colony's shape), `lab/` benchmark family (no threshold targets).
- Hypothesis: joint admission -- validation, occupant index, chain fixpoint,
  cycle walk, batch apply -- fits comfortably inside the colony's 50 ms fixed
  tick at populations up to 1,024 agents.
- Evidence: three-run local arm64 medians. The all-cycles worst case
  (`lab/joint_headon_denied_128x128`, every agent in a denied 2-cycle) costs
  4.3 us at 128 agents, 42 us at 512, and 154 us at 1,024 -- 0.3% of the tick
  budget at the demo's maximum population. Full chain drain including
  per-iteration state reset (`lab/joint_chain_reset_128x128`) costs 59 us at
  128 agents and 901 us at 1,024 (~1.8% of budget, reset included). Growth
  from 512 to 1,024 denied pairs is superlinear (3.6x), consistent with the
  per-cycle-walk `on_walk` reset and sorted-vector claims; irrelevant at
  these budgets.
- Decision: ship the joint advance with caller-owned scratch and no
  micro-optimisation; the colony adopts it with `SwapPolicy::Permit`, and its
  three recorded livelock seeds resolve (four post-wall trips each, zero
  terminal, stall counter quiet) where the per-agent driver reproduces the
  historic 890-motionless-tick wedge.
- Retry conditions: profile the admission pass if a consumer runs it above
  roughly 4,096 agents per tick, or if the denied-cycle path ever shows up in
  a real workload's profile; the `on_walk` reset is the first candidate.

## 2026-07-28 - Multi-Agent Deadlock Resolution Screening

- Area: multi-agent local movement — resolving the deadlock class that
  remains after the settled-agent fix (two travelling agents each on the tile
  the other needs; ~2% of colony wall layouts).
- Hypothesis: a rapid disposable-harness elimination pass over ~17 candidate
  mechanisms can narrow the field before any library work, more cheaply than
  investigating candidates in-library one at a time.
- Evidence: full tables, method notes, and caveats in
  [local-movement-resolution.md](local-movement-resolution.md) (screening
  study; numbers directional, harness not preserved). Headlines: a joint
  batch commit admitting moves into same-tick-vacated tiles, with 2-cycle
  swap permitted, resolved 89-100% of solvable instances at 2-11 us/tick;
  chains/rotations without swap resolved ~0%; priority inheritance (not
  ranking) is PIBT's active ingredient and the only mechanism that survives
  multi-tile footprints; a ranking oracle must share the agent's
  movement-class passability or agents park beside obstructions.
- Accepted (to build in-library, tests-first): a joint movement commit with
  an explicit swap policy (`Forbid` default, `Permit`, `PermitOnDeadlock`),
  since `commit_movement_intent` validates one destination against current
  state and cannot express vacated-this-tick admission by construction.
- Rejected: chain/rotation-only resolution, greedy claim without inheritance,
  per-agent PIBT escalation (verified null), per-tile congestion pricing
  (spreads routes but carries no direction; both streams avoid the same
  tiles), route-derived cheap ranking, directional bias as a ranking-tier
  feature (needs per-(tile, direction) cost the model cannot express), WHCA*
  as a default tier (30-90x cost, horizon fails at dense bottlenecks), and
  goal swapping as a movement-layer feature (assignment encodes information
  the movement layer cannot see; the evidence belongs to the caller's
  assignment layer).
- Deferred: LaCAM (prototype never found a plan; retry from the MIT reference
  with a persistent search tree), conflict-cluster escalation, adaptive
  space-time horizons, priority-consistent resource ordering, an explicit
  anonymous goal-set API, per-(tile, direction) cost.
- Retry conditions: re-open the ranking tier (PIBT) only if library-scale
  tests show the cheap resolver leaving real gaps on cycle-rich maps or for
  agents with extent; re-measure all costs at 128x128 with up to 1,024 agents
  in the library benchmarks before setting any tier default.

## 2026-07-26 - Keep A Sealed Colony Cheap While Planning Around Settled Agents

- Area: web colony demo planning, blocked-agent recovery, and the terminal
  verdict.
- Hypothesis: agents can be routed around teammates who have arrived and will
  never move again without giving up the region graph's cheap rejection of
  goals that terrain has sealed off.
- Evidence: measured on the 1,024-agent setting with a wall spanning every
  row, 60 ticks against a 50 ms fixed-step budget. Baseline (planning on
  terrain alone) worst 311.5 ms, mean 6.2 ms, but zero agents reported
  terminal within the window because the retry allowance was 2N+8. Planning on
  the settled-aware class with the graph still passed to the tick driver:
  worst 612.7 ms, mean 254.1 ms — a multi-second page freeze, because
  `precheck_path` returns `GraphStale` on a movement-class stamp mismatch, so
  the graph pruned nothing and every blocked agent re-searched the whole
  region every tick. Adding an explicit `precheck_path<Walker>` against the
  terrain graph on the first blocked tick: worst 312.5 ms, mean 5.2 ms, all
  1,024 correctly terminal. The remaining worst tick is the initial plan for
  1,024 agents and is present in the baseline too.
- Accepted: plan and move with a settled-aware class; keep the region graph on
  terrain; ask the terrain precheck first and the settled-aware search only
  when terrain says a route exists. Ordinary and bottleneck ticks stay in the
  1-3 microsecond range with a ~60 microsecond p95 while a jam clears.
- Rejected: installing detour routes into the retained-route store when an
  agent stalls. The demo's replan-every-tick strategy marks pathing dirty,
  which resubmits every agent and overwrites retained routes, so the fix would
  have silently done nothing whenever that toggle was on.
- Rejected: rebuilding the region graph for the settled-aware class. It churns
  topology over something that is not terrain, and is unsound in the
  un-settling direction — a graph built while a tile was settled would prune
  routes that reopen the moment its owner relaunches.
- Deferred: a flow-field formulation with a shared goal set and free-slot
  assignment. It removes this deadlock class outright rather than routing
  around it, but 128 distinct goal tiles means 128 fields, so it only pays off
  together with a goal-model change. Recorded as a candidate for the colony
  macro-harness's strategy axis.
- Retry conditions: revisit if the demo gains bidirectional traffic, which
  neither this fix nor a flow field resolves without a yield or swap protocol;
  and re-measure the sealed case if the tick driver ever gains a precheck that
  tolerates a more permissive graph stamp.
- Measurement caveat: per-tick medians on an unobstructed map vary 1.1-2.9
  microseconds run to run for an identical binary, so no conclusion here rests
  on a single sample pair; the sealed-colony numbers above are the ones with a
  signal larger than that noise.

## 2026-07-24 - Avoid Known-Unusable Weighted Field Work

- Area: repeated-goal weighted batch and product-cache preprocessing.
- Evidence: exact-SHA audit identified two compositional cliffs. A realized
  `uint32_t` overflow rebuilt the reverse field with the heap before every
  member retried exact A*, and a cache budget smaller than the product's
  mandatory distance labels built and discarded a full product before the
  ordinary batch built its own field.
- Accepted: return `CostOverflow` immediately once the bounded builder proves
  a saturated distance; exact per-member A* remains the correctness fallback.
  Preflight the product's minimum distance-label bytes against the cache budget
  before lookup/build. Dedicated tests prove the overflow avoids the second
  full flood and the oversize product records no cache miss before the normal
  one-field batch.
- Deferred: cross-call memoization of overflow verdicts. World/provider
  mutation identity is required to avoid stale negative reuse, while the
  accepted changes remove the redundant full floods without introducing a new
  cache contract.
- Retry conditions: profile repeated near-`uint32_t` cost worlds if they are a
  real workload; add memoization only with the same content/revision identity
  guarantees as other path products.

## 2026-07-24 - Preserve Incremental Region-Graph Locality on Failure

- Area: dense and sparse `update_region_graph` exception safety.
- Rejected: copying the complete graph before every non-empty incremental
  patch. Although it provided rollback, it also copied every unchanged
  per-tile label and defeated the operation's dirty-chunk scaling.
- Accepted: keep the existing local patch and global CSR rebuild, but catch
  failures after mutation begins, clear every derived structure, and advance
  revision so consumers must rebuild. Failures during dirty-mask preparation
  still leave the graph untouched. This adds no normal-path allocation or
  world-sized copy.
- Evidence: allocation-failure injection covers successive ordinals in dense
  and sparse updates and accepts only the prior complete graph or a cleared,
  stale graph. The retained 512x512 single-chunk benchmark measured a
  five-run local arm64 median of 713,980 ns against its 6,277,497 ns ceiling.
- Retry conditions: consider a strong guarantee only if affected local
  topology and derived CSR slices can be staged without copying unchanged
  tile labels or slowing the existing benchmark materially.

## 2026-07-23 - Preserve the Default Unit-Field Fast Path

- Area: Default orthogonal unit-cost distance fields, multi-goal products,
  nearest-target replay, and field-product cache replay.
- Observation: Routing every default axis neighbor through the resolved
  transition model regressed five existing hosted-runner path gates. The
  largest regressions were the eight-goal room field at about 139 ms against
  75 ms and the shared room field at about 18.5 ms against 10.7 ms.
- Hypothesis: Compile-time specialization can retain the pre-model direct
  axis-neighbor loop when the resolved model proves default orthogonal steps
  and the adjacent provider, without changing generalized model semantics.
- Evidence: After specialization, three-sample local medians were about
  2.77 ms for the shared room field, 3.50 ms for the shared sparse field,
  18.3 ms for the eight-goal room field, 0.83 ms for 100 nearest-target
  replays, and 9.8 us for cached field replay. The first hosted retry exposed
  four remaining generalized-path regressions: cached unit A* at 111 ms,
  nearest-target replay at 2.25 ms, cached field replay at 28.6 us, and the
  near-goal weighted batch at 120 us. Restoring direct default cache misses
  and reconstruction reduced three-sample local medians to 14.7 ms, 0.76 ms,
  10.4 us, and 50.6 us respectively. All are below their existing gates, and
  79 focused path tests pass under warnings-as-errors.
- Follow-up evidence: The second hosted retry passed cached unit A* but still
  measured nearest-target replay at 2.12 ms, cached field replay at 26.4 us,
  and the near-goal weighted batch at 101.25 us. A same-machine comparison
  isolated a real compiler regression: the pre-transition reader measured
  0.31 ms and 4.0 us for nearest-target and cached replay, while the
  generalized reader measured 0.76 ms and 9.7 us. Sampling placed the hot
  samples in an outlined `for_each_indexed_axis_neighbor`; forcing that small
  per-node helper inline restored 0.31 ms and 4.0 us medians. A second profile
  placed most near-goal time in the bounded field-builder neighbor loop.
  Hoisting its invariant saturated distance and bucket selection reduced the
  20-run median from about 50-52 us to 46.8 us.
- Accepted: Use direct indexed axis-neighbor iteration only when
  `ResolvedTransitionModel` proves default orthogonal connectivity. Continue
  using resolved forward/reverse enumeration for hex, diagonal, and
  provider-composed transitions. Default adjacent route-cache misses also use
  the unit A* core instead of the generalized weighted core. Keep the indexed
  axis-neighbor helper forced inline across supported compilers, with the
  reason documented at its definition, and compute bounded-flood
  per-node invariants once outside the neighbor loop.
- Rejected: Raising the five thresholds. The correlated 1.3x-1.9x regression
  was attributable to avoidable per-edge abstraction overhead rather than
  hosted-runner noise.
- Retry conditions: Re-profile if the default fast path and resolved model
  stop producing identical paths, costs, or dependency stamps, or if a future
  provider can prove equivalent default connectivity.

## 2026-07-24 - Gate Previously Smoke-Only Benchmark Families

- Area: block pipelines, maintenance strategies, persistence, exact spatial
  queries, and local coordination.
- Evidence: all five families were registered in the shared benchmark binary
  but absent from threshold targets and hosted baseline collection. Three-run
  local arm64 medians ranged from 422 ns for a fused pipeline to 10.2 ms for
  dense archive save. The largest maintenance result was 24.4 us for sparse
  coalescing, retaining the experiment's previously recorded disadvantage.
  The first hosted query gate then measured box traversal at 2.95 ms per tile
  and 6.66 us by spans, versus local medians of 218 us and 687 ns. Radius spans
  likewise measured 10.2 us hosted versus 1.81 us locally.
- Decision: add strict family manifests, threshold targets, hosted CI steps,
  and baseline artifacts. Bootstrap ceilings use the greater of six times the
  local median or twice the first hosted median, rounded upward; they are
  explicitly labeled bootstrap rather than calibrated.
- Retry conditions: replace the bootstrap ceilings with two times the maximum
  after ten same-runner hosted baseline artifacts.

## 2026-07-24 - Post-Green Audit Performance Triage

- Area: field-product cache admission, colony blocked retries, archive
  checksums, and newly gated query thresholds.
- Evidence: the exact-SHA hosted matrix passed every benchmark family. Query
  ceilings already follow the documented first-hosted-sample bootstrap rule,
  while the observed colony crawl was traced to repeated path-agent lifecycle
  work at a bottleneck rather than an unresolved search-kernel regression.
- Accepted: reject a single oversized field product without clearing useful
  entries, and scale the colony's terminal retry allowance to its active agent
  count. Keep the current query bootstrap ceilings until ten comparable hosted
  artifacts exist.
- Deferred: no CRC rewrite and no wider query ceilings. Neither has profiling
  evidence that justifies complexity or weaker gates. A profiler was not run
  because lifecycle counters and green threshold jobs resolved the performance
  uncertainty without it.
- Retry conditions: profile archive save/load if checksum work becomes a
  material share of a representative workload. Recalibrate query ceilings to
  twice the maximum after ten same-runner hosted artifacts, or profile first
  if a gate fails before then.

## 2026-07-24 - Preflight Unit Repeated-Goal Product Storage

- Area: unit repeated-goal selection in `PathRequestRuntime`.
- Evidence: a 1,024-byte cache budget cannot hold the 4,096-byte mandatory
  distance labels in the focused world. Building then rejecting that product
  produced two cache misses and cleared smaller useful products; the shared
  storage preflight skips the doomed product, preserves exact A* results, and
  records zero cache misses.
- Decision: mirror the weighted-product distance-storage preflight in the unit
  path. This removes repeated world-sized work without changing selection when
  a product can fit.
- Retry conditions: revisit only if product storage becomes compressed or can
  be admitted incrementally without clearing already-admitted entries.

## 2026-07-24 - Retain Flecs Callback Off-Board Filter

- Area: Flecs path-agent collection.
- Evidence: a structural `without<OffBoard>()` query term avoids one callback
  branch per parked entity, but Flecs 4.1.5's fluent builder reproducibly makes
  the required Clang analyzer report downstream `StackAddressEscape` findings.
  The callback filter is non-mutating and parked entities are not a measured
  dominant workload.
- Decision: reject the structural filter until the pinned upstream builder is
  analyzer-clean; keep the reason at the callback and in maintained ECS docs.
- Retry conditions: retest after a Flecs upgrade or if profiling shows parked
  entities materially affect collection time.

## 2026-07-23 - Constant-Time Area Index Validation

- Area: per-agent checked coordinate lookup through `AreaIndex`.
- Evidence: audit found that every checked lookup recomputed a fingerprint
  across all local topologies and portals, making A agent queries cost
  O(A * graph size). A dedicated 256-area, 512x512 lookup benchmark now
  isolates the query path; its five-run local arm64 median is 5.17 ns after
  the revision change.
- Decision: replace the fingerprint with a monotonic `RegionGraphT` revision
  updated by clear, rebuild, and non-empty incremental changes. Index validity
  is now O(1); coordinate lookup retains only region resolution and ordered
  area lookup.
- Retry conditions: consider a direct dense region-to-area table only if the
  new lookup benchmark shows the remaining ordered lookup is material.

## 2026-07-22 - v0.12 Benchmark Gate Closure

- Area: benchmark families added after the last threshold calibration.
- Evidence: the full Release gates found eleven literal benchmark names
  without threshold entries: five resolved-transition/weighted-product cases,
  two coarse-topology/area cases, and four Flecs collection cases. Three-run
  local arm64 medians were 11.6 us for diagonal search, 13.0 us for axial-hex
  search, 1.54 ms for the stair-provider search, 24.74 ms for an eight-goal
  512x512 weighted product build, 5.06 us for product replay, 21.35 us for a
  far coarse path, and 2.72 ms for a 256-area index build. Flecs medians were
  26 us, 0.35 ms, and 4.87 ms for collecting 1,000, 10,000, and 100,000
  agents, and 0.78 ms for collecting and applying 10,000 agents. The stair
  case deliberately expands 32,761 nodes because a single provider transition
  connects two 128x128 planes; the product and area cases are whole-world
  builds, not single point queries.
- Decision: add provisional six-times-median CPU ceilings and a source-level
  test requiring every literal benchmark in a threshold-gated family to have
  an entry. Structurally large cases remain above the 1 ms investigation line
  with work counters and rationale recorded instead of being misrepresented
  as microbenchmarks.
- Retry conditions: replace bootstrap ceilings with two-times hosted-runner
  maxima after ten same-runner baseline samples. Revisit the stair heuristic
  if provider-heavy searches become representative rather than synthetic.

## 2026-07-22 - Optional WebGPU Transport Baseline

- Area: stable-C-API WebGPU field upload, compute dispatch, and asynchronous
  summary readback.
- Evidence: the backend compiles against the exact Dawn C header shipped by
  Emdawnwebgpu `v20260423.175430`; its fake-device tests cover ownership,
  generation invalidation, loss, and asynchronous lifetime. Emscripten 6.0.3
  builds the browser example with the exact SHA-pinned port. Local headless
  Chrome exposed no adapter and therefore exercised the explicit unsupported
  result rather than a device execution path.
- Decision: accept the bounded transport as the v0.11 optional backend. Do not
  establish a timing threshold from an environment without a GPU adapter.
- Retry conditions: measure upload, dispatch, and readback independently on a
  representative browser/GPU matrix before adding performance gates or
  promoting tess-owned shader algorithms.

## 2026-07-22 - Flecs Adapter Baseline

- Area: deterministic Flecs path-agent collection and write-back.
- Evidence: a local Release build with deliberately shuffled `AgentId` values
  collected and sorted 1,000, 10,000, and 100,000 agents in three-run medians
  of 26 us, 0.35 ms, and 4.87 ms. Collecting and applying 10,000 agents took
  0.78 ms. The context owns one persistent query; correctness tests prove warm
  ticks allocate nothing and native table/entity churn does not change output.
- Decision: accept stable-ID sorting and component-notifying write-back as the
  v0.10 baseline. Sorting is required for deterministic output; Flecs query
  creation remains setup-only because upstream documents repeated creation as
  expensive.
- Retry conditions: calibrate cross-platform thresholds before gating these
  baselines. Profile radix or table-local merge alternatives only if adapter
  collection becomes material in a representative 100,000-agent frame.

## 2026-07-22 - Local Coordination Baseline

- Area: deterministic local destination reservations and congestion summaries.
- Evidence: a local Release build resolved 1,000 requests with four feasible
  options each, including contention on 64 first-choice coordinates, in a
  five-run median of about 0.36 ms. The measured coefficient of variation was
  3.14%. Correctness tests cover priority, stable IDs, alternatives, caller
  filtering, invalid ownership ranges, congestion, waits, and warm
  allocation-free reuse.
- Decision: accept the deterministic greedy resolver as the v0.9 local crowd
  substrate. It spreads contention without introducing continuous steering or
  a global matching claim, and the caller retains movement legality and
  commit-time validation.
- Retry conditions: profile and add a calibrated CI threshold if local
  coordination becomes a frame-time contributor in a representative consumer
  trace. Consider a different claimed-coordinate structure only if option
  counts grow enough for insertion costs to dominate.

## 2026-07-22 - Colony Bottleneck Replan Loop Observed

- Area: retained path-agent movement under dense dynamic occupancy.
- Evidence: the interactive colony demo was observed at roughly 900 agents
  slowing to 18-36 ms per simulation tick and then remaining at a stable
  partial-arrival count behind a painted bottleneck. Code inspection identifies
  a closed lifecycle: each occupied next step makes the agent `Blocked`; the
  next tick replans that agent; occupancy-blind A* returns `Found`; applying
  that result resets `blocked_retries`; and the same occupied step can fail
  again indefinitely. Arrived agents can make the obstruction permanent.
- Evidence after repair: the seeded 24-agent doorway regression previously
  submitted 8,600 searches across 503 planning ticks. Retrying retained steps
  reduced that to the 24 initial searches in one planning tick; within the
  bounded run every agent arrived or became explicitly `Unreachable`.
- Decision: accepted. Occupied/reserved destinations retry their retained
  step without path processing, while route-invalidating transient failures
  still re-path. All blocked modes consume one consecutive retry budget,
  successful movement resets it, and the web demo exposes terminal counts.
- Retry conditions: add richer local alternatives or occupancy-aware caller
  policies if a representative workload requires more arrivals through a
  merge; do not restore occupancy-blind per-tick re-planning.

## 2026-07-22 - Canonical Persistence Baseline

- Area: canonical authoritative-field world archives.
- Evidence: a local Release build saved a 512x512 dense world with one byte
  field and one 32-bit field (about 1.25 MiB) in a five-run median of 10.2 ms
  at 122.6 MiB/s, and preflighted plus loaded it in 9.7 ms at 128.7 MiB/s.
  Removing a redundant self-parse from the successful save path reduced its
  median from 19.0 ms while inspection remains separately testable.
- Decision: accept the scalar-at-a-time canonical codec as a cold-path
  baseline. It is endian-stable, checksummed, schema-versioned, and keeps file
  I/O outside the library. No CI timing gate is warranted until a consumer
  establishes save-size and latency requirements.
- Retry conditions: add contiguous bulk codecs for common scalar columns if
  persistence enters a latency-sensitive path or measured throughput becomes
  material for representative save sizes.

## 2026-07-22 - Area Index Baseline

- Area: graph-derived caller-keyed area grouping.
- Evidence: a local Release build grouped 256 open-chunk regions and reduced
  their directed boundary portals to 480 canonical area connections in about
  2.65 ms on a 512x512 world. Reserved warm rebuilds allocate nothing.
- Decision: accept the straightforward sort-and-reduce implementation. Area
  rebuild is derived maintenance, not a per-query hot path, and it avoids a
  second tile flood by consuming the region graph.
- Retry conditions: add incremental patching only if measured area maintenance
  becomes material in a workload with frequent topology edits.

## 2026-07-22 - Coarse Corridor and Weighted Product Baselines

- Area: shortest region-route reconstruction and persistent weighted
  multi-goal products.
- Evidence: local Release measurements on an open 512x512 world measured a
  31-chunk/30-portal coarse route at about 20.1 us, an eight-goal weighted
  product build over 262,144 reached nodes at about 24.5 ms, and exact
  corner-to-corner product replay at about 5.2 us for a 1,023-node path.
  Correctness tests cover non-monotone corridors, sparse missing topology,
  provider-composed reverse edges, cache invalidation, and allocation-free
  warm rebuild/reconstruction.
- Decision: accept coarse corridor reconstruction, weighted product caching,
  and the opt-in runtime selector. Keep the runtime default off: a full dense
  product build is a substantial up-front cost and only amortizes when reuse
  spans enough requests or processing calls.
- Retry conditions: calibrate CI thresholds from main-branch benchmark
  artifacts before making these new measurements regression gates. Revisit
  automatic selection only with representative stable-map reuse traces.

## 2026-07-22 - Span Queries Promoted; Maintenance Hook Rejected

- Area: rectangular/radius query callbacks, fused block pipelines, and
  coalesced derived-state maintenance.
- Evidence: 100,000 seeded queries match reference tile sets across top-down,
  vertical, and 3D shapes. Five-repetition local medians measured rectangular
  spans at 678 ns versus 213,076 ns per tile, radius spans at 1,789 ns versus
  157,203 ns per tile, and a fused pipeline at 417 ns versus 1,840 ns through
  an allocating intermediate. The coalescing backend reduced 512 dense
  schedules to one execution and measured 2,499 ns versus FIFO's 5,139 ns,
  but 256 distinct sparse tasks measured 21,069 ns versus immediate's 517 ns.
- Decision: accept public span emitters and fused pipelines. Keep the
  maintenance interface and immediate/FIFO/coalescing prototypes in
  `tess::experimental::maintenance`; do not integrate a scheduler hook into
  world storage because the prototype misses the mandatory sparse gate by a
  wide margin. Predicate bitsets and chunk summaries remain deferred because
  no authoritative predicate contract or mutation-cost evidence exists yet.
- Retry conditions: revisit maintenance promotion with O(1) intrusive or
  indexed deduplication and measured p95 latency on at least two realistic
  dirty-chunk scenarios. Revisit predicate acceleration when a consumer has a
  stable derived predicate whose full-map, sparse-query, mutation, and memory
  costs can be measured against the historical 4x/2x/10% gates.

## 2026-07-21 - Documentation-Only CI Fast Path

- Area: pull requests and main pushes that change only maintained
  documentation.
- Evidence: the implementation pull request classified its workflow and Python
  changes as code-affecting in six seconds, then ran and passed every existing
  platform, analysis, and benchmark job. The documentation-only proof then
  completed its classifier in 6s, hook backstop in 18s, and aggregate gate in
  2s; the six compiled job groups skipped before matrix expansion. The
  independent documentation build and C/C++ security analysis set the complete
  required-check critical path at 1m1s, down from the 16m47s code-path run.
- Decision: Accepted. Keep the narrow `docs/**`, Markdown, and `mkdocs.yml`
  allowlist.
  Keep the classifier, hook backstop, documentation build, and aggregate gate
  on every change; skip compiled jobs only after a complete Git diff matches
  the allowlist.
- Risk: a classification bug could suppress relevant signal. Empty changes,
  invalid revisions, Git errors, and any unmatched path therefore require full
  CI; renames are evaluated as delete plus add. Revisit the allowlist only when
  another file class has an independent required check with equivalent signal.

## 2026-07-21 - CI Critical-Path Work Separated From Calibration

- Area: required clang-tidy and benchmark jobs on pull requests.
- Evidence: a protected pull-request run completed required clang-tidy in
  40m41s using serial Unix Makefiles. The benchmark thresholds finished at
  about eight minutes, but ten-repetition non-gating baseline collection kept
  that required job running for 32m21s. A two-job clang-tidy trial passed in
  22m18s. A four-job trial then passed in 15m58s, a 61% reduction from the
  serial run. Suppressing PR baseline collection reduced the benchmark job to
  8m21s.
- Decision: Accepted. Match the public runner's four CPUs with a four-job cap
  for required clang-tidy, and collect benchmark calibration artifacts only on
  code-affecting `main` runs. Every benchmark threshold remains required on
  code pull requests and code-affecting main pushes; documentation-only merges
  do not produce redundant calibration artifacts.
- Risk: four clang-tidy processes increase peak memory and can interleave
  diagnostics. The public runner supplies 16 GB, and the explicit cap prevents
  unbounded parallelism. Pull-request-specific baseline artifacts are no
  longer available, but merge-commit artifacts remain comparable on the same
  runner family. Full required clang-tidy still takes about 16 minutes; retain
  it for code changes because tests and examples provide
  template-instantiation coverage that a small representative target would
  miss, and skip it only when a fail-closed change classifier proves a change
  is documentation-only.

## 2026-07-21 - Advisory Analysis Removed From Per-Commit CI

- Area: GitHub Actions advisory clang-tidy analysis.
- Evidence: the advisory preset duplicated the full-tree compilation done by
  the required clang-tidy gate with a broader, intentionally noisy rule set.
  Recent pull-request and main runs each spent about 40 minutes on this
  non-blocking job.
- Decision: Accepted. Keep the preset and its signal, but run it weekly or on
  manual request instead of on every pull request and main push.
- Risk: New advisory findings can remain undetected until the weekly run.
  Required low-noise clang-tidy checks continue to run on every change.
