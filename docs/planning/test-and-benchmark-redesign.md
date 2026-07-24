# Testing and benchmarking redesign

Status: **Proposed** (2026-07-23). This document records intent only; nothing
in it is implemented. Until the sequenced follow-up PRs land, the existing
[benchmark plan](benchmark-plan.md) and
[calibration history](benchmark-calibration.md) remain authoritative for how
CI actually behaves.

This is a top-down re-derivation of how the project should test and measure
itself, prioritizing future signal over continuity with the current setup. It
was produced by auditing the full CI failure history, re-examining every
existing gate against what it has actually caught, and cross-checking the
conclusions against two independent reviews (an external automated
architecture critique and an external benchmark-landscape review, both
2026-07-23) and a prior standalone memory-layout benchmark study by the same
maintainer whose measurement methodology this document imports. The external
reviews and the prior study are design input held outside this repository;
claims sourced from them are not independently auditable here, unlike the
CI-history evidence in section 2.

## 1. Intended end state

**Correctness**

1. Unit and in-repo oracle tests stay the foundation, unchanged — they and
   the sanitizers produced most historical catches.
2. A scenario layer of three parameterized harnesses: structured-map search
   oracle (S1), colony macro-harness — agents x churn x executors x world
   size (S2), and sparse streaming under residency budget (S3).
3. Differential/metamorphic gates as first-class citizens: incremental ==
   fresh recompute, chunk-size invariance, serial == pool, worker-count
   invariance, sparse == dense.
4. Deterministic work counters (nodes expanded, chunks merged, retries,
   allocations) as committed behavioral goldens — the primary noise-free
   regression signal.
5. A replayable, shrinking property/state-machine harness (seeded operation
   sequences over world/queued-ops/caches/residency with invariants checked
   each step) — bounded on PR, long seeds on schedule.
6. Consumer-contract tests: per-public-header standalone compilation,
   multi-TU ODR link checks, hostile include order, macro-config matrix,
   and a documented minimum-compiler matrix.

**Performance**

7. Retire per-benchmark calibrated CPU-time ceilings as gates. One coarse
   suite-level wall-clock sanity budget survives as a build-misconfiguration
   tripwire.
8. The PR tier measures *work* first: counter goldens, allocation == 0,
   bench compile+smoke; plus a standing selective paired base-vs-head timing
   run on a small sentinel set whenever perf-sensitive paths change.
9. The main tier collects benchmark artifacts (non-blocking) with
   statistical change-point alerting over trailing runs; suspected
   regressions are confirmed by an on-demand paired A/B run.
10. The recurring timing series stays on hosted runners (decided —
    conservative alerting, paired A/B as the noise-robust check); real
    performance numbers come from manual per-release campaigns on controlled
    hardware, reported in frame-budget terms.
11. Adopter-facing comparative numbers move to a separate neutral benchmark
    repository against pinned tess releases.

**CI structure**

12. The required PR gate shrinks to the jobs that have actually caught bugs
    (dev build+tests, ASan, Windows MSVC, hook backstop) plus GCC compile as
    a cheap prospective portability guard and the new counter/scenario
    smoke — critical path from roughly 19 minutes to a target of ~6-7
    minutes, bounded below by the retained Windows job (~6.2 m median) and
    validated with measured job budgets in phase 1 before anything relies
    on it.
13. clang-tidy leaves the PR critical path (diff-scoped or advisory); TSan,
    macOS, release, and werror move to main-only (none has ever failed
    independently on a PR; TSan returns to PR when concurrency-sensitive
    files change); a weekly deep tier auto-files an issue on failure. Git
    hooks: pre-commit hygiene (including the non-negotiable public-safety
    scan) unchanged; pre-push slimmed once CI is fast; the hook-backstop CI
    job unchanged.

**Ecosystem and packaging**

14. CI-tested Conan 2 recipe and vcpkg overlay port in-repo before any
    central-registry submission; a documented integration-policy contract
    (exceptions, RTTI, determinism across thread counts, thread-pool
    ownership, steady-state allocations).

**Communication**

