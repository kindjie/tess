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

Entries from 2026-07-13 through 2026-07-31 are in
[`optimization-log-archive-2026-07-31.md`](optimization-log-archive-2026-07-31.md);
entries from 2026-07-12 and earlier are in
[`optimization-log-archive-2026-06-07.md`](optimization-log-archive-2026-06-07.md).

## 2026-08-08 - Deck re-pass: repetitions, an ABAB, and a withdrawn attribution

- Area: on-device confirmation that intervening merges did not move the
  published measurements, at main `1bb7c15`; adversarially reviewed
  twice (initial package, then a follow-up-measurement addendum), with
  the second round run against the follow-up data itself.
- Method upgrades over prior passes, adopted from review: 10-repetition
  aggregates in two interleaved sweeps with clock/thermal sampling
  (governor found already at `performance` on external power; clocks
  reached 3.50 GHz, temps <= 70 C); an interleaved same-session ABAB of
  the `4d7140b` and `1bb7c15` binaries (sha256-recorded) for the fields
  question; direct-versus-under-`perf record` probes on both the fields
  cells and the world-edit cell.
- Confirmed with repetition-grade evidence (CVs 0.1-0.9%): the scoped
  route-cache advantage is ~3.2-3.3x same-pass (1.28 ms default vs
  0.39-0.40 ms off-path, forced on-path cell ~0.65 ms), with no samples
  attributed to `suffix_place` in the off-path profiles and ~44-45% in
  the same binary's default profiles; indexed queued planning grows
  16.2x for 16x operations (CV <= 0.4%) on the narrow-domain path, wide
  operations keeping their documented linear-scan fallback; the heavy
  weighted cells sit within noise of the campaign readings ("no gross
  regression visible" is the supported strength), and a fresh
  goal-churn profile matches the campaign's DWARF-validated shape
  (A* 47.3% self) — the A* effort remains the top target.
- CORRECTED: the re-pass initially attributed a fields improvement to
  the #122 buffer hand-back. Review found the improved
  `goalset_build_*` cells never touch that cache, and source reading
  went further: the `cache_miss_store`/`cache_eviction` cells use the
  rvalue store overload #122 explicitly kept unchanged, so NO current
  bench cell exercises #122's change. The ABAB proves the improvement
  itself is real and binary-level — 16-20% on every
  build-per-iteration cell in both interleavings, hit path unchanged
  at ~54 ns, between-binary gaps 4-5x the within-binary spread — but
  it spans the whole `4d7140b..1bb7c15` range, so the attribution is
  withdrawn rather than reassigned.
- Also measured: `perf record` at 499 Hz costs ~0-1% on both the
  fields cells and the world-edit cell, so the earlier passes' mixed
  collection modes explain none of the observed deltas; the campaign's
  higher world-edit reading stays unattributed (cross-session state or
  binary layout).
- Follow-ups recorded: a `store_reusing` member-reuse bench cell so
  #122's actual claim is measured (and this misattribution class is
  fenced) — CLOSED 2026-08-09: `fields/cache_store_reusing` holds one
  member product across iterations and stores through the hand-back
  overload, guarded so a rejecting store cannot pass silently; first
  M3 readings 80.7 us against `cache_miss_store`'s 79.9 us (CV <=
  0.6%), consistent with #122's accepted evidence being an allocation
  count rather than a timing, and the cell joins the metal campaign's
  fields counter pass; bisect the fields improvement only if its magnitude comes
  to matter, first probe #120 versus the later tail; randomized ABBA
  blocks next time a binary A/B is run; first handheld
  budgeted-progress artifacts (24 files) and a 205-row 10-rep
  main-suite baseline JSON captured this window, unanalyzed.

## 2026-08-08 - Scoped route-cache staleness (accepted)

- Area: unit route cache invalidation; the first optimization from the
  2026-08-07 hotspot campaign's decision list.
- Hypothesis: the campaign attributed ~90% of the world-edit agent tick
  to the invalidate/repopulate/serve cycle behind whole-cache
  invalidation. Retiring only entries whose crossed chunks changed
  should remove repopulation from the steady state where edits land off
  most routes, without touching default-mode behavior.
