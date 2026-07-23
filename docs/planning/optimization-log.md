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

## 2026-07-12 - Transactional Portal Segment Cache

- Area: weighted portal segment-cache insertion and stale compaction under
  allocation failure.
- Hypothesis: constructing dependencies off-cache and reserving complete
  compacted entry/path storage before mutation provides the strong exception
  guarantee without materially slowing successful stores or warm hits.
- Evidence: deterministic allocation-failure tests reject every successive
  allocation in `store()` and stale compaction, proving that all observable
  entries, paths, and statistics remain unchanged until one transaction
  succeeds. Matched same-host Release binaries used 10 repetitions and a
  0.1-second minimum. Median CPU time was 3288 -> 3300 ns (+0.36%) for the
  warmed single route and 3,708,932 -> 3,738,375 ns (+0.79%) for the 100-agent
  shared-cache batch.
- Decision: Accepted. Transactional construction, capacity reservation, and
  compaction remain below the repository's 5% regression limit.

## 2026-07-12 - Plan-Bound Phase Validation and Dirty Exception Safety

- Area: queued phase dispatch after replacing forgeable aggregate ranges with
  planner-issued capabilities bound to one `ExecutionPlan`, plus conservative
  dirty recording when callbacks throw.
- Hypothesis: one phase-level world stamp and compact policy mask can reject a
  stale, foreign-world, or mixed-policy phase before any side effect without
  rescanning operations. Record-only scratch partitions should retain the
  original cache density, while one scratch stamp protects later merge.
- Evidence: final matched `-O3` binaries ran in five alternating same-host
  process pairs, five repetitions per process, and a 0.05-second minimum.
  Median CPU times were 945.45 -> 928.24 ns (-1.82%) for the deliberately
  dispatch-heavy 256-operation serial tile touch, 117.24 -> 118.20 ns (+0.82%)
  for direct queued execution, 136.45 -> 136.63 ns (+0.14%) with results, and
  507.97 -> 516.00 ns (+1.58%) for integrated auto-exec. A separate final
  five-pair executor check measured tile touch at 5495.70 -> 5494.71 ns
  (-0.02%) on the worker pool and 27712.20 -> 27890.49 ns (+0.64%) with scoped
  threads.
- Decision: Accepted. Raw direct `PlannedOperation` execution retains its own
  O(1) validation. Planner-issued phases preflight generation, range, world,
  and every policy before dispatch; normal dirty merge still coalesces once per
  chunk. Dirty metadata is recorded before callbacks, and AutoExec uses an
  allocation-free, coalescing cold merge while preserving the original
  exception. Every successful-path change remains below the 5% regression
  limit.

## 2026-07-12 - Movement-Class-Safe Portal Segment Cache

- Area: weighted portal segment-cache hits after binding entries to one
  movement class.
- Hypothesis: one precomputed type-token comparison per segment operation
  prevents cross-class aliasing without materially changing same-class hits.
- Evidence: paired same-host Release binaries, 10 repetitions and 0.2-second
  minimum time. Single-route median CPU time was 3275 -> 3332 ns (+1.74%);
  batch-100 was 3,737,101 -> 3,792,253 ns (+1.48%). Warm same-class lookup
  remained allocation-free.
- Decision: Accepted. Both changes are below the 5% serial-regression limit;
  alternating movement classes safely rebind and clear the single-owner cache.

## 2026-07-12 - Checked Queued-Plan World Binding

- Area: immutable planned operations and shape-bound deferred dirty records.
- Rejected experiment: tracking and rescanning the maximum key in every record
  and plan made scheduler auto-exec about 36% slower. Checked construction and
  private validated recording already guarantee the bound, so that redundant
  bookkeeping was removed.
- Evidence: five alternating same-host processes with 10 repetitions each.
  Direct queued execution was 116.65 -> 116.89 ns (+0.2%), result-bearing
  execution was 134.82 -> 137.07 ns (+1.7%), and scheduler auto-exec was
  509.39 -> 469.15 ns (-7.9%).
- Decision: Accepted the O(1) shape/chunk stamp without maximum-key scans; all
  measured paths remain inside the 5% regression limit.

## 2026-07-12 - Exception-Safe Result Cleanup

- Area: retryable result drains and auto-exec completion cleanup on exceptions.
- Rejected experiment: a whole-function scope guard added exception safety but
  inhibited hot-path optimization, regressing integrated auto-exec by more than
  14%. The accepted form keeps explicit normal-path clears and uses cold
  catch/rollback paths.
- Evidence: 11 alternating same-host process pairs, five repetitions per
  process. Median CPU times were 117.0105 -> 117.1695 ns (+0.14%) direct,
  136.2933 -> 135.9555 ns (-0.25%) result-bearing, and 509.1044 -> 474.5871 ns
  (-6.78%) auto-exec. A separate matched successful-worker-pool A/B used five
  alternating process pairs, seven repetitions, and a 0.05-second minimum.
  Median real times were 21,727 -> 21,948 ns (+1.02%) chunk fill, 454,948 ->
  454,724 ns (-0.05%) chunk compute, and 12,342 -> 12,536 ns (+1.57%) tile
  touch.
- Decision: Accepted. Exception cleanup no longer leaks result slots, a
  throwing visitor remains retryable, and no measured path regresses by 5%.

## 2026-07-12 - Sparse Local-Topology Residency Guard

- Area: reject direct local-topology builds for non-resident sparse chunks
  before page access.
- Evidence: eight alternating optimized process pairs, five repetitions each.
  The deliberately overhead-heavy 1x1 local build was 10.69 -> 11.05 ns
  (+3.35%); a normal 32x32 chunk was 7,644.30 -> 7,650.53 ns (+0.08%). A
  fully resident 512x512 sparse graph build was 3.581 -> 3.543 ms (-1.06%),
  and one-chunk incremental update was 1.914 -> 1.910 ms (-0.21%).
- Decision: Accepted without an unchecked internal split. The safety check is
  below 5% even in the artificial one-tile case and flat in real graph work.

## 2026-07-12 - ChunkMeta Hot/Cold SoA Split (M5, v0.2.0)

- Area: Chunk metadata layout; collect_dirty/active_chunks scans
  (audit-2026-07-11 M5, deferred from the audit stack for the
  version-bump decision; shipped as the 0.2.0 development minor bump).
- Hypothesis: The flag scans stream 80-byte ChunkMeta structs to test
  one 4-byte word; SoA flag columns put 16 chunks per cache line, and
  moving the cold Box3 bounds out shrinks every other meta touch.
- Evidence (paired interleaved A/B, local arm64, release): the existing
  256-chunk `storage/world_dirty_chunks_iteration` is cache-resident
  and flat (125 ns both) -- expected, its whole metadata array fits the
  fast levels. New streaming-scale
  `storage/world_dirty_chunks_iteration_16k` (16384 chunks; sized past
  L1 on Codex review -- a first 4096-chunk cut showed only ~8% because
  both layouts still cached): 10.19 -> 7.64 us (1.33x); mark/clear and
  metadata lookups flat; no storage-family regressions. ChunkMeta
  shrinks 80 -> 20 bytes, cutting every other meta touch's footprint.
- Decision: Accepted (structural + user-directed API split). The
  maintained dirty-chunk SET killing the O(chunk_count) scan floor
  remains the recorded design-level ceiling in the future backlog.

## 2026-07-12 - Page-by-Slot Passability/Entry-Cost Threading (Rejected)

- Area: Sparse field reads in the A*/flood relaxation loops (the M10
  follow-up: is_passable_index/tile_entry_cost_index re-probe the chunk
  directory per neighbor even though resident_offset just resolved the
  same chunk).
- Hypothesis: A NodeIndexSpace offset already encodes
  (slot, local tile); decoding it (two integer ops) and reading the
  page via a new SparseWorld::page_at_slot kills 1-2 directory probes
  per neighbor -- the presumed source of the ~2x dense-vs-sparse batch
  gap noted in the M2 entry.
- Evidence (paired interleaved A/B, local arm64, release): implemented
  offset-addressed read leaves (dense delegating to the *_index leaves
  unchanged) and threaded them through the unweighted A*, weighted A*,
  and bounded-flood relaxations; 684/684 tests green. Every touched
  bench FLAT within noise (0.99-1.01x): weighted_astar_sparse_blockers
  1.928 -> 1.928 ms, weighted multigoal sparse batch 464.9 -> 461.8 ms,
  sparse-resident planner batch 89.9 -> 88.7 ms, dense controls
  unchanged. The directory probe the field reads repeat is absorbed by
  out-of-order execution behind the mandatory resident_offset probe and
  the page-data cache misses; it was never the gap.
