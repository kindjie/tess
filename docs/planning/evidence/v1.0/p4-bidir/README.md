# P4 bidirectional A* screen: retained evidence

Pre-registration: issue #251, which reused P3's domain gate, families,
correctness gates, timed-workload construction (declared up front this
time), and decision rule verbatim, and pre-declared the P3 precedent
that a confirmed material regression on the first platform settles
rejection without the second. Source under measurement: main `35f3d53b`
plus the prototype in `prototype.md` (bench source included via
intent-to-add, closing the recording gap P3's addendum documents), all
behind `-DTESS_P4_BIDIR`. Base binary SHA-256 `19cb0006…3a8e17a5`, head
`a4a9810c…e3db2218`, Apple clang, CMake Release, one worktree.

**Verdict: rejected. Five of eight cells are confirmed material
regressions on M3; the sole win is rubble_64 at -15.8%.**

## Correctness gates: all passed before any timing

`correctness.txt`: the same 160 pre-registered trials as P3 -- cost
equality with the independent BFS oracle and the incumbent on every
Found trial (which also validates the mu <= max(minf_fwd, minf_bwd)
termination rule empirically on every instance), oracle-checked route
validity, Found/NoPath agreement including disconnected pairs,
bit-identical determinism, and byte-identical flag-ignored behavior for
all five ineligible instantiations (hex, weighted, provider-composed,
sparse, 3D). Memory, with the figures the pre-registration requires:
the backward direction owns one additional node-array set carried on
the scratch -- generation (4 B) + g (4 B) + parent (8 B) = 16 B per
node, 1,048,576 B at 256x256 (65,536 nodes) plus its open vector --
against the incumbent's primary set of generation + state + g + parent
= 17 B per node (1,114,112 B at the same size) plus two open vectors.
The addition is 0.94x the incumbent's node arrays, within the
pre-registered "incumbent plus one additional set" bound, and no
warm-path allocation occurs (the set is sized once per world shape).

## The measurement

`tools/paired_bench.py`, shadow mode, `bench/sentinels.json` parameters,
seed 130061, A/A calibration clean (`m3-calibration-aa.json`). Decision
run `m3-screen-ab.json` (negative = bidirectional wins):

| cell | base median | head median | delta | 95% CI | verdict |
|---|---|---|---|---|---|
| open_64 | 1,760 ns | 1,748 ns | -0.3% | [-2.3%, +1.0%] | immaterial-scale |
| open_256 | 7,362 ns | 7,366 ns | +0.5% | [-0.9%, +1.4%] | immaterial-scale |
| wall_gap_64 | 383,487 ns | 596,505 ns | +55.0% | [+51.9%, +58.7%] | regression |
| wall_gap_256 | 10,332,373 ns | 37,229,316 ns | +259.9% | [+256.7%, +261.9%] | regression |
| maze_64 | 599,560 ns | 682,630 ns | +13.3% | [+12.1%, +14.7%] | regression |
| maze_256 | 6,892,365 ns | 11,696,535 ns | +70.0% | [+68.7%, +70.7%] | regression |
| rubble_64 | 164,975 ns | 139,210 ns | -15.8% | [-18.5%, -13.7%] | pass |
| rubble_256 | 898,927 ns | 3,228,548 ns | +265.3% | [+254.5%, +273.2%] | regression |

## The mechanism, counter-substantiated

`m3-wallgap-counters.txt` (the timed wall-gap workload, both arms,
diagnostics build): the bidirectional arm does MORE search work, not
less. On the 256x256 cell it performs 1,354,997 heap pushes against the
incumbent's 970,722 (1.40x), 1,265,868 pops against 966,913 (1.31x),
and touches 1,025,835 nodes against 744,670 (1.38x); identical
reconstruct totals (28,575) confirm equal costs on the exact timed
workload. (The passability counters are deliberately not compared
across arms: the incumbent's exhaustive loop books neighbor rejections
under its candidate/blocked counters while the prototype books every
probe as a passability check, so that column measures instrumentation
placement, not work.) The meet-in-the-middle intuition fails on
serpentine terrain because the backward frontier's Manhattan heuristic
is exactly as misleading as the forward one's: both directions flood
the maze, and the safe termination bound -- mu cannot beat max(minf)
until both floods approach the meet -- keeps both frontiers alive
nearly to completion. That 1.3-1.4x node volume is then paid at
binary-heap prices, in both directions, plus a cross-direction meeting
probe on every relax attempt, against an incumbent whose two-bucket
dial frontier is near-free per operation. The rubble_64 win (-15.8%)
is the one cell where reduced flood depth beats the constant factor;
it inverts at 256x256 (+265.3%).

## What follows from the rejection

Rejected under the rule as declared, on the first platform, with the
Deck leg unnecessary for the same platform-existential reason as P3
(pre-declared this time). The screen answers the plan's question
cleanly: against this incumbent -- whose exhaustive loop is already a
cache-tuned dial with a strong heuristic -- whole-query bidirectional
search has no niche in the eligible domain: where the heuristic works,
the incumbent is already near-minimal; where it fails (serpentines),
both directions fail it symmetrically and the doubled machinery loses.
Prototype removed from the branch; this directory retains the diff
(bench included), programs as source, both paired-bench JSONs, the
counters, and the captured correctness run.