- Method: opt-in `UnitRouteStaleness::ScopedFeasible` — per-entry
  `(chunk, version)` footprints validated lazily at serve time against
  an exact per-chunk version snapshot. Design reviewed adversarially
  (two Codex rounds, Fable final GO) before implementation; the
  implementation reviewed again (6 P2 / 4 P3, all addressed).
- Evidence (M3 Max, `bench` preset): the new survival-steady-state cell
  `path/agent_tick_100_unit_dirty_offpath_edit_scoped` runs ~130 us
  against the 415 us whole-drop baseline cell — with postcondition
  asserts proving zero retirements and pure revalidation. The forced
  worst case (`..._onpath_edit_scoped`, every route through the edited
  goal chunk, zero survivals) runs ~205 us: per-entry validation plus
  targeted re-store undercuts fingerprint-plus-wholesale-repopulation
  even when every entry retires. The default-policy cell is the
  unchanged no-regression guard. An earlier draft of the worst-case
  cell edited a mid-corridor chunk and measured ~160 us — replans
  learned detours around the edited chunk and survived (their
  footprints exclude it), so the cell was pinned to the goal chunk no
  route can avoid; the self-healing observation is worth keeping.
- Decision: accepted as opt-in policy with the semantics stated where
  it is enabled (legal, truthful cost, previously optimal;
  blocking-only edits concede nothing). Bootstrap ceilings at 4x the M3
  readings per the standing protocol; recalibrate from CI baselines.
- Follow-up, CLOSED 2026-08-08 — on-device verification: the Deck
  returned and the before/after ran at merged main (`20b280d`, same
  unpinned protocol as the campaign). Timings: scoped steady state
  394 us against the 1.313 ms default cell (3.3x, matching the 3.2x
  M3 ratio); forced worst case 648 us; default cell and clean tick
  unchanged against the campaign (1.31 vs 1.37 ms, 793 vs 778 ns).
  Profiles: `suffix_place` — 41-45% of the before profile and of the
  after-binary's default-mode control — is absent from the scoped
  steady state entirely; the scoped tick is ~80% route copies
  (`_M_range_insert` plus the libc bulk-copy loop, IPC 0.98 at 24.7%
  L1d miss), which is the design's predicted hit-serving floor. The
  control's unchanged shape and the scoped cell's vanished hotspot
  are the two halves of the claim, both observed on target hardware.
  Raw perf.data archived off-repository beside the campaign data.
- Follow-up, open: sparse-world scoped validation is a separate design
  if a workload demands it.

## 2026-08-08 - Field-product stores hand their displaced buffers back

- Area: `FieldProductCache::store` and the two `PathRequestRuntime` call
  sites that follow it (`path_runtime.h:586`, `:797`). Follow-up to the
  2026-08-07 instrument entry below, though not to the part of it that the
  `fields/cache_scan_entries_*` pair measures.
- Hypothesis: taking the product by move left the caller's member with no
  capacity, so the next `build_distance_field_product` reallocated a
  world-sized distance array; handing the displaced entry's storage back
  should make the rebuild allocation-free.
- Evidence: accepted, on an allocation count rather than a timing. On a
  64-tile world a rebuild after a displacing store went from five
  allocations to zero, counted with `ScopedAllocationCounter`.
  Mutation-verified: reverting only the hand-back restores the five.
  Timing was not used because the machine was too loaded during this work
  to produce a trustworthy reading, and the claim is about allocation
  rather than scan cost.
- Scope: the hand-back only fires on a store that displaces something -- a
  same-key replacement, or an admission the byte budget makes evict. An
  admission into a cache still under budget displaces nothing, so a
  runtime keeps reallocating until its product cache is full. A world edit
  or provider revision change produces a new key and is therefore an
  admission. This was stated too broadly in the first draft and is
  corrected here.
