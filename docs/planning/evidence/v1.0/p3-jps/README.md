# P3 gated JPS screen: retained evidence

Pre-registration: issue #249. Source under measurement: main `f16783b7`
plus the prototype recorded in `prototype.md`, all behind
`-DTESS_P3_JPS`; with the macro undefined the dev suite passes unchanged
(1592/1592). Both bench arms build from one worktree and one source and
differ only by the macro (base binary SHA-256 `b54cffe2…b373d37d`, head
`85fabfba…d2e18406`, Apple clang, CMake Release).

**Verdict: rejected on the pre-registered no-material-regression rule.
`rubble_256` regresses +109.4% on M3, tool-confirmed.** The accept bar
required at least one material win (plentiful: up to −90.2%) AND no
family x size materially regressing on either platform; a confirmed
material regression on one platform makes acceptance impossible
regardless of the other, so the Steam Deck leg was not run -- it could
only add regressions, never remove M3's. Two-platform evidence is the
pre-registration's requirement for final ACCEPTANCE claims, which this
screen never reaches.

## Correctness gates: all passed before any timing

`p3_correct.cc` -> `correctness.txt`: 160 pre-registered trials (open,
wall-gap, maze, rubble x 64x64, 256x256 x 20 closed-formula seeds), every
Found cost equal to the independent BFS oracle's optimum and to the
incumbent's, every route contiguous-passable-endpoint-checked against the
oracle, Found/NoPath agreement with the oracle on every trial including
disconnected pairs, bit-identical determinism, and the ineligible-caller
gate: 3D and sparse worlds ignore the opt-in flag byte-identically, while
hex, weighted, scaled, and provider models are routed to the weighted
core before the JPS branch exists and cannot reach it by construction.

Two implementation defects the oracle caught on the way, retained because
they are the screen's real correctness story: (1) pruning both axes
symmetrically is INCOMPLETE on 4-connected grids -- rubble trials
produced costs above optimal and false NoPath until horizontal scans
gained vertical probes (the 4-connected analogue of 8-connected JPS's
diagonal-scan straight-ray probes); (2) letting vertical scans stop on
mere goal-row alignment made every column "interesting" to those probes
and collapsed the search to per-tile expansion (hours, not
milliseconds); the vertical-then-horizontal L is the reorderable mirror
of the horizontal-then-vertical canonical L the goal-column stop already
generates, so vertical scans stop only at the goal tile or forced
neighbors. A per-(tile, incoming-direction) closed mask, reopened on
strict g improvement, is load-bearing: without it equal-g jump-point
cycles re-expand forever.

## The measurement

`tools/paired_bench.py`, shadow mode, `bench/sentinels.json` parameters
(10 repetitions, 8% relative floor, 2000 ns materiality floor, 2000
bootstrap resamples, 95% CI), seed 130049, A/A calibration first
(`m3-calibration-aa.json`: all cells within +/-1%, no flags). Decision
run `m3-screen-ab.json` (delta = head vs base; negative is a JPS win):

| cell | base median | head median | delta | 95% CI | verdict |
|---|---|---|---|---|---|
| open_64 | 1,775 ns | 1,754 ns | -1.7% | [-5.5%, +0.1%] | immaterial-scale |
| open_256 | 7,126 ns | 7,210 ns | +1.4% | [-0.1%, +6.4%] | immaterial-scale |
| wall_gap_64 | 391,159 ns | 44,888 ns | -88.5% | [-88.6%, -88.5%] | pass |
| wall_gap_256 | 9,990,869 ns | 975,706 ns | -90.2% | [-90.3%, -90.2%] | pass |
| maze_64 | 599,425 ns | 88,470 ns | -85.2% | [-85.3%, -85.0%] | pass |
| maze_256 | 6,873,463 ns | 1,310,945 ns | -81.0% | [-81.2%, -80.8%] | pass |
| rubble_64 | 161,107 ns | 116,802 ns | -27.0% | [-29.2%, -25.6%] | pass |
| **rubble_256** | 919,591 ns | 1,925,715 ns | **+109.4%** | [+107.9%, +111.2%] | **regression** (confirmed on rerun) |

The open cells are identical by construction -- the shared fast-path
preamble answers them before either search core runs -- exactly as the
pre-registration declared.

## The mechanism, counter-substantiated

`m3-rubble-counters.txt` (both arms, diagnostics build, the 20 rubble
queries): on rubble_256 the JPS arm HALVES the heap work (32,885 vs
60,763 pushes; 27,870 vs 51,541 pops; 24,573 vs 50,726 touched nodes)
while paying **11.2x the passability reads** (176,440 vs 15,696), and
identical reconstruct counts confirm equal costs. At 25% obstacle
density, jump segments are short and every horizontal scan step pays two
vertical probes; the probe reads swamp the halved heap work at 256x256
(at 64x64 the same trade still nets -27%). This is the known weakness of
scan-based JPS on high-density maps; the literature's remedy is
block-based bitboard scanning, which is a different prototype with its
own screen.

## What follows from the rejection

Rejected under the rule as declared, with the upside recorded: -81% to
-90% on structured maps is what a future candidate that fixes the
high-density read amplification (bitboard scans) could capture, and the
4-connected variant's correctness pitfalls above are the map for it. The
gate design (compile-time traits only, opt-in flag ignored byte-
identically outside the domain) held with no complexity growth, so
"gate becomes complicated" was never triggered. Prototype removed from
the branch per the plan's rules; this directory retains the diff, the
programs as source, both paired-bench JSONs, the counters, and the
captured correctness run.
