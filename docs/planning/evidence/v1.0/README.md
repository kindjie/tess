# Phase 2 prototype queue: synthesis (PR X3)

The disposition ledger for the v0.13-to-v1.0 execution plan's bounded
prototype queue. Every row names its evidence record, outcome,
limitations, and reconsideration condition; the classification column
uses the plan's vocabulary (private optimization / supported behavior /
public API / deferred research / rejected mechanism). Pre-registration
issues: #240 (P2), #241 (C2), #247 (C3), #249 (P3), #251 (P4), #253
(C4), #255 (P5), #256 (C5), plus #242 (X1 no-run).

## Headline

No prototype earned a library change. The queue produced three
permanent regression/oracle suites (the C0 substrate, the C3
reciprocal-conflict fixtures, the C4 escalation gates), two documented
caller recipes (C2 dispatch, C5 congestion pricing -- the latter
validated at full demo coverage on both platforms), one
proven-but-unpromoted mechanism (C4 escalation), and calibration rules
for future screens. The library's existing surfaces expressed every
accepted behavior without new authority.

## Ledger

| candidate | evidence | outcome | limitations | reconsider when |
|---|---|---|---|---|
| P1 portal seam index | `p1-portal-seam/` | REJECTED (rejected mechanism): measured ceiling -- an idealized seam-index stand-in bounded the attainable win below the bar on both platforms (A/A-calibrated, counter-identity verified). | Ceiling measured against the post-#213 memo baseline. | Portal-selection call profile changes materially (e.g. memo removed). |
| P2 resumable A* | `p2-resumable/` | REJECTED (rejected mechanism): +28.4% best-case added work vs a 10% bar; semantics fully proven (identity, cancellation, chunk-granular staleness, residency-corruption refusal). | Scheduling value only; semantics were never the failure. | An accepted incremental-replanning shape supplies a resume-friendly state layout (P5 did not). |
| P3 gated JPS | `p3-jps/` | REJECTED (rejected mechanism): rubble_256 +109% wall time despite -90% structured-map wins; platform-existential rule applied. | 4-connected, unit-cost domain only. | A gate that excludes rubble-like density reliably at negligible cost. |
| P4 bidirectional A* | `p4-bidir/` | REJECTED (rejected mechanism): symmetric-heuristic screen failure. | Screen-level rejection; no timing stage reached. | A consistent bidirectional termination bound for this heuristic family. |
| P5 D* Lite | `p5-dstar/` | REJECTED at stage 1 (rejected mechanism): symmetric per-neighbor accounting gives work ratio 1.012 vs the 1.5 bar (the first capture's 1.913 "pass" was a counter artifact caught in review; amendment 1 on #255 registered the correction before the rerun). M3 wall-time capture retained as context: 11 of its 12 defect-free uniform cells are confirmed material regressions. | Goal-keyed, unit-cost, dense 2D domain; stage 2 not rerun per the stop condition. | An incumbent whose per-op cost is comparable to a repair heap's, or a repair design with a >= 10x symmetric work ratio. |
| P6 priority-queue retry | -- | NOT OPENED: P2-P5 merged no new open-set structure; the prior four-ary-heap rejection stands. | -- | A structurally different open set merging. |
| C0 movement substrate | merged #245 | MERGED (supported behavior, test-tier): pool-mode fixtures, closed-formula seeds, digest pinning -- the stream's measurement bed. | Test support, not public API. | -- |
| C1 hindrance tie-break | `c1-hindrance/` | REJECTED (rejected mechanism): 22 agent-level classification regressions vs 13 improvements; the pre-registered stop condition fired before timing. | Colony's 60% seed-exclusion rate made its tick metric uninterpretable by the declared rule. | A tie-break provably classification-neutral on the pinned substrate. |
| C2 fungible goals | `c2-fungible/` | REJECTED as library authority (rejected mechanism); the caller dispatch recipe is retained (supported behavior, documentation-tier) at 47% of the measured win with zero API. | Recipe requires caller-side anonymous goal pools. | Evidence the recipe's residual 53% matters to a real consumer. |
| C3 reciprocal conflicts | merged #248 | MERGED (supported behavior, test-tier): joint-space BFS oracle + fixtures; the production tier provably fails three conflict classes; opened C4. | Oracle bounded to small components. | -- |
| C4 conflict-local escalation | merged #254 | PARTIAL (deferred research + test-tier mechanism): completion-planning escalation resolves all three C3 classes within bound; always-on arming DECLINED -- 2 of 61 residual seeds worsen one agent under trajectory divergence. Phase B (authority) not proposed. | Pareto per-agent safety unprovable under trajectory divergence; per-tick BFS probes. | Bounded-divergence intervention protocol, or an explicit quality-delta contract; incremental reachability for the seal probe. |
| C5 congestion pricing | `c5-congestion/` | RETAINED as a documented caller recipe (supported behavior, documentation-tier) at full supported coverage: 7 scenarios x 64 populations = 448 cells, classification retained or improved everywhere on BOTH platforms (byte-identical tables), canonical's own 41 arrival-incomplete tip cells all complete priced, value rule gm 0.4180 CI [0.3859, 0.4522]. | Outcome-level only (contention never instrumented); goal-wall regresses gm 1.49 (to +89%) with classification intact; 17/132 fixpoint-substrate seeds reclassify chaotically. | A consumer needing seed-stable fixpoint classification AND congestion relief simultaneously. |
| C6 crossing reservation | -- | NOT OPENED: no MECHANISM-level capacity premise was isolated (C5's wins are outcome-level), so nothing exists for a bounded reservation to represent. | -- | Instrumented contention evidence (waits, gate utilization) isolating a premise the C5 recipe cannot serve. |
| X1 execution-delay harness | #242 | NOT RUN: dispositioned on harness-scope grounds. | -- | The plan's own reopening clause. |
| X2 authority boundaries | merged #262 | MERGED (decision record): fixed-horizon claim scoped to a policy boundary in review. | -- | -- |

## Confirmations (plan checklist)

- Accepted implementations and maintained-doc updates landed: C0 (#245),
  C3 (#248), C4 (#254), C5 recipe documentation
  (`architecture/spatial-coordination.md`, #257/#264), C2 recipe
  (`c2-fungible/`), X2 fragment (#262). Nothing else was accepted.
- Fragment assembly previewed at this commit: 19 fragments valid
  (`tools/assemble_changelog.py --check`); the assembled optimization
  log projects well under the 24,000-token file limit, so no archive
  maintenance is required at assembly time.
- Roadmap updated from outcomes (see `docs/roadmap.md`); the historical
  survey remains historical.

## Calibration rules recorded for future screens

1. Count both arms with the same ruler; then weight op-count
   feasibility bars by measured per-op cost (P5: an asymmetric-counter
   1.9x "pass" cost two further measurement stages to un-earn).
2. Locally-exact interventions perturb global trajectories (C4, C5):
   per-agent Pareto gates over chaotic dynamics fail on marginal seeds
   even when aggregates improve; gate on the surface the plan pins.
3. The platform-existential rule (one confirmed material regression
   settles rejection) closed P3/P5 without second-platform spend.
4. Registration before data, amendments flagged when review-driven:
   every review round in this stream invalidated a verdict-carrying
   element (wrong judge, insufficient coverage, asymmetric counters,
   unchecked gates), and each correction was registered before its
   rerun.