- Also fixed, and independent of the above: the call sites used to `lookup`
  the product straight back after storing it, which rescanned every entry,
  reconstructed the transition `Model` inside the loop, and recorded a
  cache HIT for work the cache had not reused. That inflated the published
  hit rate by one on every build, in the counters the benchmarks report as
  evidence. Two existing tests were asserting the inflated value. The store
  now returns what it stored, so the relookup is gone on every build, not
  only on displacing ones.
- Risk: the displaced product is handed back with its contents, not just
  its storage. Before the pre-merge fix that closed it, an evicting store
  left the caller's argument reporting `Found` with another key's goals --
  strictly worse than the old moved-from state. Both displacing paths now
  `clear()` the argument, which is noexcept and retains capacity. A second
  pre-merge fix corrected a read of `entries_[i]` after eviction had
  already shifted the vector.
## 2026-08-08 - Planning indexed by chunk, bounded by chunk count

- Area: `plan_operations` hazard detection and
  `plan_parallel_execution_phases` grouping. The follow-up the 2026-08-07
  instrument entry below said would "land next and must show their
  before-and-after against these numbers".
- Hypothesis: both scans compare a candidate against every operation
  accepted so far, and per-chunk edits -- one operation per dirty chunk,
  the ordinary consumer shape -- are pairwise disjoint, so every
  comparison pays for a chunk-overlap check only to fail it. A chunk-keyed
  index over accepted operations should visit only the operations that can
  actually conflict.
- Method: open-addressed table over chunk keys with intrusive per-slot
  chains, each node carrying its key so growth relinks without consulting
  the operations, and slots generation-stamped so `clear` is O(1) rather
  than proportional to the high-water size a reused report retains.
  Operations wider than `index_max_chunks_per_operation` (64) are kept out
  of the index and scanned linearly; a candidate that wide skips the index
  and scans everything, exactly as the planner used to.
- Evidence: accepted. Apple M3 Max, `bench` preset, seven repetitions,
  `--benchmark_min_time=0.4s`. Both binaries were run back-to-back in one
  session, which matters: an earlier attempt compared runs taken an hour
  apart and disagreed with itself by 21% on a benchmark the change cannot
  affect.

| Benchmark | Before | After | Change |
| --- | ---: | ---: | ---: |
| `queued/plan_frame_256` | 58.4 us | 12.0 us | 4.9x faster |
| `queued/plan_frame_4096` | 22.99 ms | 208 us | 110.6x faster |
| `queued/plan_frame_dense_64` | 249 us | 275 us | **10% slower** |

- Reading: the speedups are the weaker half of the evidence; the scaling
  is the stronger. Before, 16x the operations cost 394x the time; after,
  17.4x. That ratio comes from within a single run, so machine state
  cannot flatter it.
- The dense row is a real cost, not noise, and is the honest price of the
  bound: a whole-domain workload gets no benefit from the index and still
  pays for the indexability check and the second list the planner now
  maintains. The before-side CV was 4.3% against the after-side 1.0%, so
  treat 10% as approximate; it is not within the noise, but it is not a
  three-figure number either. Reducing it -- deferring the phase index's
  allocation until something is actually indexed -- is a follow-up, not a
  blocker, because the workload it costs is the one the old planner was
  already good at.
- Recorded because it is the finding, not a footnote: the first version of
  this change made that dense case **834x slower** -- 168 ms against
  201 us -- by indexing whole-domain operations. `resident_chunks()` is the
  default domain selector, so that was the supported shape most likely to
  be hit, and the two benchmarks above could not see it because both use
  one private chunk per operation. Review caught it; a benchmark confirmed
  it; `queued/plan_frame_dense_64` exists so it cannot recur silently.
- Gating, because a ceiling alone does not cover it: the threshold at 4x
  the post-change reading catches the catastrophic case, but a 4x ceiling
  cannot catch a 2.6x one, and dropping just the wide-candidate fallback
  costs about that. The benchmark is therefore also a paired sentinel,
  whose floor is a relative effect size. The sentinel source map had
  `include/tess/ops/` pointing only at a thread-pool benchmark, so the
  planner in that directory had no sentinel at all.
