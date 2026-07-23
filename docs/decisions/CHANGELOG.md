# Design Changelog

Records meaningful design changes from the original TDDs.

## 2026-07-22 - Add deterministic tactical assignment

- Changed: added a caller-scored greedy assignment primitive with stable
  priority ordering, deterministic candidate tie breaks, bounded capacities,
  explicit partial and invalid outcomes, and reusable warm scratch storage.
- Reason: the maintained v0.9 roadmap needs assignment mechanics without
  embedding game semantics or claiming globally optimal multi-agent planning.
- Affected docs: spatial coordination, public surface, and changelog.
- Affected code: tactical assignment header, umbrella/install surface, and
  correctness and allocation tests.

## 2026-07-22 - Add caller-defined area indexes

- Changed: added a graph-derived area index that groups regions by caller
  semantic key, summarizes bounds and tile counts, derives canonical portal
  adjacency, supports dense and sparse graphs, and rejects changed graphs.
- Reason: the maintained v0.9 roadmap schedules an area substrate, while the
  historical project TDD correctly rejects substrate-owned room meaning. A
  caller-keyed derived index supplies reusable mechanics without taking over
  room ownership, statistics, or gameplay lifecycle.
- Affected docs: spatial coordination, architecture map, surface manifest,
  and changelog.
- Affected code: area index header, umbrella/install surface, tests, and
  topology benchmarks.

## 2026-07-22 - Complete hierarchical paths and weighted products

- Changed: added deterministic shortest region paths with ordered portals,
  chunk corridors, and sparse-residency uncertainty; persistent weighted
  multi-goal products with exact replay and nearest-target queries; and opt-in
  cross-call weighted-product selection in the path runtime.
- Reason: reachability alone could not select a coarse corridor, while weighted
  shared-goal work was rebuilt per batch and could not use the existing
  versioned byte-budgeted product cache. Persistent products remain explicit
  and dense-only so full-volume allocation is never hidden on sparse worlds.
- Affected docs: topology, path, roadmap, completion plan, optimization log,
  and changelog.
- Affected code: region graph traversal, weighted field products and cache,
  path runtime strategy, correctness tests, and benchmarks.

## 2026-07-22 - Complete block pipelines and span-query acceleration

- Changed: added allocation-free block-lazy filter, map, flat-map, reduce, and
  bounded frontier composition; exact clipped box, radius, and chunk span
  emitters; and three opt-in experimental maintenance scheduler baselines.
- Reason: the raw resolved-chunk layer lacked composable fused kernels and
  spatial runs. Query spans exceeded their historical performance gate. The
  maintenance prototype failed its sparse-overhead gate, so it remains
  isolated from authoritative storage rather than becoming a world hook.
- Affected docs: block, query, experimental maintenance, roadmap, completion
  plan, optimization log, and changelog.
- Affected code: block pipeline and span headers, experimental schedulers,
  correctness tests, and benchmarks.

## 2026-07-22 - Complete queued intents and event-driven resumable work

- Changed: added typed non-owning intent batches with planner-preserved
  version/invalidation/backend policy; cooperative generation-stamped result
  tickets and deterministic FIFO continuations; exact bounded tick-stamped
  event streams; OnEvent cadences; and direct scheduler adapters for event
  publication and background continuation.
- Reason: the synchronous field-only queue and dirty-only scheduler could not
  express the historical TDD's common request families or retain budgeted work
  and exact events across ticks.
- Affected docs: queued operations, simulation, roadmap, completion plan, and
  changelog.
- Affected code: queued intent descriptors, resumable results, event streams,
  scheduler cadence/diagnostics, benchmarks, and tests.

## 2026-07-22 - Complete provider-composed resolved transitions

- Changed: extended special providers with per-origin forward and per-target
  reverse enumeration plus provider-owned unscaled edge costs; threaded the
  resolved model through exact paths, fields, products, caches, batches,
  runtimes, retained agent routes, and movement commit; and added static
  compact-cost assessment with explicit realized-overflow results.
- Reason: topology-only provider edges made reachability disagree with exact
  planning and commit, while implicit destination costs contradicted the
  accepted special-edge contract.
- Affected docs: topology, path, roadmap, completion plan, and changelog.
- Affected code: transition providers and model, path and field algorithms,
  caches, runtime, movement, agents, and tests.

## 2026-07-22 - Resolve regular movement through one model

- Changed: routed exact unit and weighted search, reverse and bounded fields,
  multi-goal products, topology, cache stamps, and movement validation through
  one resolved regular-transition model; added diagonal fixed-point costs,
  axial-hex adjacency, explicit compact-cost overflow, and model mismatch
  rejection.
- Reason: independent neighbor implementations could disagree about legal
  movement, field reconstruction, reachability, invalidation, and commit.
- Affected docs: shape, topology, path, roadmap, and completion plan.
- Affected code: transition model, path and field implementations, topology,
  movement validation, runtime statistics, and tests.

## Template

```md
## YYYY-MM-DD - Title

- Changed:
- Reason:
- Affected docs:
- Affected code:
```

## 2026-07-22 - Introduce lattice and regular-step identities

- Changed: extended `Shape` with a defaulted lattice type; added stable,
  versioned orthogonal and axial-hex identities plus axial coordinate helpers;
  extended `MovementClass` with a defaulted step policy; and added stable
  default/diagonal policy identities and shape-policy validation.
- Reason: the resolved transition design requires geometry and regular steps
  to be explicit, stampable inputs while preserving existing two-argument
  declarations and legacy custom movement classes.
- Affected docs: shape and topology architecture.
- Affected code: lattice, shape, step-policy, and movement-class headers.

## 2026-07-22 - Reconcile the post-v0.4 roadmap with shipped concurrency

- Changed: moved the production worker pool into the shipped inventory;
  replaced stale milestone-era prototype claims in maintained architecture;
  closed the S1-S7 concurrency plan; annotated the concurrency addendum's
  shipped and unshipped lanes; and added one maintained completion sequence
  covering the lattice TDD and previously untracked historical commitments.
- Reason: the public roadmap and several maintained pages contradicted the
  implemented, tested scheduler/pool integration and omitted the newly merged
  lattice design plus other unshipped TDD goals.
- Affected docs: roadmap, planning index and completion/concurrency plans,
  concurrency addendum, and queued, block, simulation, and diagnostics
  architecture pages.
- Affected code: none.

## 2026-07-22 - Retry exact-revision dependency population

- Changed: GoogleTest, Google Benchmark, and EnTT now use a shared population
  helper that shallow-fetches the exact commit, verifies it, checks it out,
  verifies the worktree revision, and retries the full sequence up to three
  times.
- Reason: a cold worktree exposed an intermittent checkout failure in CMake's
  generated FetchContent clone script. The clone was retried, but its checkout
  was not. Retrying the complete operation closes that gap without relying on
  GitHub-generated archives whose compressed bytes are not stable artifacts.
- Affected docs: `docs/dependencies.md` and `tests/AGENTS.md`.
- Affected code: the FetchContent Git helper, population script, dependency
  declarations, and their regression tests.

## 2026-07-18 - Make adoption paths executable and publishable

- Changed: added pathfinding and simulation facade headers without removing
  the compatibility umbrella; made every maintained C++ fence a region of a
  CMake-registered compiled example or test; made installed-package and
  `FetchContent` consumers tracked smoke fixtures; separated the unreleased
  `v0.4.0` development API from the latest `v0.3.0` release in package and
  documentation metadata; added a strict MkDocs Pages site and an interactive,
  single-threaded Emscripten pathfinding example.
- Reason: adopter success depends on a short, verifiable first path and on
  documentation that cannot silently drift from C++ and CMake behavior. The
  single-threaded pathfinder demonstrates the portable core without implying
  that browser threading or the complete simulation stack is already solved.
- Affected docs: `README.md`, `docs/index.md`, `docs/getting-started.md`,
  `docs/packaging.md`, `docs/hosting.md`, `docs/support.md`, and dependency and
  support metadata.
- Affected code: facade headers, canonical examples, consumer fixtures,
  documentation and web build tools, CMake presets, and GitHub workflows.

## 2026-07-17 - Consumer install path and v0.3.0 release preparation

- Changed: the documented install route is a new `consumer` preset
  (headers-only: no tests, examples, benchmarks, warnings-as-errors,
  EnTT, or network fetches), replacing the developer-shaped `release`
  preset in the README; an opt-in `TESS_BUILD_DOCS` Doxygen target
  generates a local API reference; the README states measured
  performance figures and fixes wording an evaluator could misread
  ("planned" vs plan-driven parallel execution, target vs file names);
  CHANGELOG.md finalizes the 0.3.0 section ahead of tagging v0.3.0, so
  the front-page `find_package(tess 0.3)` snippet matches the latest
  release.
- Reason: an external adopter-perspective review found the documented
  install path built the whole test suite under -Werror with network
  fetches, performance claims lacked adopter-consumable evidence, and
  main's README described a version newer than any installable release
  (verified: `find_package(tess 0.3)` fails against an installed
  v0.2.0 under SameMinorVersion compatibility).
- Affected docs: `README.md`, `CONTRIBUTING.md`, `CHANGELOG.md`,
  `tests/AGENTS.md`.
- Affected code: `CMakePresets.json`, `CMakeLists.txt`,
  `tests/test_cmake_compatibility.py`.

## 2026-07-17 - Documentation restructure for adopters

- Changed: the README is now a user-facing overview (features, quickstart,
  install); contributor workflow moved to a new top-level CONTRIBUTING.md;
  a top-level CHANGELOG.md backfills release notes from the annotated
  release tags; docs/getting-started.md adds a concept-ladder tutorial;
  docs indexes lead with maintained material and mark the TDD archive and
  planning records as historical. cmake_minimum_required now declares the
  3.25...3.28 policy range so 3.26-3.28 policies stay NEW on newer CMake.
- Reason: separate the adoption funnel from maintainer workflow, make
  release history discoverable, and make the supported CMake policy
  window explicit.
- Affected docs: `README.md`, `CONTRIBUTING.md`, `CHANGELOG.md`,
  `docs/getting-started.md`, `docs/README.md`,
  `docs/architecture/README.md`.
- Affected code: `CMakeLists.txt`, `tests/test_cmake_compatibility.py`.

## 2026-07-17 - Support CMake 3.25 consumers

- Changed: the project, presets, and install smoke now support CMake 3.25.
  CMake 3.28 and newer retain module-scan suppression and fetched-dependency
  exclusion from the default build; 3.25 through 3.27 omit those unavailable
  build-hygiene options without changing the installed library.
- Reason: the 3.28 floor excluded supported adopter environments even though
  tess is header-only and its library and packaging features require only
  CMake 3.25.
- Affected docs: `README.md` and the Steam Runtime setup notes.
- Affected code: root and smoke CMake requirements, dependency acquisition,
  compatibility regression tests, presets, and CI test registration.

## 2026-07-17 - Configure conditional benchmark builds

- Changed: the pre-push hook configures the benchmark preset before building
  it when benchmark-sensitive files require that gate. Unrelated pushes still
  skip both benchmark commands.
- Reason: CMake build presets require an already-generated build tree, so the
  previous build-only gate failed in fresh clones.
- Affected docs: `docs/git-hooks.md` and `tests/AGENTS.md`.
- Affected code: pre-push orchestration and its Python regression tests.

## 2026-07-17 - Preserve portable allocation-failure gates

- Changed: deterministic global allocation rejection now reports itself
  unavailable, and remains inert, in MSVC checked-iterator builds. The Windows
  gate retains its full Debug suite and additionally builds and runs the
  allocation harness and portal-cache strong-guarantee cases in Release, where
  checked iterators do not inject allocations into `noexcept` vector
  constructors and moves. Direct harness tests cover selected throwing,
  nothrow, and aligned failures plus state restoration.
