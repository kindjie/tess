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
