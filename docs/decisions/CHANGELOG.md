# Design Changelog

Records meaningful design changes from the original TDDs. Entries from
2026-07-28 through 2026-08-19 are in
[`CHANGELOG-archive-2026-07-29-08-19.md`](CHANGELOG-archive-2026-07-29-08-19.md);
entries from 2026-07-11 through 2026-07-28 are in
[`CHANGELOG-archive-2026-07-11-28.md`](CHANGELOG-archive-2026-07-11-28.md);
entries from 2026-07-09 through 2026-07-10 are in
[`CHANGELOG-archive-2026-07-09-10.md`](CHANGELOG-archive-2026-07-09-10.md);
older entries are in [`CHANGELOG-archive.md`](CHANGELOG-archive.md) and
[`CHANGELOG-archive-2026-06.md`](CHANGELOG-archive-2026-06.md).

## 2026-08-24 - Phase 2 prototype queue closed: no new library authority

The v0.13-to-v1.0 execution plan's bounded prototype queue (P1-P6,
C0-C6, X1-X3) is fully dispositioned; the ledger with limitations and
reconsideration conditions is `docs/planning/evidence/v1.0/README.md`.
Every rejection and retention is evidence-backed under pre-registered
gates, with review-driven corrections registered before their reruns.

The decision this closes: v1.0 ships with NO new pathfinding, movement,
or execution authority from this stream. What the stream produced
instead -- three permanent regression/oracle suites (C0 substrate, C3
reciprocal-conflict fixtures, C4 escalation gates), two documented
caller recipes (C2 dispatch, C5 congestion pricing at full demo
coverage on both platforms), and one proven-but-unpromoted mechanism
(C4, arming declined on trajectory-divergence evidence) -- binds any
successor: the pinned gates are the bar a future candidate must
consciously flip, and the recorded calibration rules (symmetric
counters, per-op-cost-weighted feasibility bars, the
platform-existential rejection rule) govern how such a candidate is
screened.

## 2026-08-24 - Platform-existential rejection is the recorded rule

The execution plan's cross-hardware section said "a performance-only
rejection is not final until both platforms run" while its own decision
rule rejects when EITHER platform materially regresses -- so a
confirmed material regression on one platform is decision-complete and
the second leg cannot change the verdict. The P4 and P5
pre-registrations declared this refinement before their data
("platform-existential rule"), their reviews accepted it, and the
screens applied it; P3 applied it as a recorded post-data deviation
derived from its pre-registered accept bar, noted on its issue. A
pre-RC audit flagged the surviving contradiction
in the plan text; this decision records the reconciliation instead of
leaving the letter and the practice in conflict: acceptance always
requires both platforms; a rejection is settled by one CONFIRMED
material regression (A/A-calibrated thresholds plus the paired tool's
confirmation rerun) on either platform. Records must continue to state
explicitly when a second leg was skipped under this rule.

## 2026-08-24 - Authority-boundary dispositions (PR X2)

The execution plan reserved one record for capability boundaries the
prototype queue deliberately did not cross, so their absence from v1.0
reads as a decision rather than an omission. Recorded without
speculative implementation:

**Clearance and radius products remain post-1.0.** Capability-specific
topology (per-radius passability, clearance-aware transitions) changes
what a cached field product means, what invalidates it, and what a
movement class is: every consumer of `(chunk_key, content_version)`
dependencies would need a per-capability axis, and the movement-class
algebra would need per-capability composition rules. Nothing in the
v1.0 consumer surface requires it, and the cost lands on the cache and
invalidation contracts that stabilized in v0.12-v0.13.

**Theta\* (any-angle smoothing) stays deferred.** It requires
line-of-sight primitives, a smoothing contract over returned routes,
route-validity semantics under smoothing (what "contiguous face steps"
becomes), and timing evidence -- none of which exist as contracts. The
P-queue's screens also showed the incumbent's fast paths already serve
axis-aligned and detour geometry cheaply; an any-angle surface is a new
public contract, not an optimization.