- Risk, and the part worth carrying forward: the differential tests
  guarding this change twice could not detect the bug they existed for.
  Deleting the open-phase filter survived several thousand randomized
  plans, because the generator drew dense chunk sets with mostly-mutating
  policies -- so every pair conflicted, every phase was a singleton, and
  grouping had nothing to decide. Separately, no generated operation was
  ever wide enough to leave the index, because the test world had 16
  chunks against a 64-chunk bound. Both are fixed, the discriminating
  phase layout is now constructed rather than sampled, and eight
  mutations each fail their target test. A ninth -- removing the
  wide-candidate fallback -- still plans correctly and is caught only by
  the benchmark, which was verified to move.
- Decision: Accepted.

## 2026-08-08 - Paced-with-idle wake penalty in budgeted-progress cells

- Area: budgeted-progress paced arrival cells (`tess_bench_budgeted_progress`,
  design section 3.2); dev machine (Apple M3 Max), smoke configuration.
- Hypothesis: pacing frames to 60 FPS edges only changes when work runs,
  not how fast it runs inside the budget window.
- Evidence: rejected. Sleeping ~14.7 ms to each edge lets the core enter
  idle states; the first work after wake runs at reduced frequency with
  cooled caches. At a 2 ms budget the paced loop consumed ~14% fewer
  within-budget work units than the unpaced loop (4.11M vs 4.77M over
  the same frame count; frames honored their budget — elapsed p50
  2.26 ms) and worst quantum tails stretched ~2x (1.87 ms vs 0.80 ms).
  The 600 events/sim-second cell is stable unpaced and unstable paced
  at 2 ms; 8 ms absorbs the penalty entirely.
- Decision: accepted as the honest paced-with-idle measurement and
  documented in the binary header; artifacts stamp their pacing mode so
  the two loops are never mixed in one curve.
- Follow-up: a spin-paced busy-host variant (spin to the edge instead
  of sleeping, emulating a host that renders between simulation slices)
  is deferred; revisit alongside the controlled-hardware campaign,
  where DVFS behavior differs (Steam Deck).

## 2026-08-07 - Steam Deck hotspot campaign

- Area: on-device CPU hotspot attribution for the highest-cost published
  workloads; first `perf record` campaign on the handheld (the 2026-08-06
  campaign was timing and counters only).
- Method: `4d7140b` built as `linux-bench` plus `-g
  -fno-omit-frame-pointer`, run directly on stock SteamOS. Per-workload
  `perf record` (cycles:u, frame-pointer call graphs at 499 Hz; a DWARF
  cross-check at 199 Hz agreed on every stable top symbol, and the one
  larger mover was the contaminated field-rebuild share retracted below)
  plus paired counter runs. `perf_event_paranoid=2` hides kernel time;
  for world edit — the workload where transient allocation was the
  suspect — page-fault counts put the allocation-driven share of that
  hidden time near 1% (other kernel time stays unmeasured). Governor
  unpinned: per-iteration medians of the workloads reported here matched
  the 2026-08-06 pinned baseline within ~2%, so shares are
  representative, and no absolute number here supersedes the published
  baseline. An
  adversarial review of methods and conclusions against source retracted
  two initial findings, kept below because the traps generalize.
- Evidence, goal churn (44.5 ms/tick, the frame-budget breaker): the
  tick performs one replan (`tick.processed_paths=1`) and
  `weighted_astar_path` is >77% inclusive. Reference singles put
  open-map cost at ~5.7 ns/expansion against ~210 ns/expansion under
  real frontiers (`weighted_astar_room_portals`: 138.6k expansions,
  29.7 ms), so the ~34 ms replan is consistent with a rough estimate of
  160k expansions; the leading hypothesis is plateau behavior of the
  Manhattan heuristic on weighted 512x512 terrain, to be confirmed from
  the benchmark's own expansion counters during the design work.
  DWARF srcline attribution places ~37% of samples in
  open-list heap machinery (`stl_heap.h` push/pop plus the tie-break
  comparator at `path.h:1554`).