15. `docs/performance.md` becomes generated at documentation deploy from the
    latest main artifacts — no manual snapshot commits, with a testable
    freshness contract (deployment ordered after artifact production, and
    the page states exactly which commit's data it shows); the headline
    becomes scenario evidence in frame-budget terms, with microbenchmarks
    demoted to a maintainer appendix.
16. PRs get one compact check summary: counter deltas, failed property seed
    plus replay command, paired-timing verdict, and exact local repro
    commands.
17. Campaign results publish with full machine/config metadata; experiments
    stay recorded in the [optimization log](optimization-log.md); raw
    artifact retention extends well beyond the current 30 days.

**Coverage**

18. Advisory code-coverage reporting (llvm-cov) on the scheduled tier for
    the test suite, and a smoke-mode variant over the benchmark binaries as
    a subsystem gap-finder — artifacts and gap lists, never a percentage
    gate.
19. Benchmark coverage tracked as a declarative workload matrix (a
    CI-drift-checked catalog of measured cells and known-unmeasured ones)
    plus a sentinel-mapping completeness check for the paired-run set — not
    as a line-coverage number.

**Profiling**

20. An optional, signal-triggered profiling protocol for changes with
    likely performance implications: counters-first triage, local paired
    reproduction, profile-and-diff with the existing `bench-profile`
    tooling, outcome recorded in the optimization log — linked directly
    from the PR check summary whenever the paired run flags a change, and
    never a gate.

## 2. Evidence

The redesign is grounded in the repository's own CI history rather than in
principle alone. Numbers below come from a point-in-time snapshot taken
2026-07-23 (28 failed workflow runs at classification time, 15 on PR
branches, each classified individually); the totals drift as CI keeps
running — the 30041607406 firing discussed below postdates the snapshot —
so phase 1 re-derives the classification from the Actions API before acting
on it.

### 2.1 Where wall time goes

- Median successful PR run: ~18.6 minutes end to end, critical path set by
  Quality Gates (dev-clang-tidy) at ~18.5 minutes.
- Median successful main run: ~35.7 minutes, critical path set by Benchmark
  Gates (threshold checks plus baseline collection) at ~32.5 minutes.
- Next tier: Windows MSVC ~6.2 m, cppcheck ~4.9 m, dev build+tests ~4.5 m;
  everything else at or under ~2.6 m. The hook-backstop job takes ~0.4 m.
- Docs-only changes already short-circuit to ~1 minute via the change
  classifier; that fast path is kept.

### 2.2 What has actually caught bugs

Classified PR-branch failures: ASan, 2 real catches (alloc/dealloc
mismatch); clang-tidy, 3 real catches (for example
`bugprone-unchecked-optional-access`); Windows MSVC, 4 real portability
catches (allocation-behavior differences); hook backstop, 1 real
public-surface catch; plus 3 universal build breaks from a single PR that
redundantly failed a dozen jobs at once. The jobs that have *never* failed
independently on a PR: dev-tsan, dev-werror, release, and both macOS jobs.

At ~0.4 minutes for a real catch, the hook backstop has the best
signal-per-minute in the pipeline. At ~18.5 minutes for 3 real catches,
clang-tidy has real value but should not set PR latency. The benchmark gate
is examined separately below.

### 2.3 The benchmark threshold gate

The calibrated per-benchmark ceiling gate
([methodology](benchmark-calibration.md)) has fired six times in repository
history:

- Five firings were calibration churn or noise. One representative case: a
  dependency-bump PR with no code changes failed a path benchmark at 0.66%
  over its ceiling and passed on rerun.
- One firing — 2026-07-23, CI run 30041607406 on PR #59 — is a probable
  genuine catch: four related cache/batch path benchmarks exceeded ceilings
  by 19-28% (`path/cached_astar_batch_100_mixed_repeated_room_portals_512x512`
  +19%, `path/field_product_cache_hit_replay_room_portals_512x512` +28%,
  `path/nearest_target_product_100_starts_room_portals_512x512` +26%,
  `path/weighted_batch_planner_100_neargoal_open_512x512` +19%) while dozens
  of neighboring benchmarks passed with run-to-run CVs of 0.01-1%. Clustered
  large shifts confined to code the PR touches is the signature of a real
  regression (or of an intended semantics change that requires a justified
  recalibration — either way, real reviewable signal).

Against that record: 49 commits touching `bench/thresholds/`, a dedicated
calibration-history document, and roughly 14 runner-hours per week. The
structural critique is sharper than "it never fires": a
2x-of-maximum-observed ceiling *cannot* fire on anything smaller than a
large regression (a clean 50% slowdown passes), while absolute ceilings on
shared heterogeneous runners produce false positives at the margin (the
dependency-bump case exceeded its ceiling by 0.66% and passed on rerun —
a false positive, whatever the underlying noise amplitude). The one
truthful firing happened in exactly the regime — large, clustered,
localized — that a paired base-vs-head comparison detects with better
attribution, no ceiling maintenance, and no self-recalibration loophole
(the same PR that tripped the gate also edits `bench/thresholds/path.json`;
an author can recalibrate past an absolute ceiling in the PR itself, but
cannot edit a JSON to pass a base-vs-head comparison). The ceiling verdict
is also impoverished: "exceeds 93.4 ms" cannot distinguish *doing more
work* (the benchmarks' own user counters — `batch.expanded_total`,
`field_builds` — can) from *running slower per unit of work* (paired timing
can).

### 2.4 Methodology imported from prior measurement work

A prior standalone benchmark study by the same maintainer (26 workload
families, two architectures, bare-metal parallel scaling to 192 threads)
contributes proven practice: bit-exact cross-configuration equivalence
gates; thread-count invariance as a correctness gate; a cold-cache latency
mode alongside warm throughput; world-size sweeps (its only winner changes
came from working-set/cache-hierarchy knees); the framing that per-query
parallelism with per-thread state scales while band-split single-pass work
can anti-scale under NUMA and dispatch overhead; and PMU-counter
attribution reserved for dedicated hardware. An external benchmark-landscape
review (2026-07-23) additionally surveyed 77 candidate comparison libraries,
defined capability-specific comparison tracks, and recommended generated
fixtures under permissive licensing with integer PRNGs (standard-library
random distributions are not bit-portable across implementations) plus an
independently implemented oracle.

## 3. Correctness redesign

The unit foundation and existing in-repo oracles are kept as-is. Additions,
in order of marginal value:

### 3.1 Scenario layer

Three parameterized harnesses cover the axes real adopters exercise:

- **S1 — structured-map search oracle.** Point-to-point search on structured
  maps through the grid-benchmark harness (see the
  [grid benchmark data TDD](../tdd/grid-benchmark-data-and-scenario-oracle.md)),
  with two data layers: deterministic in-repo procedural generators
  (recursive-division maze and room-and-corridor, seeded integer PRNG,
  emitting the standard map format) that run on every PR with the in-harness
  Dijkstra oracle; and the external synthetic scenario sets — the only
  oracle this project did not write — via the TDD's fetch/cache/strict-data
  job design. The TDD's own licensing analysis (its sections 6.2-6.3)
  determined the synthetic sets contain no third-party game content and
  carry only ODC-By attribution obligations; the manifest introduced by
  in-flight PR #59 blocks all content on grounds that apply to the
  game-derived sets, a discrepancy to resolve in that PR's review. Octile mode
  activates once lattice semantics are validated.
- **S2 — colony macro-harness.** N agents (100 / 1k / 10k) with goals driven
  through the production stack (schedule loop, path agents, queued ops,
  result channels, render deltas), parameterized by churn rate (seeded wall
  add/remove script; zero churn = pure throughput), executor and worker
  count, world size (512-2048), and field payload width. Run modes:
  throughput, thread-scaling, cold-cache latency, and a weekly soak
  (~1M ticks asserting RSS plateau, counter stationarity, zero steady-state
  allocations, and the queue-flow accounting checks of section 3.3 —
  bounded outstanding inventory and oldest age, no backlog growth). Churn
  x agents in one harness deliberately
  tests the interaction — cache invalidation racing agent repaths mid-tick —
  that neither axis tests alone.
- **S3 — sparse streaming under budget.** Tiled structured maps, residency
  budget fractions of 5% and 25%, stream-and-retry to convergence, asserting
  the converged result equals the dense-world reference, with the queue-flow
  accounting of section 3.3 applied to residency admission and eviction
  churn.

### 3.2 Differential and metamorphic gates

Run across scenario executions: incremental topology/route-cache/precheck
results equal a fresh full recompute (sampled); serial results equal pool
results bit-identically; pool results are bit-identical across worker counts
1/2/4/8 (cheap — same run, different constructor argument — and catches
scheduling-order dependence and races the sanitizers can miss); chunk-size
invariance; sparse equals dense.

### 3.3 Counter goldens

Committed expected-values JSON for deterministic work counters
(diagnostics `PathCounters`/`QueuedPhaseCounters` families plus
scenario-level repath/retry/stream counts), asserted on serial runs only
(counters are recorded through thread-local sink pointers installed
per-thread by scoped installers; pool workers do not currently install or
aggregate into the caller's sink, so a pool run under-counts rather than
merely varying — serial-only goldens avoid this, and pool-worker
aggregation is a separate prerequisite if pool totals are ever wanted).
Updated in
the same PR when behavior intentionally changes, making behavioral drift a
reviewable diff instead of a silent change.

**Queue-flow accounting** extends the same discipline to the persistent
bounded flows arriving with PR #59 (resumable work queues, exact event
batches, experimental maintenance queues, bounded blocked-agent
lifecycles). Each applicable flow records raw deterministic counts:
admitted and terminal items, with terminal broken out by outcome
(completed, coalesced, rejected, cancelled, superseded, stale, dropped);
time-weighted outstanding inventory; accumulated submission-to-terminal
ticks; current and high-water outstanding and oldest outstanding age; and
offered versus consumed work units. Coalesced work counts an arrival at
the clean-to-pending transition, not at every repeated schedule call, so
arrival counts stay meaningful. Two kinds of assertion follow. Exact
conservation identities (admitted equals terminal plus outstanding;
terminal equals the sum of its outcome categories) are golden-gated like
any other counter. Fixed-tick-window derived quantities — average
inventory L, accepted rate lambda, mean residence time W — serve as a
steady-state accounting check (L ~= lambda x W) in the S2 soak and S3
churn scenarios, whose assertions are stationarity, bounded oldest age,
and absence of backlog growth. Rolling averages and anything
wall-clock-derived stay diagnostic, never gating. A UI-agnostic
flow-health snapshot in the diagnostics layer exposes the same counters
for future debug panels.

