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

Entries are recorded as one fragment per experiment in
[`optimization-log.d/`](optimization-log.d/) and assembled into this file at
release by [`tools/assemble_changelog.py`](../../tools/assemble_changelog.py).
Do not append here directly: every branch that does conflicts with every
other, and this file is subject to the repository's 24,000-token limit --
which it has now exceeded twice, forcing an archive split each time.

Entries from 2026-08-15 through 2026-08-17 are in
[`optimization-log-archive-2026-08-17.md`](optimization-log-archive-2026-08-17.md);
entries from 2026-08-10 through 2026-08-14 are in
[`optimization-log-archive-2026-08-14.md`](optimization-log-archive-2026-08-14.md);
entries from 2026-08-01 through 2026-08-09 are in
[`optimization-log-archive-2026-08-09.md`](optimization-log-archive-2026-08-09.md);
entries from 2026-07-13 through 2026-07-31 are in
[`optimization-log-archive-2026-07-31.md`](optimization-log-archive-2026-07-31.md);
entries from 2026-07-12 and earlier are in
[`optimization-log-archive-2026-06-07.md`](optimization-log-archive-2026-06-07.md).

## 2026-08-24 - D* Lite screen rejected at feasibility once both arms were counted with the same ruler

**Question** (pre-registered in issue #255; amendment 1 recorded
review-driven conformance corrections before the corrected run): does a
goal-keyed D* Lite that repairs search state across version-marked
edits do materially less work per edit-query cycle than fresh
`tess::astar_path`, and does the advantage survive into wall time?

**Answer: no, at stage 1.** The first capture reported a stage-1 pass
(pooled work ratio 1.913 against the >= 1.5 bar) and a wall-time
rejection -- an "inversion." Review found the pass was a counter
artifact: every incumbent neighbor candidate was counted while a D*
Lite `update_vertex` counted as one unit, hiding its per-neighbor rhs
re-scans. With symmetric accounting -- plus the registration's other
under-implemented terms fixed (true Manhattan-2 route-local offsets,
an incumbent-derived pregenerated edit trace so both arms replay one
identical workload, warm-allocation and memory gates enforced, the
sweep replayed twice with identical digests) -- the pooled median
ratio is **1.012**: the repair machinery does essentially the same
abstract work as searching fresh, and the registration's stop
condition rejects without a hardware campaign. The retained M3
wall-time capture corroborates as context: its 12 uniform-locality
cells (which the trace defects do not touch) hold 11 confirmed
material regressions, up to +1,788% relative.

**The durable lessons.** (1) Count both arms with the same ruler:
asymmetric counters manufactured a feasibility pass that two further
stages of measurement then had to un-earn. (2) The prior capture's
lesson survives in corrected form: op-count ratios predict wall time
only over comparable primitives; weight by per-op cost or measure it
directly before promising timing. (3) The consistent-pop discard
defect (a consistent vertex popped through the underconsistent branch
toggles g to infinity and back forever) is recorded for any successor.

**Consequences.** No library change existed in either arm; the record
is the disposition. P6's opening condition stays unmet (no new
open-set structure merges); P2 stays closed (cost, not semantics).

Evidence: `docs/planning/evidence/v1.0/p5-dstar/`.

## 2026-08-24 - Congestion pricing revalidated at full supported coverage; retained as a caller recipe

**Question** (pre-registered in issue #256; amendment 2 added the
plan-mandated demo-classifier judge after review caught the original
registration gating on the wrong harness; amendment 3 -- posted before
its run -- added the full supported-population matrix, exact gates,
and a pre-declared value rule after a second review round showed the
two-population sample could not close the plan's "every supported
population" gate): does one bounded dynamic price policy (per-tile
cost `1 + min(3, live agents within Manhattan 1)`, every 4 ticks,
versioned edits) preserve terminal classification and buy anything?

**Answer: retained.** Across seven scenario geometries (the native
CLI's full set, browser-incremental's progressive wall admission
included per the amendment-3 addendum) x all 64 supported populations
(448 cells): priced classification retained or
improved everywhere, zero crowd-blocked and zero durably-unreachable
in the priced arm, replay bit-identical, every scenario wall admission
assertion-checked, and the canonical tier's own 41 arrival-incomplete
tip cells (every population >= 384, stranded at the 5000-tick cap)
all complete under pricing. Pre-declared value rule: pooled gm of
priced/canonical ticks 0.4180, CI [0.3859, 0.4522] -- PASS, on both
platforms with byte-identical tables. The
boundary in exact numbers: goal-wall regresses at gm 1.49 (up to
+89%) with classification intact, and the originally-registered C0
substrate screen stays failed as the sensitivity record (17 of 132
fixpoint seeds reclassify chaotically). Outcome-level only: waits and
gate utilization were not instrumented, so no mechanism claim is made.

**Consequences.** Pricing is documented as a caller recipe with its
boundary in the spatial-coordination architecture notes; no library
change, demo spread default unchanged. C6 dispositions without a run:
no MECHANISM-level capacity premise was isolated for a crossing
reservation to represent (the earlier "served premise" phrasing
overclaimed and is corrected). The review sequence itself is part of
the record: two rounds each invalidated a verdict-carrying element
(wrong judge; insufficient coverage), and both corrections were
registered before their reruns.

Evidence: `docs/planning/evidence/v1.0/c5-congestion/`.

## 2026-08-23 - Resumable A\* screen: semantics proven, rejected on scheduling value

**Hypothesis** (pre-registered in issue #240, revision 2 plus a
scope-and-accounting amendment posted before data): a single A\* query can
pause and resume across ticks with caller-owned state without changing its
route, and bounding per-tick search work has scheduling value, defined as
worst-case per-slice heap work actually bounded AND total non-expansion work
growing at most 10% under an accounting where the resumed arm pays every
capture probe and revalidation check the mechanism performs.

**Prototype.** Slice state carried on `PathScratch` behind
`-DTESS_P2_RESUMABLE` (never merged; recorded in the evidence directory).
Single implementation, loop-top slice boundary before any frontier
operation; the dense fast-path preamble is an atomic slice 0; dependency
capture at chunk granularity `(chunk_key, content_version,
residency_generation)` on first read -- preamble reads via a scoped hook on
the passability leaves, heap-loop reads via a bounds-guarded face walk --
revalidated at every resume before any scratch access. Scope: the
orthogonal unit core, dense and sparse; the weighted fallthrough ignores
the slice detectably.

**Semantic gates: all passed.** Byte-identical routes and exact expansion
equality over every slice schedule (44/44 runs, every run verified to have
actually paused -- an earlier probe revision never checked engagement and
could have reported vacuous identity over preamble-answered runs).
Cancellation with state-object reuse; misuse (different request on resume)
aborts under asserts and is refused with recovery in release. Staleness:
version-marked edits of captured chunks detected; edits of
verified-uncaptured chunks do not false-positive; raw unmarked writes
demonstrated undetectable, pinning the declared scope. Sparse residency:
eviction, slot aliasing, same-key rematerialization (content version
restarts at zero; the generation decides), and absent-becomes-resident all
refuse at the resume boundary before any aliased scratch read. Paused
state adds 312 B + 152 B against a 4096-node incumbent scratch; no warm
allocation.