- Changed: the worker-pool padding analyzer suppression now spans the documented
  class without changing its deliberate false-sharing layout.
- Reason: a process-wide fault injector cannot safely reject MSVC Debug STL
  iterator-proxy bookkeeping; doing so calls `std::terminate` before the cache
  operation can observe `std::bad_alloc`. Release preserves Windows-specific
  failure coverage, while the suppression-only worker-pool fix avoids an ABI or
  performance change.
- Affected docs: `tests/AGENTS.md`.
- Affected code: the test allocation harness, Windows CI, and worker-pool
  analyzer annotations.

## 2026-07-12 - First-audit remediation, v0.3.0

- Changed: BREAKING pre-release hardening. `PlannedOperation` now has checked,
  immutable construction and an O(1) world-shape stamp; deferred dirty
  recording, collection, and merge return explicit failure results and reject
  cross-world use before mutation. `ExecutionPhase` is now a planner-issued,
  generation-stamped plan capability, preventing hand-built aggregate ranges
  and stale phases retained across report reuse from bypassing parallel
  ownership checks. Phase world and policy validation happens once before any
  callback, scratch, or result-channel mutation. Dirty metadata is recorded
  before callbacks, and exceptional AutoExec phases conservatively merge all
  started work before rethrowing the original exception. Sparse local topology
  reports `MissingChunk`.
  Stateful transition providers expose a monotonic revision. Route and portal
  cache budget reductions apply immediately, and portal segments bind to one
  movement class. Portal segment construction and compaction now commit
  transactionally, so allocation failure cannot publish incomplete
  dependencies, evict a live entry, or corrupt surviving path offsets. Result
  hooks are `noexcept`; throwing result visitors remain retryable across
  reentrant capacity growth, and a reentrant clear retires the old generation
  safely. Exceptional auto-exec paths clear transient results without making a
  partially executed frame safe to retry.
- Changed: version metadata has one CMake authority and generates the installed
  `tess/version.h`; the project remains pre-stable at `v0.3.0`, with earlier
  release points corrected to `v0.1.0` and `v0.2.0`. Dependency acquisition is
  pinned by default, system-package use is explicit and minimum-versioned, and
  third-party Actions and the Steam SDK base use immutable pins. Python tools
  are exact-version and artifact-hash locked, and CI enforces those hashes in
  one reusable environment. cppcheck's source archive is hash-verified, hosted
  runner OS labels roll in place, and package-manager `clang-tidy` and
  `ccache` versions remain unpinned.
- Changed: public-surface checks now cover aliases, concepts, constants,
  macros, transitive installed declarations, and stale manifest entries.
  Namespace-scope Doxygen coverage spans every installed header. Privacy hooks
  scan every non-binary tracked file and load identity-specific expressions
  from a local Git path rather than tracked source.
- Performance: final matched `-O3` A/B runs measured +0.82% direct queued,
  +0.14% result-bearing, -1.82% in the intentionally dispatch-heavy serial
  tile touch, and +1.58% integrated auto-exec. Final worker-pool and
  scoped-thread tile-touch checks were -0.02% and +0.64%. Class-safe
  portal-cache reads cost 1.5-1.7%. All are inside the 5% regression limit.
  Rejected variants and full methodology are recorded in the optimization
  log.
- Reason: repository-wide audit remediation before any stable API promise.
- Affected docs: maintained architecture documents, `docs/dependencies.md`,
  `docs/git-hooks.md`, and `docs/planning/optimization-log.md`.
- Affected code: public queued, result, path-cache, topology, version, build,
  CI, benchmark-gate, Steam runtime, and repository-quality tooling surfaces.

## 2026-07-12 - Per-agent pathing dirt + retained routes

- Changed: the span-based tick drivers process paths with a scope:
  `state.pathing_dirty` (world-scoped, set by `mark_pathing_dirty`)
  still replans every agent, but agent-scoped needs -- a goal armed via
  either `set_path_agent_goal` overload, a Blocked retry -- now replan
  ONLY those agents while `Following` agents keep walking routes
  retained in the new `PathAgentRoutes` pool on `PathAgentTickState`.
  The two-argument `set_path_agent_goal(state, agent, goal)` no longer
  marks the world flag. New API: `PathSubmitScope`, `PathAgentRoutes`,
  route-pool overloads of `advance_path_agents{,_with_movement}`, and
  scope/routes parameters (defaulted -- source-compatible) on
  `submit_path_agents`, `apply_path_agent_results`, and the three
  `process_*_path_agents` wrappers. `tick_ecs_*` drivers deliberately
  keep full-scope processing (registry-collected batches cannot hold
  the index-pairing contract). Bench: one goal re-arm per tick over
  100 weighted agents drops 72.5 -> 17.3 ms (4.2x local; new
  `path/agent_tick_100_weighted_goal_churn_512x512` pins the scoped
  submit count).
- Reason: S11.4 soak observation / future backlog -- the shared flag
  degenerated steady state to near-per-tick full-batch replans on
  building-dense maps.
- Affected docs: `docs/architecture/path.md`,
  `docs/architecture/simulation.md`, `docs/planning/optimization-log.md`.
- Affected code: `include/tess/sim/path_agent.h`,
  `include/tess/sim/path_agent_tick.h`, `bench/tess_path_agent_bench.cc`,
  `bench/thresholds/path.json`, `tests/tess_path_agent_tick_test.cc`.

## 2026-07-12 - ChunkMeta hot/cold split, v0.2.0 (audit3 M5)

- Changed: BREAKING (minor bump by decision -- the struct layout was
  never a documented guarantee): `ChunkMeta` no longer carries
  `field_dirty_flags`, `active_flags`, or `dirty_bounds`. The flag words
  live in per-world SoA columns the collect scans read directly (16
  chunks per cache line instead of streaming 80-byte structs; struct
  shrinks 80 -> 20 bytes), and the cold `Box3` bounds sits in its own
  parallel array. New read accessors: `World::dirty_flags(key)`,
  `active_flags(key)`, `dirty_bounds(key)`; `observe_dirty` bundles
  flags+bounds+version as before, and all mutation stays behind
  `mark_/clear_/observe_`-style methods, which now take the columns.
  Migration: `meta(key).field_dirty_flags` -> `dirty_flags(key)`,
  `meta(key).dirty_bounds` -> `dirty_bounds(key)`,
  `meta(key).active_flags` -> `active_flags(key)`. Evidence: new
  streaming-scale `storage/world_dirty_chunks_iteration_16k` 10.19 ->
  7.64 us (1.33x local; the 256-chunk scan is cache-resident and flat);
  no regressions across the storage family. Project version 0.1.0 ->
  0.2.0.
- Reason: audit-2026-07-11 M5 (staged migration per the remediation
  plan; deferred out of the audit stack for the version-bump decision).
- Affected docs: `docs/planning/optimization-log.md`.
- Affected code: `include/tess/storage/chunk_meta.h`,
  `include/tess/storage/world.h`, `include/tess/storage/sparse_world.h`,
  `include/tess/sim/render_delta.h`, `bench/tess_bench.cc`,
  `bench/thresholds/storage.json`, `CMakeLists.txt`, tests.

## 2026-07-12 - Intrusive LRU eviction + ECS hash/lookup cuts (audit3 W6)

- Changed: sparse-world eviction pops an intrusive doubly-linked LRU
  list head -- O(1) per miss, was an O(resident_count) timestamp scan
  (eviction_churn_512 400 -> 114 ns, capacity-independent; hits pay the
  MRU splice, 2.75 -> 4.21 ns worst case). The tile-occupancy index
  hashes coordinate lanes in parallel (index_move 8.18 -> 5.01 ns), and
  the EnTT adapter caches PathState pointers from the collect view walk
  (tick_entt_10k 687 -> 586 us).
- Reason: audit-2026-07-11 M11b (with the W1 residency bench family as
  its before/after evidence) and the ecs lows.
- Affected docs: `docs/planning/optimization-log.md`.
- Affected code: `include/tess/storage/sparse_world.h`,
  `include/tess/ecs/adapter.h`, `include/tess/ecs/entt/entt_adapter.h`.

## 2026-07-12 - Worker pool: padded counters, run claiming, bounded wakeups (audit3 W5)

- Changed: `WorkerPoolPhaseExecutor` puts its two hot atomics on their
  own cache lines, claims short runs (~count/(workers*4)) per contended
  RMW with one release-add publishing each run, wakes only
  min(runs, workers) threads per dispatch, and only the last worker out
  signals completion. Paired A/B: tile_touch_pool_w4 23.9 -> 12.4 us,
  chunk_fill_pool_w4 44.7 -> 22.1 us (~2x); compute-bound phases flat.
- Reason: audit-2026-07-11 M8 -- pool overhead dominated phases of
  cheap operations (the audit measured 25x vs serial on one-tile ops).
- Affected docs: `docs/planning/optimization-log.md`.
- Affected code: `include/tess/ops/phase_executor.h`.

## 2026-07-12 - Path micro: reached counter, saturating f, single-probe (audit3 W4)

- Changed: `PathScratch` counts reached nodes instead of recording their
  indices (only the count was ever read; `DistanceFieldScratch` keeps its
  list for dependency capture); the unweighted A* core's f arithmetic
  and dial step saturate, restoring symmetry with the weighted core
  (audit C3); `NodeIndexSpace::resident_offset` folds the sparse
  residency test and node offset into one directory probe, used by both
  A* cores and the weighted field build/read loops (paired A/B: sparse
  multigoal batch 93.9 -> 90.2 ms local). The audit M9 interleaved
  node-record experiment measured 3-9% slower and was reverted -- the
  parallel-array layout now carries a comment pointing at the log
  entry. Declined by analysis: reordering the weighted relaxation's
  entry-cost read (tentative_g is computed from it).
- Reason: audit-2026-07-11 M9 (rejected on evidence), M10, C3, and the
  sparse neighbor-probing low.
- Affected docs: `docs/planning/optimization-log.md`.
- Affected code: `include/tess/path/path.h`,
  `include/tess/path/detail/astar.h`,
  `include/tess/path/detail/weighted_batch.h`,
  `include/tess/path/node_index_space.h`.

## 2026-07-12 - Tick-engine overhead: schedule, movement, planning (audit3 W3)

- Changed: `Schedule::seal()` builds a phase-major dispatch order (one
  pass per tick instead of SimPhase::Count full scans) and an
  OnDirty-only index that dirty merges write to (only OnDirty cadences
  read `pending_mask`); `run_tick` guards `in_run_` with RAII so a
  throwing task no longer latches the schedule (audit C2, tested).
  Movement validation resolves each endpoint once and threads the
  resolved tiles through passability, occupancy/reservation, version
  checks, commit writes, and dirty marks (was 4-7 resolves per step);
  version checks early-return when the intent carries no expectations.
  `plan_operations` gains a reuse overload planning into a caller-owned
  `ExecutionReport` (rows, planned ops, and pooled chunk lists recycled;
  steady state is allocation-free, tested); `expand_domain` fills
  in place via the collect_* accessors; auto-exec reuses a task-owned
  report. `dirty_axis_end`/`axis_end` now share chunk_meta's saturating
  helper (audit C1); `detail::popcount` is `std::popcount`. Scheduler
  bench medians: empty_tick 50->7 ns, cadence_dispatch_100 510->165 ns,
  dirty_trigger 49->8 ns (local).
- Reason: audit-2026-07-11 M4a/b, M6, M7, C1, C2, and the popcount low
  -- per-tick engine overhead with mechanical fixes.