### 3.4 Property/state-machine harness

Seeded random operation sequences over the mutable surface — queued edits,
residency stream/evict/reload, cache fill/invalidate, schedule ticks — with
invariants checked after every step, automatic shrinking, and a committed
replay command per failure. Bounded sequences on PR; long seeds weekly. This
generalizes the existing one-off seeded equivalence tests into
infrastructure.

### 3.5 Consumer-contract tests

Per-public-header standalone compilation via CMake
`VERIFY_INTERFACE_HEADER_SETS`, with a prerequisite restructure: the current
default header file set mixes public and implementation headers, and CMake
verifies every public set by default, so the supported public headers must
first move to their own named set that the verification selects explicitly.
Multi-TU consumer tests, stated precisely: duplicate-symbol link checks and
macro-configuration checks (a successful link does not prove the absence of
inline/template ODR violations); the header-affecting `TESS_ENABLE_*`
macros are enumerated and applied uniformly across all TUs of a
configuration, and cross-TU macro mismatch is documented as unsupported
rather than tested. Hostile include order and repeated inclusion; and a
documented minimum-compiler matrix rather than "C++20 plus whatever CI
runs".

### 3.6 Advisory code-coverage reporting

No code coverage is currently collected anywhere in the project; the only
measured coverage is Doxygen documentation coverage, and the only test
coverage map is the curated `tests/AGENTS.md` catalog. Add a coverage
configuration (clang `-fprofile-instr-generate -fcoverage-mapping`) and a
scheduled-tier job that publishes an llvm-cov report as an artifact,
optionally surfaced in the generated documentation. Explicitly non-gating:
a coverage-percentage gate repeats the calibrated-ceiling mistake — a
number that rarely fires truthfully, demands maintenance, and incentivizes
assertion-free tests. The value is diagnostic: showing where the existing
suites are thin before the scenario and property layers land, and
quantifying what those layers add once they do.