- Evidence, world edit (1.37 ms/tick): ~90% is the route-cache
  invalidate/repopulate/serve cycle — `RouteCacheScratch::suffix_place`
  43-45% self (hash mix plus linear probe), `vector::_M_range_insert`
  14-18%, and ~17-20% in an unsymbolized libc cluster identified from
  instruction bytes as the AVX2 bulk-copy loop. IPC 1.25 at 10.9% L1d
  miss is consistent with a memory-bound cycle (no stall or bandwidth
  counters were captured). `prepare_process` invalidates the whole route
  cache on every edited tick, so a one-tile edit forces full
  repopulation of the suffix index.
- Evidence, parallel backend (main-suite pool benchmarks; ratios are
  not comparable to the pinned scaling-sweep protocol): chunk compute
  scales 1.92x/3.76x at widths 2/4 with a ~98%-payload profile —
  healthy. Chunk fill at IPC 1.05 and 22.5% L1d miss is consistent with
  a bandwidth limit rather than dispatch overhead as the cause of its
  1.29x at width 4. Tile touch at 18.5 us pooled against 3.1 us serial
  puts the per-phase dispatch floor near 15 us on this device.
- Retracted, with the traps recorded: a 12.9% "distance-field rebuild"
  share in goal churn was warm-up and setup contamination — the timed
  loop performs no field builds, and the share fell to 7.9% under
  DWARF. A "topology rebuild dominates the example frame loop" claim
  from a looped `tess_colony_2d` run was per-process initialization
  amplified 400x — 171 of 175 sample chains ran through the startup
  `build_region_graph`, not the incremental update. Within-group shares
  from multi-benchmark perf.data are equal-time artifacts and were not
  used for ranking.
- Decision: the first optimization target is the world-edit route-cache
  cycle — scoped invalidation instead of whole-cache invalidation per
  edited tick, gated directly by
  `path/agent_tick_100_unit_dirty_world_edit`. Goal-churn A* is the
  larger absolute cost but is design work (expansion reduction first,
  open-list mechanics second) and gets a design review before code.
  Planning-path coverage is not addressed here; those measurements land
  separately with their own instruments.
- Artifacts: raw perf.data, benchmark logs, and counter output are
  retained off-repository on the profiling device.

## 2026-08-07 - Instruments before fixes: planning and cache eviction

- Area: queued per-frame planning; field-product cache eviction. No
  optimization in this entry — only the measurements the fixes will be
  judged against, per the standing rule that a performance change without
  before-and-after numbers from its owning family is not accepted.
- Gap: the 2026-08-07 audit found `plan_operations` scanning every
  previously accepted operation per new operation, and phase grouping
  comparing each operation against every member of the current phase —
  both quadratic — with **nothing timing either**. Every queued benchmark
  plans outside its measured loop (`parallel_phase_support.h:117-119`,
  `tess_bench.cc:760`, `:787`), and the single in-loop planner call
  (`tess_scheduler_bench.cc:191`) plans exactly one operation. Separately,
  `FieldProductCache` walks `entries_` linearly three times per
  miss-and-store — `lookup`, the existing-key scan inside `store_with_key`,
  and `evict_to_budget` — while the only benchmark exercising that path
  holds about two entries.
- Instruments added: `queued/plan_frame_256` and `queued/plan_frame_4096`
  time planning plus phase grouping over disjoint per-chunk operations —
  the worst case for grouping, since the phase never closes, and the
  ordinary case for one edit per dirty chunk. `fields/cache_scan_entries_8`
  and `_128` hold per-store work identical and differ only in resident
  entry count, so their delta is the aggregate of those three scans. It is
  NOT attributable to eviction alone, and the benchmark does not claim to
  be: all three are linear, so a complexity change in any of them shows.
- First readings (Apple M3 Max, `bench` preset, three repetitions,
  coefficient of variation under 0.4%):