- Affected docs: `docs/planning/audit-2026-07-11-remediation.md`.
- Affected code: `include/tess/sim/schedule.h`,
  `include/tess/sim/movement.h`, `include/tess/sim/auto_exec.h`,
  `include/tess/sim/render_delta.h`, `include/tess/ops/queued.h`,
  `include/tess/storage/chunk_meta.h`, `tests/tess_sim_schedule_test.cc`,
  `tests/tess_queued_planning_test.cc`, `tests/CMakeLists.txt`.

## 2026-07-11 - Batch grouping, residency probes, settle-target floods (audit3 W2)

- Changed: `weighted_path_batch` scatters shared-goal results through
  counting-sort member buckets (was an O(groups x requests) rescan) and
  hands each field build the group's validated start tiles as settle
  targets -- the goal-rooted flood stops once every consumer start has
  settled instead of exhausting the reachable component (early exit is
  armed only under TreatAsBlocked; an Indeterminate-policy flood must
  still discover missing-chunk boundaries). The batch also verifies the
  field's residency stamp once per group (debug assert) instead of
  recomputing the O(resident_count) fingerprint per member;
  `residency_fingerprint` itself now iterates resident slots directly
  (was 3 directory probes per chunk) and the sparse region-graph
  freshness check reads generation+meta through one probe via the new
  `SparseResidentWorld::resident_ref`. New benches pin the sparse batch
  and near-goal scenarios; the near-goal open-map batch drops ~118x
  (5.79 ms -> 49 us local) while the far-goal multigoal batches are
  unchanged.
- Reason: audit-2026-07-11 M1/M2/M3 -- the batch-pathing tick is the
  hot path; these remove its quadratic member scan, its redundant
  fingerprint traffic, and its whole-map floods for clustered agents.
- Affected docs: `docs/planning/audit-2026-07-11-remediation.md`,
  `docs/planning/optimization-log.md`.
- Affected code: `include/tess/path/path.h`,
  `include/tess/path/detail/weighted_batch.h`,
  `include/tess/storage/sparse_world.h`,
  `include/tess/topology/topology.h`,
  `tests/tess_path_weighted_batch_test.cc`,
  `bench/tess_path_weighted_bench.cc`.

## 2026-07-11 - Bench integrity: de-elision, parallel gates, residency family (audit3 W1)

- Changed: five benchmarks that compiled to empty loops
  (`storage/field_span_acquisition`, `storage/chunk_field_write_read_iteration`,
  `storage/single_chunk_page_iteration`, `storage/flat_array_iteration`,
  `block/scratch_allocate_u32`) and one partially-elided one
  (`diagnostics/record_timing`) now measure real work via
  escape-then-clobber and opaque-input patterns; their ceilings are
  re-set (bootstrap x6-local for the three loop benches, 25 ns floor for
  the sub-ns ones) pending the 10-artifact recalibration. The
  `parallel/` family is now gated (`bench/thresholds/parallel.json`,
  real_time ceilings from 10 CI artifacts -- the deferred precondition
  was met). Threshold targets gate on the median of
  `TESS_BENCHMARK_GATE_REPETITIONS` (default 3) repetitions instead of a
  single unreplicated sample. New ungated `residency/` family
  (`bench/tess_residency_bench.cc`) covers sparse lookup,
  ensure_resident hits, and eviction churn at budget -- the baseline
  evidence for the audit M11b LRU fix.
- Reason: audit-2026-07-11 H1 and bench lows -- gates that measure
  nothing protect nothing, and later remediation workstreams need
  trustworthy before/after numbers.
- Affected docs: `docs/planning/audit-2026-07-11.md`,
  `docs/planning/audit-2026-07-11-remediation.md`.
- Affected code: `bench/tess_bench.cc`, `bench/tess_diagnostics_bench.cc`,
  `bench/tess_residency_bench.cc`, `bench/CMakeLists.txt`,
  `bench/thresholds/{storage,block,parallel}.json`,
  `.github/workflows/ci.yml`.

## 2026-07-11 - v0.1.0 (S11 close)

- Changed: the implemented prototype is tagged `v0.1.0` while its public API
  remains explicitly pre-stable. CMake metadata, `TESS_VERSION_*`, and the
  smoke test agree. The initial milestone plan is preserved as the planning
  record; the architecture README describes the shipped pre-1.0 surface. The
  reference consumer's composite 10k-tick soak (its S11.4 test)
  locks the integrated behavior the acceptance criteria describe.
- Reason: S11.6 -- every milestone M0-M15 shipped with its gates,
  documentation, and consumer adoption in place.
- Affected docs: `docs/planning/initial-milestone-plan.md`,
  `docs/architecture/README.md`.
- Affected code: `CMakeLists.txt`, `include/tess/tess.h`,
  `tests/tess_smoke.cc`.

## 2026-07-11 - Threshold recalibration + trends snapshot (M14, S11.3)

- Changed: every gated benchmark ceiling in `bench/thresholds/` for the
  key, storage, block, queued, path, topology, and diagnostics families
  is now twice the maximum observed across ten main-run CI baseline
  artifacts (runs 29056942917-29167134881, 10 repetitions each),
  tightened from the single-artifact 3x policy per the 10-artifact rule
  in `docs/performance.md`; 2x headroom absorbs the shared-runner pool's
  heterogeneous-CPU spread, and nanosecond-scale gates keep an absolute
  25 ns floor (2x-of-observed below that fails a correct benchmark on a
  merely-slower runner SKU -- observed empirically during review; these
  gates exist for 5-100x gross regressions). The trends snapshot
  (`docs/assets/benchmark-trends.svg` + the `docs/performance.md` table)
  is regenerated from the same ten artifacts.
- Fixed: the `tess_bench_ci_baselines` target had never been extended
  past the diagnostics family -- scheduler (S7), ecs (S8), render-delta
  (S9), and fields (S11.1) gates existed with no baseline collection.
  They are wired in now (through the binaries their threshold targets
  use), keep their bootstrap ceilings, and get recalibrated once enough
  artifacts carrying them accumulate.
- Reason: S11.3 (consolidation) -- the deferred tightening pass and the
  deferred >=5-artifact snapshot regeneration, done once, reviewably.
- Affected docs: `docs/performance.md`,
  `docs/assets/benchmark-trends.svg`.
- Affected code: `bench/thresholds/{key-conversions,storage,block,
  queued,path,topology,diagnostics}.json`, `bench/CMakeLists.txt`.

## 2026-07-11 - Consolidation examples + CI example smoke (M15, S11.2)

- Added: three examples closing the M15 checklist.
  `examples/colony_2d.cc` is the flagship composition -- queued
  construction edits through an `AutoExecTask` in the PreUpdate phase,
  an OnDirty Topology-phase task doing the incremental per-class region
  update and re-path, movement-class (`MovementClass` walker) agents
  routing around the wall the ops built, and a `DeltaCollector`
  publishing versioned render frames, all under one `tess::Schedule`
  driven by `run_schedule_frame`. `examples/ant_farm_vertical.cc` runs
  a degenerate-axis (y extent 1) x-z cross-section world: one
  distance-field product flooded from all food chambers, per-ant
  `nearest_target` descents, and the shared product served from the
  `FieldProductCache` (asserted hits). `examples/stairs_3d.cc` shows
  `StairTransitions` connecting two z-levels that share no passable
  face, the precheck agreeing, and an incremental `update_region_graph`
  severing the link after demolition. Every example is self-checking
  (nonzero exit on violated expectations), and the dev CI job gains an
  "Example smoke" step that executes every built example binary and
  asserts the expected count ran.
- Reason: S11.2 (consolidation) -- the initial milestone's example checklist
  and a CI
  guarantee that examples keep running, not just compiling.
- Affected docs: `README.md` (stale "two examples" paragraph replaced
  with the full list).
- Affected code: new `examples/colony_2d.cc`,
  `examples/ant_farm_vertical.cc`, `examples/stairs_3d.cc`;
  `examples/CMakeLists.txt`, `.github/workflows/ci.yml`.

## 2026-07-11 - Fields benchmark family (M9/M14, S11.1)

- Added: `bench/tess_fields_bench.cc` + thresholds + the CI step -- the
  gated family M9 never had: distance-field product builds scaling with
  the goal count (1/16/256 goals over an open 64x64 world; 85-103 us
  local), the nearest-target gradient query over a built product
  (64 ns), FieldProductCache hit (25 ns), the mixed miss+store steady
  state, byte-budgeted LRU eviction under a cycling working set, and
  the warm-build allocation gate (zero allocations into reserved
  product/scratch storage; family runs through the diagnostics binary).
  Stateful correctness checks are guarded by iteration count because
  the harness's one-iteration calibration pass runs them too.
- Reason: S11.1 (consolidation) -- closes the M14 family checklist.
- Affected docs: none.
- Affected code: new `bench/tess_fields_bench.cc`,
  `bench/thresholds/fields.json`, `bench/CMakeLists.txt`,
  `.github/workflows/ci.yml`.

## 2026-07-11 - GPU backend interface, interface only (M13, S10)

- Added: `include/tess/gpu/descriptors.h` (GpuFieldFormat derived from
  schema value types; FieldMirrorDesc/`field_mirror_desc` computed from
  compile-time layout facts -- tess pages are SoA per chunk, so mirrors
  are chunk-key-major slices; UploadDesc/`upload_desc` staging a live
  chunk span; DispatchDesc; explicit ReadbackPolicy/ReadbackDesc with no
  full readback by default) and `include/tess/gpu/backend.h`
  (GpuCapabilities, the compile-time-polymorphic GpuBackend concept
  with bool-refusal semantics and CPU fallback, and the default
  NoGpuBackend that compiles everywhere and refuses everything). The
  test-only MockGpuBackend records call sequences. Benchmarks are
  deliberately absent: nothing executes in the initial milestone (per the
  plan's
  "ungated smoke only" note, resolved as no-bench + this record).
- Reason: M13 -- a real backend can be added later without redesigning
  core; CPU-only builds carry zero GPU obligations.
- Affected docs: new `architecture/gpu.md` (+ README index),
  `surface.json`, `tests/AGENTS.md`.
- Affected code: new `gpu/backend.h`, `gpu/descriptors.h`, `tess.h`,
  `CMakeLists.txt`; new `tests/gpu_mock_backend.h`,
  `tests/tess_gpu_interface_test.cc`, `tests/CMakeLists.txt`.

## 2026-07-11 - Render-delta bench family and headless consumer (M11, S9.5)

- Added: `bench/tess_render_delta_bench.cc` + thresholds + the CI step:
  sparse-tile collection (64 scattered tiles over 4096 chunks, 2.3 us
  local), box-granular collection (2.4 us), entity recording at 1k
  entities x 8 steps coalesced vs per-step (24 vs 40 us -- the
  coalescing ratio is visible as trend), the full 4096-chunk baseline
  (14 us), and the allocation gate (zero allocations in a steady
  mark/collect/record/publish cycle; the family runs through the
  diagnostics binary so the gate executes in CI). Bootstrap ceilings
  ~10x local pending the consolidation recalibration. Also
  `examples/render_delta_consumer.cc`: a headless shadow-grid consumer
  that late-joins through a baseline, deliberately drops a frame,
  detects the version gap, and resyncs -- the honest end-to-end home of
  the gap/baseline protocol.
- Reason: S9.5 (M11 close on the tess side).
- Affected docs: none beyond prior slices.
- Affected code: new `bench/tess_render_delta_bench.cc`,
  `bench/thresholds/render-delta.json`, `bench/CMakeLists.txt`,
  `.github/workflows/ci.yml`; new `examples/render_delta_consumer.cc`,
  `examples/CMakeLists.txt`.