### 3.7 Scope notes

Scheduled-only: coverage-guided fuzzing of the map/scenario parser and the
property harness's operation decoder. Not doing: mutation testing
(cost/benefit poor at this suite size); multi-agent collision-free planning
semantics (out of scope until the library has them). Suite hygiene
(table-driving duplicative tests, CTest labels by risk/cost tier) proceeds
opportunistically — except the CTest label mapping, which section 6
promotes to a prerequisite for pre-push slimming.

## 4. Performance redesign

The central inversion: measure *work* on PRs, measure *time* where the
hardware is trustworthy.

### 4.1 PR tier (blocking)

- Counter goldens and allocation == 0 (exact, noise-free, no repetitions —
  which is what makes the PR budget realistic).
- Bench targets compile and run one smoke iteration (the existing
  `--benchmark_min_time=0.001` ctest smoke): catches build/API rot and
  asserts the `bench_check` correctness hooks, with no per-benchmark timing
  assertions.
- One coarse suite-level wall-clock sanity budget (for example, smoke suite
  under 5x its typical total) as a build-misconfiguration tripwire —
  counters cannot see a missing `-O2`.
- A standing **selective paired run** whenever the change classifier sees
  perf-sensitive paths: a small sentinel benchmark set, base and head
  binaries built and interleaved in one job — a path-filtered
  `pull_request` job, not a manually dispatched one. Building two revisions
  affordably needs a benchmark-only build preset (the current `bench`
  preset also builds the full test suite and both bench binaries). The
  2026-07-23 catch is exactly
  this leg's job, at the same point (PR) with better evidence. Sentinel
  selection favors composite microsecond-to-millisecond workloads (cached
  batches, field products, weighted batches, agent ticks) where paired
  statistics are reliable — precisely the benchmarks that fired — over
  nanosecond micro-benches where they are not. Statistical discipline: keep
  the sentinel set small (running 160+ simultaneous hypothesis tests is a
  multiple-comparisons machine); flag only when the 95% confidence lower
  bound exceeds a ~7-10% practical-effect threshold plus an absolute
  materiality floor; re-run once before treating a flag as real; report
  pass/advisory/regression, advisory-only during a shake-out period.

