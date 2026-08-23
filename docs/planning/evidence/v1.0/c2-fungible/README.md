# C2 fungible goals: retained evidence

Pre-registration: issue #241 (revision 2 plus amendment 1, which fixed the
candidate's initial assignment to Control B's optimal matching and pinned
the reassignment policy's move set, scan order, and one-move-per-tick rule
before any arm code existed). Fixture: the anonymous-goal-pool mode merged
as its own commit (#245, `d615c752`) before this program was written, per
the pre-registration's ordering rule. Source under measurement: that merged
fixture plus the recorded arm program in `programs.md`; arm code was never
committed to the tree.

**Verdict: rejected on the pre-registered acceptance bar. The measured
finding is the pre-named alternative: dispatch quality, not reassignment.**
The primary metric is a tick count, so per the pre-registration no
two-platform timing was required.

## Program and output

`programs.md` records the single three-arm binary; `arms.txt` is its
captured output (Apple clang 21.0.0, `-std=c++23 -O2 -DNDEBUG`, single
run; the program's own determinism gate replays arms B and C per seed and
requires bit-identical outcomes). All randomness is the fixture's
closed-formula seeds plus a fixed bootstrap seed; runs are deterministic.

Arms, all on identical instances over 7 families x 3 pool sizes x the
pre-registered trial counts (396 seed-cells, 5 settles each including
replays):

- **A greedy dispatch**: repeated global minimum (agent, goal) edge over
  BFS bare-terrain distance, ties by agent then goal index.
- **B optimal dispatch**: exact rectangular assignment (successive
  shortest augmenting paths), same distance.
- **C candidate**: B's assignment, then the amendment's policy between
  ticks: single best strictly-improving move per tick (unheld-goal moves
  plus pairwise exchanges, improvement must exceed one tile), applied
  through `set_path_agent_goal` only.

## What decided the experiment

Acceptance required the paired geometric mean of ticks-to-fixpoint,
candidate vs Control B, to be at least 8% in the candidate's favour
(gm <= 0.92) with the bootstrap CI excluding 1.0. Measured:

| comparison | gm | 95% CI | seeds |
|---|---|---|---|
| pooled C/B | 0.9797 | [0.9580, 0.9986] | 382 |
| C/B at M = N | 1.0154 | [0.9960, 1.0457] | 124 |
| C/B at M = 1.25N | 0.9577 | [0.9147, 0.9911] | 129 |
| C/B at M = 2N | 0.9684 | [0.9281, 0.9929] | 129 |
| **A/B (dispatch quality)** | **1.4739** | [1.4103, 1.5411] | 278 |

The candidate's improvement is real (the pooled CI excludes 1.0) but a
quarter of the declared bar, and at M = N reassignment is *harmful*
(exchange churn with no goal surplus; ring M=48 gm 1.0924). Meanwhile
greedy dispatch is 47% slower than optimal dispatch on the same pools:
the anonymous-pool advantage overwhelmingly lives in the one-shot
assignment a caller makes at dispatch, exactly the boundary the
pre-registration's Control B existed to test.

The null was not vacuous: Control B still saw a strictly-improving
matching on 11.8% of ticks pooled (per-cell up to 48%), so the mechanism
had headroom it could not convert. Colony M=48 exceeded the 20% seed
exclusion cap (5/20), making that cell's tick metric uninterpretable; its
residual counts favour Control B (+2), consistent with the rejection.
Residual (non-arrived) deltas were never negative in any cell: the
candidate never improved terminal outcomes, and the 14 excluded seeds are
severity changes the reassignment itself introduced.

## Gates, all passed

`GoalOccupied` is zero in every arm on every seed. FlowAccounting was
attached in all arms: admission and retention identities hold at
quiescence; `offered == N + reassignment calls` and `superseded ==
reassignment calls` exactly (exchanges count two calls), so every
reassignment surfaced as supersession; `completed` equals the arrived
count. Assignment validity (holder/held coherence, goals only from the
declared pool) checked every tick. Determinism: arms B and C replayed
bit-identically per seed. Reassignment state is O(agents + goals) plus
the per-instance BFS distance fields shared by all arms; no per-tick
allocation (buffers preallocated per run).

## What follows from the rejection

The finding is a caller recipe: with an anonymous goal pool, spend the
effort on a good one-shot dispatch matching (optimal is cheap at these
sizes and worth 47% over greedy); in-movement reassignment on top of it
is not worth a mechanism, and with no goal surplus it is
counterproductive. No library change is proposed. Reconsideration: a
candidate operating where the caller layer cannot (inside the tier's
decision loop, with congestion-aware rather than distance-based costs)
would be a different experiment under the plan's rules.