## 2026-07-11 - Replay validator and randomized replay acceptance (M11, S9.4)

- Added: `tests/render_delta_replay.h` -- the consumer-model
  RenderReplayGrid (invalidation apply that re-reads the current world
  for covered tiles, a shadow entity->tile map fed by entity deltas,
  the version contract enforced exactly as a consumer would, baselines
  clearing shadow + entity state with an explicit re-snapshot seam).
  Randomized script tests pin the section-8 acceptance "delta replay
  matches projected state": per-tick consumption, coalesced eight-tick
  frames, a lossy consumer reconverging through gap detection + full
  baseline, and a sparse resident-set replay.
- Reason: S9.4 (M11).
- Affected docs: `tests/AGENTS.md`.
- Affected code: new `tests/render_delta_replay.h`;
  `tests/tess_render_delta_frame_test.cc`.

## 2026-07-11 - Baselines, applicability hardening, path overlays (M11, S9.3)

- Added: `collect_baseline` (full scope ONLY -- scoped baselines are
  deliberately absent: a partial baseline adopting the frame version
  would permanently lose out-of-scope invalidations from a gap);
  `PathOverlayDelta` + `stage_path_overlay` + `collect_path_overlays`
  (full-replacement per-frame route decorations, nodes copied at call
  time, gated on `has_goal && Found` to provably avoid stale-ticket
  asserts; overlay overflow drops the overlay, never the frame).
  Changed: `delta_frame_applicable` now rejects truncated BASELINES too
  -- one that overflowed chunk storage covers only part of the world
  while claiming full sync, so baseline consumers size chunk capacity
  to the world and the truth table pins the rejection.
- Reason: S9.3 (M11).
- Affected docs: `architecture/simulation.md`, `surface.json`,
  `tests/AGENTS.md`.
- Affected code: `sim/delta_frame.h`;
  `tests/tess_render_delta_frame_test.cc`.

## 2026-07-11 - Entity-delta hook seam through the ECS pipeline (M11, S9.2)

- Added: a trailing defaulted `DeltaCollector*` on
  `advance_path_agents_with_index`, all `tick_ecs_*`/`tick_entt_*`
  drivers (which stamp `begin_tick` with the new tick before movement),
  and the EnTT lifecycle intents (recording Spawned/Despawned/
  Teleported/Parked/Placed exactly when the intent succeeds; a parked
  despawn records nothing because parking already released its tile).
  The movement observer records each committed step beside the index
  move, so entity-delta completeness holds for the sanctioned ECS
  surface by construction. Source-compatible: all params default to
  nullptr, and a positional `graph` argument cannot bind to the
  collector (distinct pointer types).
- Reason: S9.2 (M11) -- entity deltas are pushed at commit, never
  diffed from mirrors.
- Affected docs: `architecture/ecs.md`, `tests/AGENTS.md`.
- Affected code: `ecs/adapter.h`, `ecs/entt/entt_adapter.h`;
  `tests/tess_ecs_adapter_test.cc`, `tests/tess_ecs_entt_test.cc`.

## 2026-07-11 - DeltaFrame render bridge core (M11, S9.1)

- Added: `include/tess/sim/delta_frame.h` -- the versioned frame
  protocol. Tile deltas are INVALIDATION records (no field values;
  consumers re-read the current world at apply, idempotent convergence);
  collection happens once per published frame through the lost-update-
  safe observe/clear-observed protocol, so multi-tick tile coalescing is
  the storage's native union semantics, free. Per-chunk encoding:
  per-tile records up to `sparse_tile_threshold`, one clipped box record
  above it (and as the degradation when tile storage cannot hold a
  chunk -- never a truncation). Entity records (Moved coalescible
  last-writer-wins within a frame; Teleported/Spawned/Despawned/Parked/
  Placed are barriers) with the coalescing map cleared by walking the
  frame's records (O(records), probe chains kept by backward-shift
  erase). Versioning: collectors start at 1; 0 is the fresh-consumer
  echo so late joiners can only start from a baseline; the version bumps
  iff a frame carries state; truncation (capacity or a hard `clear()`,
  which poisons the stream until a baseline) is a STRUCTURAL gap in
  `delta_frame_applicable`, never advisory. Also hoisted `EntityHandle`
  into `include/tess/ecs/entity_handle.h` so the bridge names entity
  identity without the ECS pipeline include.
- Reason: S9.1 (M11 close), per the reviewed design: the pre-review
  version-0 late-join hole, scoped-baseline gap hazard, silent clear(),
  and advisory-truncation findings are all closed structurally.
- Affected docs: `architecture/simulation.md` (DeltaFrame section),
  `surface.json`, `tests/AGENTS.md`.
- Affected code: new `sim/delta_frame.h`, new `ecs/entity_handle.h`,
  `ecs/adapter.h`, `tess.h`, `CMakeLists.txt`; new
  `tests/tess_render_delta_frame_test.cc`, `tests/CMakeLists.txt`.

## 2026-07-11 - Ecs benchmark family (M10/M14, S8.6)

- Added: `bench/tess_ecs_bench.cc` + `bench/thresholds/ecs.json` + the CI
  threshold step: AgentId-sorted collect over churn-scrambled pools at
  1k/10k/100k (11/104/1092 us local), write-back at 1k/10k (7/75 us),
  the occupancy-index move hot path (9.4 ns), and the adapter-overhead
  headline -- the full EnTT tick vs a raw PathAgentState span doing
  identical marching-agent work (1k: 86 vs 12 us; 10k: 807 vs 140 us
  local; the ratio is collect+sort+write-back+index maintenance and is
  trend-only, never gated). Steady-state ticks are timed with resets and
  re-path ticks absorbed outside the timed region. The family runs
  through the diagnostics bench binary so `ecs/tick_entt_alloc_gate`
  (aborts unless a steady-state tick reports zero allocations,
  benchmark-plan section 14) executes in CI. A separate
  movement-commit-only case was folded into the tick pair -- quiet ticks
  are movement-dominated already. Bootstrap ceilings pending CI
  recalibration in the consolidation stage.
- Affected docs: none.
- Affected code: new `bench/tess_ecs_bench.cc`,
  `bench/thresholds/ecs.json`, `bench/CMakeLists.txt`,
  `.github/workflows/ci.yml`.

## 2026-07-11 - ECS examples: custom store + EnTT pawns (M10/M15, S8.5)

- Added: `examples/custom_ecs_min.cc` (always built) -- a self-contained
  hand-rolled store (parallel arrays, generational ids, the game's own
  position component) implementing every adapter concept and driving the
  generic `tick_ecs_*` pipeline to arrival, proving the concepts are not
  EnTT-shaped; and `examples/entt_pawns.cc` (behind `TESS_ENABLE_ENTT`)
  -- pawn entities spawned through the lifecycle intents, a mid-flight
  goal reassignment, and a despawn freeing its tile, ending with an index
  sync check. Covers M15's "EnTT pawn movement" and "custom adapter"
  example items early.
- Affected docs: none (examples are self-documenting).
- Affected code: new `examples/custom_ecs_min.cc`,
  `examples/entt_pawns.cc`, `examples/CMakeLists.txt`.

## 2026-07-11 - EnTT adapter (M10, S8.4)

- Added: `include/tess/ecs/entt/entt_adapter.h` (gated by the
  consumer-side `TESS_ENABLE_ENTT` macro + an `ENTT_VERSION` `#error`
  enforcing include-before): `EnttHandleAdapter` (null special-cased both
  directions -- entt's null zero-extends to a non-null handle value),
  `EnttTilePositionAdapter` (default PositionAdapter; games substitute
  their own), `EnttPathAgentContext`, `EnttPathAgentSource` (AgentId-
  sorted collection: entries sorted FIRST, batch filled in sorted order;
  PathGoal reconciled into the lifecycle with Unreachable-stays-terminal
  semantics), `EnttPathAgentSink` (idempotent mirror; PathGoal consumed
  on arrival), lifecycle intents (spawn / off-board spawn / despawn /
  teleport / park / place / set / clear goal -- the only sanctioned
  mutation paths; teleport RETAINS PathGoal and re-arms from the new
  position), and `tick_entt_*` drivers that are thin instantiations of
  the generic `tick_ecs_*` pipeline (one pipeline, not two).
- Reason: M10. Off-board (`OffBoard` park/place) exists because the
  downstream consumer parks unplaceable agents after world edits;
  section-8 sync invariants are scoped to on-board agents.
- Affected docs: `architecture/ecs.md` (EnTT section), `surface.json`,
  `tests/AGENTS.md`.
- Affected code: new `ecs/entt/entt_adapter.h`, `CMakeLists.txt`; new
  `tests/tess_ecs_entt_test.cc`, `tests/CMakeLists.txt`.

## 2026-07-10 - EnTT dependency wiring (M10, S8.3)

- Added: `cmake/TessEnttDeps.cmake` pinning EnTT `v3.16.0` at the
  downstream consumer's known-good, MSVC-exercised SHA (also the latest
  upstream tag as of 2026-07-10); `tess_require_entt()` prefers an
  existing `EnTT::EnTT` target or `find_package`, then FetchContent
  (`SYSTEM`, `EXCLUDE_FROM_ALL`). New CMake option `TESS_ENABLE_ENTT`
  (default OFF so network-free builds never fetch; ON in the dev,
  release, bench, bench-profile, and windows-msvc presets) gates only
  tess's own EnTT-dependent test/example/bench targets -- the adapter
  header itself stays gated by the same-named consumer-side preprocessor
  macro, the ImGui precedent.
- Reason: M10 build policy -- core stays free of third-party CMake
  surface; the pin pairs with the consumer's and upgrades in lockstep
  only.
- Affected docs: `docs/dependencies.md` (new EnTT section).
- Affected code: new `cmake/TessEnttDeps.cmake`, `CMakeLists.txt`,
  `CMakePresets.json`.

## 2026-07-10 - ECS-agnostic adapter layer (M10, S8.2)

- Added: `include/tess/ecs/adapter.h`, the dependency-free ECS layer:
  `EntityHandle` (+ `kNullEntityHandle`), the `EntityHandleAdapter` /
  `PositionAdapter` / `PathAgentSource` / `PathAgentSink` concepts, shared
  POD components (`AgentId`, `TilePosition`, `PathGoal`, `PathState`,
  `OffBoard`), `PathAgentBatch` SoA scratch, `TileOccupancyIndex`
  (injective tile->entity open-addressing map with backward-shift
  deletion; box/radius queries deferred beyond the initial milestone --
  probing every box
  coordinate is not a useful spatial query and `entity_at` is the
  primitive), `advance_path_agents_with_index` over the S8.1 commit
  observer, and the `tick_ecs_*` pipeline (collect -> dirty-gated
  exactly-once processing -> index-synchronized movement -> apply).
- Reason: M10. The seam is "agents in deterministic order in, state
  write-back out": adapters mirror lifecycle state instead of
  re-implementing tickets/retries/result application. Determinism
  contract: sources sort by monotonic `AgentId`, never native entity
  value or pool order. The runtime is exclusive to the agent system
  (tickets persist in components across quiet ticks).
- Affected docs: new `docs/architecture/ecs.md` (+ README index),
  `surface.json`, `tests/AGENTS.md`.
- Affected code: new `ecs/adapter.h`, `tess.h`, `CMakeLists.txt`; new
  `tests/tess_ecs_adapter_test.cc`, `tests/CMakeLists.txt`.

## 2026-07-10 - Movement-advance commit observer (M10, S8.1)

- Changed: `advance_path_agents_with_movement` gained an observer overload
  taking `on_commit(agent_index, from, to)`, invoked once per successful
  `commit_movement_intent` -- after the agent position and occupancy fields
  are updated, before arrival handling -- and never for failed validations
  (which write nothing). The existing form delegates with a no-op.