### 4.2 Main tier (non-blocking)

The full bench suite runs once per main push and uploads an artifact (the
existing baseline-collection machinery is kept — it is good data collection
currently attached to a bad gate). A change-point detector over the trailing
~30 artifacts opens an issue with the suspect commit range instead of
failing the push; hosted-runner variance means it is tuned conservatively
(sustained shifts across at least 3 consecutive artifacts and above the
practical-effect floor). Artifacts must carry a runner fingerprint — CPU
model, runner-image version, compiler and flags; the current artifact
metadata records little beyond runner OS — and the detector stratifies by
fingerprint: a fleet or image migration is a series break, not a
regression, and when fingerprints shift the detector dispatches a paired
confirmation instead of opening an issue from unpaired data. Suspected
regressions are confirmed on demand by a
full paired A/B run under the same statistical criteria. Paired runs execute
only on hosted runners or trusted post-merge commits — never untrusted code
on personal or self-hosted hardware.

### 4.3 Retired

The `tess_bench_*_thresholds` gating targets, the per-benchmark ceilings in
`bench/thresholds/*.json`, and the recalibration workflow. The
[calibration history](benchmark-calibration.md) freezes as a historical
record. Deliberately prioritizing future signal over historical trend
continuity: no ceiling lineage is preserved.

### 4.4 Recurring series and campaigns (decided)

The recurring timing series comes from hosted-runner main artifacts only —
no self-hosted runners, no scheduled personal-hardware relays, no scheduled
cloud instances. Accepted trade-off: alerting can only credibly flag
sustained large shifts; subtler drift is caught later by campaigns, and the
paired A/B design carries more weight as the only noise-robust timing
comparison available on hosted hardware.

Real performance numbers come from manual campaigns on controlled hardware
(the maintainer's development machine, the handheld target via the existing
remote-bench tooling in `tools/steamdeck/`, and cloud bare metal when a
scaling study warrants it), at minimum per release: thread-scaling curves to
the physical-core knee, cold-cache latency versus warm throughput,
world-size sweeps, and PMU attribution. Campaign results are versioned
artifacts with full machine and configuration metadata.

### 4.5 Benchmark coverage: a matrix and a mapping, not a percentage

"Coverage" for benchmarks is not line coverage — benchmarks are supposed to
be unrepresentative of code breadth — but three specific guarantees:

- **Subsystem gap-finding.** A scheduled coverage-instrumented run of the
  bench binaries in smoke mode (timing distortion is irrelevant to a
  coverage-only run) reporting which public headers and subsystems no
  benchmark executes at all. Precedent: the 2026-07-11 performance audit
  manually discovered that sparse storage had zero benchmark coverage
  ([audit](audit-2026-07-11.md)); this report finds that class of gap
  mechanically, which matters every time a new subsystem lands without
  benchmarks. Line-level detail is not meaningful here (instrumentation
  cannot distinguish timed regions from untimed setup); subsystem and
  entry-point granularity is.
- **A declarative workload matrix.** The meaningful coverage axis is the
  workload space: which cells of (family x world size x chunk configuration
  x density x dense/sparse x executor) are measured and which are known to
  be unmeasured. Record it the way `tests/AGENTS.md` records test
  guarantees — a curated catalog with a CI drift check — so gaps are
  reviewable statements instead of audit-time discoveries, and silent caps
  are visible. The scenario parameter matrix of section 3.1 is effectively
  this coverage specification.