**Fixed-horizon WHCA remains superseded by the conflict-local gate.**
C3 built the reciprocal-conflict fixtures and proved the production
tier's three failure classes; C4 Phase A resolved all three with
completion-planning escalation bounded by component, never by horizon
-- the queued-yields fixture exists precisely because bounded-horizon
candidates hide exactly that failure mode. The C3 pins and C4's merged
harness are the standing gate any temporal-planning candidate must
pass. That gate is a policy boundary, not an impossibility proof: the
retained evidence rules out candidates that plan only over the
immediately live conflict (the seal forms outside their window), and
C4 tested its completion planner, not every fixed-horizon design -- a
horizon long enough to cover the fixture (oracle-optimal settle is 15
ticks) or a receding-horizon variant remains untested and would be
judged by the same pins.

**Movement-tier planning authority was evaluated and declined on
evidence.** C4's two-phase structure separated mechanism from authority
grant; Phase A's substrate measurement (2 of 61 residual seeds worsen
one agent under trajectory divergence, against an aggregate
improvement) decided against always-on arming, so no public planning
authority ships in v1.0. The promotion blockers are recorded in the C4
evidence: bounded trajectory divergence (or an explicit quality-delta
contract) and incremental reachability for seal prediction.

**The execution-delay harness (X1) was dispositioned without a run**
on harness-scope grounds -- an existing authority boundary, recorded
in its own fragment (2026-08-22) and the synthesis ledger; it is
listed here because the plan carries X1 into this record.

## 2026-08-23 - Movement-tier planning authority declined after Phase A evidence