- Reason: the M10 ECS adapter must keep an external tile->entity occupancy
  index synchronized with every committed step (cross-cutting acceptance
  section 8). An observer at the exact commit point keeps one lifecycle
  implementation, preserves the failure taxonomy in one place, and avoids
  the alternatives -- duplicating the advance loop in the adapter or O(n)
  position diffing that loses per-step attribution.
- Affected docs: `docs/architecture/simulation.md` (S8.2 will add the ecs
  doc that consumes this).
- Affected code: `sim/path_agent.h`; `tests/tess_path_agent_test.cc`.

## 2026-07-10 - Codex review fixes: EveryN pokes, hook follow-ups (M5, S7)

- Fixed: two of three connector findings (the third -- the PolicyMismatch
  clear -- had already landed in the pre-merge hardening commit).
  (1) `request_run` now arms EveryN tasks too, as its contract says: one
  extra run without shifting the countdown's lockstep phase (the poke was
  previously ignored and silently consumed at the next scheduled fire).
  (2) The auto-exec task clears its queue BEFORE draining, so follow-up
  operations a result hook enqueues land in the fresh queue for the next
  run instead of being discarded by the end-of-run clear; the channel
  still clears after the drain, completing the paired-clear discipline.
  Both pinned by regression tests.
- Affected docs: none beyond header comments.
- Affected code: `sim/schedule.h`, `sim/auto_exec.h`;
  `tests/tess_sim_schedule_test.cc`, `tests/tess_sim_auto_exec_test.cc`.

## 2026-07-10 - Pre-merge review hardenings: mismatch drop, cadence clamps, reentrancy guard (M5, S7)

- Fixed: three review findings. (1) The auto-exec PolicyMismatch path now
  drops the queue (paired clears): keeping it would wedge the task forever
  in release builds, rescanning the same poisoned frame while new enqueues
  pile on. (2) `Schedule::add_task` validates and re-clamps hand-built
  cadences (Cadence is a public aggregate, so a zero EveryN bypassing the
  factory clamp would wrap its countdown to ~4.29B ticks, and a zero
  background budget would spin in_progress forever). (3) run_tick carries
  an in-run reentrancy assert, and the header names the two calls task
  bodies must not make (add_task, nested run_tick) alongside the three
  that are safe. Also documented: ChunkFn is shared across pool workers
  and must be safe for concurrent invocation; an OnDirty task poked via
  request_run receives pending_dirty == 0 (a full-run request, pinned by
  test).
- Reason: pre-merge review of the S7 branch (no blockers; these were the
  should-fixes and note-level hardenings).
- Affected docs: none beyond header comments.
- Affected code: `sim/schedule.h`, `sim/auto_exec.h`;
  `tests/tess_sim_schedule_test.cc`.

## 2026-07-10 - Scheduler bench family (M5, S7 slice 6)

- Added: `bench/tess_scheduler_bench.cc` + `bench/thresholds/scheduler.json`
  + the CI threshold step: empty-tick dispatch floor (47 ns local),
  100-task cadence dispatch (506 ns), the dirty-trigger path (46 ns), and
  the full auto-exec pipeline per tick over a 64-chunk resident update
  (564 ns). Bootstrap ceilings pending CI recalibration; parallel speedups
  stay trend-only per the standing bench policy.
- Reason: S7 slice 6 -- the plan's scheduler bench family.
- Affected docs: none.
- Affected code: `bench/tess_scheduler_bench.cc`, `bench/CMakeLists.txt`,
  `bench/thresholds/scheduler.json`, `.github/workflows/ci.yml`.

## 2026-07-10 - Worker pool promoted to the production backend (M5, S7 slice 5)

- Changed: docs only. `WorkerPoolPhaseExecutor` is recorded as the
  production parallel backend (the S7 evaluation outcome the concurrency
  plan called for): auto-exec routes phases to it by operation count,
  serial == pool is pinned byte-identical, and TSan covers the schedule and
  auto-exec binaries. work_contract remains an unadopted experiment. The
  coalesced maintenance lane and runtime ownership claim checking are
  explicitly deferred beyond the initial milestone with rationale (no
  initial-milestone consumer; they belong with a deferred-edit flow).
- Reason: S7 slice 5 -- the concurrency stream's landing record.
- Affected docs: `architecture/queued-operations.md`,
  `planning/concurrency-plan.md`.
- Affected code: none.

## 2026-07-10 - Auto-exec task with per-phase routing and goldens (M5, S7 slices 3-4)