- Decision: Rejected; reverted (no-win rule). The dense-vs-sparse gap
  hunt moves to the remaining candidates: the resident_offset probe
  itself (unavoidable without an index restructure) and scattered
  page memory vs the dense world's contiguous pages.

## 2026-07-12 - Portal Segment Cache Open-Addressed Index (Rejected)

- Area: `WeightedPortalSegmentCache::find` (the M10 follow-up: lookup
  and the store dup-check linear-scan up to the 256-entry budget).
- Hypothesis: An open-addressed (start, goal) index turns the O(budget)
  scans into O(1) probes.
- Evidence (paired interleaved A/B, local arm64, release): implemented
  (u32 slot table, half-full, most-recent-per-key -- semantically
  equivalent since stale entries never re-validate under monotonic
  chunk versions; all cache tests passed) and measured:
  `path/weighted_portal_segment_cache_batch_100_room_portals_512x512`
  flat (3.72 -> 3.69 ms, within noise -- segment-cache time is
  negligible against the per-segment A* work), and the single-request
  `..._room_portals_512x512` REGRESSED 8% (3.26 -> 3.54 us): with the
  handful of live segments the benches actually produce, the hash +
  probe costs more than the short linear scan.
- Decision: Rejected; reverted (same policy as M9). Revisit only if a
  profile ever shows find() hot with a near-budget cache -- and pair it
  with the pair-tag conversion entry below if both land.

## 2026-07-12 - Portal-Route Pair-Tag -> MovementClass Conversion (Deferred)

- Area: `portal_route.h` builders + `WeightedPortalSegmentCache`
  (recorded S11 backlog note; no code change).
- Status: The unit-route runtime binds a normalized movement class and
  the field-product cache folds class identity into its keys, but the
  portal-route builders are still pair-tagged (<PassableTag, CostTag>)
  and the portal segment cache keys segments on request + chunk
  versions only -- callers reusing one cache across classes (or tag
  pairs) must keep one cache per class (documented in
  `docs/architecture/path.md` and `path_runtime.h`).
- Conversion sketch, when profiles or a misuse report justify it:
  (1) add `<World, Class>` builder overloads that resolve the class's
  tag pair exactly as `weighted_astar_path<World, Class>` does; (2) fold
  the normalized class identity (`tess::detail::tag_identity`-style)
  into `WeightedPortalSegmentCache`'s segment key so one cache serves
  many classes safely; (3) deprecate the raw pair-tag overloads after
  the consumer migrates. (The open-addressed segment-index idea this
  once composed with was tried and rejected the same day -- see the
  entry above.)
- Decision: Deferred -- the per-class-cache contract is documented and
  cheap; no evidence of misuse or profile cost today.

## 2026-07-12 - Per-Agent Pathing Dirt + Retained Routes

- Area: Path-agent tick drivers (the S11.4 backlog item: the shared
  PathAgentTickState::pathing_dirty flag meant ONE goal re-arm or
  blocked step replanned the ENTIRE batch next tick).
- Hypothesis: Goal arming is agent-scoped -- only that agent needs a
  plan; a WORLD change is what genuinely invalidates everyone. With
  per-agent routes retained across processing passes, a scoped pass can
  replan just the needy agents.
- Evidence: PathSubmitScope::NeedsOnly submits only NeedsPath/Blocked
  agents; Following agents keep walking routes retained in the new
  PathAgentRoutes pool (the runtime rebuilds its result storage per
  pass, so retention is what makes selective submission sound; route
  vectors keep capacity, warm churn ticks measured allocation-free).
  mark_pathing_dirty stays world-scoped and forces the full replan; the
  two-arg set_path_agent_goal(state, agent, goal) no longer touches it.
  New `path/agent_tick_100_weighted_goal_churn_512x512` (one re-arm per
  tick, 100 weighted agents, mixed map): 72.5 -> 17.3 ms per tick
  (4.2x, paired interleaved A/B local arm64; the residual is the single
  re-armed agent's own weighted search, which dominates). On
  building-dense maps the pre-split cost scaled with the whole batch.
- Decision: Accepted. Deliberately NOT converted: the tick_ecs_*
  drivers keep All-scope processing -- their batch is re-collected from
  the registry each tick, so index-paired retention would break on
  entity add/remove; converting them needs entity-keyed route retention
  (follow-up if consumer profiles ask for it). CONTRACT: span-based
  callers that reorder/remove agents between ticks must
  mark_pathing_dirty (documented on PathAgentRoutes).

## 2026-07-12 - Intrusive LRU + ECS Hash/Lookup Cuts

- Area: Sparse eviction and ECS adapter hot paths (audit-2026-07-11
  M11b + ecs lows).
- Evidence (paired A/B, local arm64): intrusive doubly-linked LRU
  replaces the O(resident_count) timestamp scan --
  residency/eviction_churn_512 400 -> 114 ns (3.5x) and now
  capacity-independent (churn_64 100 ns vs churn_512 114 ns; the old
  scan grew linearly). Documented tradeoff: ensure_resident hits pay
  the MRU splice (2.75 -> 4.21 ns worst-case round-robin; the
  already-MRU early-out helps workloads with locality). Occupancy-index
  probe_start hashes lanes in parallel (one avalanche over per-lane
  multiplies, was three chained mix rounds): ecs/index_move
  8.18 -> 5.01 ns. EnTT collect caches PathState* from the view walk
  (addresses stable; only the entry vector is sorted in between):
  ecs/tick_entt_10k 687 -> 586 us (~15%).
- Decision: Accepted, all three.

## 2026-07-12 - Worker Pool: Padded Counters, Run Claiming, Bounded Wakeups

- Area: WorkerPoolPhaseExecutor dispatch overhead (audit-2026-07-11 M8).
- Hypothesis: Adjacent hot atomics ping-pong one cache line; per-op
  fetch_add and notify_all-everyone amplify coherence and wakeup traffic
  on phases of cheap operations.
- Evidence: alignas(128) on next_offset_/finished_operations_ (128 to
  cover Apple Silicon lines and the x86 adjacent-line prefetcher; the
  A/B below ran at 64, where line pairing was allocation-dependent),
  claiming runs of ~count/(workers*4) ops per RMW (one release-add
  publishes the run), waking min(runs, workers) threads, and
  last-worker-only
  completion notify. Paired A/B (local arm64, real_time):
  tile_touch_pool_w4 23.9 -> 12.4 us, chunk_fill_pool_w4 44.7 -> 22.1 us
  (~2x); chunk_compute_pool_w4 flat (compute-bound, as expected). TSan
  suite green; memory ordering unchanged (release-add chain + mutex
  handshake).
- Decision: Accepted.

## 2026-07-12 - A* Interleaved Node Record (Rejected)

- Area: A* per-node state layout (audit-2026-07-11 M9).
- Hypothesis: Interleaving the hot {generation, g, state} trio into one
  12-byte record cuts per-relaxation cache lines from up to three
  parallel arrays to one.
- Evidence: Measured 3-9% SLOWER across the path family (local arm64:
  weighted_astar_sparse_blockers 1.94 -> 2.11 ms, room_portals
  10.50 -> 10.84 ms, astar_open_2d_512x512 2.11 -> 2.13 us).
  Partial-field visits dominate: closed-neighbor checks read only
  generation+state, and the packed generation_ array keeps 16 entries
  per cache line vs 5 records. Reverted; a comment in PathScratch
  records the outcome.
- Decision: Rejected. Keep the parallel arrays.

## 2026-07-12 - Sparse Neighbor Single-Probe + Reached Counter

- Area: A* / distance-field neighbor loops (audit-2026-07-11 M10 and
  the sparse-probing low).
- Evidence: NodeIndexSpace::resident_offset folds the residency test
  and offset computation into one directory probe (was two); paired
  A/B on the sparse-resident multigoal batch: 93.9 -> 90.2 ms (~4%).
  PathScratch::touched_ became a plain counter (only its size was ever
  read; DistanceFieldScratch keeps its list for dependency capture) --
  neutral-to-positive on dense benches, removes a world-sized scratch
  allocation. NOTE: local absolute numbers drifted ~10% across this
  long session (thermal); only paired A/B runs were trusted.
- Decision: Accepted. Follow-ups logged, not implemented: thread the
  resolved slot into passability/entry-cost reads (needs a page-by-slot
  accessor; remaining sparse gap lives there), and an open-addressed
  index for WeightedPortalSegmentCache's O(256) linear lookup/store
  scans (bench exists: weighted_portal_segment_cache_batch family).
  The unweighted distance-field build/read loops and the boxed weighted
  build still use the split is_resident_index+offset double probe --
  deliberate: none showed in the profiles that motivated M10; convert
  alongside the page-by-slot work if they ever do.
  Also declined: moving the weighted relaxation's entry-cost read
  behind the g-comparison (audit low) -- tentative_g is computed FROM
  the entry cost, so the reorder is impossible.

## 2026-07-11 - Weighted Batch Settle-Target Early Termination

- Area: Shared-goal weighted batches with starts clustered near the goal.
- Hypothesis: The goal-rooted bounded field floods the entire reachable
  component, but the batch knows every consumer start up front; stopping
  once all group starts settle (Dijkstra settlement is final) bounds the
  flood to the agents' region on open maps (audit-2026-07-11 M3, the
  S11.4 soak observation).
