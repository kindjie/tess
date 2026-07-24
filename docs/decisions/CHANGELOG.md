# Design Changelog

Records meaningful design changes from the original TDDs. Entries from
2026-07-09 through 2026-07-10 that postdate the earlier archive are in
[`CHANGELOG-archive-2026-07-09-10.md`](CHANGELOG-archive-2026-07-09-10.md);
older entries are in [`CHANGELOG-archive.md`](CHANGELOG-archive.md) and
[`CHANGELOG-archive-2026-06.md`](CHANGELOG-archive-2026-06.md).

## 2026-07-23 - Align parallel provider edges from plan through commit

- Changed: provider-aware movement validation now considers special edges
  when a geometrically regular edge to the same destination is blocked or
  missing topology. Any legal matching edge permits the commit.
- Reason: exact searches enumerate regular and provider transitions as a
  union. Commit must accept that same union, including deliberate bridges
  across blocked diagonal clearance.
- Affected docs: simulation and path architecture, ECS integration summaries,
  design changelog, and test inventory.
- Affected code: movement validation and movement-class regression coverage.

## 2026-07-23 - Align provider-aware movement exception handling

- Changed: provider-aware movement validation and commit now propagate
  transition-enumeration exceptions instead of terminating through a
  `noexcept` boundary.
- Reason: transition providers are not required to enumerate without throwing;
  movement must match the exception contract already used by path and field
  construction.
- Affected docs: simulation architecture, design changelog, and test inventory.
- Affected code: movement validation and provider-aware movement commit.

## 2026-07-23 - Close final roadmap-completion audit gaps

- Changed: external-grid dimensions must fit both storage and coordinate
  widths; WebGPU dispatch requires its input mirror to be registered; the
  ImGui inspector establishes residency before its precondition-based metadata
  access; and temporary MSVC and cppcheck coverage workarounds now have
  explicit retry conditions.
- Reason: final independent audits found two portable validation gaps and
  three low-severity maintenance ambiguities after all hosted gates passed.
- Affected docs: dependency notes, design changelog, and test inventory.
- Affected code: grid fixture parsing, WebGPU dispatch, ImGui tools, and
  project analysis options.

## 2026-07-23 - Keep the external-grid bootstrap portable

- Changed: scenario optimum parsing now uses strict classic-locale extraction,
  and the incompatible required-data option pair is rejected before CMake
  probes the compiler.
- Reason: Xcode 16.4's libc++ lacks floating-point `from_chars`, while an
  inherited but unavailable CI compiler launcher could mask the intended
  required-data diagnostic during configuration tests.
- Affected docs: changelog and test inventory.
- Affected code: root CMake configuration and the external-grid test harness.

## 2026-07-23 - Harden roadmap completion after independent audits

- Changed: provider-composed unit products now honor special-edge costs;
  archive loads keep version counters monotonic; resumable queues reject
  callback-time mutation; transition enumeration propagates exceptions; area
  indexes use graph revisions; and blocked/provider retry edge cases are
  explicit and bounded.
- Reason: two independent reviews found silent stale-cache and wrong-path
  outcomes, one vector lifetime hazard, and hot-path or lifecycle contracts
  that were either unsafe or underspecified.
- Affected docs: path, simulation, persistence, queued operations, spatial
  coordination, ECS, maintenance, optimization log, and test inventory.
- Affected code: transition/path products, archives, resumable work, region and
  area indexes, movement/path ticks, experimental maintenance, and colony demo.

## 2026-07-22 - Close the post-v0.4 roadmap as v0.12 development

- Declare the integrated v0.5-v0.12 surface as the unreleased `0.12.0`
  development API while retaining `v0.4.0` as the latest supported release
  tag and FetchContent example.
- Close v0.12 only after the optimized correctness suite and every calibrated
  benchmark family pass; require literal gated benchmark names to have
  threshold entries so new families cannot silently escape CI ceilings.
- Keep planned extensions, evidence-backed deferrals, explicit non-goals, and
  the external-data rights gate out of the v0.12 completion claim.

## 2026-07-22 - Bound occupancy-blocked path agents without re-planning

- Changed: occupied/reserved retained steps now wait without a new search;
  all transient blocks share a bounded consecutive retry budget, successful
  movement resets it, and exhaustion becomes explicit `Unreachable`.
- Reason: occupancy is intentionally absent from planning passability, so a
  per-tick A* returned the same route, reset accounting, and caused the colony
  demo's unbounded cost/stall loop at a bottleneck.
- Affected docs: path/simulation architecture, completion plan, optimization
  log, and changelog.
- Affected code: path-agent result/movement lifecycle, tick preparation,
  seeded bottleneck and selective-submit tests, and terminal demo telemetry.

## 2026-07-22 - Add the optional stable-C-API WebGPU backend