- Added: `include/tess/sim/auto_exec.h` -- `AutoExecTask` runs the whole
  queued-ops pipeline as one schedule task over a caller-owned FrameOps
  queue: plan, parallel phase planning, execution serial or on the worker
  pool (chosen per phase by op count), dirty merge after EACH phase (the
  partitioned scratch is re-prepared per phase; a post-loop merge would
  drop all but the last phase's records), ack drain through the task's
  result hook, and paired queue+channel clears ending every run. Policy
  uniformity is pre-validated so runtime aborts are unreachable, which is
  what makes serial and pool execution provably identical.
- Goldens (slice 4): auto-exec == the hand-rolled plan/execute/merge
  pipeline (whole-world fields, chunk versions, dirty flags); serial ==
  pool (worlds, metadata, drained ack sequences) with pool phases taken;
  both binaries green under the TSan preset.
- Reason: S7 slices 3-4 (M5 close): the plan->execute->dirty-apply->drain
  requirement with the S1-validated pool as a per-phase production choice.
- Affected docs: `architecture/simulation.md`, `architecture/surface.json`,
  `tests/AGENTS.md`.
- Affected code: new `sim/auto_exec.h`, `tess.h`, `CMakeLists.txt`; new
  `tests/tess_sim_auto_exec_test.cc`, `tests/CMakeLists.txt`.

## 2026-07-10 - Schedule frame driver (M5, S7 slice 2)

- Added: `run_schedule_frame` + `ScheduleFrameSummary` -- the frame-to-ticks
  bridge over `FixedStepAccumulator`, running the schedule once per granted
  fixed tick so every cadence counts sim ticks rather than frames (exact
  under SimSpeed changes, backlog, and pause; pinned by test). The design's
  proposed task-adapter header was cut: the auto-exec golden composes
  test-local tasks, and the downstream consumer's agent tick is its own
  task body, so no adapter had a consumer.
- Reason: S7 slice 2 (M5 close).
- Affected docs: `architecture/simulation.md`, `architecture/surface.json`,
  `tests/AGENTS.md`.
- Affected code: `sim/schedule.h`; `tests/tess_sim_schedule_test.cc`.

## 2026-07-10 - Schedule core: phases, cadences, budgets (M5, S7 slice 1)

- Added: `include/tess/sim/schedule.h` -- the M5 schedule. Ordered
  `SimPhase` execution of type-erased tasks (fn-pointer + context, no
  std::function); cadences EveryTick / EveryN (exact under disablement:
  the countdown advances regardless, so re-enabling never shifts lockstep)
  / OnDirty (fires iff the task's OWN mask bits are pending; consumes only
  those bits) / Background (deterministic items-only budget with more_work
  continuation; a wall-clock valve was cut deliberately -- it would make
  tick outcomes nondeterministic and had no initial-milestone consumer) /
  Manual.
  Task-result dirty masks merge immediately (later phases fire same tick,
  earlier next tick); notify_dirty/request_run are frame-owner-thread
  only. `SimClock` hoisted from the path-agent tick header into `time.h`
  so both layers share one type. Dispatch after seal() is allocation-free.
- Reason: S7 slice 1 (M5 close). The design review's determinism fixes are
  folded in from the start: EveryN countdown initialized to n (no
  first-tick underflow), own-bit-only OnDirty clearing, and dirty-feeding
  (never scanning) as the structural no-full-world-scans guarantee.
- Affected docs: `architecture/simulation.md`, `architecture/surface.json`,
  `tests/AGENTS.md`.
- Affected code: new `sim/schedule.h`, `sim/time.h` (SimClock hoist),
  `sim/path_agent_tick.h`, `tess.h`, `CMakeLists.txt`; new
  `tests/tess_sim_schedule_test.cc`, `tests/CMakeLists.txt`.

## 2026-07-10 - Result-bearing queued bench gate (M4, S6 slice 4)

- Added: `queued/execute_resident_update_with_results` -- the resultless
  resident-update workload through
  `execute_plan_deferred_dirty_with_results` plus the per-frame drain and
  channel clear a consumer performs, with a correctness check that exactly
  one ack is delivered per frame. Gated at 1500 ns CPU next to the 880 ns
  resultless ceiling (locally 140 vs 114 ns, ~23% ack-delivery overhead);
  bootstrap threshold pending CI recalibration in the consolidation stage.
- Reason: S6 slice 4 -- the plan's bounded-overhead requirement for
  result-bearing versus resultless plan+execute.
- Affected docs: none.
- Affected code: `bench/tess_bench.cc`, `bench/thresholds/queued.json`.

## 2026-07-10 - Result-bearing execution wrappers (M4, S6 slice 3)

- Added: `execute_phase_partitioned_dirty_with_results<Policy>` and
  `execute_plan_deferred_dirty_with_results<Policy>` in
  `ops/result_channel.h`. The callback gains the operation's channel value
  (`fn(view, T&)`), accumulated op-exclusively on the executing thread;
  completions are stamped worker-side because `PlannedExecutionResult`
  default-constructs to Executed and the serial executor early-stops, so a
  post-barrier sweep would misread never-run operations as executed. All
  phase/plan operations are prepared upfront (aborted tails read Pending),
  and the phase range is validated before the channel is touched.
- Reason: S6 slice 3 -- the executor-agnostic delivery path: identical
  drain order and content under serial and threaded executors for
  successful plans (pinned by test against both), failure reasons instead
  of values at runtime, allocation-free warm frames.
- Affected docs: `architecture/queued-operations.md`,
  `architecture/surface.json`, `tests/AGENTS.md`.
- Affected code: `ops/result_channel.h`, `ops/queued.h` (one friend
  declaration + a ResultChannel forward declaration);
  `tests/tess_queued_results_test.cc`.

## 2026-07-10 - PlannedOperation carries its enqueue source (M4, S6 slice 2)

- Changed: `PlannedOperation` gains a trailing `std::source_location source`
  member, copied from the queued operation at plan time, so run-time
  completions can report the enqueue site exactly as plan-time rejections
  already do through `OperationReport::source`. Behavior-neutral otherwise;
  the only aggregate-init site is `plan_operations`.
- Reason: S6 slice 2 -- the result-bearing execute wrappers (next slice)
  stamp `OpCompletion.source` from the planned operation on the executing
  thread; without this member they would need the full `ExecutionReport`
  plumbed through, which breaks for subset-span plans.
- Affected docs: none (member documented inline; not a surface symbol).
- Affected code: `ops/queued.h`.

## 2026-07-10 - Result-channel core: OpCompletion + drain-only ResultChannel (M4, S6 slice 1)

- Added: `include/tess/ops/result_channel.h` -- `OpCompletion` (both failure
  domains plus chunk count and enqueue-site source_location, with a
  `completed` flag so a default record can never read as success),
  `OpResultState` (Unbound/Pending/Ready/Failed), the caller-owned dense
  `ResultChannel<T>` keyed by OpHandle with `drain_results(visitor)` in
  handle order (failures deliver reasons, never values; drain-once;
  lookups readable until clear), and `record_plan_completions(report,
  channel)` delivering plan-time rejections through the same drain.
- Reason: S6 slice 1 (M4 close). Design review (workflow + two adversarial
  critiques) converged on a deliberately DRAIN-ONLY initial-milestone result
  surface: the pipeline has no asynchronous execution path, so a poll-only
  future could never be
  observed pending and was the source of every footgun found (stale-assert
  semantics, fail-safe fallbacks, dead expect()); futures return when
  budget-deferred execution gives them a consumer. Recorded as a TDD
  divergence alongside the deferred cancelled/superseded states.
  Publication is executor-agnostic with zero atomics: per-op slot writes on
  the executing thread, all reads after the synchronous execute call, join
  barrier supplies visibility.
- Affected docs: `architecture/queued-operations.md`,
  `architecture/surface.json`, `tests/AGENTS.md`.
- Affected code: new `ops/result_channel.h`, `tess.h`, `CMakeLists.txt`;
  new `tests/tess_queued_results_test.cc`, `tests/CMakeLists.txt`.

## 2026-07-10 - Codex review fixes: span-path advertisement, stair narrowing, direct route-cache guard (M6, S5)

- Fixed: three of four connector-review findings. (1) `WalkableCostField`
  declared `passable_tag`, satisfying `HasPassableSpan` without providing
  `passable_span`, so using it for region labeling failed to compile inside
  the topology flood; the marker is removed (its two-field predicate must
  not take the raw-span fast path) and the concept's contract is documented.
  (2) `StairTransitions` narrowed the stair value to `uint8_t` before the
  range check, so a wider field holding 257 wrapped into `PositiveX`; the
  check now precedes any narrowing and rejects negatives. (3) The public
  `cached_astar_path` accepts movement classes but `RouteCacheScratch` keys
  entries on (start, goal) only, so DIRECT callers (outside
  `PathRequestRuntime`'s binding) could be served another class's route; the
  cache now binds itself to each call's normalized class and a rebind drops
  the entries, counted in the new `RouteCacheStats::class_rebinds`.
- Declined: validating entry cost in `validate_movement_intent`. Commit
  validates the class's PASSABILITY predicate only -- cost is a search
  concern, and commit staying more permissive than the weighted search is
  the deliberate legacy asymmetry (`WalkableCostField` is the opt-in that
  folds cost into passability at commit too). Documented in simulation.md.
- Affected docs: `architecture/simulation.md`, `tests/AGENTS.md`.
- Affected code: `topology/movement_class.h`,
  `topology/transition_provider.h`, `path/route_cache.h`;
  `tests/tess_topology_movement_test.cc` (WalkableCostField labeling
  compiles and excludes zero-cost tiles; wide stair value does not wrap),
  `tests/tess_path_movement_class_test.cc` (direct route-cache class guard).

## 2026-07-10 - Pre-merge review fixes: stair down-transitions, class-binding order (M6, S5)

- Fixed: two defects found (and reproduced) by the pre-merge review.
  (1) `StairTransitions` never emitted the DOWN transition of a stair whose
  landing steps sideways across an x/y chunk boundary at a local z below the
  chunk top: enumeration scanned only the chunk itself and its -z neighbor.
  It now scans the four sideways same-z neighbors too, so every legal
  landing chunk (own, +z, or sideways face neighbor) emits its down
  direction. (2) `PathRequestRuntime::process_unit_cached` bound the
  movement class BEFORE `prepare_process`, so a policy-triggered
  `clear_caches()` (e.g. `clear_every_world_change = 1`) zeroed the binding
  mid-call and the caches refilled under an unbound identity a later class
  could silently reuse; the binding now happens after the prepare pass.
  Also: an out-of-range stair field value now reads as `None` instead of
  leaking an unintended straight-up transition, the `bind_unit_class` and
  path.md docs now state explicitly that the caller-driven weighted portal
  segment cache is NOT covered by the class binding (keep one per class,
  as before), and topology.md notes the sparse reaches-missing pass
  re-enumerates every resident chunk per update.
- Reason: both defects lived exactly in the gaps the original fixtures
  could not reach (multi-z-extent chunks; the `clear_every_world_change`
  knob) -- each is now pinned by a regression test.
- Affected docs: `architecture/path.md`, `architecture/topology.md`,
  `tests/AGENTS.md`.
- Affected code: `topology/transition_provider.h`, `path/path_runtime.h`;
  `tests/tess_topology_movement_test.cc` (sideways-crossing stair both
  directions + incremental equality, out-of-range stair value),
  `tests/tess_path_movement_class_test.cc` (policy-clear rebind guard).

## 2026-07-10 - StairTransitions vertical provider (M6, S5 slice 8)

- Added: `StairTransitions<StairTag>` in `topology/transition_provider.h` --
  an integral field holds a `StairDirection`, marking the tile as the foot
  of a stair whose landing is one step sideways and one z-level up. The
  offset form is deliberate: two stacked passable tiles are already six-axis
  adjacent, so only an offset stair adds connectivity. Both directions are
  emitted, each from the chunk owning its origin tile (down from the
  landing's chunk -- the foot's chunk or its +z face neighbor), keeping
  incremental re-derivation exact. Endpoint traversability stays a
  movement-class question (label filter), so stair edges are per-class. A
  landing that would cross two chunk boundaries at once (sideways off the
  x/y edge AND up off the top z layer) violates the face-neighbor contract
  and contributes nothing, documented as the placement limit. On sparse
  worlds a foot in a non-resident chunk needs no special handling: the
  landing tile's own -z boundary exit already marks its region as reaching
  missing topology.
- Reason: S5 slice 8 -- the concrete vertical provider closing M6's
  stairs/ladders deliverable on the S5.7 contract, with no byte-identity
  shield (new code), so connectivity, per-class filtering, incremental
  equality, and the diagonal limit are all pinned by property tests.
- Affected docs: `architecture/topology.md`, `architecture/surface.json`,
  `tests/AGENTS.md`.
- Affected code: `topology/transition_provider.h`;
  `tests/tess_topology_movement_test.cc` (two-level fixture, both-direction
  reachability, Builder-only stair, incremental == full across stair edits,
  diagonal skip, same-chunk landing).

## 2026-07-10 - Transition providers for the region graph (M6, S5 slice 7)

- Added: `include/tess/topology/transition_provider.h` -- the
  `TransitionProviderFor<P, World>` concept and the `AdjacentTransitions`
  default. A provider contributes EXTRA directed tile-to-tile transitions
  (stairs, ladders) enumerated once per chunk; `build_region_graph` /
  `update_region_graph` take an optional trailing provider and append one
  directed `RegionPortal` per transition whose endpoints both resolve to
  labeled regions (provider edges are automatically per-class). The landing
  tile must stay within the origin chunk or a face neighbor -- the exact
  range incremental updates re-derive -- asserted in debug builds. The
  provider TYPE is stamped on the graph beside the movement class
  (`matches_provider`); an update with a different provider type falls back
  to a full rebuild. On sparse worlds a transition landing in a non-resident
  chunk marks its origin region as reaching missing topology, so reachability
  degrades to Indeterminate, never a wrong Unreachable.
- Reason: S5 slice 7 -- M6's special-transitions contract. The default
  provider keeps every existing build byte-identical (pinned by test) while
  giving the vertical stair provider (next slice) a sound, incremental-safe
  extension point.
- Affected docs: `architecture/topology.md`, `architecture/surface.json`,
  `tests/AGENTS.md`.
- Affected code: new `topology/transition_provider.h`;
  `topology/topology.h` (provider stamp, per-chunk provider portals,
  reaches-missing pass), `tess.h`, `CMakeLists.txt`;
  `tests/tess_topology_movement_test.cc` (default identity, bridge portals,
  incremental == full, provider-mismatch rebuild, sparse Indeterminate).

## 2026-07-09 - Class-aware agent tick and the runtime class binding (M6, S5 slice 6)

- Changed: the agent pipeline speaks classes end to end.
  `process_unit_path_agents`, `advance_path_agents_with_movement`,
  `tick_unit_path_agents[_with_movement]`, and
  `PathRequestRuntime::process_unit_cached` take `ClassOrTag`;
  `process_weighted_path_agents`, `tick_weighted_path_agents[_with_movement]`,
  and `process_weighted_batch` gain `<World, Class, MaxCost>` forms in which
  ONE movement class drives the search, the precheck, and commit validation,
  while the legacy `<PassableTag, CostTag>` overloads keep the historical
  semantics (LegacyWeighted search; precheck on passability only, matching
  tag-built graphs). `PathRequestRuntime` now binds itself to the movement
  class of each unit process call: the unit route cache keys entries on
  (start, goal) plus a world-version fingerprint and nothing on the class, so
  a rebind clears the unit caches (correct even on misuse) and counts in the
  new `PathRuntimeStats::class_cache_invalidations`; one runtime per
  (world, class) is the PERF contract, not a correctness precondition.
- Reason: S5 slice 6 -- without the binding, a runtime reused across classes
  would serve one class's cached route to another (same start/goal, same
  world version, different passability), the exact cross-class collision the
  graph stamp closes for prechecks. Pinned by test: a Walker-cached 21-step
  detour is never served to a Builder asking the same (start, goal), which
  gets its 7-step route through the wall instead.
- Affected docs: `architecture/path.md`, `architecture/simulation.md`,
  `tests/AGENTS.md`.
- Affected code: `path/path_runtime.h` (class binding, weighted impl split),
  `sim/path_agent.h`, `sim/path_agent_tick.h`;
  `tests/tess_path_movement_class_test.cc` (stale-route regression,
  per-class tick divergence with zero movement rejections).

## 2026-07-09 - Class-aware movement commit validation (M6, S5 slice 5)

- Changed: `validate_movement_intent` / `commit_movement_intent` take a
  movement class OR a raw passable tag (`ClassOrTag`, normalized exactly as
  in astar_path) instead of a bare `PassableTag`. Each endpoint's passability
  is evaluated on its own resolved page -- from and to may live on different
  pages -- replacing the coord-scope `field<PassableTag>` point reads; the
  identity class performs the same resolve+field reads the legacy code did.
- Reason: S5 slice 5 -- plan == commit. A* (slice 2) and the region graph
  (slice 3) already speak the class vocabulary; commit validation was the
  remaining seam still hard-wired to a single global passability, which would
  let a Builder plan a step through a construction site and then have the
  commit reject it. Pinned by test: every step weighted A* accepts for a
  class validates as Moved for that same class, and BlockedFrom/BlockedTo are
  per class on both endpoints.
- Affected docs: `architecture/simulation.md`, `tests/AGENTS.md`.
- Affected code: `sim/movement.h`;
  `tests/tess_path_movement_class_test.cc` (plan==commit property,
  per-class block statuses).

## 2026-07-09 - Precheck class agreement through the graph stamp (M6, S5 slice 4)

- Changed: `precheck_path<ClassOrTag>(graph, world, start, goal, scratch)` --
  the movement class the search uses is now the explicit first template
  argument, and the gate checks `is_region_graph_fresh_for<ClassOrTag>`
  instead of the classless freshness: a graph labeled for a different
  movement class (or predating any stamp) reports `GraphStale` and degrades
  to running A*. `PathRequestRuntime::precheck_prepass<ClassOrTag>` threads
  the class from `process_unit_cached` / `process_weighted_batch` (the
  weighted batch prechecks on PASSABILITY only, matching the legacy weighted
  asymmetry it searches with).
- Reason: S5 slice 4 -- closes the documented precondition hole: the graph
  type encodes only residency, so before the stamp a graph built over a
  different passability compiled fine and its definitive `Unreachable` could
  prune a route the search's own class could walk (the one way the gate could
  turn a solvable query into a wrong failure). That agreement is now enforced
  at runtime, not delegated to the caller.