- Evidence: New `path/weighted_batch_planner_100_neargoal_open_512x512`
  drops from 5.79 ms to 49 us locally (~118x); the far-goal multigoal
  batches (dense and sparse-resident) are unchanged within noise. New
  unit tests pin oracle-identical results, full-flood behavior for
  unreachable members, and that invalid starts are excluded from the
  settle set (failing-first: 1024 reached vs the <200 truncated bound).
  Early exit arms only under TreatAsBlocked; Indeterminate floods must
  still discover missing-chunk boundaries. The MaxCost-overflow fallback
  to the unbounded builder drops targets (rare; floods fully, correct).
- Decision: Accepted for weighted_path_batch (batch-local scratch). The
  retained-field two-call build/read API keeps full floods.
- Follow-up: consider settle targets for the runtime's repeated-goal
  unit-cost field products if profiles show the same tail.

## 2026-07-11 - Batch Residency Fingerprint Hoist (Structural)

- Area: Sparse weighted batches; per-member field reads.
- Hypothesis: Recomputing the O(resident_count) residency fingerprint
  per member (and 3 directory probes per chunk inside it) is avoidable
  work (audit-2026-07-11 M2).
- Evidence: Slot-direct fingerprint iteration plus a once-per-group
  debug assert replacing per-member verification. On the new 256-chunk
  sparse-resident batch bench the effect is within noise (~0.2 ms of a
  92 ms batch): at this scale the fingerprint term is dwarfed by
  per-neighbor directory probes inside the flood itself
  (NodeIndexSpace re-probes; audit path-micro item, W4). The term
  scales O(requests x resident_count), so the fix is kept as a scaling
  guard with no bench regression.
- Decision: Accepted (structural; no measurable local win, none
  expected at bench scale). The 2x dense-vs-sparse batch gap the new
  bench exposes is the W4 slot-threading target, not fingerprint cost.

## 2026-06-07 - Concurrent Phase Backend Library Spike

- Area: Tile-world queued operation phase execution.
- Hypothesis: A small external concurrency primitive might reduce risk for
  future production worker backends if it maps cleanly to Tess's current
  phase contract: execute one contiguous planned-operation index range,
  complete or join before returning, keep per-operation dirty/result scratch
  caller-owned, and avoid hidden cancellation or lifecycle semantics.
- Evidence: Reviewed current upstream state for
  `buildingcpp/work_contract` at commit
  `3f56a17e36db57846a086e20d8788478287f3c86` and
  `buildingcpp/signal_tree` at commit
  `f7b59510e117bc6156af86a6b8689ca4a3832e3c`. `signal_tree` is a concurrent
  readiness set: it selects ready signal ids, is idempotent, does not store
  work payloads, and leaves worker waiting/join/result handling to the caller.
  That is useful scheduler infrastructure, but it is not a phase executor.
  `work_contract` is closer to a scheduler because it manages recurrent
  schedulable contracts with non-blocking and blocking groups, explicit async
  release, thread-local reschedule/release controls, and exception callbacks.
  Those lifecycle features are stronger than Tess needs for the current phase
  adapter and would introduce semantics not yet covered by Tess tests.
- Decision: Deferred. Keep the internal `ExecutorPhaseRange` /
  `for_each_operation(first, count, fn)` adapter as the production-facing
  contract for now, with `SerialPhaseExecutor` and test-only threaded
  executors covering the memory and ordering rules. Do not add either
  dependency in this slice.
- Follow-up: Revisit `work_contract` only after Tess has a production worker
  backend prototype that needs recurrent work handles or blocking wakeups, and
  first prove scoped phase completion, deterministic result reduction, and
  shutdown behavior in Tess-owned tests. Revisit `signal_tree` only if a later
  scheduler has many stable ready lanes where idempotent readiness and
  non-FIFO fairness are the real bottleneck.

## 2026-06-05 - Weighted Shared-Goal Distance Field

- Area: Weighted many-agent shared-goal pathfinding.
- Hypothesis: Reverse Dijkstra over positive entry costs should amortize
  weighted pathfinding for many starts sharing one goal, similar to the
  unit-cost distance-field win.
- Evidence: Added `build_weighted_distance_field` and
  `weighted_distance_field_path`, plus correctness and allocation tests against
  weighted A*. On the 512x512 weighted sparse shared-goal batch,
  independent weighted A* runs around 236-238 ms for 100 agents, while the
  weighted field batch runs around 15.5 ms. Diagnostics drop from about
  10.3M neighbor candidates, 5.0M passability checks, and 4.0M heap pushes to
  about 859k neighbor candidates, 367k passability checks, and 243k heap
  pushes.
- Decision: Accepted. Weighted shared-goal fields are the first choice for
  weighted batches with substantial goal reuse.
- Follow-up: Add weighted multi-goal field grouping when weighted workloads
  have a small set of repeated goals.

## 2026-06-05 - Weighted Batch Planner API

- Area: Weighted many-agent request dispatch.
- Hypothesis: A public batch helper can make the accepted weighted reuse
  policy explicit: use one bounded weighted field per repeated goal and use
  weighted A* for singleton goals.
- Evidence: Added `weighted_path_batch` and stable-result batch scratch. Unit
  coverage verifies grouped repeated goals, singleton fallback, stable result
  spans, and cost agreement with weighted A*. The 100-agent eight-goal sparse
  benchmark runs around 46.5 ms, matching the hand-grouped bounded field path.
- Decision: Accepted. This is the default API for current weighted batches
  with repeated goals.
- Follow-up: Add hierarchy/topology when many unique far goals make one field
  per goal too expensive.

## 2026-06-05 - Supplied-Waypoint Portal Route Product

- Area: Weighted room/portal route products.
- Hypothesis: If topology supplies portal waypoints, stitching exact weighted
  A* segments through those waypoints should reduce tile search volume while
  keeping the general weighted A* fallback unchanged.
- Evidence: Added `WeightedPortalRouteProduct`, build/replay tests, dependency
  invalidation tests, and room-portal benchmarks. On the 512x512 weighted
  room-portal case, normal weighted A* runs around 10.8 ms with about 139k
  expanded nodes. The supplied-waypoint product build runs around 1.0 ms with
  about 17k expanded nodes, and replay is around 10 ns.
- Decision: Accepted as a product primitive. It is exact for the supplied
  waypoint route but not a general optimal portal query.
- Follow-up: Add a topology-owned portal graph builder that chooses waypoints
  and proves when the waypoint route is globally optimal or acceptable.

## 2026-06-05 - Chunk-Boundary Portal Route Product

- Area: Weighted room/portal route products.
- Hypothesis: Deriving waypoints from adjacent chunk-boundary portals should
  make the topology MVP measurable without requiring callers to supply every
  portal coordinate.
- Evidence: Added `build_weighted_chunk_portal_route_product`, unit coverage
  for automatic boundary selection, and 512x512 weighted room-portal build and
  replay benchmarks.
- Decision: Accepted as a minimal topology product. It verifies each segment
  with weighted A*, but chooses one axis-ordered chunk route and therefore is
  not a full shortest-path portal graph.
- Follow-up: Add a persistent graph with alternate chunk/portal routes only
  after profiling shows this boundary-derived MVP is insufficient.

## 2026-06-05 - Portal Candidate Selection And Segment Reuse