- **Sentinel-mapping completeness.** The selective paired run (section 4.1)
  only fires usefully if every perf-sensitive source area maps to at least
  one sentinel benchmark. Declare the mapping (classifier path pattern to
  sentinel entries) and check it: a perf-sensitive directory with no
  sentinel representative fails the check. The 2026-07-23 catch worked
  because the right cache/batch benchmarks happened to exist; this makes
  that structural.

### 4.6 Profiling protocol for flagged changes (optional, signal-triggered)

The profiling tooling exists (the `bench-profile` preset and capture/summary
tools, documented in the [benchmark plan](benchmark-plan.md) profiling
workflow) but nothing connects it to the moments a contributor needs it.
Since contributors here are largely automated agents, an explicit protocol
attached to the signal is what makes the tooling get used. It is optional
guidance, never a gate — hosted runners have no reliable PMU access, so
profiling is inherently a local or campaign activity.

Triggers: the change classifier flags perf-sensitive paths on a PR; the
selective paired run returns `advisory` or `regression`; a change-point
issue opens on main; a campaign shows an anomaly.

Protocol, in escalation order:

1. **Counters first.** Read the counter deltas in the PR check summary. If
   work changed (`expanded_nodes`, `dirty_chunks_merged`, retries, ...),
   the diagnosis is algorithmic — the golden diff already names the
   behavior change, and profiling is usually unnecessary. Decide whether
   the new work is intended; update goldens in the same PR if so.
2. **Reproduce paired.** If counters are flat but time moved
   (constant-factor), reproduce locally with a paired base-vs-head run of
   the flagged benchmarks using the exact command from the check summary.
   No reproduction, no investigation — hosted-runner noise is not a
   finding.
3. **Profile and diff.** Build both revisions under the `bench-profile`
   preset, capture with the profiling tool at matched filters and
   durations, and diff the symbol summaries. Attribute the delta to a
   symbol or call path before concluding anything.
4. **Record.** Accepted, rejected, and inconclusive outcomes all go to the
   [optimization log](optimization-log.md), per the repository's existing
   performance-work convention — inconclusive entries prevent the next
   agent from repeating the same dead end.

The PR check summary links this protocol whenever it emits a paired-run
verdict, and CONTRIBUTING.md gains a pointer when the protocol is wired up
(phase 2).

## 5. CI topology

| Tier | Trigger | Contents | Wall clock |
| --- | --- | --- | --- |
| PR (blocking) | `pull_request`, code-gated by the existing classifier | dev build+tests (+ counter goldens, scenario smoke: S1 procedural coarse stride, S2 N=100 serial+pool at two worker counts), ASan, Windows MSVC, GCC compile, hook backstop, bench compile+smoke, path-filtered selective paired run, path-filtered TSan when concurrency-sensitive files change; diff-scoped clang-tidy advisory via annotations/step summary (fork `pull_request` tokens are read-only; any privileged reporter is a separate `workflow_run` job that never executes PR code) | ~6-7 min target (validated in phase 1; Windows ~6.2 m is the floor) |
| Main (push) | push to `main` | PR set + full matrix (TSan, macOS, release, werror, full clang-tidy, cppcheck) + bench artifact run + change-point alerting + S1 strict data job (once unblocked) + S3 config + S2 N=1k artifacts | ~15 min, non-blocking beyond the PR set |
| Weekly | cron + dispatch | Full oracle corpus/stride, S2 N=10k both executors, worker-count invariance sweep, world-size pair, soak, budget sweep, ASan full + TSan-capped scenarios, long property seeds, parser fuzzing, advisory llvm-cov reports (test suite + bench smoke gap-finder) | <= 45 min; auto-files an issue on failure |
| Campaign | manual / release | Controlled-hardware full suite: scaling knees, cold-cache latency, PMU attribution, handheld target; comparative-repo refresh on release; performance-page data refresh | hours, off-CI |

Windows portability catches were real, so Windows stays a PR gate. macOS has
never fired independently and moves to main. A regression that a demoted job
would have caught surfaces one merge later, at main — acceptable given the
historical record, and the auto-issue keeps it owned. Roughly 15% of runs
are cancelled superseded pushes; the existing concurrency cancellation and
docs-only fast path are kept.

## 6. Git hooks