- Changed: added independently gated field mirrors, generation-bearing
  consumer products, compute dispatch, and bounded asynchronous readback on
  the stable WebGPU C API, plus an exact-port browser smoke example.
- Reason: v0.11 intentionally advances beyond the historical interface-only
  TDD without weakening its dependency-free CPU core or CPU-authority rules.
  Explicit provider resources avoid standardizing a premature shader ABI.
- Affected docs: GPU architecture, dependency inventory, roadmap, completion
  plan, optimization log, public surface, historical TDD note, and changelog.
- Affected code: descriptors, optional backend, stable-C test double,
  correctness tests, browser example, build tooling, and Pages smoke test.

## 2026-07-22 - Bound optional ImGui world tooling to edit intents

- Changed: added independently gated dense/sparse world overview and chunk
  inspection helpers plus a boolean field widget that returns a caller-applied
  edit intent from a const world.
- Reason: v0.10 calls for optional substrate tooling, while the historical TDD
  rejects a renderer/editor implementation and keeps meaning application-owned.
  Const inspection and explicit intents make that boundary enforceable.
- Affected docs: diagnostics architecture and guide, dependency inventory,
  roadmap, completion plan, public surface, and changelog.
- Affected code: optional ImGui tools header, API-matching stub, independent
  gate tests, and install surface.

## 2026-07-22 - Add the optional Flecs adapter

- Changed: added a Flecs 4.1.5 adapter with generation-preserving handles,
  persistent deterministic collection, generic path-agent write-back, and
  synchronized lifecycle and render-delta intents behind independent header
  and build gates.
- Reason: the maintained v0.10 roadmap advances beyond the historical TDD's
  explicit Flecs deferral. Reusing the generic pipeline preserves the TDD's
  ECS-agnostic core and safe-phase structural-mutation constraints.
- Affected docs: ECS architecture, entity guide, dependency inventory,
  roadmap, completion plan, public surface, and changelog.
- Affected code: Flecs adapter, pinned opt-in dependency acquisition, presets,
  tests, and example.

## 2026-07-22 - Add versioned authoritative world archives

- Changed: added canonical little-endian dense and resident-set sparse world
  archives with explicit schema/field identities, CRC-32 validation,
  compatibility classification, migration-required outcomes, capacity
  preflight, and derived-state invalidation on load.
- Reason: the maintained v0.10 roadmap advances persistence beyond the
  historical TDD deferral, while preserving the original rule that shape,
  key, lattice, and schema changes require explicit migration or rejection.
- Affected docs: persistence architecture, architecture map, roadmap,
  completion plan, optimization log, public surface, and changelog.
- Affected code: archive schema and codec, umbrella/install surface,
  correctness tests, and save/load benchmarks.

## 2026-07-22 - Complete deterministic local move coordination

- Changed: added priority- and stable-ID-ordered local destination
  reservations with caller legality filtering, explicit waits, congestion
  summaries, and reusable allocation-free warm scratch storage.
- Reason: v0.9 needed a deterministic crowd-contention substrate while the
  project boundary keeps continuous steering, global multi-agent optimization,
  and movement legality application-owned.
- Affected docs: spatial coordination, architecture map, roadmap, completion
  plan, optimization log, public surface, and changelog.
- Affected code: local coordination header, umbrella/install surface,
  correctness and allocation tests, and representative benchmark.

## 2026-07-22 - Gate and bootstrap external grid benchmark data

- Changed: corrected the external-data TDD's rights, compile-time shape,
  strict-mode, and sparse-convergence contracts; recorded the proposed pinned
  source; and added the network-free parser, reference oracle, empty gated
  manifest, and opt-in availability driver.
- Reason: PR #57's oracle design matches the shipped lattice, but ODC-By does
  not license individual contents and runtime dimensions cannot instantiate
  tess shapes. Safe harness work can ship without enabling external download.
- Affected docs: benchmark-data TDD, dependency inventory, roadmap, completion
  plan, TDD index, and changelog.
- Affected code: test harness, manifest validation, opt-in CMake driver, CI
  tooling tests, and regression tests.

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

## 2026-07-22 - Scrub Git hook environment in dependency population

- Changed: the exact-revision population script now unsets inherited
  `GIT_DIR`-family environment variables before running its Git commands,
  and its failure message reports every attempt's error instead of only
  the last one.
- Reason: builds triggered by Git hooks (the pre-push checks) inherit
  Git's hook environment; an inherited `GIT_DIR` redirected the
  population's `git init` and `git remote add` at the parent repository,
  so every fresh-build-tree push failed with "remote origin already
  exists" — which the retry loop then reported in place of the first
  attempt's real error.
- Affected docs: `tests/AGENTS.md`.
- Affected code: the population script and its regression tests.

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