- Area: Weighted room/portal route products.
- Hypothesis: Comparing a small set of chunk-boundary portal candidates should
  improve route choice without adding meaningful overhead, and warmed segment
  reuse should avoid repeated A* work for stable supplied-waypoint portal
  routes.
- Evidence: Added six-order chunk-boundary candidate selection, a greedy
  monotone candidate, candidate and scan counters, route cost-ratio counters,
  an isolated candidate-selection benchmark, and warmed portal segment-cache
  benchmarks. On the 512x512 weighted room-portal case, candidate selection
  scans 7,456 boundary tiles in about 7.4 us. The greedy candidate drops the
  full chunk-boundary product from around 1.0 ms, 17.4k expanded nodes, and a
  1.37 route-cost ratio to around 0.72 ms, 12.6k expanded nodes, and a 1.12
  route-cost ratio. Warmed single-route portal segment-cache rebuilds run
  around 11.3 us with zero expanded nodes. A 100-agent shared portal-leg batch
  with exact segment reuse runs around 4.4 ms with about 566 expanded nodes per
  agent. Isolating the endpoint segments shows the unique start-to-first-portal
  A* searches alone run around 2.3 ms with about 398 expanded nodes per agent,
  while the shared last segment expands about 45 nodes. Replacing those unique
  first segments with one exact local-domain weighted field to the first portal
  plus cached shared portal legs drops the 100-agent batch to about 2.1 ms; the
  local field expands about 1,025 nodes once and reconstruction averages about
  39 nodes per agent.
- Decision: Accepted. Candidate selection is cheap enough to keep, and segment
  reuse is useful for repeated stable portal routes. The high cost ratio means
  this is still a product/throughput primitive, not a global optimal portal
  planner.
- Follow-up: Improve waypoint quality with a real portal graph or weighted
  portal-edge summaries before optimizing the remaining segment A* cost.
  Promote local-domain portal-entry fields into a product API only after the
  topology layer owns room/region domains.

## 2026-06-05 - Exact Weighted Route Products

- Area: Route-product dependency support.
- Hypothesis: Storing a verified weighted path plus chunk-version
  dependencies gives a safe route product primitive without claiming
  region-selective optimality.
- Evidence: Added `WeightedRouteProduct`, build/replay helpers, and tests that
  replay succeeds across unrelated chunk edits and fails when a captured chunk
  changes.
- Decision: Accepted as an exact product primitive. It is not a general
  weighted route cache because an unrelated edit could create a shorter route.
- Follow-up: Use this dependency shape in future topology/portal products,
  where the product can record enough evidence to prove optimal reuse.

## 2026-06-05 - Weighted Unit-Cost Axis Detour Fast Path

- Area: Weighted A* local fast paths.
- Hypothesis: If an axis-aligned straight line is blocked and a one-tile
  parallel detour has only unit entry costs, returning that detour is optimal
  under positive axis-adjacent movement.
- Evidence: Added a weighted detour fast path and unit coverage. The existing
  weighted axis-detour benchmark is unchanged because it covers expensive
  direct terrain, not a blocked unit-cost line.
- Decision: Accepted with a narrow guard. Do not apply this to merely
  expensive direct terrain.
- Follow-up: Add a dedicated blocked weighted detour benchmark only if this
  path appears in production-style workloads.

## 2026-06-05 - Weighted Multi-Goal Field Grouping

- Area: Weighted many-agent pathfinding with a small set of repeated goals.
- Hypothesis: Building one weighted field per unique goal should still beat
  independent weighted A* when a batch has several repeated goals.
- Evidence: Added 100-agent sparse weighted benchmarks with eight unique
  goals. Independent weighted A* runs around 470-504 ms locally; grouped
  weighted fields run around 119-133 ms with the same reconstructed paths.
  The grouped field path still performs one full weighted field build per
  unique goal, so cost scales with unique goals rather than agents.
- Decision: Accepted. Group requests by goal and build one weighted field per
  goal when weighted batches have repeated destinations.
- Follow-up: Use bounded weighted field construction for small integral costs;
  use topology or hierarchy for batches with many unique far goals.

## 2026-06-05 - Bounded Weighted Distance-Field Buckets

- Area: Weighted shared-goal field construction for small positive costs.
- Hypothesis: A Dial-style bucket queue should reduce binary heap traffic when
  weighted entry costs are bounded small integers.
- Evidence: Added `build_bounded_weighted_distance_field` and correctness,
  fallback, allocation, benchmark, and threshold coverage. On the 512x512
  sparse weighted shared-goal batch, general weighted field construction runs
  around 15.1 ms while the bounded field runs around 6.3 ms. On the eight-goal
  grouped sparse batch, general weighted fields run around 118.8 ms while
  bounded fields run around 46.6 ms.
- Decision: Accepted for exact bounded-cost weighted field builds. It falls
  back to the general weighted builder if a reached tile exceeds `MaxCost`.
- Follow-up: Profile bucket occupancy and modulo-collision churn only if
  bounded weighted field benchmarks regress or weighted costs grow beyond the
  current small-cost assumption.

## 2026-06-05 - Explicit Chunk Version Dependencies

- Area: Route-product support.
- Hypothesis: A small public helper for chunk/version dependencies can support
  future route products without changing current route-cache semantics.
- Evidence: Added `ChunkVersionDependencies` with explicit chunk capture,
  whole-world capture, and validation tests. The helper correctly remains
  valid across unrelated chunk edits and invalidates when a captured chunk
  version changes.
- Decision: Accepted as supporting code only. Current route-cache hits still
  rely on conservative caller invalidation or whole-world fingerprints.
- Follow-up: Wire dependencies into cached route products only after the
  product records enough route/topology evidence to prove reuse remains
  optimal.

## 2026-06-05 - Weighted Unit-Cost Direct Fast Path

- Area: Weighted A* common-case latency.
- Hypothesis: If a direct Manhattan route is passable and every entered tile
  has entry cost 1, returning it is optimal because no positive-cost
  axis-adjacent path can beat Manhattan distance.
- Evidence: `path/weighted_astar_open_512x512` dropped from about 60 us to
  about 2.9 us. Expensive-axis and weighted obstacle cases still fall back to
  general weighted A* and keep their previous behavior.
- Decision: Accepted. This protects unit-cost maps using the weighted API
  without weakening the general weighted optimality path.
- Follow-up: Consider exact weighted detour or gap fast paths only when the
  optimality proof is as local and cheap as the direct unit-cost proof.

## 2026-06-05 - Route Cache World-Version Fingerprint

- Area: Route-cache dependency support.
- Hypothesis: A coarse whole-world chunk-version fingerprint gives callers a
  correct invalidation hook without pretending region-selective route
  validation is solved.
- Evidence: Added `capture_world_versions(world)` and
  `invalidate_if_world_changed(world)` on `RouteCacheScratch`. Unit coverage
  verifies that a chunk version change drops cached route entries while
  preserving hit/miss counters.
- Decision: Accepted as conservative support. It is opt-in and does not change
  existing route-cache hit behavior.
- Follow-up: Replace whole-world fingerprints with explicit route-product
  chunk dependencies only after topology/portal products are designed.

## 2026-06-05 - Weighted Portal Topology

- Area: Weighted room/portal single-query performance.
- Hypothesis: The weighted room-portal case needs a graph or portal product
  rather than more local A* bookkeeping.
- Evidence: The weighted room-portal single path remains around 11 ms and is
  dominated by search volume. The accepted weighted field helps shared-goal
  batches, but a single weighted route through many rooms still expands a
  large tile search.
- Decision: Deferred. Implementing weighted portal topology would add the
  extra data structures that are intentionally outside the current A* and
  supporting-code scope.
- Follow-up: Design chunk/room portal products with weighted edge summaries,
  movement class keys, and version dependencies before adding a topology-aware
  weighted query.

## 2026-06-05 - Weighted A* Stress Profiling

- Area: Weighted A* benchmarks and diagnostics.
- Hypothesis: Weighted entry costs need stress cases beyond open-grid and
  single-axis detour paths before choosing another open-set optimization.
- Evidence: Added weighted sparse-blocker, room-portal, and 100-request mixed
  batch benchmarks. Corrected diagnostics show no allocations in warm runs.
  The weighted sparse case runs around 2.0 ms and performs about 98k neighbor
  candidates, 49k passability checks, 50k cost reads, and 41k heap pushes. The
  weighted room-portal case runs around 10.7-12.4 ms and performs about 554k
  neighbor candidates, 157k passability checks, 269k cost reads, 224k heap
  pushes, and 219k heap pops. The weighted 100-request mixed batch runs around
  509-588 ms with about 20.8M neighbor candidates and 7.5M heap pushes.
