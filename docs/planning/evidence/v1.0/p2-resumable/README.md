# P2 resumable A\* screen: retained evidence

Pre-registration: issue #240 (revision 2 plus the scope-and-accounting
amendment posted before any staleness or scheduling data was gathered).
Source under measurement: main `e235fe05` plus the prototype recorded in
`prototype.md`, all behind `-DTESS_P2_RESUMABLE`. With the macro undefined
the dev suite passes unchanged (1585/1585, one environment-gated skip), so
the prototype is provably confined to its macro.

**Verdict: semantic feasibility proven; rejected on the pre-registered
scheduling-value criterion.** No timing was read, and per the
pre-registration a counter-based conclusion needs no two-platform campaign.

## Programs and outputs

All three programs are recorded as source in `programs.md`. Compiled with
Apple clang 21.0.0 on the M3 host, `-std=c++23 -O2`, includes
`-Iinclude -I<build>/generated/include`, defines as listed per program.
Determinism is closed-formula (SplitMix64 seeded `f(trial)`); no run-time
entropy.

- `p2_identity.cc` (`-DTESS_P2_RESUMABLE`) -> `identity.txt`. Route and
  expansion identity over 20 random 64x64 maps (10 connected) times slice
  schedules k in {1, 8, 64} and a seeded irregular schedule, plus a clean
  serpentine family built from the canonical fast-path-defeating recipe in
  `tests/path_test_util.h`. `checked=44 route_mismatch=0
  expansion_mismatch=0 paused_runs=44 atomic_runs=0`. The `paused_runs`
  column exists because an earlier revision of this probe never verified
  that slicing engaged; the serpentine family plus the assertion close that
  vacuity hole.
- `p2_gates.cc` (release `-DNDEBUG` and assert builds) -> `gates.txt`.
  Dense: version-marked edit of a captured chunk detected (D1); marked edit
  of a verified-uncaptured chunk does NOT false-positive and the resumed
  route still matches (D2, non-vacuous: 13 of 16 chunks captured); raw
  un-marked writes are demonstrated undetectable, pinning the declared
  scope (D3); resuming with a different request aborts in assert builds and
  is refused with recovery via `reset()` in release builds (D4);
  cancellation reuse in both directions matches contiguous results (D5); no
  deps reallocation across warm slices and the memory figures below (D6).
  Sparse: eviction of a captured chunk (S1), slot aliasing by a different
  key (S2), same-key rematerialization whose content version restarts at
  zero (S3), a non-resident-at-capture chunk becoming resident (S4), and a
  version-marked edit (S5) all read as stale at the resume boundary before
  any scratch access; the unchanged-world control (S6) completes and
  matches contiguous.
- `p2_value.cc` (`-DTESS_P2_RESUMABLE -DTESS_ENABLE_DIAGNOSTICS -DNDEBUG`)
  -> `value.txt`. The scheduling-value comparison under the amended
  accounting.

## Memory and allocation gates (figures recorded, as pre-registered)

Paused-state addition on the 64x64 dense serpentine: 13 dependency entries
(312 B) plus `sizeof(P2Slice)` = 152 B, against the incumbent per-query
scratch of 4096 nodes for the same world. The deps buffer is reserved on
the cold first slice and neither grows nor moves across warm slices
(capacity and data pointer pinned by D6).

## What decided the experiment

The pre-registered accept bar was: the bounded arm reduces worst-case
per-tick heap-loop work (it does: `max_slice_exp` never exceeds k in any
run) AND total non-expansion work grows by at most 10%, where the resumed
arm's total pays the library's four declared counters plus every capture
probe and every revalidation check. From `value.txt`:

| map | k=1 | k=8 | k=64 |
|---|---|---|---|
| serpentine 64x64 (16 chunks) | +252.7% | +53.3% | +28.4% |
| serpentine 128x128 (64 chunks) | +642.6% | +100.3% | +32.6% |
| serpentine 256x256 (256 chunks) | +1836.5% | +248.7% | +50.2% |
| random 64x64, 10 maps, mean (max) | +314.1% (+374.6%) | +68.0% (+73.7%) | +37.3% (+38.9%) |

Every cell fails the 10% bar; the closest any configuration comes is +28.4%.
The dominant term is revalidation: per-resume cost equals the captured
dependency count, and the capture set grows with map extent (13 -> 43 -> 151
chunks across the doublings) because the fast-path preamble's plane scans
read along full axis extents before falling through. Those chunks are
genuinely read -- the capture is not an over-approximation -- so this is
the pre-registered pathology ("revalidation cost scales with map ... per
slice") arising from real reads: fine slicing multiplies a per-resume walk
whose size tracks map extent, and k=1 on the 256x256 map pays 19.4x the
baseline's total work.

The rejection is robust to the accounting choice. Under the most favorable
alternative -- ignoring capture probes entirely and charging only
revalidation -- growth at k=64 is +3.5% (64x64), +9.6% (128x128), +28.3%
(256x256): still over the bar at scale, with k <= 8 failing everywhere.
Re-entry bookkeeping the library already counts also grows with slicing
(start/goal passability re-checks: 2 contiguous vs 2 per slice).

## What was NOT rejected

Semantic feasibility held completely: byte-identical routes, exact
expansion equality, slice-schedule invariance, cancellation with state
reuse, chunk-granular staleness detection scoped to version-marked edits,
and residency-generation revalidation that turns the sparse slot-aliasing
corruption case into a refusal. A future incremental-replanning candidate
(the pre-registered reconsideration condition) can reuse that design; the
dependency-capture cost structure is the part that must change.

The weighted-core extension recorded in the amendment was never
implemented; the rejection makes it moot.