- Affected docs: `architecture/path.md`, `tests/AGENTS.md`.
- Affected code: `path/precheck.h`, `path/path_runtime.h`,
  `bench/tess_topology_bench.cc` (explicit class argument);
  `tests/tess_path_precheck_test.cc` (wrong-class -> GraphStale, per-class
  rule-out), `tests/tess_path_precheck_runtime_test.cc` (Builder falls back
  to A* under a walker-stamped graph, Builder-stamped graph rules out without
  searching).

## 2026-07-09 - Per-class region labeling and the graph class stamp (M6, S5 slice 3)

- Changed: `build_local_chunk_topology`, `build_region_graph`, and
  `update_region_graph` take a movement class OR a raw passable tag. The
  identity class floods the raw `field_span` exactly as the legacy single-tag
  build did (byte-identical labels and codegen, pinned by test); a composed
  class evaluates its predicate on the resolved page per tile. Portal pairing
  is untouched -- it queries labels, so per-class labels yield per-class
  portals automatically. `RegionGraphT` gains a movement-class stamp
  (`built_class_`, a `core/tag_identity.h` token of the NORMALIZED class)
  recorded at build time and mirrored on `matches_shape`: a stamp mismatch in
  `update_region_graph` forces a full rebuild with the requested class's
  labels, the public `matches_class<ClassOrTag>()` reports the binding, and
  the new `is_region_graph_fresh_for<ClassOrTag>(world, graph)` is the
  class-aware freshness form (later slices route precheck through it so a
  wrong-class graph degrades to GraphStale, never a wrong Unreachable).
- Reason: S5 slice 3 -- the same vocabulary that drives search (slice 2) now
  drives labeling, so a Walker graph and a Builder graph over one world are
  first-class. The stamp closes the documented precheck precondition gap: the
  graph type encodes neither shape nor class, so binding both at build time is
  what keeps a definitive Unreachable trustworthy.
- Affected docs: `architecture/topology.md`, `architecture/surface.json`,
  `tests/AGENTS.md`.
- Affected code: `topology/topology.h`; new
  `tests/tess_topology_movement_test.cc` (identity byte-identity,
  Walker/Builder divergence + Builder-only bridge, per-class incremental ==
  full, stamp-mismatch rebuild, per-class freshness, alloc-free warm
  relabel), `tests/CMakeLists.txt`; `bench/tess_topology_bench.cc` +
  `bench/thresholds/topology.json` (composed-class 512x512 labeling gate;
  measured on par with the raw scan locally, bootstrap threshold 3x the
  identity gate pending CI calibration).

## 2026-07-09 - Movement classes through the A* leaves and weighted cores (M6, S5 slice 2)

- Changed: the pathfinding passability/cost leaves (`detail::is_passable`,
  `is_passable_index`, `tile_entry_cost_index`) now evaluate a movement class
  at the resolved (page, tile) seam. The passability leaves accept a class OR
  a raw tag (normalized through `movement_class_of`, so every legacy
  `<World, Tag>` call site compiles unchanged and stays byte-identical via the
  `WalkableField` identity); the cost leaf requires an actual class, because a
  raw tag would normalize to the unit-cost identity and silently discard a
  legacy `CostTag`. The weighted family -- `weighted_astar_path`,
  `build_weighted_distance_field`, `build_weighted_distance_field_in_box`,
  `build_bounded_weighted_distance_field`, `weighted_distance_field_path`,
  `weighted_path_batch` -- gained single-`Class` cores; the historical
  `<PassableTag, CostTag>` overloads remain as thin forwarders through
  `movement::LegacyWeighted` (exact semantics, including the cost-agnostic
  passability asymmetry). `tag_identity` hoisted from a private
  `FieldProductCache` member to `include/tess/core/tag_identity.h`
  (`tess::detail`) so later slices can stamp graphs and runtimes with a class
  identity. Diagnostics placement is unchanged (`path_cost_read` in the cost
  leaf, `path_passability_check` at callers), so counters do not drift.
- Reason: S5 slice 2 -- one vocabulary must drive search so per-class region
  labeling (slice 3) and precheck/commit agreement (slices 4-5) can share it.
  Fusing the pair into one class also removes the latent trap of mixing a
  passability tag and a cost tag from different logical classes. The
  route-product/portal-route family and the path runtime still carry tag
  pairs and forward through the legacy overloads; they convert with the
  runtime in slice 6.
- Affected docs: `architecture/path.md`, `tests/AGENTS.md`; changelog entries
  from 2026-07-06 moved to `CHANGELOG-archive.md` (token cap).
- Affected code: `path/path.h`, `path/detail/astar.h`,
  `path/detail/weighted_batch.h`, `path/distance_field_box.h`,
  `path/field_product_cache.h`, new `core/tag_identity.h`, `CMakeLists.txt`;
  new `tests/tess_path_movement_class_test.cc` (identity and LegacyWeighted
  node-for-node equivalence, Walker/Builder divergence, sparse missing-chunk
  contract), `tests/CMakeLists.txt`.

## 2026-07-09 - Movement vocabulary DSL (M6, S5 slice 1)

- Added: `include/tess/topology/movement_class.h` (namespace `tess::movement`) --
  a compile-time DSL where a `MovementClass<PassExpr, CostExpr>` fuses a
  passability predicate and an entry-cost expression composed from typed-field
  leaves. Boolean terms `Field`/`NotZero`/`Not`/`AllOf`/`AnyOf`; cost
  expressions `UnitCost`/`ConstantCost`/`FieldCost`/`SelectCost` with a
  `normalize_cost` byte-exact to the weighted A* leaf; identity classes
  `WalkableField`/`WalkableCostField`/`LegacyWeighted`; the `MovementClassFor`
  concept and `movement_class_of` tag/class normalization.
- Reason: first slice of the M6 movement-vocabulary close (S5). Labeling,
  pathfinding, and commit validation currently each bake in a single global
  passability (a raw `PassableTag`); this vocabulary is the shared, allocation-
  free, constexpr foundation later slices thread through region labeling,
  precheck, the weighted agent tick, and `sim/movement.h` so plan and commit
  agree. Leaves read the constexpr `ChunkPage::field<Tag>` at the (page, tile)
  seam because world-scope accessors are not constexpr, and the whole predicate
  inlines to the same ops a hand-written cast emits. `WalkableField` is a
  distinct struct (not an alias) carrying the raw tag + a `field_span` fast path
  so the identity class stays byte-identical to today's single-field scan. Pure
  vocabulary; no wiring yet.
- Affected docs: `architecture/topology.md`, `architecture/surface.json`.
- Affected code: new `topology/movement_class.h`, `tess.h`, `CMakeLists.txt`;
  new `tests/tess_movement_class_test.cc`, `tests/CMakeLists.txt`,
  `tests/AGENTS.md`.

## 2026-07-09 - Path product/cache invalidation contracts (audit-2 W-A)

- Changed: (1) Route, portal-route, and distance-field products treat an empty
  dependency set as invalid, and every builder now captures dependencies for
  non-Found results (`capture_all`), so a cached failure can never replay
  after a world edit that might change the answer. (2)
  `build_distance_field_product` additionally depends on the face neighbors
  of every touched chunk, covering the blocked frontier: an edit that opens a
  fully-sealed chunk now invalidates the product. (3)
  `RouteCacheScratch::store` skips a single route larger than the node cap
  (new `stats().oversized_skips`) instead of invalidating resident entries
  and storing it anyway, and cap value 0 now disables storage rather than
  meaning "unlimited", matching the portal segment cache. (4)
  `build_weighted_portal_route_product` accepts its own `waypoints()` span
  (rebuild-from-own-product no longer reads a cleared vector). (5)
  `PathTicket.generation` widened to 64 bits so retained tickets cannot alias
  across generation wraparound. (6) Documented: the caller-owned staleness
  contract of `cached_astar_path`, the one-cache/runtime-per-world identity
  contract, and the chunk-portal route builder's heuristic (non-authoritative)
  NoPath tier.
- Changed (review follow-up): the multi-agent review of this branch found
  the segment-cache overload of `build_weighted_portal_route_product` had
  been missed (same self-alias UB -- ASan-confirmed -- and missing failure
  capture); the alias guard now covers all product-owned spans (including a
  previously returned `PathResult.path`) via a shared `stash_if_owned`
  helper. It also flagged two quadratic passes and an over-broad capture,
  fixed as: `capture_all` appends directly (O(chunk_count)), the
  blocked-frontier pass dedupes through a scratch seen-set +
  `add_chunk_unique` (linear), and InvalidStart/InvalidGoal products now
  depend only on the offending in-bounds tiles' chunks instead of every
  chunk (out-of-bounds failures carry no dependencies and are permanently
  invalid -- callers pay only the cheap bounds rejection on rebuild).
  `reserve_dependencies` docs now state the chunk_count bound.
- Reason: second audit (2026-07-09) findings H1, H2, M3, M4, M5, M6 and cache
  lows -- failure products carried empty dependency sets that validated
  vacuously forever, and unreached chunks never invalidated field products,
  so removing a wall could leave agents permanently pathless.
- Affected docs: `planning/audit-2026-07-09.md`,
  `planning/audit-2026-07-09-remediation.md`, `decisions/CHANGELOG.md`.
- Affected code: `path/path.h`, `path/portal_route.h`,
  `path/field_product_cache.h`, `path/route_cache.h`, `path/path_runtime.h`,
  `tests/tess_path_test.cc`, `tests/tess_path_cache_test.cc`,
  `tests/tess_path_runtime_test.cc`.
## 2026-07-09 - Executor dispatch guards and partial-plan dirty (audit-2 W-B)

- Changed: (1) `WorkerPoolPhaseExecutor` now documents and enforces its
  single-dispatch contract -- a `dispatch_active_` flag maintained under the
  existing dispatch mutex makes nested or concurrent `for_each_operation`, and
  `reserve_operations` during a dispatch, fail fast via `TESS_ASSERT` in debug
  builds (release builds compile the check out and keep zero overhead). (2)
  `execute_plan` / `execute_plan_deferred_dirty` now include the chunks written
  before an abort in the returned `chunk_count`, and the scheduler tick marks
  pathing dirty whenever any chunk was written, so a plan aborted partway by a
  `PolicyMismatch` no longer leaves path caches stale over already-mutated
  passability. (3) A blocked movement step no longer consumes re-path budget;
  only `prepare_path_agent_processing` counts attempts, so
  `max_blocked_retries = N` grants exactly N re-path attempts (previously a
  movement-blocked cycle was double-counted), and the budget semantics --
  including the by-design indefinite re-path loop against a permanently parked
  blocker -- are documented at `max_blocked_retries`. (4) Thread-spawn loops in
  both executors now join already-started threads and rethrow if a
  `std::thread` constructor throws mid-spawn instead of terminating via a
  joinable-thread unwind; `ScopedThreadPhaseExecutor` documents its no-throw
  callback requirement. (5) `FixedStepAccumulator` clamps the ~1 ulp negative
  bank left by the rounded-division one-tick borrow.
- Reason: second-audit findings H3/M2 (nested/concurrent dispatch deadlocks
  and use-after-realloc were silent), M1 (partially executed plans skipped the
  pathing-dirty mark), and the workstream's low-severity items.
- Affected docs: `decisions/CHANGELOG.md`.
- Affected code: `ops/phase_executor.h`, `ops/queued.h`, `sim/scheduler.h`,
  `sim/path_agent.h`, `sim/path_agent_tick.h`, `sim/time.h`,
  `tests/tess_phase_executor_test.cc`, `tests/tess_sim_scheduler_test.cc`,
  `tests/tess_path_agent_tick_test.cc`, `tests/tess_path_agent_test.cc`,
  `tests/tess_path_runtime_sparse_test.cc`.