- Decision: Accepted the benchmark and diagnostic coverage. The specific
  bottleneck is search volume plus memory-heavy world/cost reads and binary
  heap traffic, not allocations or floating-point arithmetic.
- Follow-up: Any single-query path benchmark above 1 ms remains an
  investigated bottleneck. Next useful work is reducing weighted search volume
  through exact domain-specific fast paths or weighted shared-goal products,
  while preserving the current unit-cost regression gates.

## 2026-06-05 - Weighted A* Indexed Heap

- Area: Weighted A* open-set duplicate entries.
- Hypothesis: Updating open nodes in place with an indexed heap would remove
  duplicate closed pops and improve weighted stress cases.
- Evidence: The experiment eliminated weighted `diag.closed_pops` and improved
  `path/weighted_astar_room_portals_512x512` from about 10.8 ms to about
  9.0 ms. It regressed common weighted cases: open 512x512 rose from about
  60 us to 79 us, axis detour from about 22 us to 40 us, sparse blockers from
  about 2.0 ms to 2.6 ms, and the 100-request mixed batch from about 513 ms to
  613 ms.
- Decision: Rejected. The extra indexed-heap bookkeeping costs more than it
  saves for most current weighted workloads.
- Retry conditions: Reconsider only if a future profile is dominated by
  duplicate heap pops rather than neighbor/cost reads, or if a lower-overhead
  indexed open set is introduced.

## 2026-06-05 - Route Product Dependency Direction

- Area: Route-cache invalidation and future route products.
- Hypothesis: Route reuse should eventually track dependencies so stable route
  products can survive unrelated world edits without risking stale optimality.
- Evidence: Whole-cache invalidation is correct but coarse. The weighted batch
  benchmark shows independent weighted A* is too expensive for repeated agent
  workloads, while current exact/suffix route caches only reuse already-known
  paths and do not know which world edits affect them.
- Decision: Deferred as additional product data structure work. Keep current
  conservative invalidation for now.
- Follow-up: Design route products around public, non-private dependencies:
  movement class, cost/passability field identity, goal or exact request,
  touched tile/chunk keys, and chunk/version stamps. Revalidate or invalidate
  products when any dependent chunk version changes; do not attempt
  region-selective invalidation without an optimality proof.

## 2026-06-05 - Route Cache Invalidation Hook

- Area: Route-cache lifecycle support.
- Hypothesis: Cached route data needs an explicit invalidation hook so callers
  can respond to passability or movement-rule changes without also resetting
  hit/miss counters used by benchmarks and diagnostics.
- Evidence: New unit coverage verifies that `invalidate()` drops cached route
  entries, forces the next repeated query to recompute, and preserves prior
  hit/miss stats. This deliberately avoids region-selective invalidation,
  because removing a blocker outside a cached path can still create a shorter
  optimal route.
- Decision: Accepted as a supporting API. Keep conservative whole-cache
  invalidation until route products have stronger dependency tracking.
- Retry conditions: Revisit region-selective invalidation when topology,
  chunk-version dependencies, or route-product ownership are designed.

## 2026-06-05 - Weighted A* Entry Costs

- Area: Weighted terrain support for the path API.
- Hypothesis: Weighted costs should use a separate general A* API so
  unit-cost-only fast paths, bucket queues, distance fields, and route caches
  keep their existing optimality assumptions.
- Evidence: New unit tests cover avoiding expensive direct tiles, treating
  zero-cost tiles as blocked, and rejecting zero-cost endpoints. Local Release
  benchmarks report `path/weighted_astar_open_512x512` around 60 us and
  `path/weighted_astar_axis_detour_512x512` around 22 us. The unit-cost
  `path/astar_open_2d_512x512` benchmark remained around 2.2 us in the same
  local run.
- Decision: Accepted. Add `weighted_astar_path` for positive integral entry
  costs, keep weighted reuse/fast paths out of scope, and add weighted
  threshold cases below the 1 ms investigation trigger.
- Retry conditions: Revisit weighted open-set or weighted reuse only after
  profiles show weighted workloads are a bottleneck; do not reuse unit-cost
  shortcuts without a weighted optimality proof.

## 2026-06-05 - A* Fallback Passability Checks

- Area: A* fallback search in `include/tess/path/path.h`.
- Hypothesis: Many neighbor passability reads are unnecessary because closed
  nodes can be rejected before reading world storage, and already-open nodes
  were proven passable when first discovered.
- Evidence: Diagnostic fallback benchmarks showed `diag.passability_checks`
  near `diag.neighbor_candidates`, including hundreds of thousands to millions
  of checks against already-closed or already-open nodes.
- Decision: Accepted in the June 5 implementation. A* now rejects closed
  neighbors
  before passability lookup, skips repeat passability lookup for open nodes,
  and reads neighbor passability by known tile index.
- Result: Release fallback slice improved modestly, with mixed 100-request
  512x512 wall-gap workload dropping to about 129 ms on the local run.

## 2026-06-05 - A* Heap Nodes Carry Coordinates

- Area: A* open-set node payload.
- Hypothesis: Storing `Coord3` in each `OpenNode` would avoid converting tile
  index back to coordinate on every expansion.
- Evidence: Focused release fallback benchmarks regressed across the tested
  cases. Examples from the local run: `wall_gap_512x512` rose to about 4.73 ms,
  `no_path_1024x1024` rose to about 122 ms, and
  `batch_100_mixed_512x512` rose to about 168 ms.
- Decision: Rejected. The larger heap element increased memory traffic more
  than it saved in coordinate conversion work.
- Retry conditions: Reconsider only if the open set representation changes or
  a profile shows coordinate conversion dominating after heap/memory costs are
  reduced.

## 2026-06-05 - A* Indexed Heap / Decrease-Key

- Area: A* open-set duplicate entries.
- Hypothesis: An indexed heap or decrease-key open set would reduce duplicate
  heap entries and high `closed_pops` counts in no-path and maze fallback
  cases.
- Evidence: Diagnostic fallback benchmarks showed large `diag.closed_pops`
  counts, for example hundreds of thousands in 512x512 and 1024x1024 no-path
  or striped-maze cases.
- Decision: Deferred, not rejected. It likely needs an additional open-set
  indexing structure, which is outside the current local-A* optimization scope.
- Retry conditions: Revisit when additional pathfinding data structures are in
  scope, or when current fallback timings become the primary MVP blocker.

## 2026-06-05 - A* Shallow Equal-Score Tie-Break

- Area: A* open-set ordering.
- Hypothesis: Preferring lower `g` on equal `f` would reduce duplicate open-set
  improvements and high `closed_pops` counts in no-path and maze cases.
- Evidence: The experiment eliminated `diag.closed_pops` and improved local
  no-path and striped-maze timings substantially. However, it regressed
  successful wall-gap detours and mixed batches badly; the local
  `batch_100_mixed_512x512` release slice rose from about 126 ms to about
  431 ms.
- Decision: Rejected as the default tie-break. The current higher-`g`
  tie-break remains better for successful detours and mixed-agent workloads.
- Retry conditions: Reconsider as a selectable strategy only if future topology
  prechecks or request classification can identify exhaustive failure searches
  before A* runs.

## 2026-06-05 - A* Geometry-Gated Shallow Tie-Break

- Area: A* open-set ordering.
- Hypothesis: Use shallow equal-score tie-breaking only when the request spans
  multiple axes, while preserving the current deeper tie-break for axis-aligned
  detours.
- Evidence: The local run preserved optimal paths and kept direct open paths
  fast, but it still regressed successful wall-gap and mixed-agent workloads.
  The mixed 100-request 512x512 release slice rose from about 126 ms to about
  368 ms. The runtime comparator branch also added overhead.
- Decision: Rejected. Request geometry is not enough to distinguish diagonal
  no-path searches from diagonal successful detours.
- Retry conditions: Revisit only with topology/reachability classification or
  another zero-overhead way to select tie-break policy before entering A*.

## 2026-06-05 - A* Comparator Arguments by Reference

- Area: A* heap comparator.
- Hypothesis: Passing `OpenNode` comparator arguments by `const&` would avoid
  copies in frequent `std::push_heap` and `std::pop_heap` comparator calls.
- Evidence: Local release fallback runs improved no-path and striped-maze
  cases, but regressed wall-gap and mixed-agent batches. The
  `batch_100_mixed_512x512` release slice rose from about 126 ms to about
  144 ms in repeated local samples.
