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

Entries from 2026-07-12 and earlier are in
[`optimization-log-archive-2026-06-07.md`](optimization-log-archive-2026-06-07.md).

## 2026-07-28 - Joint Movement Admission Cost At Colony Scale

- Area: `advance_path_agents_with_joint_movement` on a 128x128 world (the
  colony's shape), `lab/` benchmark family (no threshold targets).
- Hypothesis: joint admission -- validation, occupant index, chain fixpoint,
  cycle walk, batch apply -- fits comfortably inside the colony's 50 ms fixed
  tick at populations up to 1,024 agents.
- Evidence: three-run local arm64 medians. The all-cycles worst case
  (`lab/joint_headon_denied_128x128`, every agent in a denied 2-cycle) costs
  4.3 us at 128 agents, 42 us at 512, and 154 us at 1,024 -- 0.3% of the tick
  budget at the demo's maximum population. Full chain drain including
  per-iteration state reset (`lab/joint_chain_reset_128x128`) costs 59 us at
  128 agents and 901 us at 1,024 (~1.8% of budget, reset included). Growth
  from 512 to 1,024 denied pairs is superlinear (3.6x), consistent with the
  per-cycle-walk `on_walk` reset and sorted-vector claims; irrelevant at
  these budgets.
- Decision: ship the joint advance with caller-owned scratch and no
  micro-optimisation; the colony adopts it with `SwapPolicy::Permit`, and its
  three recorded livelock seeds resolve (four post-wall trips each, zero
  terminal, stall counter quiet) where the per-agent driver reproduces the
  historic 890-motionless-tick wedge.
- Retry conditions: profile the admission pass if a consumer runs it above
  roughly 4,096 agents per tick, or if the denied-cycle path ever shows up in
  a real workload's profile; the `on_walk` reset is the first candidate.

## 2026-07-28 - Multi-Agent Deadlock Resolution Screening

- Area: multi-agent local movement — resolving the deadlock class that
  remains after the settled-agent fix (two travelling agents each on the tile
  the other needs; ~2% of colony wall layouts).
- Hypothesis: a rapid disposable-harness elimination pass over ~17 candidate
  mechanisms can narrow the field before any library work, more cheaply than
  investigating candidates in-library one at a time.
- Evidence: full tables, method notes, and caveats in
  [local-movement-resolution.md](local-movement-resolution.md) (screening
  study; numbers directional, harness not preserved). Headlines: a joint
  batch commit admitting moves into same-tick-vacated tiles, with 2-cycle
  swap permitted, resolved 89-100% of solvable instances at 2-11 us/tick;
  chains/rotations without swap resolved ~0%; priority inheritance (not
  ranking) is PIBT's active ingredient and the only mechanism that survives
  multi-tile footprints; a ranking oracle must share the agent's
  movement-class passability or agents park beside obstructions.
- Accepted (to build in-library, tests-first): a joint movement commit with
  an explicit swap policy (`Forbid` default, `Permit`, `PermitOnDeadlock`),
  since `commit_movement_intent` validates one destination against current
  state and cannot express vacated-this-tick admission by construction.
- Rejected: chain/rotation-only resolution, greedy claim without inheritance,
  per-agent PIBT escalation (verified null), per-tile congestion pricing
  (spreads routes but carries no direction; both streams avoid the same
  tiles), route-derived cheap ranking, directional bias as a ranking-tier
  feature (needs per-(tile, direction) cost the model cannot express), WHCA*
  as a default tier (30-90x cost, horizon fails at dense bottlenecks), and
  goal swapping as a movement-layer feature (assignment encodes information
  the movement layer cannot see; the evidence belongs to the caller's
  assignment layer).
- Deferred: LaCAM (prototype never found a plan; retry from the MIT reference
  with a persistent search tree), conflict-cluster escalation, adaptive
  space-time horizons, priority-consistent resource ordering, an explicit
  anonymous goal-set API, per-(tile, direction) cost.
- Retry conditions: re-open the ranking tier (PIBT) only if library-scale
  tests show the cheap resolver leaving real gaps on cycle-rich maps or for
  agents with extent; re-measure all costs at 128x128 with up to 1,024 agents
  in the library benchmarks before setting any tier default.

## 2026-07-26 - Keep A Sealed Colony Cheap While Planning Around Settled Agents

- Area: web colony demo planning, blocked-agent recovery, and the terminal
  verdict.
- Hypothesis: agents can be routed around teammates who have arrived and will
  never move again without giving up the region graph's cheap rejection of
  goals that terrain has sealed off.
- Evidence: measured on the 1,024-agent setting with a wall spanning every
  row, 60 ticks against a 50 ms fixed-step budget. Baseline (planning on
  terrain alone) worst 311.5 ms, mean 6.2 ms, but zero agents reported
  terminal within the window because the retry allowance was 2N+8. Planning on
  the settled-aware class with the graph still passed to the tick driver:
  worst 612.7 ms, mean 254.1 ms — a multi-second page freeze, because
  `precheck_path` returns `GraphStale` on a movement-class stamp mismatch, so
  the graph pruned nothing and every blocked agent re-searched the whole
  region every tick. Adding an explicit `precheck_path<Walker>` against the
  terrain graph on the first blocked tick: worst 312.5 ms, mean 5.2 ms, all
  1,024 correctly terminal. The remaining worst tick is the initial plan for
  1,024 agents and is present in the baseline too.
- Accepted: plan and move with a settled-aware class; keep the region graph on
  terrain; ask the terrain precheck first and the settled-aware search only
  when terrain says a route exists. Ordinary and bottleneck ticks stay in the
  1-3 microsecond range with a ~60 microsecond p95 while a jam clears.
- Rejected: installing detour routes into the retained-route store when an
  agent stalls. The demo's replan-every-tick strategy marks pathing dirty,
  which resubmits every agent and overwrites retained routes, so the fix would
  have silently done nothing whenever that toggle was on.
- Rejected: rebuilding the region graph for the settled-aware class. It churns
  topology over something that is not terrain, and is unsound in the
  un-settling direction — a graph built while a tile was settled would prune
  routes that reopen the moment its owner relaunches.
- Deferred: a flow-field formulation with a shared goal set and free-slot
  assignment. It removes this deadlock class outright rather than routing
  around it, but 128 distinct goal tiles means 128 fields, so it only pays off
  together with a goal-model change. Recorded as a candidate for the colony
  macro-harness's strategy axis.
- Retry conditions: revisit if the demo gains bidirectional traffic, which
  neither this fix nor a flow field resolves without a yield or swap protocol;
  and re-measure the sealed case if the tick driver ever gains a precheck that
  tolerates a more permissive graph stamp.
- Measurement caveat: per-tick medians on an unobstructed map vary 1.1-2.9
  microseconds run to run for an identical binary, so no conclusion here rests
  on a single sample pair; the sealed-colony numbers above are the ones with a
  signal larger than that noise.

## 2026-07-24 - Avoid Known-Unusable Weighted Field Work

- Area: repeated-goal weighted batch and product-cache preprocessing.
- Evidence: exact-SHA audit identified two compositional cliffs. A realized
  `uint32_t` overflow rebuilt the reverse field with the heap before every
  member retried exact A*, and a cache budget smaller than the product's
  mandatory distance labels built and discarded a full product before the
  ordinary batch built its own field.
- Accepted: return `CostOverflow` immediately once the bounded builder proves
  a saturated distance; exact per-member A* remains the correctness fallback.
  Preflight the product's minimum distance-label bytes against the cache budget
  before lookup/build. Dedicated tests prove the overflow avoids the second
  full flood and the oversize product records no cache miss before the normal
  one-field batch.
- Deferred: cross-call memoization of overflow verdicts. World/provider
  mutation identity is required to avoid stale negative reuse, while the
  accepted changes remove the redundant full floods without introducing a new
  cache contract.
- Retry conditions: profile repeated near-`uint32_t` cost worlds if they are a
  real workload; add memoization only with the same content/revision identity
  guarantees as other path products.

## 2026-07-24 - Preserve Incremental Region-Graph Locality on Failure

- Area: dense and sparse `update_region_graph` exception safety.
- Rejected: copying the complete graph before every non-empty incremental
  patch. Although it provided rollback, it also copied every unchanged
  per-tile label and defeated the operation's dirty-chunk scaling.
- Accepted: keep the existing local patch and global CSR rebuild, but catch
  failures after mutation begins, clear every derived structure, and advance
  revision so consumers must rebuild. Failures during dirty-mask preparation
  still leave the graph untouched. This adds no normal-path allocation or
  world-sized copy.
- Evidence: allocation-failure injection covers successive ordinals in dense
  and sparse updates and accepts only the prior complete graph or a cleared,
  stale graph. The retained 512x512 single-chunk benchmark measured a
  five-run local arm64 median of 713,980 ns against its 6,277,497 ns ceiling.
- Retry conditions: consider a strong guarantee only if affected local
  topology and derived CSR slices can be staged without copying unchanged
  tile labels or slowing the existing benchmark materially.

## 2026-07-23 - Preserve the Default Unit-Field Fast Path

- Area: Default orthogonal unit-cost distance fields, multi-goal products,
  nearest-target replay, and field-product cache replay.
- Observation: Routing every default axis neighbor through the resolved
  transition model regressed five existing hosted-runner path gates. The
  largest regressions were the eight-goal room field at about 139 ms against
  75 ms and the shared room field at about 18.5 ms against 10.7 ms.
- Hypothesis: Compile-time specialization can retain the pre-model direct
  axis-neighbor loop when the resolved model proves default orthogonal steps
  and the adjacent provider, without changing generalized model semantics.
- Evidence: After specialization, three-sample local medians were about
  2.77 ms for the shared room field, 3.50 ms for the shared sparse field,
  18.3 ms for the eight-goal room field, 0.83 ms for 100 nearest-target
  replays, and 9.8 us for cached field replay. The first hosted retry exposed
  four remaining generalized-path regressions: cached unit A* at 111 ms,
  nearest-target replay at 2.25 ms, cached field replay at 28.6 us, and the
  near-goal weighted batch at 120 us. Restoring direct default cache misses
  and reconstruction reduced three-sample local medians to 14.7 ms, 0.76 ms,
  10.4 us, and 50.6 us respectively. All are below their existing gates, and
  79 focused path tests pass under warnings-as-errors.
- Follow-up evidence: The second hosted retry passed cached unit A* but still
  measured nearest-target replay at 2.12 ms, cached field replay at 26.4 us,
  and the near-goal weighted batch at 101.25 us. A same-machine comparison
  isolated a real compiler regression: the pre-transition reader measured
  0.31 ms and 4.0 us for nearest-target and cached replay, while the
  generalized reader measured 0.76 ms and 9.7 us. Sampling placed the hot
  samples in an outlined `for_each_indexed_axis_neighbor`; forcing that small
  per-node helper inline restored 0.31 ms and 4.0 us medians. A second profile
  placed most near-goal time in the bounded field-builder neighbor loop.
  Hoisting its invariant saturated distance and bucket selection reduced the
  20-run median from about 50-52 us to 46.8 us.
- Accepted: Use direct indexed axis-neighbor iteration only when
  `ResolvedTransitionModel` proves default orthogonal connectivity. Continue
  using resolved forward/reverse enumeration for hex, diagonal, and
  provider-composed transitions. Default adjacent route-cache misses also use
  the unit A* core instead of the generalized weighted core. Keep the indexed
  axis-neighbor helper forced inline across supported compilers, with the
  reason documented at its definition, and compute bounded-flood
  per-node invariants once outside the neighbor loop.
- Rejected: Raising the five thresholds. The correlated 1.3x-1.9x regression
  was attributable to avoidable per-edge abstraction overhead rather than
  hosted-runner noise.
- Retry conditions: Re-profile if the default fast path and resolved model
  stop producing identical paths, costs, or dependency stamps, or if a future
  provider can prove equivalent default connectivity.

## 2026-07-24 - Gate Previously Smoke-Only Benchmark Families

- Area: block pipelines, maintenance strategies, persistence, exact spatial
  queries, and local coordination.
- Evidence: all five families were registered in the shared benchmark binary
  but absent from threshold targets and hosted baseline collection. Three-run
  local arm64 medians ranged from 422 ns for a fused pipeline to 10.2 ms for
  dense archive save. The largest maintenance result was 24.4 us for sparse
  coalescing, retaining the experiment's previously recorded disadvantage.
  The first hosted query gate then measured box traversal at 2.95 ms per tile
  and 6.66 us by spans, versus local medians of 218 us and 687 ns. Radius spans
  likewise measured 10.2 us hosted versus 1.81 us locally.
- Decision: add strict family manifests, threshold targets, hosted CI steps,
  and baseline artifacts. Bootstrap ceilings use the greater of six times the
  local median or twice the first hosted median, rounded upward; they are
  explicitly labeled bootstrap rather than calibrated.
- Retry conditions: replace the bootstrap ceilings with two times the maximum
  after ten same-runner hosted baseline artifacts.

## 2026-07-24 - Post-Green Audit Performance Triage

- Area: field-product cache admission, colony blocked retries, archive
  checksums, and newly gated query thresholds.
- Evidence: the exact-SHA hosted matrix passed every benchmark family. Query
  ceilings already follow the documented first-hosted-sample bootstrap rule,
  while the observed colony crawl was traced to repeated path-agent lifecycle
  work at a bottleneck rather than an unresolved search-kernel regression.
- Accepted: reject a single oversized field product without clearing useful
  entries, and scale the colony's terminal retry allowance to its active agent
  count. Keep the current query bootstrap ceilings until ten comparable hosted
  artifacts exist.
- Deferred: no CRC rewrite and no wider query ceilings. Neither has profiling
  evidence that justifies complexity or weaker gates. A profiler was not run
  because lifecycle counters and green threshold jobs resolved the performance
  uncertainty without it.
- Retry conditions: profile archive save/load if checksum work becomes a
  material share of a representative workload. Recalibrate query ceilings to
  twice the maximum after ten same-runner hosted artifacts, or profile first
  if a gate fails before then.

## 2026-07-24 - Preflight Unit Repeated-Goal Product Storage

- Area: unit repeated-goal selection in `PathRequestRuntime`.
- Evidence: a 1,024-byte cache budget cannot hold the 4,096-byte mandatory
  distance labels in the focused world. Building then rejecting that product
  produced two cache misses and cleared smaller useful products; the shared
  storage preflight skips the doomed product, preserves exact A* results, and
  records zero cache misses.
- Decision: mirror the weighted-product distance-storage preflight in the unit
  path. This removes repeated world-sized work without changing selection when
  a product can fit.
- Retry conditions: revisit only if product storage becomes compressed or can
  be admitted incrementally without clearing already-admitted entries.

## 2026-07-24 - Retain Flecs Callback Off-Board Filter

- Area: Flecs path-agent collection.
- Evidence: a structural `without<OffBoard>()` query term avoids one callback
  branch per parked entity, but Flecs 4.1.5's fluent builder reproducibly makes
  the required Clang analyzer report downstream `StackAddressEscape` findings.
  The callback filter is non-mutating and parked entities are not a measured
  dominant workload.
- Decision: reject the structural filter until the pinned upstream builder is
  analyzer-clean; keep the reason at the callback and in maintained ECS docs.
- Retry conditions: retest after a Flecs upgrade or if profiling shows parked
  entities materially affect collection time.

## 2026-07-23 - Constant-Time Area Index Validation

- Area: per-agent checked coordinate lookup through `AreaIndex`.
- Evidence: audit found that every checked lookup recomputed a fingerprint
  across all local topologies and portals, making A agent queries cost
  O(A * graph size). A dedicated 256-area, 512x512 lookup benchmark now
  isolates the query path; its five-run local arm64 median is 5.17 ns after
  the revision change.
- Decision: replace the fingerprint with a monotonic `RegionGraphT` revision
  updated by clear, rebuild, and non-empty incremental changes. Index validity
  is now O(1); coordinate lookup retains only region resolution and ordered
  area lookup.
- Retry conditions: consider a direct dense region-to-area table only if the
  new lookup benchmark shows the remaining ordered lookup is material.

## 2026-07-22 - v0.12 Benchmark Gate Closure

- Area: benchmark families added after the last threshold calibration.
- Evidence: the full Release gates found eleven literal benchmark names
  without threshold entries: five resolved-transition/weighted-product cases,
  two coarse-topology/area cases, and four Flecs collection cases. Three-run
  local arm64 medians were 11.6 us for diagonal search, 13.0 us for axial-hex
  search, 1.54 ms for the stair-provider search, 24.74 ms for an eight-goal
  512x512 weighted product build, 5.06 us for product replay, 21.35 us for a
  far coarse path, and 2.72 ms for a 256-area index build. Flecs medians were
  26 us, 0.35 ms, and 4.87 ms for collecting 1,000, 10,000, and 100,000
  agents, and 0.78 ms for collecting and applying 10,000 agents. The stair
  case deliberately expands 32,761 nodes because a single provider transition
  connects two 128x128 planes; the product and area cases are whole-world
  builds, not single point queries.
- Decision: add provisional six-times-median CPU ceilings and a source-level
  test requiring every literal benchmark in a threshold-gated family to have
  an entry. Structurally large cases remain above the 1 ms investigation line
  with work counters and rationale recorded instead of being misrepresented
  as microbenchmarks.
- Retry conditions: replace bootstrap ceilings with two-times hosted-runner
  maxima after ten same-runner baseline samples. Revisit the stair heuristic
  if provider-heavy searches become representative rather than synthetic.

## 2026-07-22 - Optional WebGPU Transport Baseline

- Area: stable-C-API WebGPU field upload, compute dispatch, and asynchronous
  summary readback.
- Evidence: the backend compiles against the exact Dawn C header shipped by
  Emdawnwebgpu `v20260423.175430`; its fake-device tests cover ownership,
  generation invalidation, loss, and asynchronous lifetime. Emscripten 6.0.3
  builds the browser example with the exact SHA-pinned port. Local headless
  Chrome exposed no adapter and therefore exercised the explicit unsupported
  result rather than a device execution path.
- Decision: accept the bounded transport as the v0.11 optional backend. Do not
  establish a timing threshold from an environment without a GPU adapter.
- Retry conditions: measure upload, dispatch, and readback independently on a
  representative browser/GPU matrix before adding performance gates or
  promoting tess-owned shader algorithms.

## 2026-07-22 - Flecs Adapter Baseline

- Area: deterministic Flecs path-agent collection and write-back.
- Evidence: a local Release build with deliberately shuffled `AgentId` values
  collected and sorted 1,000, 10,000, and 100,000 agents in three-run medians
  of 26 us, 0.35 ms, and 4.87 ms. Collecting and applying 10,000 agents took
  0.78 ms. The context owns one persistent query; correctness tests prove warm
  ticks allocate nothing and native table/entity churn does not change output.
- Decision: accept stable-ID sorting and component-notifying write-back as the
  v0.10 baseline. Sorting is required for deterministic output; Flecs query
  creation remains setup-only because upstream documents repeated creation as
  expensive.
- Retry conditions: calibrate cross-platform thresholds before gating these
  baselines. Profile radix or table-local merge alternatives only if adapter
  collection becomes material in a representative 100,000-agent frame.

## 2026-07-22 - Local Coordination Baseline

- Area: deterministic local destination reservations and congestion summaries.
- Evidence: a local Release build resolved 1,000 requests with four feasible
  options each, including contention on 64 first-choice coordinates, in a
  five-run median of about 0.36 ms. The measured coefficient of variation was
  3.14%. Correctness tests cover priority, stable IDs, alternatives, caller
  filtering, invalid ownership ranges, congestion, waits, and warm
  allocation-free reuse.
- Decision: accept the deterministic greedy resolver as the v0.9 local crowd
  substrate. It spreads contention without introducing continuous steering or
  a global matching claim, and the caller retains movement legality and
  commit-time validation.
- Retry conditions: profile and add a calibrated CI threshold if local
  coordination becomes a frame-time contributor in a representative consumer
  trace. Consider a different claimed-coordinate structure only if option
  counts grow enough for insertion costs to dominate.

## 2026-07-22 - Colony Bottleneck Replan Loop Observed

- Area: retained path-agent movement under dense dynamic occupancy.
- Evidence: the interactive colony demo was observed at roughly 900 agents
  slowing to 18-36 ms per simulation tick and then remaining at a stable
  partial-arrival count behind a painted bottleneck. Code inspection identifies
  a closed lifecycle: each occupied next step makes the agent `Blocked`; the
  next tick replans that agent; occupancy-blind A* returns `Found`; applying
  that result resets `blocked_retries`; and the same occupied step can fail
  again indefinitely. Arrived agents can make the obstruction permanent.
- Evidence after repair: the seeded 24-agent doorway regression previously
  submitted 8,600 searches across 503 planning ticks. Retrying retained steps
  reduced that to the 24 initial searches in one planning tick; within the
  bounded run every agent arrived or became explicitly `Unreachable`.
- Decision: accepted. Occupied/reserved destinations retry their retained
  step without path processing, while route-invalidating transient failures
  still re-path. All blocked modes consume one consecutive retry budget,
  successful movement resets it, and the web demo exposes terminal counts.
- Retry conditions: add richer local alternatives or occupancy-aware caller
  policies if a representative workload requires more arrivals through a
  merge; do not restore occupancy-blind per-tick re-planning.

## 2026-07-22 - Canonical Persistence Baseline

- Area: canonical authoritative-field world archives.
- Evidence: a local Release build saved a 512x512 dense world with one byte
  field and one 32-bit field (about 1.25 MiB) in a five-run median of 10.2 ms
  at 122.6 MiB/s, and preflighted plus loaded it in 9.7 ms at 128.7 MiB/s.
  Removing a redundant self-parse from the successful save path reduced its
  median from 19.0 ms while inspection remains separately testable.
- Decision: accept the scalar-at-a-time canonical codec as a cold-path
  baseline. It is endian-stable, checksummed, schema-versioned, and keeps file
  I/O outside the library. No CI timing gate is warranted until a consumer
  establishes save-size and latency requirements.
- Retry conditions: add contiguous bulk codecs for common scalar columns if
  persistence enters a latency-sensitive path or measured throughput becomes
  material for representative save sizes.

## 2026-07-22 - Area Index Baseline

- Area: graph-derived caller-keyed area grouping.
- Evidence: a local Release build grouped 256 open-chunk regions and reduced
  their directed boundary portals to 480 canonical area connections in about
  2.65 ms on a 512x512 world. Reserved warm rebuilds allocate nothing.
- Decision: accept the straightforward sort-and-reduce implementation. Area
  rebuild is derived maintenance, not a per-query hot path, and it avoids a
  second tile flood by consuming the region graph.
- Retry conditions: add incremental patching only if measured area maintenance
  becomes material in a workload with frequent topology edits.

## 2026-07-22 - Coarse Corridor and Weighted Product Baselines

- Area: shortest region-route reconstruction and persistent weighted
  multi-goal products.
- Evidence: local Release measurements on an open 512x512 world measured a
  31-chunk/30-portal coarse route at about 20.1 us, an eight-goal weighted
  product build over 262,144 reached nodes at about 24.5 ms, and exact
  corner-to-corner product replay at about 5.2 us for a 1,023-node path.
  Correctness tests cover non-monotone corridors, sparse missing topology,
  provider-composed reverse edges, cache invalidation, and allocation-free
  warm rebuild/reconstruction.
- Decision: accept coarse corridor reconstruction, weighted product caching,
  and the opt-in runtime selector. Keep the runtime default off: a full dense
  product build is a substantial up-front cost and only amortizes when reuse
  spans enough requests or processing calls.
- Retry conditions: calibrate CI thresholds from main-branch benchmark
  artifacts before making these new measurements regression gates. Revisit
  automatic selection only with representative stable-map reuse traces.

## 2026-07-22 - Span Queries Promoted; Maintenance Hook Rejected

- Area: rectangular/radius query callbacks, fused block pipelines, and
  coalesced derived-state maintenance.
- Evidence: 100,000 seeded queries match reference tile sets across top-down,
  vertical, and 3D shapes. Five-repetition local medians measured rectangular
  spans at 678 ns versus 213,076 ns per tile, radius spans at 1,789 ns versus
  157,203 ns per tile, and a fused pipeline at 417 ns versus 1,840 ns through
  an allocating intermediate. The coalescing backend reduced 512 dense
  schedules to one execution and measured 2,499 ns versus FIFO's 5,139 ns,
  but 256 distinct sparse tasks measured 21,069 ns versus immediate's 517 ns.
- Decision: accept public span emitters and fused pipelines. Keep the
  maintenance interface and immediate/FIFO/coalescing prototypes in
  `tess::experimental::maintenance`; do not integrate a scheduler hook into
  world storage because the prototype misses the mandatory sparse gate by a
  wide margin. Predicate bitsets and chunk summaries remain deferred because
  no authoritative predicate contract or mutation-cost evidence exists yet.
- Retry conditions: revisit maintenance promotion with O(1) intrusive or
  indexed deduplication and measured p95 latency on at least two realistic
  dirty-chunk scenarios. Revisit predicate acceleration when a consumer has a
  stable derived predicate whose full-map, sparse-query, mutation, and memory
  costs can be measured against the historical 4x/2x/10% gates.

## 2026-07-21 - Documentation-Only CI Fast Path

- Area: pull requests and main pushes that change only maintained
  documentation.
- Evidence: the implementation pull request classified its workflow and Python
  changes as code-affecting in six seconds, then ran and passed every existing
  platform, analysis, and benchmark job. The documentation-only proof then
  completed its classifier in 6s, hook backstop in 18s, and aggregate gate in
  2s; the six compiled job groups skipped before matrix expansion. The
  independent documentation build and C/C++ security analysis set the complete
  required-check critical path at 1m1s, down from the 16m47s code-path run.
- Decision: Accepted. Keep the narrow `docs/**`, Markdown, and `mkdocs.yml`
  allowlist.
  Keep the classifier, hook backstop, documentation build, and aggregate gate
  on every change; skip compiled jobs only after a complete Git diff matches
  the allowlist.
- Risk: a classification bug could suppress relevant signal. Empty changes,
  invalid revisions, Git errors, and any unmatched path therefore require full
  CI; renames are evaluated as delete plus add. Revisit the allowlist only when
  another file class has an independent required check with equivalent signal.

## 2026-07-21 - CI Critical-Path Work Separated From Calibration

- Area: required clang-tidy and benchmark jobs on pull requests.
- Evidence: a protected pull-request run completed required clang-tidy in
  40m41s using serial Unix Makefiles. The benchmark thresholds finished at
  about eight minutes, but ten-repetition non-gating baseline collection kept
  that required job running for 32m21s. A two-job clang-tidy trial passed in
  22m18s. A four-job trial then passed in 15m58s, a 61% reduction from the
  serial run. Suppressing PR baseline collection reduced the benchmark job to
  8m21s.
- Decision: Accepted. Match the public runner's four CPUs with a four-job cap
  for required clang-tidy, and collect benchmark calibration artifacts only on
  code-affecting `main` runs. Every benchmark threshold remains required on
  code pull requests and code-affecting main pushes; documentation-only merges
  do not produce redundant calibration artifacts.
- Risk: four clang-tidy processes increase peak memory and can interleave
  diagnostics. The public runner supplies 16 GB, and the explicit cap prevents
  unbounded parallelism. Pull-request-specific baseline artifacts are no
  longer available, but merge-commit artifacts remain comparable on the same
  runner family. Full required clang-tidy still takes about 16 minutes; retain
  it for code changes because tests and examples provide
  template-instantiation coverage that a small representative target would
  miss, and skip it only when a fail-closed change classifier proves a change
  is documentation-only.

## 2026-07-21 - Advisory Analysis Removed From Per-Commit CI

- Area: GitHub Actions advisory clang-tidy analysis.
- Evidence: the advisory preset duplicated the full-tree compilation done by
  the required clang-tidy gate with a broader, intentionally noisy rule set.
  Recent pull-request and main runs each spent about 40 minutes on this
  non-blocking job.
- Decision: Accepted. Keep the preset and its signal, but run it weekly or on
  manual request instead of on every pull request and main push.
- Risk: New advisory findings can remain undetected until the weekly run.
  Required low-noise clang-tidy checks continue to run on every change.