Current behavior (`docs/git-hooks.md`, `tools/git_hooks.py`): `pre-commit`
runs index-blob hygiene checks — whitespace, conflict markers, public-safety
patterns, formatting, staged-file token limits, noreply email; `commit-msg`
checks the subject; `pre-push` runs a full dev configure+build+test cycle,
the install and FetchContent consumer smokes, and a benchmark
configure+build when benchmark files, CMake files, or public headers
changed in the pushed range. The CI hook-backstop job reruns
the same script authoritatively — hooks are bypassable, so the backstop is
load-bearing, and its record (best signal-per-minute in the pipeline) says
it earns its place.

Why hooks exist: shift failures left of what was a ~19-minute CI, and
enforce hygiene invariants where they must live — the public-safety scan is
non-negotiable and must run pre-commit, because a leaked name in a pushed
commit is already public. Trade-offs: minutes of friction on every push
(multiplied for agent contributors, who push frequently); duplicated logic
with CI (well mitigated — one script serves both sides); and the hooks
themselves are a maintenance surface (see the hook-environment fixes in
PRs #54 and #58).

Redesign changes: `pre-commit` and `commit-msg` stay exactly as they are —
seconds of cost, prevention-shaped value. `pre-push` slims down *after* the
PR-tier latency target is validated (phase 1) and a tested source-to-test
mapping exists (the CTest labels of section 3.7 are its prerequisite, which
promotes them from opportunistic to required for this step): the default
becomes configure + build + the affected-test subset, with the full suite
behind an opt-in environment variable; the consumer smokes move behind the
same opt-in (CI's dev job runs them on every code PR); and the conditional
benchmark build is dropped (bench compile rot is caught by the PR
bench-smoke job, and with threshold gates retired there is no local gate to
pre-verify). This hook change lands in phase 2 alongside the other tooling.
The hook-backstop CI job is unchanged.

## 7. Communication by audience

- **Adopters.** `docs/performance.md` is generated at documentation deploy
  from the latest main artifact plus the latest campaign results — hardware,
  date, configuration, and reproduce command stated. Freshness is a
  contract, not a hope: today the documentation and CI workflows start
  independently on the same push, so deployment must be ordered after
  artifact production (for example, a `workflow_run`-triggered deploy), the
  page states exactly which commit's data it renders, the campaign-results
  store (a data branch or published dataset) is defined in the same phase,
  and the fallback when no artifact exists yet is rendering the previous
  data with its commit stated. The headline becomes
  scenario evidence in frame-budget terms ("N agents with churn cost Y ms of
  a 16.7 ms frame on <machine>", tick p50/p95/p99, memory, allocations),
  with open-grid microbenchmark medians demoted to a maintainer appendix:
  nanosecond medians answer neither integration risk nor frame-time
  predictability. Comparative claims link to the external benchmark
  repository.
- **Maintainer.** Change-point auto-issues with commit ranges; per-main
  counter and timing artifacts for bisection, retained well beyond 30 days
  (the JSON is small — a data branch or long-retention artifacts); the
  optimization log remains the experiment ledger.
- **Contributors (largely automated agents).** A ~6-minute blocking PR
  loop; deterministic failures (counters and equalities, not noisy
  ceilings); one compact PR check summary — counter deltas, failed property
  seed plus replay command, paired-timing verdict with a link to the
  profiling protocol (section 4.6) when it flags, exact local repro
  commands — instead of thirteen threshold JSON files to decode, delivered
  fork-safely through check output and the step summary rather than a
  token-privileged comment. The `tests/AGENTS.md` catalog and the
  pre-commit hygiene flow are unchanged (pre-push slims per section 6).

## 8. Ecosystem and packaging

- **Packaging.** An in-repo, CI-tested Conan 2 header-library recipe (with
  `test_package`) and a vcpkg overlay port (with `usage` and a consumer
  build), exercised across representative triplets and both EnTT-on/off
  configurations, ahead of any central-registry submission
  ([packaging](../packaging.md) currently records that none exists). For a
  library seeking adoption this is the largest ecosystem gap.
- **Integration-policy contract.** Documented and tested, not just claimed:
  exceptions and RTTI policy; determinism across thread counts and
  platforms; thread-pool ownership and oversubscription behavior;
  steady-state allocation guarantees; the supported-compiler floor. Game
  developers evaluate integration risk before performance; the scenario
  suite's invariance gates back every claim with a test.
- **Compile cost.** The existing header compile-cost check stays; weekly
  per-header instantiation and object-size tracking extends it.

## 9. Separate comparative benchmark repository (decided: yes, scoped, later)

Comparative "tess versus the ecosystem" measurement moves to a separate
neutral repository, benchmarking pinned tess releases — not per-commit — on
capability-specific tracks, starting small (unit-cost and weighted 2D
search, shared-goal fields, dynamic-edit replanning) against a first wave of
established C++ and Rust grid-pathfinding baselines, per the external
landscape review.

Rationale: comparisons published from inside the measured project invite
self-favoring suspicion; competitor adapters and their dependencies and
licenses stay out of this repository's network-free CI and permissive tree;
and comparative campaigns run at release cadence, not CI cadence.
Mitigations for the second-repo cost: refresh only on release; keep the
initial track set to two or three; generated fixtures are produced by this
repository's scenario generators and published as versioned
permissively-licensed fixture packs; the comparative repository maintains
its own independently implemented oracle. Voxel-ecosystem storage tracks,
multi-agent planning tracks, and GPU tracks are explicitly deferred.

In-repo `docs/performance.md` then tells one story — tess versus tess over
time on named hardware — and links outward for tess versus the world.

## 10. Future implementation sequence

Each phase is its own PR (or small PR series); none is started by the PR
that introduces this document.

1. CI restructure: re-derive the failure classification from the Actions
   API at implementation time (the section 2 snapshot has drifted); re-tier
   jobs per section 5 including the path-filtered TSan PR job; retire
   threshold gating; keep artifact collection; add the weekly auto-issue;
   measure the resulting job budgets to validate the PR latency target.
   Workflow-only.
2. Counter-golden and change-point tooling (with runner fingerprinting in
   artifacts); paired A/B timing as both a path-filtered `pull_request`
   sentinel job and a `workflow_dispatch` full-confirmation mode, backed by
   a benchmark-only build preset, with the sentinel-mapping completeness
   check; advisory coverage reporting (test suite and bench-smoke
   gap-finder) and the benchmark workload-matrix catalog with its drift
   check; the profiling protocol wired into the PR check summary and
   pointed to from CONTRIBUTING.md; the pre-push slimming of section 6
   (prerequisite: the tested CTest-label source-to-test mapping).
3. Scenario layer: procedural generators, then the S2 macro-harness (with
   PR-tier smoke), then S3; S1 external-data activation via manifest
   unblocking, fetch tool, and the strict data job (grid-benchmark TDD
   phases 1-2), resolving the rights discrepancy noted in section 3.1.
4. Consumer-contract and packaging: header-set verification, ODR tests,
   macro-config matrix, Conan 2 recipe, vcpkg overlay, integration-policy
   document.
5. Generated performance page; retire the manual snapshot policy
   (CONTRIBUTING.md update).
6. Property/state-machine harness; weekly long seeds; parser fuzzing.
7. First controlled-hardware campaign (development machine, handheld
   target, cloud bare metal): scaling, latency, world-size; publish and
   record in the optimization log.
8. Comparative repository bootstrap against a pinned release.

## 11. Risks

1. **Losing the only PR timing tripwire.** A catastrophic regression in a
   path the sentinel set misses reaches main before detection. Mitigation:
   counters catch algorithmic blowups on PR; the suite-level sanity budget
   catches build misconfiguration; main alerting catches the rest within one
   merge; the selective paired run covers exactly the historically observed
   true-positive regime.
2. **Change-point alert tuning.** Too chatty and it gets ignored like the
   ceilings were. Start conservative, tune in the open with tests on the
   detector itself.
3. **Foundation churn.** The scenario layer stacks on the in-flight
   grid-benchmark harness (PR #59); keep scenario code additive and review
   the loader contract against the TDD's section 9 during that PR's review.
4. **Golden-counter portability.** Counter goldens require fully
   deterministic expansion order across gcc/clang/MSVC. Integer-only
   arithmetic makes this achievable; verify on the Windows job before
   goldens become gating, and fall back to per-platform goldens or ratio
   bounds if needed.
5. **Comparative-repository scope creep.** The full landscape is a research
   program, not a side artifact. Pinned releases, two or three tracks, and
   a small first candidate wave are the controls.

## 12. Open questions

- Sentinel-set contents and size for the selective paired run (initial
  proposal: the four benchmarks that fired on 2026-07-23 plus one
  representative per remaining family, capped around 12).
- Whether counter goldens are stored per-platform from the start or only
  after a portability failure.
- Change-point detector choice and window (simple control-chart rules over
  medians versus a formal change-point test) — decided in phase 2 with
  tests.
- Where long-retention benchmark artifacts live (data branch versus
  extended artifact retention versus published dataset).
- Naming and hosting for the comparative repository, and the fixture-pack
  publishing format.