- Decision: Rejected. Mixed-agent throughput is more important than isolated
  no-path wins, and the by-value comparator remains the better default for the
  current open-set representation.
- Retry conditions: Reconsider if `OpenNode` grows larger or the open-set
  representation changes.

## 2026-06-05 - A* Full Separating Barrier Precheck

- Area: A* no-path precheck for uniform passability grids.
- Hypothesis: If the direct path probe hits a blocked tile and that tile's
  axis plane is fully blocked, the plane separates start from goal under
  axis-adjacent movement and A* can return `NoPath` immediately.
- Evidence: Local release benchmarks dropped `path/astar_no_path_512x512`
  from about 17.8 ms to about 0.8 us and `path/astar_no_path_1024x1024` from
  about 88 ms to about 1.6 us. Wall-gap, striped-maze, and mixed-batch release
  timings stayed in the same range because non-separating barriers fall back to
  normal A*.
- Decision: Accepted. The precheck is exact for the current passability model:
  it only returns `NoPath` when a fully blocked axis plane separates the query.
- Retry conditions: If movement rules later permit jumping, teleporting, or
  non-axis transitions across the plane, gate or disable this precheck for
  those movement classes.

## 2026-06-05 - A* Alternate Direct Axis Orders

- Area: Uniform-cost direct-path fast path.
- Hypothesis: The direct Manhattan probe should try all shape-relevant axis
  orders before falling back to A*, because any passable Manhattan route is
  optimal under unit-cost axis-adjacent movement.
- Evidence: A new 512x512 benchmark with the X-first direct route blocked but
  the Y-first route clear runs in about 2.7 us with no heap work. The naive
  six-order version regressed fallback cases in 2D, so it was narrowed to the
  two meaningful 2D orders for degenerate-Z shapes. The focused release slice
  then kept wall-gap, no-path, striped-maze, and mixed-batch timings in the
  same range as baseline.
- Decision: Accepted. Direct probing now tries only shape-relevant axis-order
  permutations and falls back to A* when none are passable.
- Retry conditions: Gate or revise this fast path when non-unit movement costs
  or movement classes make arbitrary Manhattan axis orders non-equivalent.

## 2026-06-05 - A* Scan Then Build Direct Path

- Area: Uniform-cost direct-path fast path.
- Hypothesis: Direct probes should scan passability first and build the path
  only after a probe succeeds, avoiding path reconstruction work for failed
  probes that fall back to A*.
- Evidence: Diagnostics became cleaner for fallback cases, but release timings
  regressed common direct paths because successful paths walked coordinates
  twice. The local `open_512x512` slice rose from about 2.2 us to about 3.0 us,
  and `alternate_direct_512x512` rose from about 2.7 us to about 3.2 us.
- Decision: Rejected. Direct-path success latency matters more than reducing
  failed-probe reconstruction bookkeeping.
- Retry conditions: Reconsider only if direct-path reconstruction becomes
  materially more expensive or if probes can build a compact reusable route
  representation without a second coordinate walk.

## 2026-06-05 - A* Axis-Aligned One-Tile Detour

- Area: Uniform-cost fast path for simple blocked straight-line requests.
- Hypothesis: If an axis-aligned direct path is blocked but a one-tile
  parallel lane is clear, the detour path is optimal with Manhattan+2 cost and
  can return without fallback A*.
- Evidence: A new 512x512 benchmark with a single blocked tile on an
  axis-aligned route runs in about 2.3 us with no heap work. Open direct paths,
  alternate-direct paths, and no-path barrier rejection stayed fast. Wall-gap,
  striped-maze, and mixed-batch timings remained in the same range and still
  fall back to A* when the detour lane is blocked.
- Decision: Accepted for the current unit-cost axis-adjacent movement model.
- Retry conditions: Gate or revise this fast path when non-unit movement costs,
  movement classes, reservations, or dynamic blockers make Manhattan+2 detours
  non-equivalent.

## 2026-06-05 - A* Scan Then Build Axis Detour

- Area: Axis-aligned detour fast path.
- Hypothesis: Detour probes should scan passability first and build the path
  only after a detour succeeds, reducing reconstruction work for failed detour
  attempts that fall back to A*.
- Evidence: Failed-detour diagnostics became cleaner, but the successful
  `axis_detour_512x512` release case slowed from about 2.3 us to about 2.5 us,
  and fallback cases did not improve enough to offset the success-path cost.
- Decision: Rejected. The accepted detour path keeps one-pass build/probe
  behavior for low latency.
- Retry conditions: Reconsider if failed detour attempts become common enough
  in production workloads to outweigh successful-detour latency.

## 2026-06-05 - A* Top-Down 2D Single-Plane Gap

- Area: Uniform-cost fast path for simple wall-with-gap requests.
- Hypothesis: When a direct probe is blocked by a non-separating axis plane,
  scanning the plane for the cheapest passable crossing and verifying the two
  Manhattan legs can prove an optimal path without fallback A*.
- Evidence: The local release `path/astar_wall_gap_512x512` case dropped from
  about 4.1 ms to about 5.6 us with no heap work. The
  `path/astar_batch_100_mixed_512x512` case dropped from about 138 ms to about
  0.32 ms because its repeated wall-gap requests now use the precheck. Open
  512x512 and 1024x1024 direct paths stayed in the same range. The striped-maze
  case still falls back to A* and stayed around 11-12 ms.
- Decision: Accepted for top-down 2D, unit-cost, axis-adjacent movement. The
  route is returned only after passability of the concrete path through the
  chosen gap is verified.
- Retry conditions: Revisit if weighted movement, reservations, dynamic
  blockers, or movement classes make the cheapest passable plane crossing
  insufficient to prove optimality.

## 2026-06-05 - A* Top-Down 2D Forced-Gap Sequences

- Area: Uniform-cost fast path for repeated single-gap vertical barriers.
- Hypothesis: In top-down 2D, when progress toward the goal hits a blocked
  x-plane with exactly one passable gap, that crossing is forced. Repeating
  this scan only when the next x step is blocked can build and verify an
  optimal route through striped barrier layouts without fallback A*.
- Evidence: Local release `path/astar_striped_maze_512x512` dropped from about
  11-12 ms to about 0.18 ms, and `path/astar_striped_maze_1024x1024` dropped
  from about 52 ms to about 0.72 ms. Diagnostics show zero heap pushes/pops in
  both cases. Open direct paths and the wall-gap fast path stayed in the same
  range.
- Decision: Accepted for top-down 2D, unit-cost, axis-adjacent movement when
  encountered x-planes are fully open or have exactly one passable gap. The
  concrete route is still passability-checked tile by tile.
- Retry conditions: Revisit if weighted movement, reservations, dynamic
  blockers, horizontal forced-gap sequences, or multi-gap barrier choices enter
  the current pathfinding scope.

## 2026-06-05 - A* Scan Every X Plane For Forced Gaps

- Area: Forced-gap sequence detection.
- Hypothesis: Scanning every x-plane between start and goal would distinguish
  fully open planes from single-gap forced planes and keep the proof simple.
- Evidence: The stricter full scan kept correctness but pushed local
  `path/astar_striped_maze_1024x1024` to about 1.2 ms, crossing the current
  1 ms investigation trigger. Most time was spent scanning fully open columns
  that did not affect the route.
- Decision: Rejected in favor of scanning a plane only when the next x step is
  blocked.
- Retry conditions: Reconsider only if path queries gain cached topology or
  compact per-plane metadata outside the current local-A* scope.

## 2026-06-05 - A* Vertical 2D Gap Generalization

- Area: Degenerate-axis 2D fast paths and benchmark coverage.
- Hypothesis: The accepted top-down single-plane and forced-gap fast paths
  should apply to any 2D shape, including vertical YZ layouts, because the
  proof depends on the two active axes rather than named x/y axes.
- Evidence: New vertical 512x512 benchmarks match top-down behavior. Local
  release runs reported `path/astar_vertical_wall_gap_512x512` around 5.9 us
  and `path/astar_vertical_striped_maze_512x512` around 0.18 ms, both with zero
  heap pushes/pops in diagnostics.
- Decision: Accepted. The 2D gap fast paths now select active axes from shape
  traits instead of assuming top-down XY layout.
- Retry conditions: Revisit if non-axis movement, movement classes, weighted
  costs, or reservations make the current unit-cost proof insufficient.

