## 2026-08-23 - Gated JPS: -81% to -90% on structured maps, rejected on a +109% rubble regression

**Hypothesis** (pre-registered in issue #249): a 4-connected Jump Point
Search specialization behind a strict compile-time capability gate
(dense, orthogonal, DefaultSteps, unit cost, 2D, no provider) materially
speeds up eligible exhaustive searches on at least one platform without
materially regressing any family x size on either.

**Prototype.** Behind `-DTESS_P3_JPS` with a caller opt-in flag on
`PathScratch`, retaining the incumbent's entire pre-search fast path and
replacing only the exhaustive heap loop. Horizontal-primary canonical
scans: vertical scans stop only at the goal tile or forced neighbors;
horizontal scans also stop where a vertical probe finds something (the
4-connected analogue of 8-connected JPS's diagonal-ray probes); closed
per (tile, incoming direction), reopened on strict g improvement. Three
correctness lessons the oracle caught, recorded for any future
4-connected attempt: symmetric two-axis pruning is incomplete (rubble
costs above optimal); goal-ROW stops in vertical scans make every column
probe-positive and collapse the search to per-tile expansion; and
without the per-direction closed mask, equal-g jump-point cycles
re-expand forever.

**Correctness: all gates passed before timing.** 160 pre-registered
trials across open/wall-gap/maze/rubble at 64x64 and 256x256: cost
equality with an independent BFS oracle and the incumbent on every Found
trial, oracle-checked route validity, Found/NoPath agreement including
disconnected pairs, bit-identical determinism, and byte-identical
flag-ignored behavior for 3D and sparse worlds (hex, weighted, and
provider models cannot reach the branch by construction).

**Performance: rejected as pre-registered.** M3, paired interleaved A/B
with A/A calibration (clean), 10 reps, 8%/2000 ns materiality floors:
wall-gap -88.5%/-90.2%, maze -85.2%/-81.0%, rubble_64 -27.0%, open
immaterial (the shared preamble serves it) -- and **rubble_256 +109.4%,
CI [+107.9%, +111.2%], confirmed on the tool's rerun**. The accept bar
required no material regression on either platform, so the confirmed M3
regression settles the verdict and the Steam Deck leg was not run: it
could only add regressions, never remove this one. Counters substantiate
the mechanism: the JPS arm halves heap work on rubble_256 (32.9k vs
60.8k pushes) but pays 11.2x the passability reads (176.4k vs 15.7k) --
at 25% obstacle density jump segments are short and every horizontal
step pays two vertical probes, the known weakness of scan-based JPS on
high-density maps.

**Decision: reject; prototype removed from the branch.** Evidence
(prototype diff, programs as source, both paired-bench JSONs, counters,
correctness capture) in `docs/planning/evidence/v1.0/p3-jps/`.
Reconsideration: a block-based bitboard-scanning variant addresses
exactly the read amplification that decided this screen and would be a
new experiment; the recorded -81% to -90% structured-map ceiling is what
it stands to capture. A future 8-connected (DiagonalSteps) movement
addition also reopens the question with the classic algorithm.