## 2026-07-09 - Topology index/shape hardening (audit-2 W-C)

- Changed: (1) `RegionGraph::region_index` guards are wrap-proof -- the chunk
  guard no longer adds 1 before comparing (the out-of-world sentinel ChunkKey
  wrapped past it into an OOB read) and the offset arithmetic is 64-bit (a
  region id near 2^32 wrapped back to a valid-but-wrong index). (2) Region
  graphs now record their build shape (chunk-grid and chunk tile extents);
  `update_region_graph` fully rebuilds and `is_region_graph_fresh` reports
  stale on any mismatch, instead of the chunk-count-only check that let
  equal-count shape mismatches incremental-patch onto wrong adjacency.
  (3) `ShapeTraits` gains division-based (wrap-proof) compile-time overflow
  asserts and an int64 bound on size axes. (4) `detail::box_axis_end` and the
  region-bounds union saturate at int64 max instead of wrapping on extents
  >= 2^63. (5) Documented `ChunkView::is_boundary` degenerate-axis semantics
  and the sparse one-graph-per-world staleness contract.
- Reason: audit findings M7 (region-index wraparound), M8 (shape-mismatch
  incremental patching leaving dangling portal targets), and the related
  topology/shape/meta low-severity items.
- Affected docs: `decisions/CHANGELOG.md`.
- Affected code: `topology/topology.h`, `core/shape.h`,
  `storage/chunk_meta.h`, `block/block.h`, `tests/tess_topology_test.cc`,
  `tests/tess_topology_sparse_test.cc`, `tests/tess_storage_test.cc`.
## 2026-07-09 - Diagnostics alloc-hook and capture-contract fixes (audit-2 W-D)

- Changed: three second-audit fixes on the diagnostics layer. (1) M9: the
  benchmark/test allocation hooks no longer record a deallocation for a null
  pointer -- `operator delete(nullptr)` / `free(nullptr)` are legal no-ops, so
  counting them skewed the deallocations/allocations balance; both the plain
  operator new/delete branch and the sanitizer free hook now null-check before
  recording. (2) M10: the sanitizer-hook branch is excluded on Windows
  (`!defined(_MSC_VER)`, covering MSVC and clang-cl) because MSVC
  /fsanitize=address defines `__SANITIZE_ADDRESS__` but its ASan runtime never
  calls the `__sanitizer_*` hooks and `<pthread.h>` does not exist there.
  (3) M11: documented the threading contract of `capture_timing` /
  `capture_diagnostics` (unsynchronized reads: capture on the recording thread
  or externally synchronize; only the returned snapshot is safe to share) with
  matching notes in trace.h and the ImGui panels header. Docs only, no
  behavior change.
- Reason: findings M9-M11 of the 2026-07-09 second audit.
- Affected docs: `decisions/CHANGELOG.md`.
- Affected code: `bench/tess_diagnostics_alloc_hooks.cc`,
  `diagnostics/export.h`, `diagnostics/trace.h`, `debug/imgui/panels.h`,
  `tests/tess_diagnostics_enabled_test.cc`.
## 2026-07-09 - Test hardening: flake guards, format checking, coverage gaps (audit-2 W-E)

- Changed: test-only hardening from the second audit; no library behavior
  changes. (1) The worker-pool warm no-alloc test tolerates one-time lazy
  runtime allocations on live pool workers by requiring only the last of
  several warm dispatches to be allocation-free (the counter is
  process-global). (2) The ImGui stub's `Text` now carries real ImGui's
  printf-format attribute so `-Wformat` checks panel format strings under the
  `-Werror` presets. (3) The two multi-worker rendezvous spins in the queued
  tests are bounded (30s, clear failure message) instead of hanging to the
  ctest timeout on regression. (4) New coverage: `PrecheckStatus::InvalidStart`
  (out-of-bounds and walled starts), `MovementFailureCounts`
  reserved/stale buckets plus the full `is_transient_movement_failure`
  classification, the four previously unasserted `PathCounters` fields
  (initializations, start/goal passability checks, closed neighbors) via both
  direct events and a real A* maze, a `TESS_ASSERT_MSG` death test, and a
  `UInt128` negative-int constructor death test. (5) Weak assertions
  strengthened: the smoke test pins the released version against a
  hand-maintained literal mirror of the CMake project version instead of
  comparing macros to themselves (a `tess.h` bump that forgets the test now
  fails; a CMake-only bump remains undetectable there), and the warm
  no-alloc agent/tick tests now also pin observable work (submitted/found
  stats, skipped processing with advancement) so a no-op cannot pass. The
  allocation counter documents its relaxed-ordering under-count caveat.
- Reason: second-audit findings M14/M15 plus grep-verified untested surfaces
  and tautological or effect-blind assertions; each weakness could hide a real
  regression (latent flake, unchecked format strings, suite-long hangs,
  silently skipped work).
- Affected docs: `tests/AGENTS.md`.
- Affected code: tests only — `tests/tess_phase_executor_test.cc`,
  `tests/imgui_stub/imgui.h`, `tests/tess_queued_test.cc`,
  `tests/tess_path_precheck_test.cc`, `tests/tess_path_agent_test.cc`,
  `tests/tess_path_agent_tick_test.cc`, `tests/tess_diagnostics_enabled_test.cc`,
  `tests/tess_assert_test.cc`, `tests/tess_shape_test.cc`,
  `tests/tess_smoke.cc`, `tests/allocation_counter.cc`.
## 2026-07-09 - CI and tooling audit remediation (audit-2 W-F)

- Changed: five infrastructure fixes from the 2026-07-09 second audit.
  (1) The `parallel/*` benchmark family is now executed in CI: a
  `tess_bench_parallel_smoke` CTest in the style of the other family smokes,
  and a `parallel/.*` command in `tess_bench_ci_baselines` so baseline
  artifacts accumulate samples. Threshold gating is deliberately deferred:
  repo policy calibrates ceilings from CI baseline artifacts (~3x observed
  maximum), none exist yet for this family, and each gating target enumerates
  an explicit thresholds file, so omitting `bench/thresholds/parallel.json`
  defers the gate without code changes. (2) `tools/benchmark_trends.py`
  discovers every result JSON in a baseline artifact (excluding
  `metadata.json`) instead of reading only block/storage/key, and an
  explicit `--benchmark` selector that matches nothing is now an error
  instead of a silent "n/a"; `tools/benchmark_artifact_metadata.py` gained
  its first tests. (3) `tools/benchmark_thresholds.py` rejects unknown
  per-benchmark limit keys against an allowlist so a typo cannot silently
  disable a gate. (4) A public-safety deny pattern for the private
  downstream consumer's name (split-string style, case-insensitive) so the
  hooks catch reintroduction. (5) Doc drift from the 2026-07-07 gate
  promotions: the Windows MSVC job and public-surface manifest check are
  described as required gates everywhere, the README threshold-suite list
  includes `topology` and `diagnostics`, the advisory GCC compile job is
  listed, stale advisory comments in the workflow were removed or dated,
  and small fixes (path.json note provenance, deck help env vars,
  `.pytest_cache/` ignore, `actions/cache@v4` in the dependency inventory).
- Reason: the audit found compiled-but-never-run benchmarks, tools that
  silently ignored data or typos, a policy without an enforcing pattern,
  and documentation contradicting the actual CI gate status. All fixes are
  infrastructure-only; no tess API or runtime behavior changed.
- Affected docs: `README.md`, `tests/AGENTS.md`, `dependencies.md`,
  `decisions/CHANGELOG.md`.
- Affected code: `bench/CMakeLists.txt`, `bench/thresholds/path.json`,
  `tools/benchmark_trends.py`, `tools/benchmark_artifact_metadata.py`,
  `tools/benchmark_thresholds.py`, `tools/check_public_surface.py`,
  `tools/git_hooks.py`, `tools/steamdeck/deck`, `tests/test_git_hooks.py`,
  `tests/test_benchmark_tools.py`, `.github/workflows/ci.yml`,
  `.gitignore`.

## 2026-07-09 - TraceBuffer pinned to its storage (M12, S4)

- Changed: `diagnostics::TraceBuffer` is now non-copyable and non-movable (all
  four special members deleted).
- Reason: the buffer owns its ring metadata and per-category timing
  accumulators but only references caller storage through a `std::span`. An
  implicit value copy produced two buffers with independent metadata over the
  same backing array, so passing a buffer by value into a helper that installs
  `ScopedTrace` would collect records the caller's original buffer never sees --
  `capture_diagnostics` would silently miss them. Pinning the buffer to its
  storage makes that misuse a compile error; every caller already constructs it
  in place and passes it by reference, so nothing else changed. Flagged by the
  Codex connector review of PR #8.
- Affected docs: `decisions/CHANGELOG.md`.
- Affected code: `diagnostics/trace.h`, `tests/tess_diagnostics_trace_test.cc`.

## 2026-07-09 - Diagnostics review follow-up (M12, S4)

- Changed: three review-feedback fixes on the S4 diagnostics branch. (1) Removed
  the private downstream consumer's name from the new diagnostics docs, using
  generic "downstream consumer/adoption" wording instead -- tracked content is
  treated as public per `AGENTS.md`. (2) Documented the optional, consumer-
  provided Dear ImGui integration dependency that `debug/imgui/panels.h`
  targets. (3) Wired the diagnostics benchmark threshold target into CI so
  `bench/thresholds/diagnostics.json` actually gates regressions.
- Reason: the ImGui-panels and diagnostics slices named the private consumer in
  a repository intended to be public, added an optional dependency without the
  required entry in `docs/dependencies.md`, and shipped a threshold file that no
  CI step exercised (a silent no-gate). All three are addressed here without
  changing any tess API or runtime behavior.
- Affected docs: `architecture/diagnostics.md`, `decisions/CHANGELOG.md`,
  `dependencies.md`.
- Affected code: `.github/workflows/ci.yml`.

## 2026-07-09 - Diagnostics ImGui Panels (M12, S4 slice 5)

- Added (new header `debug/imgui/panels.h`, doubly gated by `TESS_ENABLE_IMGUI`
  && `TESS_ENABLE_DIAGNOSTICS`): reference Dear ImGui panels over the export
  snapshots -- `draw_timing_panel`, `draw_path_counters_panel`,
  `draw_queued_counters_panel`, `draw_allocation_counters_panel`, the composite
  `draw_diagnostics_panel`, and the `category_name` label helper. tess core
  never fetches or links ImGui; the consumer defines the gates on its own
  target and includes `<imgui.h>` before the header (a `#error` enforces the
  order). Only Text/TextUnformatted/Separator are used, with portable
  `unsigned long long` printf casts. Not included by `tess.h`.
- Reason: fifth slice of the M12 diagnostics close (S4) -- the ImGui skeleton.
  The panels are the reference renderer a downstream overlay adopts in the final
  slice. tess validates the header against a minimal ImGui stub
  (`tests/imgui_stub/imgui.h`, `tess_diagnostics_panels_test`) so a panel bug is
  caught in tess CI, not only in the consumer; the stub mirrors the real API for
  the three primitives used.
- Affected docs: `architecture/diagnostics.md`, `architecture/surface.json`.
- Affected code: new `debug/imgui/panels.h`, `tests/imgui_stub/imgui.h`,
  `tests/tess_diagnostics_panels_test.cc`, `CMakeLists.txt`,
  `tests/CMakeLists.txt`.

## Earlier Entries

Older design-changelog entries are progressively archived in
[`CHANGELOG-archive.md`](CHANGELOG-archive.md) to keep this file under the
24k-token per-file limit; the split is by file size, not by a clean date
boundary. New entries go at the top of this file; when it approaches the
limit again, its oldest entries move to the archive.