## 2026-06-05 - A* 3D Single-Plane Gap

- Area: Uniform-cost fast path for simple 3D slab-with-gap requests.
- Hypothesis: If a direct 3D route is blocked by an axis plane, scanning that
  plane for the cheapest passable crossing and verifying a concrete Manhattan
  route through it can prove an optimal path without fallback A*.
- Evidence: A new `path/astar_slab_gap_3d_64x64x16` benchmark initially took
  about 1.3 ms and expanded about 32.8k A* nodes. After the fast path it runs
  around 5.3 us with zero heap pushes/pops. The open 3D benchmark stays below
  1 us. Added no-gap, multi-gap, and carved-corridor 3D cases also stay below
  the 1 ms investigation trigger; the corridor case still uses fallback A*
  but expands only about 142 nodes.
- Decision: Accepted for the current unit-cost axis-adjacent movement model.
  The concrete route through the chosen plane crossing is still checked tile by
  tile; failures fall back to A*.
- Retry conditions: Revisit when 3D multi-plane portals, stairs, movement
  classes, reservations, dynamic blockers, or weighted costs enter scope.

## 2026-06-05 - A* Remaining Fallback Profile

- Area: Current post-fast-path fallback cases.
- Hypothesis: After direct, gap, forced-gap, and slab-gap fast paths, remaining
  fallback A* cases should be small enough to stay below the 1 ms
  investigation trigger.
- Evidence: Diagnostic runs show `path/astar_corridor_3d_64x64x16` still uses
  fallback A*, but only expands 142 nodes with 142 open-set pushes and 142
  open-set pops, running around 9 us locally. The 100-agent open and mixed
  batches now report zero open-set pushes/pops because all sampled requests hit
  verified fast paths.
- Decision: No additional A* optimization accepted in this iteration. The
  current fallback profile does not justify an indexed heap or additional
  pathfinding data structures inside the current scope.
- Retry conditions: Revisit if new benchmarks or production traces show a
  remaining fallback case over 1 ms or high heap churn in realistic workloads.

## 2026-06-05 - A* Fallback-Stress Benchmarks

- Area: Heap-backed A* under maps that defeat current uniform-cost fast paths.
- Hypothesis: Sparse blockers, room/portal partitions, branch-heavy lattices,
  and repeated shared-destination requests will expose the next bottleneck more
  clearly than open grids or single-wall fast-path cases.
- Evidence: Sparse blockers run around 0.82 ms locally with about 11.8k heap
  pops and 19.1k heap pushes. Room/portal partitions run around 0.35 ms with
  about 5.4k expanded nodes. The 100-request shared-room/portal batch runs
  around 35 ms because it repeats the same fallback search shape 100 times;
  there is no route cache, hierarchy, or shared batch planner in current scope.
  Diagnostics report zero allocations and zero stale pops, so the current
  bottleneck is graph expansion plus heap maintenance rather than allocation
  churn or duplicate-pop cleanup.
- Accepted: Moved z-only neighbor stride/local-coordinate work into the 3D-only
  branch of the indexed neighbor helper. This is a small flat-world supporting
  code cleanup and preserves behavior.
- Rejected: Reversing the final open-set index tie-break made room/portal and
  sparse-blocker cases slower by increasing heap pushes, pops, and expansions.
  Increasing room size from 32 to 64 tiles also made the room/portal case worse
  by roughly doubling expansions.
- Decision: Keep the fallback-stress benchmarks. Keep individual fallback
  searches under the 1 ms threshold and bound the investigated repeated
  100-request fallback batch explicitly instead of forcing it under 1 ms without
  the future data structures needed to share work.
- Retry conditions: Revisit indexed heaps, region graphs, route caches, or
  batch/shared-destination planning once those data structures enter scope.

## 2026-06-05 - A* Unit-Cost Bucket Open Set

- Area: Fallback A* open-set maintenance for the current unit-cost Manhattan
  path model.
- Hypothesis: Since unit-cost axis-adjacent movement with a consistent
  Manhattan heuristic generates fallback nodes at the current `f` score or
  `f + 2`, a two-bucket monotone queue can remove binary heap maintenance while
  preserving optimal path ordering.
- Evidence: Release threshold runs dropped `path/astar_sparse_blockers_512x512`
  from about 0.82 ms to about 0.35 ms, `path/astar_room_portals_512x512` from
  about 0.35 ms to about 0.15 ms, and
  `path/astar_batch_100_shared_room_portals_512x512` from about 35 ms to about
  15 ms. The bucket queue expands more nodes on the sparse and room/portal
  stress cases, but the removed heap maintenance more than offsets the extra
  graph work. Path unit tests and the path benchmark threshold target pass.
- Decision: Accepted for the current unit-cost fallback. Keep the binary heap
  idea rejected for this slice unless weighted costs or non-Manhattan movement
  require a more general open-set policy.
- Retry conditions: Revisit when weighted terrain, non-unit movement costs,
  non-axis movement, or movement classes enter the public path API.

## 2026-06-05 - Shared-Goal Distance Fields

- Area: Many-agent path batches with repeated goals or goal chunks.
- Hypothesis: For 100 agents sharing one goal, a reverse distance field can
  build one goal-rooted search tree and reconstruct each path more cheaply than
  running 100 independent point-to-point A* searches.
- Evidence: Release threshold runs report
  `path/astar_batch_100_shared_room_portals_512x512` around 15.6 ms versus
  `path/distance_field_batch_100_shared_room_portals_512x512` around 2.8 ms.
  The sparse shared-goal batch drops from about 38.9 ms to about 3.5 ms. The
  8-goal room/portal batch drops from about 56.8 ms with independent A* to
  about 17.7 ms by building one field per goal. Benchmark counters report
  unique starts, unique goals, unique start/goal chunks, and average expanded
  nodes to make reuse opportunities visible.
- Accepted: Add `DistanceFieldScratch`, `build_distance_field`, and
  `distance_field_path` for the current unit-cost passability model. The
  scratch records the goal used to build the field and rejects mismatched
  reconstruction requests.
- Rejected: Applying distance fields blindly to mixed routes with many unique
  goals is not accepted in this iteration. The mixed repeated room/portal batch
  has 21 unique goals, so broad route caching, hierarchy, or selective field
  reuse needs a separate design.
- Decision: Use reverse distance fields for shared-goal and small common-goal
  batches. Keep point-to-point A* for one-off routes until route caches,
  hierarchy, or weighted path APIs enter scope.
- Retry conditions: Revisit field storage/reuse policies when topology,
  invalidation, chunk residency, weighted costs, or path product caching are
  introduced.

## 2026-06-05 - Exact Route and Suffix Cache

- Area: Many-agent path batches with repeated point-to-point routes or starts
  that lie on an already-computed route to the same goal.
- Hypothesis: A small caller-owned route cache can avoid rerunning A* for
  repeated stable-map routes. Exact route hits can return the stored path
  directly, and same-goal suffix hits are optimal for unit positive edge costs
  when the new start is already on a cached optimal path.
- Evidence: Targeted Release benchmarks report
  `path/astar_batch_100_mixed_repeated_room_portals_512x512` around 41.1 ms
  versus
  `path/cached_astar_batch_100_mixed_repeated_room_portals_512x512` around
  12.5 ms with 30 misses and 70 exact hits. The suffix-specific open batch
  reports `path/astar_batch_100_suffix_open_512x512` around 95.9 us versus
  `path/cached_astar_batch_100_suffix_open_512x512` around 4.0 us with
  1 miss and 99 suffix hits.
- Accepted: Add `RouteCacheScratch` and `cached_astar_path` for explicit
  caller-managed route reuse. The cache keeps whole path nodes, reports
  entries, exact hits, suffix hits, misses, and stored path nodes, and assumes
  the caller clears it when passability or movement rules change.
- Deferred: Room/portal hierarchy remains a larger topology feature. It needs
  persistent portal graph ownership, invalidation, and tests over general
  map structure; adding benchmark-only room knowledge would not be a usable
  library optimization.
- Decision: Keep exact route and same-goal suffix reuse. Continue using
  independent A* or distance fields when there is no route/suffix reuse.
- Retry conditions: Revisit cache indexing when route counts become large
  enough for linear lookup to show up in profiles, and revisit hierarchy when
  topology graph ownership is designed.

## 2026-06-06 - Unit-Cost Distance-Field Products

- Area: Reusable unit-cost distance fields for stable maps with repeated goal
  sets.