| Benchmark | Median |
| --- | ---: |
| `queued/plan_frame_256` | 59.6 us |
| `queued/plan_frame_4096` | 23.4 ms |
| `fields/cache_scan_entries_8` | 85.7 us |
| `fields/cache_scan_entries_128` | 93.2 us |

- Reading: 16x the operations costs **392x** the time. Pure quadratic
  scaling predicts 256x, so the excess is consistent with quadratic work
  plus growing allocation and cache pressure. In absolute terms a
  4096-chunk frame spends 23 ms in planning alone, which exceeds a 16.7 ms
  frame budget before any execution happens. The audit predicted the shape
  from source; the magnitude is what the instrument adds.
- Scan delta is 7.5 us between 8 and 128 resident entries — real and reproducible, but small against the ~85 us product
  build that dominates each store. Read the pair as a complexity check on
  the cache's linear scans, not as a claim that they dominate.
- The first version of this pair varied goal COUNT across keys (2-10
  against 2-130) while claiming identical work. Goal count changes flood
  seeding, key comparison length, stored byte size and therefore the
  resident count, so the two sizes ran different workloads. Holding
  cardinality constant and varying only goal positions both fixed the
  claim and produced a cleaner signal: the delta grew from 4.7 us to
  7.5 us once the confound was removed.
- Gate sensitivity, recorded because it is easy to over-read: the scans
  are about 7% of each `cache_scan` reading, so a ceiling set at 4x cannot
  fire for a scan regression — one scan going quadratic at 128 entries
  still passes. The ceilings give trend visibility. Complexity is watched
  by the paired sentinel run instead, whose floor is a relative effect
  size, so `fields/cache_scan_entries_128` is registered in
  `bench/sentinels.json`.
- Ceilings: **bootstrap, deliberately loose**, at 4x these readings. They
  were taken on an M3 Max while the gates run on Linux runners, so a 2x
  ceiling would flake rather than gate. Recalibrate at 2x the maximum over
  ten CI baseline artifacts, with the rest of their families.
- Follow-up: the fixes themselves (chunk-keyed hazard index; intrusive
  least-recently-used list, mirroring the 2026-07-12 residency conversion)
  land next and must show their before-and-after against these numbers.

## 2026-08-06 - At-budget portal-segment store swept dependencies twice

- Area: `WeightedPortalSegmentCache::store_checked`, the at-budget branch.
- Hypothesis: the `store_capacity_status` pre-pass added in #100 walks every
  entry calling `dependencies.is_valid`, then `compact_checked` walks them
  again with the same predicate. If the pre-pass is redundant, removing it
  should recover the difference, and the at-budget branch is the steady state
  for any cache with a segment budget, so the cost is not exceptional.
- Method: a standalone harness storing 20,000 distinct 12-node segments into a
  256-entry budget after warming to budget, with every timed store confirmed on
  the compaction branch (`sweeps` equal to store count). Compiled from one
  source against three header trees — `a63371e` (pre-#100), `4a919fb` (v0.12),
  and this branch — with AppleClang, `-O2 -DNDEBUG`, interleaved A/B/C runs on
  an otherwise idle machine.
- Evidence: medians of roughly 9,590 ns/store pre-#100, 10,480 ns/store on
  v0.12 (+9.4%), and 9,740 ns/store here (+1.6% over pre-#100, -7.1% against
  v0.12). Direction was consistent across every interleaved pair; an occasional
  high first-run sample was warm-up and did not shift the median.
- Decision: accepted. Both store branches already validate transactionally —
  `compact_checked` builds its kept set in scratch and returns before the
  `entries_`/`paths_` swap, and `reserve_append_capacity_checked` returns
  before its reserve — so the pre-pass caught nothing the remaining checks
  miss. A constant-time `store_capacity_precheck` preserves the one thing the
  pre-pass did provide: rejecting an impossible store before the candidate
  entry's dependency capture allocates.
- Follow-up: no benchmark sentinel drives this cache to its budget, so the
  gate saw neither the original regression nor this recovery. Adding an
  at-budget store sentinel needs ceiling calibration under the existing
  benchmark rules and is not a quiet addition; it remains open.

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
