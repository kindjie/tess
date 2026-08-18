## 2026-08-18 - The portal-tick cache premise held; its ceiling did not

- Area: `detail::best_chunk_portal` in the portal steady-state tick,
  and the deferred per-tick cache experiment sized against it. No code
  changed; this records a re-measurement that keeps a deferred
  experiment honest.
- Why: the deferred cache rested on two figures measured on 2026-08-11
  at an older `main` - a 65.32% profile share and a 67.13% within-tick
  call-redundancy rate. `main` has since advanced ~40 commits,
  including the budgeted-replanning and bounded-recovery rewrite of
  `include/tess/sim/path_agent_tick.h`, so both inputs were stale and
  every figure derived from them inherited that.
- Profile: `path/agent_tick_100_weighted_goal_churn_portal_512x512` on
  controlled hardware, three `perf record -F 499 -g --call-graph fp`
  captures of a `-O3 -DNDEBUG -g -fno-omit-frame-pointer` build,
  18,470 pooled samples, zero lost. Sampled user-cycle self share:
  `best_chunk_portal` 68.42/69.14/68.90%,
  `process_weighted_batch_impl` 16.91/16.46/15.92%,
  `vector<Coord3>::_M_range_insert` 5.52/5.68/6.28%. That three-way
  ordering of report entries repeats in all three captures and matches
  the 2026-08-11 ordering. Timing, from retained benchmark JSON at a
  fixed iteration count with five repetitions: median 30,451 ns.
- Census: the measurement-only redundancy scaffold was re-applied to
  the current tree and both cells re-run at the same fixed iteration
  counts. Every published figure reproduces - 456,750 calls, 217.5 per
  tick, 0.12% cold, 67.13% within-tick duplicates, 32.75% cross-tick,
  95.24% replayed ticks for the repeated cell; 66.68% within-tick and
  zero cross-tick for the fresh one. The decompressed dumps are
  byte-identical to the 2026-08-11 dumps: every tick stamp, semantic
  key and scan count is unchanged. The analysis script was validated by
  reproducing the older published figures from the older dumps before
  it was pointed at the new ones.
- What that settles: the redundancy input is current, not stale. It
  also shows only that these two synthetic cells do not exercise what
  the tick rewrite changed - not that the tick at large is invariant.
- What it does not settle: the ceiling. Multiplying share by redundancy
  by tick time gives ~14.1 us/tick and a ~65 ns per-call cache budget,
  but that product assumes a duplicate call costs what an average call
  costs, and multiplies a sampled CPU-cycle share by a wall-clock
  total measured from a differently-sized run. Duplicates within a
  tick touch data the original just warmed, so they are plausibly
  cheaper than average and the product likely over-estimates removable
  time. The equal 32-tile scan per call fixes the scanned-tile count,
  which is not the same as equal work and further still from equal
  cost. Two adversarial review passes rejected the ceiling framing;
  the figure is recorded as a scenario.
- Also observed, not claimed: the 2026-08-11 fourth entry
  `build_bounded_weighted_distance_field_core` was 3.34% there and is
  0.67% in these captures. Symbolization and inlining are not
  established as comparable across the two binaries, so this is a
  thread to pull, not a measured change.
- Ranking limit: each capture holds a contiguous cluster of
  unsymbolized `libc.so.6` addresses totalling ~3.2%, more than the
  fourth resolved entry. Nothing below ~3% in these captures is a
  ranking of code regions.
- Status: the per-tick cache stays deferred. With cost weighting
  measured, what still separates the scenario from a ceiling is the
  second premise - share and total come from different runs and
  different clocks. Closing that needs both measured together, or a
  prototype timed end to end against an interleaved control, in the
  shape this repository already used to reject the four-ary heap. A
  seam-only `(from, to)` index remains unruled-out and unmeasured; this
  census counts calls, not scoring operations.