- Hypothesis: Copying a built reverse field into a reusable product can make
  repeated nearest-target and replay queries much cheaper than rebuilding the
  field, while chunk-version dependencies conservatively reject stale products.
- Evidence: Targeted local benchmark runs report
  `path/distance_field_product_build_8_goal_room_portals_512x512` around
  13 ms while visiting about 247k reachable tiles and copying about 1 MiB of
  dense field data. The corresponding
  `path/nearest_target_product_100_starts_room_portals_512x512` batch is about
  0.32 ms, `path/field_product_cache_hit_replay_room_portals_512x512` is about
  4 us, stale rejection is about 0.8 us, and constrained LRU eviction is about
  27 us.
- Accepted: Add `GoalSet`, `DistanceFieldProduct`, nearest-target replay, and
  a caller-owned byte-budgeted `FieldProductCache` for unit-cost fields. Keep
  replay and cache lookup exact and conservative: products carry reached
  chunk-version dependencies, and stale products return `NoPath` or are
  rejected from the cache.
- Deferred: Weighted field products and `PathRequestRuntime` integration remain
  out of this slice. Runtime policy needs a separate decision once product
  ownership, invalidation cadence, and topology interaction are clearer.
- Decision: Keep dense field products for now. The 8-goal product build is a
  batch/product-construction workload, so its threshold is explicitly above
  the 1 ms single-query investigation line; nearest-target and cache replay
  thresholds stay under 1 ms.
- Retry conditions: Revisit sparse product storage or differential cache
  invalidation if dense product byte size or build-copy time starts dominating
  real workloads.

## 2026-06-06 - Runtime Field-Product Reuse Policy

- Area: Integrating unit-cost distance-field products into
  `PathRequestRuntime`.
- Hypothesis: Repeated-goal runtime batches can opt into field-product reuse
  while preserving the existing exact route/suffix cache as the default unit
  pathing behavior.
- Evidence: Runtime tests cover opt-in repeated-goal reuse, stale product
  rejection after world edits, world-change cache clearing, and warm
  allocation-free agent processing. Local benchmark comparison on a 100-agent
  shared wall-gap workload reports the default route/suffix cache around
  0.31 ms and the opt-in field-product cache around 0.68 ms; the route cache
  wins there because many starts are suffix hits on already-cached paths. A
  start-chunk policy gate makes that same field-product opt-in path skip the
  suffix-friendly group and match route-cache timing around 0.31 ms. A
  scattered-start wall-gap workload uses the product, but still measures
  around 0.41 ms versus route/suffix around 0.21 ms because suffix reuse
  remains strong in the current map.
- Accepted: Add `PathRuntimeCachePolicy::use_unit_field_product_cache`,
  `unit_field_product_min_goal_reuse`,
  `unit_field_product_min_start_chunks`, and a byte budget for the
  runtime-owned field-product cache. Only repeated single-goal groups with
  enough distinct start chunks use products; singleton and suffix-friendly
  single-chunk groups keep the route/suffix path. Runtime counters report
  candidate, used, and skipped field-product groups.
- Decision: Keep runtime field products opt-in. They are useful for workloads
  where callers know field reuse should beat suffix reuse, but the existing
  route cache remains the safer default for converging paths.
- Retry conditions: Revisit automatic runtime selection after more workload
  benchmarks identify when field products consistently beat route/suffix
  caching.

## 2026-06-07 - CI Path Threshold Calibration

- Area: Path benchmark thresholds on GitHub-hosted Ubuntu runners.
- Evidence: PR CI on the `ubuntu-24.04` runner completed the product
  benchmarks within their thresholds, but exceeded several existing path
  thresholds: weighted sparse and portal A*, weighted portal segment batches,
  existing 100-agent A* batches, and
  `path/agent_runtime_100_weighted_mixed_512x512`. The failing values were
  broad runner calibration misses rather than a product-specific regression.
  A follow-up run narrowed the remaining misses to two multigoal weighted
  batch thresholds at about 81-82 ms against 80 ms, plus the existing
  `path/astar_batch_100_mixed_512x512` batch at about 1.08 ms against 1 ms.
- Accepted: Raise only the path thresholds exceeded by that CI run, using the
  observed runner timings with headroom. Keep single-query product replay,
  nearest-target, stale rejection, and cache lookup thresholds below the 1 ms
  investigation line.
- Deferred: No optimization work was started because the failed gates covered
  pre-existing weighted and batch workloads, and the new product benchmarks
  passed their gate on CI.
- Follow-up: A later hosted-runner pass measured the existing
  `path/astar_sparse_blockers_512x512` single-query benchmark at about
  1.002 ms against its 1.000 ms gate. Accepted a narrow threshold adjustment
  to 1.1 ms as runner jitter; this does not change the 1 ms investigation
  rule for new single-query benchmarks.
- Retry conditions: Profile the affected weighted and batch workloads before
  further threshold changes if future PRs exceed these calibrated runner
  bounds.

## 2026-07-06 - Pre-A* Scan Cost Model Accepted; Grouping Rescans Removed

- Area: `astar_path` pre-A* fast-path scans;
  `PathRequestRuntime::process_repeated_goal_fields` and
  `weighted_path_batch` goal grouping.
- Hypothesis: The plane-gap/forced-gap/barrier fast paths carry an
  O(world-slice) worst case when they all miss, and the repeated-goal
  grouping passes carried O(n^2)/O(n^3) request rescans that a flat-hash
  grouping pass removes without behavior change.
- Evidence: Two new worst-case benchmarks pin the scan-miss cost:
  `path/astar_plane_gap_miss_512x512` measured about 1.76 ms (sealed wall
  gap, every 2D scan fails, full-flood NoPath A* of about 131k nodes) and
  `path/astar_plane_gap_miss_3d_64x64x16` about 9.0 us (sealed best 3D
  gap, A* reroutes through a second gap). Seeded randomized equivalence
  tests (fixed `std::mt19937` seeds) pin grouped statuses, costs, and all
  `field_product_*`/batch stats counters against per-request A* oracles
  before and after the grouping rewrite, and warm reruns of both grouping
  passes are allocation-free under `ScopedAllocationCounter`.
- Accepted: Rewrite both grouping passes as single O(n) flat-hash passes
  (goal -> group id, counting-sort member buckets, sort+unique distinct
  start chunks) with runtime/scratch-owned reusable storage. Add the two
  scan-miss benchmarks to `bench/thresholds/path.json` with deliberately
  generous 10x-measured ceilings as documentation gates.
- Deferred: No structural change to the pre-A* scans themselves. The miss
  cost is bounded by one world slice per failed scan and the fast paths
  win on real layouts; the accepted evidence is the benchmark pair plus
  the cost-model section in `docs/architecture/path.md`.
- Retry conditions: Revisit the scan ordering (or gate the plane scans
  behind a cheap occupancy summary) if the miss benchmarks regress past
  their generous ceilings or profiling shows scan overhead dominating
  realistic mixed workloads.

## 2026-07-11 - Shared Pathing-Dirty Flag Amplifies Batch Replans (S11.4)

- Observation (sampled while sizing the S11.4 consumer soak; profile
  dominated by `weighted_astar_path` under `process_weighted_batch`):
  `set_path_agent_goal` and the consumer's movement-failure handling
  both mark the SHARED `PathAgentTickState::pathing_dirty` flag, so ONE
  agent drawing a new goal (arrival, failed-goal re-choose) or ONE
  blocked step replans the ENTIRE weighted batch next tick. On a
  building-dense 512x512 map where each weighted search costs
  milliseconds (`path/agent_runtime_100_weighted_mixed_512x512` ~100 ms
  per replan of 100), steady state degenerates to a near-per-tick full
  batch: a 12-agent 10k-tick run took minutes. On mostly-open maps the
  same amplification exists but each search is microseconds
  (`weighted_astar_open_512x512`), so it stays invisible.
- Accepted: keep the coarse flag for the initial milestone -- it is correct
  (never a stale route), and the consumer soak is sized to open-ish maps where
  the amplification is cheap. Documented in the soak test's comment.
- Deferred: per-agent invalidation granularity -- goal arming should
  only need to (re)submit THAT agent; a world change is what genuinely
  invalidates everyone. Needs a tess-side distinction between
  agent-scoped and world-scoped pathing dirt -- follow-up API work.
- Retry conditions: revisit when a consumer needs dozens-plus of
  weighted agents on maze-like maps with frequent goal churn, or if the
  `agent_runtime` weighted family regresses.