**Scheduling value: rejected.** The per-slice expansion bound holds
everywhere, but the cost bar fails in every configuration: best case
+28.4% (k=64, 64x64 serpentine) against the 10% ceiling, +50.2% at
256x256, and fine slicing is pathological (k=1 at 256x256 pays 19.4x the
baseline's total non-expansion work). The dominant term is per-resume
revalidation, whose size is the captured dependency set; that set grows
with map extent (13 -> 43 -> 151 chunks across two doublings) because the
fast-path preamble genuinely reads along full axis extents before falling
through. The rejection is robust to accounting: charging revalidation
alone still fails at k <= 8 everywhere and at every k on 256x256.

**Decision: reject.** The prototype is removed from the branch; the
evidence directory `docs/planning/evidence/v1.0/p2-resumable/` retains the
recorded prototype diff, the three measurement programs as source, and
their captured outputs. No timing was read, so no two-platform campaign
was required. Reconsideration condition (pre-registered): acceptance of a
future incremental-replanning candidate reopens the question with a
different continuation shape, and can reuse the proven staleness and
residency-revalidation design; the dependency-capture cost structure is
what must change.

## 2026-08-23 - Reciprocal conflicts: production tier fails three fixtures; C4 opens

**Question** (pre-registered in issue #247 plus one amendment recorded
before the evidence): do the current movement tiers resolve canonical
reciprocal conflicts within a pre-registered bound -- arrival at the
no-progress fixpoint AND ticks <= max(3 x optimal, optimal + 8), optimal
from an exhaustive joint-space BFS oracle -- or does PR C4
(conflict-local temporal escalation) open on a proven failing fixture?

**Answer: C4 opens.** The production PIBT tier
(`tick_weighted_path_agents_with_pibt` under `RouteAttachmentRanking`,
the C0-pinned configuration) fails three of six hand-built fixtures,
deterministically: the mid-corridor pocket-yield corridor (both agents
wedge), the four-agent junction cross (all four wedge), and the
queued-yields corridor. Where it passes, it is exactly optimal (Permit
head-on swap in 5 ticks; the 2x2 rotation cycle in 1 tick under both
swap policies).

**The failure is myopia, not ranking.** A diagnostic arm ran the same
machinery under per-agent exact BFS ranking -- the configuration the
pinned pocket-yield regression proves can yield when the yielder is
cornered adjacent to the pocket -- and it fails the same three fixtures
identically. A mid-corridor pocket is enterable from exactly one tile;
the retreating yielder passes it while retreat and pocket rank equally,
and beyond that tile no single-step decision recovers. One step of
foresight separates the pinned regression's pass from these failures.

**The queued-yields self-seal is the sharpest exhibit.** The tier's
partial success creates the unsolvable instance: one opposing pair
arrives and settles, and the settled tiles wall the one-wide corridor,
structurally sealing the other pair's goals on terrain that is fully
open (`structural_seals = 0`). Order-of-arrival is the difference
between the oracle's full 15-tick resolution and a permanent seal, which
gives C4 a concrete case beyond any bounded look-at-the-live-conflict
horizon -- the plan's WHCA-failure-mode clause made concrete.

**Everything merged.** Fixtures, oracle (anchored on hand-computed
cases: rotation 1; Permit head-on 5, since parity makes 4 impossible;
Forbid bare corridor unsolvable), digest table with `SwapPolicy` mixed
in, and verdicts pinned at observed values -- three of them failures
that C4 must flip. Context arms recorded: the joint movement tick
passes swaps and rotations and fails the three foresight fixtures; the
sequential mover fails everything, as the library documents (no swap or
cycle capability). Every arm replays bit-identically. Tick counts
decide; no timing, so no two-platform campaign.

Evidence: `docs/planning/evidence/v1.0/c3-reciprocal/`; regression
suite: `tests/tess_reciprocal_conflict_test.cc`.

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

## 2026-08-23 - Fungible goals: rejected; the value is dispatch quality, not reassignment

**Hypothesis** (pre-registered in issue #241, revision 2 plus amendment 1
recorded between the fixture merge and any arm code): with an anonymous
goal pool of M >= N interchangeable goals, reassigning targets during
movement beats the best assignment a caller can make at dispatch, without
changing terminal outcomes or breaking accounting. Acceptance bar: paired
geometric mean of ticks-to-fixpoint against the OPTIMAL one-shot dispatch,
at least 8% in the candidate's favour with the bootstrap CI excluding 1.0.

**Design.** The pool fixture merged first (#245) as its own arm-neutral
commit: agents placed goal-less, 21-entry digest table, M = N pool equal
to the paired C0 instance's goal sequence, larger pools prefix-coherent.
Three arms in one binary on identical instances: greedy dispatch, optimal
dispatch (exact rectangular assignment), and the candidate -- optimal
dispatch plus the amendment's policy (single best strictly-improving move
per tick over unheld-goal moves and pairwise exchanges, improvement > 1
tile, applied only through `set_path_agent_goal`). One shared distance
(BFS on bare terrain) for dispatch, reassignment, and the contention
metric. 7 families x 3 pool sizes x pre-registered trials = 396
seed-cells; every arm replayed per seed with bit-identical outcomes
required.

**Result: reject.** Pooled gm(C/B) = 0.9797, CI [0.9580, 0.9986] over 382
included seeds -- a real but 2% improvement against an 8% bar. At M = N
the candidate shows no benefit and trends harmful (gm 1.0154, CI
[0.9960, 1.0457], includes 1.0); the ring cell (gm 1.0924) shows the
mechanism: with no goal surplus the policy can only exchange, and
exchange churn slows settling. The
per-surplus cells (M = 1.25N: 0.9577, M = 2N: 0.9684) are the candidate's
best and still miss the bar. The rejection is not a vacuous null: Control
B still saw an improvable matching on 11.8% of ticks pooled, headroom the
policy could not convert into settling speed. Residual (non-arrived)
counts never favoured the candidate in any cell, and colony M = N
exceeded the 20% exclusion cap, falling to residual counts that also
favour Control B; dropping the
uninterpretable cell's surviving seeds moves the pooled statistic only
from 0.9797 to 0.9789.

**The retained finding is the pre-named alternative.** Greedy dispatch is
47% slower than optimal dispatch on the same pools (gm(A/B) = 1.4739, CI
[1.4103, 1.5411], over the 278 of 396 seeds surviving the same severity
rule): the anonymous-pool advantage overwhelmingly lives in
the one-shot matching made at dispatch, which is fully expressible today
through `set_path_agent_goal` with no library change. The caller recipe:
solve the assignment well once (optimal is cheap at 48 x 96), and do not
bother reassigning mid-movement on distance grounds -- with no surplus it
showed no benefit and trended harmful.

**Gates all passed**: `GoalOccupied` zero in every arm (its reclassification
as a defect gate held), flow admission and retention identities at
quiescence, `superseded` exactly equal to reassignment calls so no
invisible goal mutation, `completed` equal to arrivals, per-tick
assignment validity, bit-identical replay of every arm on every seed.

**Decision: reject; no arm code merges.** Evidence (program source and
captured output) in `docs/planning/evidence/v1.0/c2-fungible/`.
Reconsideration: an in-tier mechanism using congestion-aware costs where
the caller layer cannot reach would be a new experiment; a distance-based
caller-layer policy is answered here.

## 2026-08-23 - Conflict-local escalation: resolves all three C3 conflicts; global arming declined

**Question** (pre-registered in issue #253 with two recorded
amendments): can a conflict-local, completion-planning escalation --
detection, bounded component extraction, an exact joint-space solver,
ordered execution -- flip C3's three pinned production-tier failures
within C3's own bound while preserving canonical behavior elsewhere?

**The mechanism** (Phase A, harness-level, behind no public API):
trigger on K = 8 no-progress ticks OR an imminent seal (an arrival
whose settle would strand a live agent -- discovered necessary because
queued_yields' seal forms at tick ~7 and terminal-set monotonicity
makes repair impossible, only prevention); component closure with
region radius 2 growing once to 4 on unsolvable; A_max 6, T_max 32,
solver cap 250k states (amendment 2, reduced from 2M, verified
outcome-identical), over-bounds = counted skip, never truncation;
whole-plan invalidation; deterministic throughout.

**Fixture gates: all pass.** pocket_yield 24 ticks (bound 24, 1 fire),
junction_cross 20 (bound 27, 1 fire, 3,024 solver states),
queued_yields 22 (bound 45, 2 fires, zero seals, all four arrive --
the C3 self-seal exhibit resolved by ordering). The three C3 passes
are untouched: zero fires, tick-identical, digest-identical.

**Substrate gate: failed, and the failure is the finding.** Across the
132 C0 seeds: all 71 clean seeds strictly inert (zero fires,
digest-identical); 61 residual seeds -> 56 identical, 3 strictly
better (three sealed agents converted to arrived), 1 mixed, 1 worse;
aggregate +3 arrived, -2 wedged, -1 sealed. But per-agent severity
non-worsening fails on 2 seeds: a locally sound, oracle-exact
intervention still perturbs the global trajectory, and the divergence
can strand an agent far from any fired component. Per the
pre-registration's stop condition the gate was applied, failed, and
NOT amended a second time.

**Disposition: attempted-with-partial-success; Phase B not proposed.**
The mechanism, fixtures, and pinned substrate measurement merge as
test support; the production tier is unchanged and C3's pins still
describe it; no planning authority is granted. Three fixed mechanism
defects are recorded for any successor (settled-agents-in-region plan
invalidation, aborted-tick starvation, trigger cooldowns), plus the
open problem that decides promotion: bounding trajectory divergence --
an intervention protocol whose global effect is provably no worse per
agent, or an accepted quality-delta contract in place of Pareto
safety. Detector cost (per-tick reachability probes) is a second
promotion blocker, needing incremental reachability.

## 2026-08-23 - Bidirectional A*: rejected; both frontiers fail a misleading heuristic symmetrically

**Hypothesis** (pre-registered in issue #251, reusing P3's domain gate,
families, correctness gates, and decision rule verbatim): a whole-query
bidirectional unit search over the P3-eligible domain materially speeds
up at least one family x size on one platform without materially
regressing any on either.

**Prototype.** Behind `-DTESS_P4_BIDIR`, caller opt-in on `PathScratch`,
incumbent fast paths retained, only the exhaustive loop replaced. Two
Manhattan-guided frontiers alternating by open-list size; best meeting
cost mu; termination mu <= max(minf_forward, minf_backward), validated
empirically by the oracle on all 160 trials. The backward direction
carries one additional node-array set on the scratch -- the
pre-registered memory bound, met by construction.

**Correctness: all gates passed** -- cost equality with oracle and
incumbent, route validity, NoPath agreement, determinism, and
byte-identical flag-ignored behavior for hex, weighted,
provider-composed, sparse, and 3D callers (instantiated, per the gate
tightened in P3's deviation note).

**Performance: rejected -- five confirmed material regressions on M3.**
wall_gap +55.0%/+259.9%, maze +13.3%/+70.0%, rubble_256 +265.3%; the
sole win rubble_64 -15.8%; open cells immaterial (shared preamble). A/A
clean. Counters on the timed wall-gap workload: the bidirectional arm
did MORE work, not less -- 1.40x pushes (1.35M vs 0.97M), 1.31x pops,
1.38x touched nodes, equal reconstruct totals -- because a serpentine
misleads the backward Manhattan heuristic exactly as much as the
forward one, both directions flood, and the safe termination bound
keeps both floods alive nearly to completion, all paid at binary-heap
prices against the incumbent's near-free two-bucket dial. The Deck leg was unnecessary: the no-regression rule is
platform-existential and M3's regressions are confirmed
(pre-registered as the P3 precedent this time).

**Decision: reject; prototype removed from the branch.** Evidence in
`docs/planning/evidence/v1.0/p4-bidir/` (prototype diff including the
branch-only bench source -- the recording gap P3's addendum documents
is closed here by tracking the bench via intent-to-add). Whole-query
bidirectional search has no niche against this incumbent in this
domain: where the heuristic works the incumbent is near-minimal; where
it fails, it fails both directions symmetrically. Reconsideration: a
weak-heuristic or heuristic-free search surface would reopen the
question; P5's D* Lite screen does not.

## 2026-08-22 - Portal seam index screened and rejected on measured ceiling

- **Area:** chunk-seam portal selection (`tess::detail::best_chunk_portal`)
  after PR #213's selection-scoped portal memo.
- **Hypothesis:** the memo answered repeated calls within one selection
  (66.7-67.1% of calls), so the residual per-call seam walk still holds a
  material, removable share of the end-to-end portal tick, and a private
  seam-local index could remove it. Pre-registered as issue #234 before any
  measurement, with one amendment recorded before any A/B number existed.
- **Method:** the screen measures the ceiling instead of estimating it. An
  earlier draft proposed deriving the removable share from profile
  attribution; that was withdrawn as undecidable, because the seam walk is a
  fully inlined lambda in a header template at `-O3`, so per-line self time
  inside it is not credible and Zen 2 cycle sampling skids without IBS. Worse,
  it would have left "which instructions count as scan" to be settled after
  the numbers arrived.

  In its place, a **seam-keyed static stand-in**: at first query per ordered
  chunk pair, precompute the passable seam targets in the authoritative scan
  order into a flat table, then score only those. The stand-in performs no
  invalidation, no validity check, and no memory discipline, so it bounds a
  real index's **obligations**. It does not bound representation: it holds
  each entry in its own heap-allocated target list, and a flat-arena or packed
  layout could do less per-call work on the hot path. Both screened cells hold
  topology static, so an index would pay its invalidation obligation zero
  times there — meaning a better-engineered index could beat this stand-in on
  exactly these workloads. What is measured is the ceiling of *this
  representation*, not of every possible index. It is keyed on the seam and
  never on `(current, goal)`; a goal-keyed cache would measure cross-selection
  memoization, a different candidate. The measurement scaffolding is retained
  as evidence.
- **Identity:** both arms build from one worktree at merged `main`
  `11cf6428`, differing only by `-DTESS_P1_SEAM_STANDIN`. Before timing,
  counter identity was verified at fixed iteration counts on both devices:
  every portal, tick, and segment counter matched exactly, so the measured
  delta is attributable to seam work and not to changed downstream work.
- **Environment:** M3 under the `bench` Release configuration; Steam Deck
  (Zen 2, SteamOS 3.x, all eight CPUs at the `performance` governor) with a
  steamrt4 clang Release build. Paired interleaved A/B through the
  repository's own `tools/paired_bench.py` at its declared parameters — ten
  repetitions, 8% relative effect floor, 2000 ns materiality floor, 2000
  bootstrap resamples, 95% confidence. Absolute times are never compared
  between devices.
- **Calibration:** A/A passes were clean on both hosts. M3 paired deltas ran
  0.2-1.0%; Steam Deck ran 0.0-0.5%. Twice the A/A p95 therefore sits well
  under the 8% relative floor, so the floor binds rather than measured noise.
  On the primary cell the 2000 ns absolute floor is the larger constraint —
  about 12.8% on M3 and 9.6% on the Deck.

  The pre-registration's amendment described adopting the tool's declared
  parameters as "stricter, not looser". That was one-sided and is corrected
  here: raising the absolute floor from the maintenance campaign's 500 ns to
  the tool's 2000 ns tightened the **go** bar while loosening the
  material-**regression** criterion on the Deck primary cell, from an
  effective 8% to 9.6%. The outcome is unaffected — the observed 10.8%
  regression clears both — but the direction should be recorded both ways.
- **Result, primary cell**
  (`path/agent_tick_100_weighted_goal_churn_portal_512x512`): M3 improved
  18.4% (95% CI 16.2-19.2% faster); the Steam Deck **regressed 10.8%**
  (95% CI 10.2-11.6% slower).
- **Result, secondary cell**
  (`path/agent_tick_100_weighted_fresh_churn_portal_512x512`): M3 improved
  4.1%; the Deck regressed 1.1%. Neither is material.
- **Result, guardrails:** every edit and dirty cell was immaterial on both
  devices, but "immaterial" is not "inside noise": two Steam Deck intervals
  exclude zero — `unit_dirty_world_edit` at +0.5% (CI +0.19% to +0.57%) and
  `weighted_shared_dirty`'s confirmation at +0.4% (CI +0.06% to +0.55%). Both
  sit far under any material threshold and change no conclusion.

  The guardrail reading is weak for a second and more important reason. The
  stand-in's table is `thread_local`, lives for the process, and is guarded
  only by comparing the world's address. `paired_bench` runs all five cells in
  one process per round, and `weighted_shared_dirty` shares the
  `WeightedPathWorld` instantiation — and therefore that table — with both
  portal cells, over stack-allocated worlds whose addresses can repeat. That
  cell also edits the world, and the stand-in never invalidates, so its head
  arm may not be computing the same portals at all. The two decision cells are
  insulated: they use a byte-identical map, and the `unit_dirty` cells use a
  separate `PathWorld` instantiation with its own table. Nothing here can move
  the decision, but the dirty-cell guardrails are not evidence that a real
  index would be free on edit-heavy work. That cost remains unmeasured.
- **Decision: reject; the index is not implemented.** The governing ground is
  the pre-registered go bar, which reads the stand-in's own decision
  statistic. That statistic, declared in advance as the geometric mean of the
  two portal cells, was 11.5% on the M3 against a 16% bar. It fails on the
  interval as well as the point estimate: composing the interval-optimistic
  ends still yields only 12.4%. Any stricter composition of the amendment's
  per-cell floors than the 8% device floor used here would raise the bar above
  16%, so the result fails under every reading. The Steam Deck's own decision
  statistic is a 5.8% regression, so the "other device at least at its own
  threshold" prong fails independently.

  The cross-hardware rule corroborates, with a narrower claim than it first
  appears to carry. The Deck's confirmed 10.8% regression on the primary cell
  shows that *this representation* regresses on Zen 2, not that any index
  must. The go bar does not depend on that distinction.

  The remaining headroom makes a better representation an unpromising bet
  rather than an untried one: the secondary cell improved only 4.1% while
  paying no index obligations at all, so closing a 4.5-point geomean gap would
  have to come from layout alone.

  On the tool's labels and thresholds: `paired_bench` gates on
  `ci_low > 8%` **and** a median paired delta above 2000 ns, so the two floors
  bind different statistics rather than combining into one percentage. The
  9.6% figure quoted above is the absolute floor expressed against the Deck
  cell's base time, useful for comparison but not a check the tool performs.
  Separately, the M3 artifact labels the primary cell `immaterial-scale`
  despite its 18.4% improvement: that label means an *unflagged* result at
  this base time is not a statistical refutation, not that the observed effect
  is immaterial. The Deck's cell at the same scale flagged and confirmed.
- **What the split means.** The same change is a large win on Apple Silicon
  and a material loss on Zen 2. The stand-in trades a tight sequential scan
  over packed page data for indirect iteration over a heap-allocated target
  list. Fewer tiles are examined, but the survivors are reached through a
  pointer with a larger per-entry footprint. That trade appears to pay on the
  M3 and not on the Deck. This is a hypothesis about the mechanism, not a
  measured attribution; the screen decided on the end-to-end result and did
  not profile the split.
- **Limitations:** the measured object is one representation — a table of
  passable seam targets iterated in scan order. A packed-bitset representation
  is a different candidate with a different memory profile, and this result
  does not decide it. Note, though, that a bitset variant still visits every
  seam tile position and would save only the page loads, which is the part the
  Deck appears to prefer as it stands. Both workloads hold topology static, so
  nothing here measures invalidation cost.
- **Reconsideration condition:** a change that materially raises seam-scan
  width — larger chunks, a denser passability predicate, or a movement class
  with expensive passability — re-opens the ceiling measurement. A change that
  only lowers call count does not, because the memo already owns call count. A
  materially different index representation may be screened separately, but
  must clear the same two-device bar.
- **Evidence:** `docs/planning/evidence/v1.0/p1-portal-seam/` retains the
  calibration and ceiling artifacts for both devices and the measured
  scaffolding, alongside a README recording the exact invocations, the device
  and binary identities, and the counter-identity dumps. The rejected code is
  removed from the branch; the recorded scaffolding is the retained artifact.

## 2026-08-22 - PIBT hindrance tie-break rejected on classification regression

- **Area:** the PIBT movement tier's resolution of candidates that tie on the
  route-attachment rank, currently decided by enumeration order.
- **Hypothesis:** choosing among equal-rank candidates by a hindrance measure
  improves movement quality on dense fixtures without regressing terminal
  outcomes, determinism, or tick cost. Pre-registered as issue #238 with one
  amendment recorded before implementation.
- **Definition, rewritten before implementing.** The first definition —
  "agents for which this tile is currently their best-ranked candidate" — was
  withdrawn as incoherent with the algorithm. PIBT computes ranks lazily, per
  agent, only when that agent begins deciding, and discards the frame after.
  Under the reading "ranks retained from earlier deciders" no such store
  exists, so the highest-priority decider always sees hindrance zero; under
  the reading "decided agents' chosen tiles", every decided destination is
  already claimed and vertex-rejected before a tie-break could see it. Both
  readings degenerate. The replacement counts other active agents whose
  retained route's next point is the candidate tile, which is fixed before any
  decision in the pass, independent of decision order, and adds no
  ranking-oracle calls.
- **Implementation:** no library change. The tie-break composes into the
  ranking oracle as `rank * 8 + min(hindrance, 7)`, which makes attachment the
  primary key, hindrance the secondary, and enumeration order the tertiary —
  the tier already breaks rank ties by enumeration order. Both arms are two
  oracle objects in one binary. The composition is piecewise because
  `RouteAttachmentRanking` returns disjoint attached and detached ranges and
  scaling the detached range would overflow; a test pins that the scaled
  attached maximum stays below the detached base for the committed shape.
- **A design flaw the tests caught.** The first draft rebuilt the hindrance
  index before each tick. Routes do not exist until the tier's planning pass
  runs, and planning happens inside the same call as movement, so that index
  was a pass stale and empty outright on the first tick. The rebuild moved to
  first use within a pass, which places it after planning and before any
  decision. It stays order-independent because the index is a pure function of
  the agent array and retained routes, neither of which a ranking call
  changes.
- **Method:** C0's committed fixtures and seed schedule — 132 seeds across
  seven families — run paired, arm against arm, on identical instances. Both
  arms replay bit-identically per seed, which is a gate rather than a result.
- **Result, classification:** on the colony family the candidate produced **22
  agent-level regressions against 13 improvements**, and sealed counts rose
  from 294 to 302. Every other family was byte-identical in classification.
- **Result, exclusion:** colony excluded **12 of 20 seeds** (60%) for
  classification change, far above the pre-registered 20% cap, so colony's
  tick metric is uninterpretable by the rule declared in advance and the
  decision there falls to residual counts — which regress.
- **Result, ticks:** pooled paired geometric mean over the 120 comparable
  seeds was 0.9948, a 0.52% improvement against an 8% material bar. On the two
  families with any tick headroom the effect is absent: warehouse was
  identical to the tick (2,246 against 2,246) and ring improved 0.11%.
- **Decision: reject.** The pre-registered stop condition fires on the first
  criterion — any seed regressing its terminal classification is a rejection —
  so the tick metric never needed to decide. It agrees anyway.
- **The null is not vacuous, but the first check offered for that was the
  wrong measurement.** A tie-break that never fires would produce the same
  flat result while testing nothing, so the claim needs evidence. Counting
  ranking calls with a nonzero hindrance term — 2.4% to 14.9% depending on
  family — does not supply it: the composition preserves every strict base
  ordering, so hindrance can only matter where two candidates in one decision
  frame tie on base rank and differ in hindrance. A nonzero term on a
  candidate that was never tied proves nothing.

  What does establish it is outcome divergence. Six of seven families produced
  different results under the two arms: ring 3,351 against 3,343 ticks,
  random_sparse 1,858 against 1,857, random_medium 1,854 against 1,855,
  random_dense 2,182 against 2,186, adversarial 372 against 369, and colony
  diverging on 12 of 20 seeds. The mechanism demonstrably fires and changes
  decisions; it simply does not improve them.

  Warehouse is the exception and is recorded as **inconclusive on engagement**
  rather than as the clearest case. Its two arms are identical — 2,246 ticks
  against 2,246, no classification change — so nothing distinguishes "the
  tie-break fired and did not matter" from "it never fired there".
- **Stopped early, not silently dropped.** The pre-registration also declared
  a 95% bootstrap interval over the seed set, swap and backtrack-depth
  secondaries, and an allocation gate. None was collected: the arm was
  rejected on the first stop condition, and the plan permits a correctness
  failure to stop an experiment before its remaining measurements. The
  backtrack-depth counter was never built. The allocation gate is unevidenced
  rather than passed — plausible, since no library code changed and the index
  reuses its capacity warm, but it was not measured.
- **Limitations:** the fixtures are one 64x64 shape and the tier is PIBT with
  `RouteAttachmentRanking`; a different oracle would tie differently and could
  give hindrance more to discriminate. No timing was measured, because the
  arm was rejected on correctness before tick cost could contribute, and the
  cross-hardware rule was therefore never reached. Colony's result is
  dominated by seals rather than live blocking, which this mechanism does not
  address, so colony was always a weak test of the hypothesis.
- **Reconsideration condition:** a ranking oracle whose ties are both frequent
  and load-bearing among top candidates, or a fixture family whose congestion
  is live blocking rather than sealing. A larger fixture shape would also
  require rechecking the piecewise composition before reuse.

## 2026-08-20 - External maintenance adapter promotion campaign

- **Hypothesis:** the registered dirty-bit backend will materially beat the
  queued-coalescing primary control on at least one of Apple M3 and Steam Deck
  without a material primary or immediate-execution guardrail regression on
  either device.
- **Candidate and method:** source
  `b4a882bbdaa32a704109d5bdd773a1adfe45b492`, built separately with the
  recorded native Apple toolchain and pinned Steam Runtime image. Each device
  ran a separate 30-block queued-coalescing A/A calibration, then 30 paired,
  SHA-ranked blocks across dense, sparse, mixed, flush, budgeted, and
  16/64/256/1,024/4,096 registered-task cells. Every cell compared dirty bit
  with immediate, FIFO, and queued-coalescing backends. CPU time was the
  decision metric; absolute times were never compared across devices.
- **Correctness gate:** the exact source completed the normal suite with no
  failures (1,567 passed and one intentionally unsupported capability
  skipped). Adapter-focused ASan/UBSan, TSan, and warnings-as-errors runs each
  passed 21/21 cases, and campaign-tool tests passed 43/43. The adapter cases
  cover deterministic 1,000-run flush, archive-v2 and independent-rescan
  equivalence, dirty ownership, typed content/residency generations,
  generation-safe clear, retry, budget, exceptions, shutdown, concurrency,
  and warmed dense/sparse zero-allocation behavior across all backends.
- **Calibration:** every A/A workload was valid. The largest paired relative
  noise p95 was 2.21% on Steam Deck, below the frozen 10% invalidation ceiling.
  Candidate thresholds remained the predeclared maximum of the fixed floors
  and twice each device's measured A/A noise.
- **M3 result:** `flat` overall. It provides neither the material primary win
  required to graduate dirty bit nor a material regression.
- **Steam Deck result:** the aggregate primary comparison against queued
  coalescing is `flat` at +1.46%, with a 95% interval of +1.36% to +1.64%
  against the 8% relative and 4.98 us absolute thresholds. The overall device
  decision is nevertheless `material_regression`: dirty bit is materially
  slower than immediate execution in budgeted (+10.06%), flush (+10.16%),
  scaling-256 (+10.32%), and scaling-1,024 (+10.85%) cells. The scaling-4,096
  immediate interval crosses the regression boundary and is `inconclusive`.
- **Memory limitation:** isolated scaling-4,096 M3 processes recorded one-off
  roughly 1.3 MiB peak-RSS excursions under dirty bit, FIFO, and immediate.
  Exact work counters, non-monotonic repetitions, lower dirty-bit median than
  coalescing, and green sanitizer/allocation gates do not indicate a leak.
  The protocol declared no memory threshold, so this remains descriptive and
  no post-result gate was invented.
- **Decision:** `keep_experimental`. `DirtyBitScheduler` does not satisfy the
  portable performance rule. This performance-only result does not block the
  separately validated stable task, handle, result, adapter, and immediate-
  execution contract from promotion.
- **Evidence and limitation:** the public sanitized bundle is retained under
  [`evidence/v0.13/maintenance/`](../evidence/v0.13/maintenance/), separately
  manifested by its inner `PUBLIC_EVIDENCE_SHA256SUMS`; the raw set stays
  external and unchanged under
  `CROSS_DEVICE_EVIDENCE_V4_SHA256SUMS`, SHA-256
  `407f6279aad3a27442ca4fb8673712baf1b7c4a152c20e74607ff0a10cd77cb0`, with
  the sanitized and omitted members pinned in the directory's redaction map.
  The Deck wrapper's aggregate console transcript/status was not separately
  captured; authoritative calibration and candidate phase statuses,
  inventories, logs, governor evidence, and replay outputs are retained.
- **Reconsideration:** retry only after a relevant implementation, adapter,
  benchmark, fixture, compiler, flag, or SDK change, then refreeze and rerun
  correctness plus both hardware legs. A mechanical move needs an explicit
  representativeness record rather than an assumed carry-forward.

## 2026-08-19 - Reduce CI setup and Traffic oracle latency

Issue 218 was a recovered infrastructure failure: two attempts were cancelled
while Ubuntu packages downloaded unusually slowly, then the same workflow run
succeeded on attempt three. Across 90 observed package-install steps, p50 was
16 seconds, p95 was 109 seconds, and the maximum was 1,306 seconds. Another run
continuously downloaded 50.4 MB for 21 minutes 34 seconds, confirming that APT
inactivity timeouts cannot impose a total duration bound.

The package sample came from completed setup-step timings queried through the
Actions API across 19 recent `main` push runs, from issue-218 run 32244691550
through run 32290830241.

Accepted changes remove APT from the common Linux ccache path by downloading
the upstream 4.13.6 static binary under a pinned SHA-256 digest. The remaining
libc++ and coverage package installs fail closed with retries and inactivity
timeouts. Runner-provided GCC 12/14, Clang 16, clang-tidy 18, and Ninja avoid
redundant installation and are checked explicitly so image drift is visible.
Successful same-run retries reconcile a bot-owned failure issue only while the
bot's unedited report remains the latest activity; ambiguous or human-owned
issues remain open.

The Traffic Lab's exact route oracle, rather than its crowd replay, dominated
Debug and sanitizer latency. A representative hosted PR previously spent
6 minutes 17 seconds in Dev CTest and another 11 minutes 38 seconds running two
generic Traffic example acceptances. After decomposition, a local Debug
Traffic slice retained all scenario checks and both 512/1,600-tick crowd
outcomes in 11.77 seconds; the 2,048 exact comparisons passed separately in
4.96 seconds under the optimized bench preset. These local and hosted figures
are not a paired benchmark. Required optimized PR and main gates now own the
exact comparisons, while Debug, GCC, ASan, Windows, and coverage retain the
long-run behavior checks.

No existing CI job was demoted. The prior failure classification still shows
independent signal from the required portability, sanitizer, static-analysis,
documentation, and benchmark gates, while existing benchmark sentinels and
coverage remain advisory. Reclassify only after post-change run history shows
a new low-signal critical path.

Migrating providers was deferred. Free public hosted runners avoid a second
control plane and currently offer a better cost boundary than an unmeasured
replacement. Reconsider a main-push-only, no-secrets shadow pilot after the
internal changes settle; require at least ten paired commits and a predeclared
25% improvement in both p50 and p95 end-to-end time without weaker reliability
or security before granting a provider authoritative work.

## 2026-08-18 - Traffic Lab planner investigation

- **Question:** explain the Traffic Lab's planning tail before changing
  behavior, then prefer a generally useful library change only when the
  measured problem falls within Tess's intended pathfinding contract.
- **Contract and workload:** Tess documents unit A* for unit-cost terrain,
  weighted A* for positive varying entry costs, shared-goal fields for repeated
  goals, and opt-in portal-first routes when bounded suboptimality is
  acceptable. The Traffic Lab has 1,024 distinct start/goal pairs, static
  terrain during the measured 128-tick startup, and an eight-request FIFO.
  Every terrain entry cost is initialized to one and no Traffic Lab operation
  changes it.
- **Root cause:** the baseline is not a retained-route or invalidation problem.
  The queue drains exactly the 1,024 initial requests, eight per tick, with no
  topology edit or replan. Across the 128 deterministic tick positions, median
  planning time correlates with touched nodes at 0.9979 for funnel and 0.9994
  for multi-gate. A statically unit-cost workload is routed through the
  weighted API: both planners use Manhattan distance, but barrier scenarios
  miss the unit planner's exact plane-gap shortcut and fall into weighted A*'s
  obstacle-blind wavefront expansion. Eight first searches can therefore touch
  about one million nodes in one tick. The request-count budget is not a work
  or wall-time bound, as the proposed budgeted-agent-replanning design already
  states.
- **Controlled strategy probe:** an untracked C++ probe at commit
  `8dda47f6a7980e4e348b66b888e81ea95b77e129` used the exact 1024x512 shape,
  fields, barriers, openings, and 1,024 request pairs. It was compiled `-O3
  -DNDEBUG` with Apple Clang 21.0.0 on arm64 macOS 26.5.1. Exact weighted A*
  supplied the cost oracle, using the demo's compound movement class rather
  than a legacy tag approximation. The probe source SHA-256 was
  `44057de133582810405a202fd1212ab5ac2491fe0193785111a5110fb3af392e`.
  Deterministic result and counter totals are the primary evidence; the single
  ordered process's per-request timing is exploratory and does not meet the
  repository's 2,000-sample p99 publication floor.

  | Scenario and strategy | Exact-cost routes | Reported expanded | p50/p95 |
  | --- | ---: | ---: | ---: |
  | funnel, weighted exact | 1,024 | 91,701,010 | 5,259 / 5,793 us |
  | funnel, unit exact | 1,024 | 1,271,312 | 7.3 / 8.2 us |
  | funnel, supplied gates | 1,024 | 1,274,336 | 9.3 / 13.6 us |
  | multi-gate, weighted exact | 1,024 | 12,935,564 | 587 / 1,227 us |
  | multi-gate, unit exact | 1,024 | 1,058,176 | 6.8 / 7.3 us |
  | multi-gate, supplied gates | 1,024 | 1,061,120 | 7.5 / 10.2 us |

  Unit and supplied-gate results matched the weighted status and optimal cost
  for every request. The reported expanded-node field fell 72.1x for funnel
  and 12.2x for multi-gate under unit search; on the unit shortcut this field
  is constructed path length, so it is not presented as a total-work ratio.
  Supplied-gate routes use the existing weighted portal-route product with
  exact weighted segments and no segment cache. The scenario supplies a
  nearest opening because its static barriers make crossing an opening
  mandatory and every entry cost is one. Each request's start and goal have
  the same row, so a route crossing at row `g` has the fixed horizontal cost
  plus `2 * abs(start_row - g)` vertical cost; choosing the nearest opening is
  therefore optimal. The portal builder reads `PassableTag` and `CostTag`,
  while the movement class additionally rejects `ConstructionTag`. Scenario
  initialization maintains the stronger invariant that every construction
  tile is impassable and every non-construction tile is passable; final tests
  must either pin that equivalence or use one movement predicate throughout.
- **Route and crowd parity:** equal optimal cost is insufficient for a traffic
  experiment. Only 52 funnel and 160 multi-gate unit routes were byte-identical
  to weighted A*. Unit routes often turn vertically at the spawn edge rather
  than approach the barrier first. In a 1,600-tick deterministic replay this
  changed congestion materially:

  | Scenario and strategy | Arrived | Accumulated waits |
  | --- | ---: | ---: |
  | funnel, weighted exact | 438 | 296,604 |
  | funnel, unit exact | 48 | 516,792 |
  | funnel, supplied gates | 789 | 115,579 |
  | multi-gate, weighted exact | 776 | 127,736 |
  | multi-gate, unit exact | 128 | 474,432 |
  | multi-gate, supplied gates | 1,024 | 768 |

  The unit substitution is rejected. Supplied-gate routes deliberately are
  not byte-identical either (534 funnel and 560 multi-gate matches), but retain
  the intended barrier-first approach: at tick 512 their blocked/wait counts
  exactly match the weighted baseline (funnel 80/441, multi-gate 16/73). Their
  later improvement removes equal-cost route-shape gridlock rather than the
  choke point, so the changed long-run congestion outcome is an intentional
  scenario behavior change, not parity.
- **Alternatives evaluated:** lowering the FIFO request count would trade a
  smaller tick for a longer initial planning drain and would still not bound
  one search. Existing goal-monotone chunk portals found only 128 of 1,024
  funnel routes, so their exact fallback retains the tail. A prototype using
  the existing region graph's non-monotone coarse path found all routes and
  reduced funnel segment expansions to 97,587 with the default segment-cache
  budget, but its routes were 5.0% more costly in aggregate and up to 12.3%
  more costly than exact; the current 4/3 Manhattan quality cap accepted only
  607 of 1,024 funnel routes. Generalizing this tier to compound movement
  classes and runtime graph guidance would add useful weighted capability, but
  the static gate scenarios do not justify that behavioral and API scope.
  Resumable A* is the only candidate that could make one arbitrary search a
  cooperative work quantum; the existing budgeted-progress plan correctly
  keeps it contingent on broader stage-5 evidence, persistent-state semantics,
  and resumed-versus-contiguous correctness tests.
- **Recommendation before revised review:** keep weighted movement semantics
  and use scenario-supplied gate waypoints only for funnel and multi-gate;
  aligned and shuffled-crossing remain direct exact searches. Expose the
  existing generic queued-replan lifecycle as a public callback-based wrapper
  so callers can combine the FIFO with any `PathResult`-returning Tess strategy
  without duplicating agent-state transitions. The current exact unit and
  weighted helpers remain convenience wrappers over it. The callback is
  invoked synchronously as `(agent_index, request)`. Its returned path may be
  borrowed but must remain valid through the immediate copy into retained
  route storage. It must not mutate or reenter the supplied agents, routes, or
  queue, or alias the destination route. The generic mechanism manages queue
  and lifecycle state but neither validates nor certifies the callback's path
  legality, cost, or optimality; the exact helper APIs retain their stronger
  guarantees. If the callback throws, the front item, agent lifecycle, and
  retained route remain unchanged, but callback-owned side effects are not
  rolled back. This is a small, generally useful library boundary; gate
  selection itself remains correctly local to the demo. Do not add a unit
  joint-tick wrapper, region-graph portal extension, or resumable search for
  this fix.
- **Fix acceptance evidence:** after revised review, require every planned path
  to be legal, deterministic, and equal in status and optimal cost to direct
  weighted A*. Byte-identical routes and crowd states are explicitly not the
  contract. Pin gate crossing, 128-tick queue drain, representative 512-tick
  choke-point behavior, the existing eight-request ceiling, the full native
  percentile campaign with at least 2,048 fixed-tick samples per scenario, the
  separate counter pass, browser capture, and the complete test suite. Timing
  remains advisory. Also repeat the 1,600-tick replay and record its arrivals,
  accumulated waits, progress, and state hashes: the long-run behavior change
  is part of the chosen scenario strategy, rather than an incidental result
  hidden by the 512-tick check. Reconsider a general weighted topology-guided
  route only for a measured varying-cost, distinct-goal workload where current
  goal-monotone portals fail; reconsider resumable A* only under the existing
  stage-5 evidence requirements.
- **Accepted implementation and native result:** the reviewed public callback
  drain now owns only FIFO and agent lifecycle, while the Traffic Lab supplies
  gate waypoints locally. The Release timing pass repeated every 128-tick
  scenario in 16 fresh processes (2,048 samples each). It used nearest-rank
  percentiles and the existing publication floors; timing remains advisory.

  | Scenario | Update p50/p95/p99 us | Planning p50/p95/p99 us |
  | --- | ---: | ---: |
  | aligned | 132.50 / 159.54 / 170.08 | 34.62 / 49.46 / 57.83 |
  | shuffled-crossing | 125.00 / 159.25 / 173.25 | 42.83 / 58.08 / 68.29 |
  | funnel | 186.42 / 238.33 / 258.75 | 101.42 / 138.21 / 155.25 |
  | multi-gate | 160.62 / 195.17 / 210.21 | 78.79 / 92.92 / 106.21 |

  Funnel planning p99 fell from 47,532.71 us to 155.25 us (306x), and
  multi-gate fell from 9,617.96 us to 106.21 us (90.6x). The separate
  diagnostic pass reported zero touched nodes, heap pops, and neighbor
  candidates in all scenarios. Funnel performed 1,270,288 passability checks
  and reconstructed 1,274,336 nodes; multi-gate performed 1,057,152 and
  1,061,120 respectively. This is direct-segment work, not a displaced heap
  tail.
- **Browser and behavior result:** Chrome 151.0.7922.138 captured one fresh
  headless page per requested initial scenario at 1920x1080. The corrected
  frame endpoint covers synchronous measurement bookkeeping, the metrics DOM
  update, and final `requestAnimationFrame` scheduling; it inherently excludes
  its own timestamp and bounded-sample commit, and does not include
  asynchronous paint. Every scenario filled 4,096 frame samples and recorded
  zero catch-up frames:

  | Scenario | Render p99/max ms | Frame p50/p95/p99 ms | Frame max ms | Wasm linear memory |
  | --- | ---: | ---: | ---: | ---: |
  | aligned | 0.2 / 0.4 | 0.1 / 0.4 / 0.5 | 0.8 | 71.5 MiB |
  | shuffled-crossing | 0.3 / 1.2 | 0.2 / 0.6 / 0.8 | 1.7 | 71.5 MiB |
  | funnel | 0.3 / 0.4 | 0.2 / 0.7 / 2.0 | 4.7 | 71.5 MiB |
  | multi-gate | 0.2 / 0.4 | 0.1 / 0.5 / 0.6 | 0.8 | 71.5 MiB |

  The 74,973,184-byte value is the complete Wasm linear-memory allocation,
  not native RSS or model-owned heap. Removing three unused
  `PathRequestRuntime` reservations reduced the aligned fresh-page allocation
  from the separately observed 464,715,776 bytes (443.2 MiB) by 83.9%. The
  canvas remained exactly 2:1, fit horizontally, and caused no horizontal
  overflow at both 1366x768 and 1920x1080. Fixed-tick browser families had
  only 682--683 samples, so browser update p99 remained suppressed and the
  native campaign retains authority. The browser artifact preserves summarized
  output and rebuilt Wasm, loader, and app hashes, but not raw browser samples;
  its percentiles are therefore not independently recomputable. Exhaustive
  native validation matched direct compound-class
  weighted status and optimal cost for all 2,048 guided requests, checked
  every node and edge, selected-gate crossing, repeat determinism, and
  whole-map legacy/compound predicate equivalence. The 512- and 1,600-tick
  arrivals, waits, progress, and state hashes matched the reviewed acceptance
  values.
- **Advisory artifact integrity:** the timing, counter, and browser artifact
  basenames are `tess-traffic-lab-percentiles-guided.json`,
  `tess-traffic-lab-counters-guided.json`, and
  `tess-traffic-lab-browser-guided.json`. Their SHA-256 values are respectively
  `15e42cbeda92e881f0fce37ca00924c988cbef3d7946a8d80a605864f428883d`,
  `a79fa7a9057e3e09df21dc7aec9405293876c3ae97d500017644b3f8efe926ff`, and
  `a3d2937226085afd24ad37a38e3b30a2e1f190bdad78d58cf54a86050fb61bea`.
  These remain untracked machine-local evidence; the maintained record keeps
  the portable method and accepted conclusions.

## 2026-08-18 - Traffic Lab 1024×512 baseline and tail attribution

- **Hypothesis:** a 1024×512 full-map overview with 1,024 agents is a useful
  first large-grid congestion lab, while 1024² should wait for tail-latency
  evidence and a rendering design that justifies four times as many tiles.
- **Controlled workload:** the compile-time model uses an eight-search-per-
  tick FIFO. Aligned and shuffled-crossing use open terrain; funnel and
  multi-gate use the same four-column central barrier with different
  deterministic openings.
- **Timing method:** Apple Silicon macOS, Apple Clang 21.0.0, Release. Each
  scenario ran 128 fixed ticks in each of 16 fresh processes, producing 2,048
  samples. The native executable preallocated its sample buffer and serialized
  only after the measured loop. Percentiles use nearest rank; repository
  publication floors are 20/200/2,000 samples for p50/p95/p99. These numbers
  are advisory and have no CI authority.

  Update time:

  | Scenario | p50 µs | p95 µs | p99 µs | max µs |
  | --- | ---: | ---: | ---: | ---: |
  | aligned | 135.83 | 172.25 | 240.46 | 567.21 |
  | shuffled-crossing | 127.29 | 159.25 | 179.17 | 249.17 |
  | funnel | 41,607.00 | 45,918.62 | 47,724.38 | 50,977.17 |
  | multi-gate | 4,705.25 | 9,412.33 | 9,783.46 | 12,769.88 |

  The original table's 28.6 MiB "conservative resident-memory estimate" is
  withdrawn. It was a synthetic model-storage allowance that omitted reusable
  path scratch and runtime capacities, so neither "resident" nor
  "conservative" was supported. A later fresh-page capture records exact Wasm
  linear-memory allocation instead.

  Planning time:

  | Scenario | p50 µs | p95 µs | p99 µs |
  | --- | ---: | ---: | ---: |
  | aligned | 35.38 | 50.08 | 76.29 |
  | shuffled-crossing | 42.46 | 56.33 | 67.83 |
  | funnel | 41,510.04 | 45,803.42 | 47,532.71 |
  | multi-gate | 4,558.25 | 9,287.38 | 9,617.96 |

- **Counter pass:** the separately compiled `TESS_ENABLE_DIAGNOSTICS` binary
  ran one deterministic 128-tick repetition. Its wall times are deliberately
  unpublished. Aligned and shuffled-crossing stayed on the direct corridor
  path; funnel and multi-gate invoked full heap search:

  | Scenario | Touched nodes | Heap pops | Neighbor candidates |
  | --- | ---: | ---: | ---: |
  | aligned | 0 | 0 | 0 |
  | shuffled-crossing | 0 | 0 | 0 |
  | funnel | 93,545,738 | 91,648,586 | 365,931,036 |
  | multi-gate | 14,511,504 | 12,774,220 | 51,016,440 |

  | Scenario | Passability checks | Reconstructed nodes |
  | --- | ---: | ---: |
  | aligned | 1,027,072 | 1,028,096 |
  | shuffled-crossing | 1,202,572 | 1,203,596 |
  | funnel | 2,009,102 | 3,228,042 |
  | multi-gate | 1,935,988 | 2,832,980 |

- **Sampling profiles:** 2 kHz Samply captures used the `bench-profile`
  configuration (`-O3 -g -fno-omit-frame-pointer`) and repeated the aligned,
  multi-gate, and funnel scenarios 200, 20, and 3 times respectively. The
  captures contained 6,741, 27,503, and 25,949 samples. CLI symbol summaries
  attributed 97.6% of multi-gate and 99.5% of funnel leaf samples to the
  weighted-A* and regular-neighbor expansion family. Aligned attributed 19.5%
  there and 70.8% to the enclosing, partly inlined fixed-tick body. The latter
  bucket cannot separate movement from bookkeeping, so no narrower aligned
  cause is claimed.
- **Browser pass:** one Chrome session at a 1,665×984 CSS-pixel viewport
  captured the first samples after each reset; buffers stop at 4,096 rather
  than overwriting startup work. The historical frame endpoint preceded
  measurement bookkeeping, the metrics DOM update, and final animation-frame
  scheduling. These values therefore cover only partial synchronous callback
  work and are retained as baseline history, not as full-callback evidence.
  They do not include asynchronous paint completion. Render and partial-frame
  timing are milliseconds:

  | Scenario | Frames | Render p99/max | Partial frame p50/p95/p99 | Partial frame max | Catch-up frames |
  | --- | ---: | ---: | ---: | ---: | ---: |
  | aligned | 4,096 | 1.0 / 7.3 | 0.5 / 1.2 / 1.7 | 7.5 | 0 |
  | shuffled-crossing | 3,269 | 0.8 / 4.1 | 0.4 / 1.2 / 1.6 | 4.3 | 0 |
  | funnel | 2,885 | 0.9 / 5.5 | 0.5 / 1.4 / 49.4 | 1,287.3 | 26 |
  | multi-gate | 2,778 | 1.3 / 6.1 | 0.6 / 3.0 / 43.1 | 73.4 | 0 |

  Each browser frame family clears the 2,000-sample p99 floor. Browser update
  families contain only 358–514 single-fixed-tick calls, so their p99 values
  remain suppressed; the native campaign owns fixed-tick tail claims. Funnel's
  26 catch-up frames explain why its callback maximum is far above p99.
- **Result:** funnel tail cost is algorithmic search work, not rendering or
  movement bookkeeping: planning accounts for almost its entire update, its
  p99 is 47.5 ms, and its observed maximum update slightly exceeds the 50 ms
  simulation period. Multi-gate has a 9.8 ms update p99. All timing runs kept
  the eight-search ceiling, and deterministic counter runs explain the
  scenario difference without mixing instrumented wall time into the result.
  Browser rendering itself stays below 1.3 ms at p99. The historical partial
  frame values are insufficient to decide full animation-callback budget.
  Funnel entered a catch-up cascade with an observed 1.29 s partial sample.
- **Decision:** retain 1024×512 and the full-map overview for version one. Do
  not introduce 1024², zoom/pan, or a more complex renderer. Keep timing
  advisory; deterministic scenario and search-budget checks remain blocking.
  The browser exposes a bounded capture only under `?measure=1`, while normal
  previews retain live exponential averages.
- **Limitations and reconsideration:** native samples pool deterministic tick
  positions across fresh processes on one host; they are not a calibrated
  cross-machine threshold. Samply identifies hot call paths but not why the
  CPU executes them at a given rate, and its measurements are not benchmark
  timings. Browser figures come from one foreground session and are not paint
  completion or a cross-device calibration; timer resolution produces visible
  quantization. Their partial frame endpoint is superseded by the corrected
  guided capture. Reconsider 1024² only after a concrete experiment needs the
  extra area and both native planning tails and browser frame tails fit their
  stated budgets.

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

## 2026-08-18 - Request-scoped memo for chunk-portal seam queries

- Area: `detail::select_chunk_portal_waypoints` and the two
  `best_chunk_portal` call sites beneath it. Accepted.
- Evidence for the target: a Steam Deck profile of
  `path/agent_tick_100_weighted_goal_churn_portal_512x512` put
  `best_chunk_portal` at 68.4-69.1% of sampled user-cycle self share
  across three captures, and an offline census of every call in two
  portal cells measured 67.13% and 66.68% of calls repeating a key
  already answered in the same tick. Cost-weighted rather than
  call-counted, those rates are 66.73% and 65.14% net of instrument, so
  duplicates are marginally cheaper than average but not materially.
- Shape of the duplication: every `(tick, seam)` pair carries exactly
  one distinct `(current, goal)` query, and each is visited by 3.04
  candidate routes with zero same-route repeats. The redundancy is
  therefore cross-candidate reuse of the same seam under the same
  query, never the same seam under a different query.
- Result, paired on device, main against branch, alternating rounds
  with bootstrap intervals: the repeated-goal portal cell falls 30,415
  to 19,045 ns (-37.3% [-37.5, -37.1]) and the fresh-goal portal cell
  134,296 to 125,585 ns (-6.4% [-7.2, -4.8]).
- Capacity: 128 and 256 entries measure the same within overlapping
  intervals (-36.8% and -37.3% on the repeated cell). 256 was chosen for
  headroom rather than speed: the measured maximum live entry count is
  74 per selection, and entry count scales with chunk distance, so the
  larger table saturates only on routes about three times longer. The
  packed entry is 32 bytes, so the thread-local table is 8 KB.
- Design: the key is `(tile index of current, signed six-way step)`. The
  goal is omitted because it is invariant across one selection; `from`
  is omitted because it is the chunk containing `current`, and a caller
  passing an inconsistent pair bypasses the memo rather than colliding.
  A generation stamp retires every entry when a selection begins, and an
  RAII scope makes a nested selection — reachable through a
  user-supplied passability predicate — bypass the memo instead of
  sharing its generation. Saturation falls back to the uncached call and
  is sticky, so a saturated selection does not re-walk the table on
  every later miss.
- Rejected along the way: a compile-time flag, because a macro that
  changes inline definitions in a header-only library is an ODR hazard
  and would leave the gated path untested; a `PathRuntimeCachePolicy`
  field, because the direct portal builders never receive one and the
  memo retains nothing across calls for a caller to reason about; and a
  hit-rate guard, because it would disable the memo during exactly the
  cold phase whose entries later candidates reuse.
- Mechanism confirmed on the merged binary, not inferred from the
  prototype. Scratch counters compiled in for one run reported 456,750
  memoized calls and 306,600 hits on the profiled cell - 67.13%, the
  census figure to the digit - and zero calls taking the non-keyable
  bypass across 46.6k calls in a second cell, so the
  `from == chunk_coord(current)` invariant holds everywhere reachable
  and nothing silently skips the memo. Independently, the public
  `portal_scan_tiles()` counter on
  `path/weighted_chunk_portal_product_room_portals_512x512` falls 7,456
  to 3,328 (-55.4%) between main and the branch; that cell's topology
  differs from the profiled cells, so its rate differs from 67% as
  expected. The scratch counters were removed before commit.
- Why the merged shape wins less than the prototype did (-37.3% against
  -43.2%): the hit rate is identical, so the difference is the
  consistency guard and the tile-index conversions the merged version
  adds, not fewer hits. The guard costs a `chunk_coord` per call and
  has never once rejected; it is kept because it converts a
  load-bearing assumption into an enforced property.
- The consistency guard was measured rather than argued about. Three
  shapes ran paired on device against each other: the guard as written,
  the guard moved onto the hit path with `from` stored in the entry and
  compared there, and the guard compiled out of release builds.
  Moving it to the hit path is 1.9% SLOWER on the repeated cell
  ([+1.7, +2.3]) - hits are two thirds of calls, so the extra compare
  lands on the majority path and the entry grows 32 to 40 bytes -
  and compiling it out of release measures -0.0% ([-0.3, +0.4]) and
  +0.1% ([-1.7, +0.9]), which is nothing. So the guard is free on the
  target hardware and stays in release: there is no cost to recover by
  weakening it. An earlier 2.3% figure from an unpaired host run was
  noise, as its overlapping variation suggested.
- Worth recording about the measurement: the exact-strategy control cell
  moves -1.2%, and the memo provably never runs there. That residue is a
  codegen and layout confound from the added code, so the portal figures
  contain an unquantified component of the same effect.
- Artifacts: `portal-tick-profile-2026-08-18`,
  `portal-redundancy-census-2026-08-18`,
  `portal-cost-weighted-2026-08-18`, `portal-cache-prototype`.

## 2026-08-18 - Pathfinding strategy comparison

- Hypothesis: source-backed teaching examples plus paired benchmark evidence
  can explain when route caches, weighted batches, and distance fields repay
  their lifecycle cost without implying that any strategy is universally
  fastest.
- Method: Release Google Benchmark CPU time on one Apple M3 Max, single
  threaded, with ten repetitions and a minimum one-second sample per
  repetition. Each pair used the same world and request array. The run could
  not pin thread affinity, reported a load average around 4.0, and therefore
  remains informational rather than a portable threshold.
- Shared-goal result: 100 independent unit-cost A* requests took a median
  17.80 ms; one distance-field build plus 100 reconstructions took 2.78 ms,
  about 6.4x faster.
- Exact-repeat result: 100 independent A* requests took 48.88 ms; the exact
  route cache took 14.52 ms with 70 hits and 30 misses, about 3.4x faster.
- Suffix result: 100 independent A* requests took 113.08 us; the route cache
  took 17.41 us with one miss and 99 suffix hits, about 6.5x faster.
- Weighted-batch result: 100 independent weighted A* requests across eight
  goals took 441.16 ms; the planner built eight fields with no A* fallbacks
  and took 42.29 ms, about 10.4x faster.
- Decision: publish the measurements as machine-labelled workload evidence
  alongside the source-synchronized comparison. Keep API selection conditional
  on measured reuse, and make no benchmark threshold or implementation change.

## 2026-08-18 - Endpoint guard narrowed to substantial barriers

- Premise: the optional browser policy should spread routes around interior
  congestion while retaining the known safety fallback for a dense one-sided
  barrier immediately before an endpoint band.
- Reproduction: replay the checked-in `browser-guard` native scenario, whose
  297 wall coordinates preserve the user-drawn browser fixture, with 1,024
  agents in canonical and spread modes. Seven wall tiles crossed the protected
  approach zones: five along one horizontal wall on the left and two along
  another on the right.
- Finding: the original any-tile guard silently disabled the option for the
  entire leg. Canonical and checked spread modes were identical at 3,277 ticks,
  388,436 routed waits, 1,458 low-progress ticks, and zero seed waves. Both
  still reached all 1,024 goals, so terminal outcome alone hid the policy
  suppression.
- Controlled probe: retain every wall but bypass only the global endpoint
  veto. One normal seed wave then completed all 1,024 agents in 455 ticks with
  25,166 waits, no crowd-blocked or unreachable agents, and no low-progress
  ticks. The existing one-shot merge detector observed 22 merge tiles and
  correctly scheduled no second wave.
- Change: count accepted construction tiles in each eight-column approach zone
  and suppress spreading only when either zone contains at least 64 tiles,
  half the map height. This keeps the mechanism demo-local and additive; it
  does not infer portals, change passability, or alter movement authority.
- Verification: the central two-gate native fixture now includes sparse wall
  touches in both approach zones and must still schedule exactly one seed wave.
  Direct 63/64 boundary checks include duplicate submissions. The existing
  96-tile goal-wall control remains canonical with zero seed waves, and
  maximum-scale terminal checks remain authoritative.
- Decision and limit: accept the narrower guard. The 64-tile threshold
  distinguishes the two measured geometries without claiming a general
  endpoint-capacity proof. Reconsider it only with a failing deterministic
  endpoint fixture; compare terminal outcomes before optimizing tick or wait
  counts.

## 2026-08-18 - Endpoint cross-cuts and stable-topology seeding accepted

- Area and contract: the browser colony's tutorial-owned endpoint placement
  and optional congestion response. Every supported slider population must
  retain a correctly classified terminal outcome, and an incrementally drawn
  wall must receive the same kind of bounded response as its batch equivalent.
- Root cause: the eight goal-free vertical aisles were not cross-cuts through
  their adjacent populated columns. At 896 agents, the 128-agent cohort for
  away column 113 settled first and sealed that full height at tick 409. Three
  spread-routed agents targeting column 115 remained at column 112 and were
  classified crowd-blocked. At 864 agents, column 113 held only 96 goals and
  never sealed. At 1,024, the added column-111 cohort delayed column-113
  closure to tick 439; the last observed affected agent crossed by tick 430.
  Canonical routing failed differently at 896: two agents became trapped in
  one-column pockets between already settled populated columns.
- Scale evidence before the change: the checked-in `browser-guard` replay
  reached 894 plus two crowd-blocked agents canonically and 893 plus three
  crowd-blocked with spreading at 896. At 928 the outcomes were 927+1 and
  925+3; at 960 they were 959+1 and 958+2. Both modes happened to reach all
  agents at 1,024, so a maximum-only test concealed the non-monotonic defect.
- Endpoint change: relocate only the row-64 agent from each of the eight dense
  endpoint columns into the unused sparse outer column at rows 56 through 63.
  Row 64 is then a shared horizontal cross-cut through every dense column and
  through the sparse column. Goals remain unique and every open-terrain leg
  remains 109 steps. A native structural oracle settles every other goal and
  proves that a delayed agent can still reach either endpoint.
- Endpoint alternatives rejected: alternating 16-agent rows made endpoint
  closure impossible but regressed the browser replay to 731 ticks. Placing
  the eight sparse goals at rows 64 through 71 blocked the cross-cut's outer
  continuation and lost two agents from populations 336 through 448. Spacing
  those goals across the full height passed the terminal sweep but regressed
  the 1,024-agent wall tip to 2,040 ticks. Rows 56 through 63 preserved the
  cross-cut without scattering the convoy lanes.
- Interactive root cause and change: topology edits already canceled an
  in-flight seed but left the leg's one-shot eligibility consumed. The first
  incremental probe exposed this but silently skipped six occupied wall tiles,
  so its 2,042-tick spread and 2,083-tick canonical counts describe only 291
  accepted walls and are not acceptance evidence. The corrected runner admits
  up to four walls per tick and retries an occupied coordinate in order. With
  all 297 walls and the cross-cut layout, the old one-shot behavior took 2,986
  ticks and 535,127 waits, close to the 3,172-tick, 564,929-wait canonical
  control. Each topology edit now resets seed eligibility and records its
  schedule tick; a new congestion seed may start after eight edit-free ticks.
  Waiting for all canonical work to drain was rejected because it regressed
  the two-gate control to 1,469 ticks. The idle-only gate keeps work bounded by
  the existing eight-query budget.
- Results after both changes, 1,024 agents: open travel completed in 236 ticks
  and 1,317 waits with no seed; wall tip in 1,323 ticks and 290,749 waits; two
  gates in 792 ticks and 62,849 waits; four gates in 600 ticks and 26,165
  waits with no seed; and the batch browser replay in 471 ticks and 28,079
  waits. The guarded goal wall completed canonically in 1,004 ticks and
  229,359 waits. The exact-topology incremental replay completed in 801 ticks
  and 78,557 waits with one seed and all 297 walls accepted. Every case reached
  all agents with no crowd-blocked or unreachable outcome.
- Scale verification: run `tess_web_colony_model --scenario browser-guard
  --agents N --mode spread --max-ticks 1000 --require-complete` for every
  `N` from 16 through 1,024 in steps of 16. All 64 supported populations
  completed. The same 64-population sweep completed canonically. CI retains
  canonical and spread 896 controls, the structural delayed-agent oracle, and
  a 1,024-agent incremental replay that requires all 297 wall admissions.
- Decision and limits: accept both demo-local changes. The cross-cut fixes a
  proven endpoint-layout defect; it is not a general multi-agent pathfinding
  policy. The incremental fixture preserves the final coordinate set and
  coordinate order with up to four acceptances per tick; occupied coordinates
  delay later admissions, so it does not reproduce original pointer timing. A
  user who pauses longer than the idle window and resumes drawing can
  legitimately cause another bounded wave; topology cancellation and the
  per-tick query cap remain authoritative.
  This supersedes only the known full-column limitation in the earlier aisled
  endpoint record; its routing-policy limits still apply.
