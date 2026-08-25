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
  `weighted_shared_dirty`'s confirmation at +0.3% (CI +0.06% to +0.55%). Both
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