PR C4's two-phase structure separated building the conflict-local
escalation mechanism (Phase A, harness-level, issue #253) from granting
the movement tier planning authority through a public API (Phase B).
Phase A's evidence decided Phase B: the mechanism resolves every
motivating reciprocal-conflict class within the pre-registered bound
and is strictly inert wherever the production tier succeeds, but
per-agent non-worsening fails on 2 of 61 residual substrate seeds --
locally exact interventions still diverge global trajectories -- so
always-on arming was declined and no public authority is granted. The
production movement tier is unchanged; the C3 regression pins that
document its reciprocal-conflict failures remain in force. Any future
promotion starts from the two recorded blockers: bounding trajectory
divergence (or replacing Pareto safety with an accepted quality-delta
contract) and replacing per-tick reachability probes with incremental
reachability.

## 2026-08-22 - Movement experiments share one fixture substrate

The v0.13-to-v1.0 execution plan's movement stream named five fixture
families — warehouse, ring, colony, random, adversarial — as if they were
existing infrastructure. They were not in the tree. `warehouse` and
`adversarial` appeared in no source file, the PIBT tier's in-repo evidence was
one pinned ring seed with a test-local breadth-first helper, and the screening
harness that produced that tier's gate evidence lived outside the repository
and no longer exists, as its own study records under limitations.

Three queued experiments name those families: the PIBT hindrance tie-break,
the reciprocal-conflict screen, and the execution-delay harness. Each could
have narrowed itself to whatever happened to exist, but three separately
narrowed fixture sets produce three results that cannot be compared, which is
the one thing a serialized stream exists to prevent.

The substrate is therefore built once, before any arm exists, and the plan
gains a preceding PR. Four decisions inside it are load-bearing:

Seeds are a closed formula over a trial index with pre-registered trial
counts, not a curated list. A list would prove commit order rather than
knowledge order, since the same author controls when the fixtures land and
could have run an arm privately first. A formula leaves no per-seed freedom,
and any later exclusion must be recorded with its cause.

Every option that can censor a result is owned by the fixture rather than the
experiment. `SwapPolicy` changes what "no mover could succeed" means. The knob
that can manufacture seals outright is `blocked_exhaustion_policy`: under
`MarkUnreachable` an exhausted agent settles, and a settled agent can cut
another agent's goal off. It is pinned to `RemainBlocked` rather than
inherited, so exhaustion never settles anyone. Under that policy
`max_blocked_retries` still matters, because exhaustion stops an agent
replanning, so it is pinned large as well.

A fourth option turned out to belong in the same category and was missed at
first. Settling changes passability for the movement class, so retained routes
crossing a newly settled tile go stale, and a stale-routed agent parks rather
than replanning. Left unhandled, the harness manufactured its own deadlocks:
354 of 960 agents on the ring family classified as wedged, on the lattice
where this tier's own pinned regression solves the entire population. The
substrate therefore invalidates routes on every settled-set change, and that
choice is fixture-owned for the same reason the others are. A regression test
pins the ring family at zero wedges.

Runs terminate on a no-progress fixpoint, with the tick cap demoted to a
safety bound. Classifying at a cap is invalid because the terminal set grows
monotonically and is final only at quiescence; under a shared cap an agent
still making progress would be recorded as a mover failure when in fact the
experiment stopped, which biases residual counts against slower arms.

Classification carries five categories rather than the three the prior
evidence used. A live mutual wedge is invisible to a reachability test under
the terminal set — neither wedged agent is terminal, so both read as ordinary
residuals — and that category is precisely the deadlock signal the
execution-delay harness needs. A goal tile held by a terminal agent is an
assignment failure that fungible-goal work would fix by reassignment, and is
kept separate from a corridor seal, which no reassignment helps.

Two claims made while drafting are recorded as withdrawn rather than dropped.
Reusing the ring lattice shares the terrain but does not make results
numerically comparable to the pinned ring regression, which ranks with
per-agent exact breadth-first tables rather than the route-attachment oracle
the substrate pins. And the pinned ring seed does not demonstrate neutrality:
it was selected because it discriminates one movement tier from another. It
remains admissible for the hindrance axis only because it was not selected on
hindrance.

The congestion revalidation experiment is explicitly not a consumer. Its
terminal-classification requirement is judged by the web_colony demo's own
recovery classifier, whose vocabulary is distinct and stays distinct.

## 2026-08-22 - Execution-delay harness dispositioned without a run

The v0.13-to-v1.0 queue listed a private harness that would inject delayed and
uncertain action completion, compare three execution models, and decide whether
a new execution contract was justified. It is closed without being built.

Two findings closed it, and both are about what the harness could measure
rather than about the idea being uninteresting.

The library does not assume same-tick completion in any way a caller is bound
by. Occupancy and reservation are caller-owned world fields, and both movement
tiers treat an externally set occupancy bit as an immovable do-not-enter, so a
caller that wants to animate a move across several ticks already has the
primitive: commit, then hold the tiles. That recipe uses only documented
fields. The execution layer is already the caller's, on the same boundary the
earlier local-movement study drew for assignment.

The one capability an in-tier mechanism could add is suppressing same-tick
chain admission into a held origin. Both tiers admit a follower into a vacated
origin inside the advance call and expose no option to prevent it, while a
harness can only re-mark occupancy after the call returns. The arm representing
held tiles is therefore permanently one tick more permissive than the model it
names. That bias is not neutral: it points toward "held tiles cost little",
which is the conclusion the experiment would have reported.

A second defect made the intended comparison empty. The movement tier
reconsiders every active agent every pass, so an agent blocked on a held tile
already retries and is admitted on the first tick after release. An explicit
dependency-edge arm would have differed from a held-tile arm only in whether
the harness applied releases before or after the advance call — a loop-ordering
convention rather than a mechanism. A flat result would have confirmed the
pre-registered expectation without testing it, which is the failure mode the
screening study named when it warned that an exact zero across variants is a
wiring defect until instrumentation proves otherwise.

Related and worth recording: permanent deadlock cannot be caused by held tiles,
because at a fixpoint nothing is in flight and no tiles are held. The category
the harness was built to populate is structurally near-empty.

The pre-RC gate is a recorded disposition rather than a forced production
change, and this is one. Reconsider if a tier option ever suppresses chain
admission into a designated tile, or if a consumer reports a real failure under
the commit-at-decision recipe.

## 2026-08-21 - Publish versioned documentation trees

- Adopted mike for versioned documentation. The site publishes `/latest/`,
  a per-release `/<major>.<minor>/` archive, and `/dev/` from `main`, with
  Material's version selector reading mike's `versions.json`. Before this,
  one site was continuously deployed from `main` while describing itself as
  the current release, so development changes were presented as released
  documentation.
- Rejected keeping released documentation at flat root paths with only a
  `/dev/` prefix added. mike cannot serve real content from the site root —
  every option either nests content under a version or emits a redirect — so
  that layout would have required a bespoke two-tree build rather than the
  supported tool. The migration cost was one-time and internal: 51 documented
  links moved under `/latest/`, and no published release note contained one.
- Aliases are copies rather than mike's default symlinks. GitHub Pages does
  not serve symlinked directories, and `latest` is where every documented
  link resolves, so a symlink alias would return 404 for the entire released
  site.
- The generated API reference and WebAssembly demos are staged inside `docs/`
  before deploy. mike runs mkdocs itself and has no prebuilt-directory mode,
  so content that must appear inside a version tree has to be somewhere
  mkdocs copies; both remain generated and gitignored.
- `gh-pages` is storage rather than the served branch. The workflow commits
  each version tree there and then uploads the whole branch as the Pages
  artifact, so the Pages source stays GitHub Actions and the custom domain
  keeps working from repository settings. Serving the branch directly would
  have required a repository settings change and a tracked `CNAME`, and would
  have broken the site if the two were not switched together.
- The initial `0.13` tree was bootstrapped from the adoption branch rather
  than the `v0.13.0` tag, because dispatching against the tag would run the
  tag's workflow, which has no publish job. Its site content differs from the
  tag only by documents the site excludes and by the link migration itself.
  From the next release onward, a tag push publishes exactly the tagged
  content.
- Only the publish job carries a write token, and its checkout does not
  persist credentials; it authorizes its own remote commands instead. The
  build job that runs the container and browser steps stays read-only.

## 2026-08-21 - Scope discrepancy experiments and name the RC-1 evaluation

- Recorded a discrepancy-aware allocation experiment program outside the
  pre-1.0 prototype queue. It does not gate `v1.0.0`, adds nothing to the
  bounded queue, and yields to the execution plan whenever the two contend
  for serialized controlled-hardware time. Its ordering is set by feasibility
  against this codebase and the available machines rather than by the order
  its source proposed.
- Demoted accumulated discrepancy from success criterion to diagnostic.
  A policy that minimizes a quantity cannot be judged primarily on that
  quantity; the same rule now applies to balance for a partitioner and to hit
  rate for a cache policy. Primary endpoints must be independent of the
  mechanism under test, and every accepted result carries a wrong-signal
  control.
- Required an offline counterfactual replay over retained artifacts before
  any selection-policy implementation. Where the incumbent order already
  shows tight staleness and deadline distributions, the program is discarded
  without new runtime machinery.
- Classified multi-domain server hardware as a best-effort target, secondary
  before 1.0 and open to promotion after it. It sits outside the
  cross-hardware decision rule, gains no calibrated gate or support-policy
  commitment, carries single-platform evidence labelled as such, and may
  neither pessimize nor destabilize the two supported platforms. Determinism
  is not relaxed for it.
- Deferred device-placement experiments because no operation currently has
  both a CPU and a GPU implementation between which placement could be a
  runtime choice. The reason is the empty operation set, not the memory
  architecture of the available machines.
- Recorded that the RC-1 downstream evaluation will use a co-developed
  reference consumer rather than an independent one, that the relationship
  belongs in the evidence so the result is not mistaken for independent
  validation, and that findings only an unfamiliar reader can produce fall
  outside its reach. The gate is judged on coverage of the listed surfaces,
  which currently omit pathfinding and path invalidation.

## 2026-08-21 - Sequence central package registries around 1.0

- Deferred a curated vcpkg port until general availability. vcpkg's maintainer
  guide admits a project to the curated registry only once it has a release at
  least six months old, six months of active public development, or equivalent
  demonstrated maturity. This repository's first release published
  2026-07-18, so the earliest qualifying date is around 2027-01-18, which is
  close to the projected GA window. This reinforces rather than changes the
  standing decision that the in-tree overlay stays checkout-based through GA.
- Recorded a naming risk to settle before that port is authored. vcpkg's
  distinctive-name policy treats short, common-word names that do not lead to
  the project in a search engine as ambiguous, and its documented remedy is an
  owner prefix. `tess` is short, is a common given name, and collides with an
  established OCR project's usual abbreviation, so expect a rename request.
- Selected Conan Center as the near-term registry. Its documented requirements
  cover recipe quality only, with no maturity or popularity gate, and it
  accepts new recipes continuously, including header-only C++20 libraries.
- Fixed the shape a central Conan recipe must take, so it is not mistaken for
  a move of the in-tree one. A registry recipe downloads the tagged source
  archive through `conandata.yml` rather than exporting a checkout; deletes
  the installed CMake package files so the registry's own generator supplies
  them; and points `url` at the registry with `homepage` at this project. The
  in-tree `conanfile.py` stays exactly as it is, because release CI packages
  the checkout under test. The two recipes are separate artifacts.
- Established that registries hash the tag's source archive rather than a
  release's portable-headers assets, which deliberately carry no build files.
  A future release may attach a deterministic source archive so a registry can
  hash a published asset instead; a published release cannot be amended to add
  one after the fact.
- Validated a candidate Conan Center recipe against the `v0.13.0` tag archive
  on 2026-08-21, including a package build and its test package. It is held
  pending a submission decision and is not part of this repository.
- Noted the submission sequence Conan Center requires: an issue before the
  pull request, a signed contributor agreement, a branch named for the
  package, one recipe per pull request, and only the released version in the
  initial submission.

## 2026-08-20 - Graduate maintenance with immediate execution

- Graduated the documented task, budget, metrics, opaque handle, explicit
  result, structural backend, registered scheduler, immediate execution, and
  external dense-and-sparse chunk adapter spellings under
  `tess::maintenance`. The stable adapter defaults to immediate execution.
- Kept FIFO, queued-coalescing, dirty-bit, and the virtual scheduler under
  `tess::experimental`. M3 was flat without a material regression, but the
  Steam Deck dirty-bit result materially regressed the immediate guardrail in
  budgeted, flush, and 256- and 1,024-task scaling workloads; the 4,096-task
  cell was inconclusive. The portable decision rule therefore rejects
  dirty-bit graduation even though its correctness gates passed.
- Made graduation a source-level facade over the exact measured task,
  scheduler, and adapter types. No implementation or adapter body, MNT-3
  campaign configuration, build flag, benchmark, or fixture changed, and the
  stable default names the same immediate specialization measured by the
  campaign. The generic paired-sentinel source-map update is CI metadata, not
  an MNT-3 input. The retained M3 and Steam Deck evidence therefore remains
  representative.
- Preserved the compile-time structural customization boundary and callback
  exception semantics. The stable contract does not include a virtual ABI,
  object layout, mixed Tess versions, cross-DSO identity, or experimental
  backend behavior.
- This supersedes the historical maintenance TDD's expectation that a
  coalescing backend would graduate with the public contract. Portable
  evidence selects synchronous immediate execution while leaving deferred
  backend work open to later experimentation.
- This also supersedes the v1-stabilization TDD's requirement that a stable
  aggregate never transitively include an experimental header. The alias-only
  facade must see the measured implementation declarations; it therefore
  makes experimental maintenance spellings reachable through the explicit
  `tess/maintenance.h` aggregate. The compatibility umbrella `tess/tess.h`
  deliberately excludes maintenance, and the support contract grants
  stability only to the documented `tess::maintenance` names and semantics.

## Template

```md
## YYYY-MM-DD - Title

- Changed:
- Reason:
- Affected docs:
- Affected code:
```
