# Changelog

Notable, release-facing changes to `tess`. All `0.x` releases are
pre-stable: minor versions may change public APIs and data layouts
without compatibility shims. The format loosely follows
[Keep a Changelog](https://keepachangelog.com/); design-level decisions
and their rationale are recorded separately in
[`docs/decisions/CHANGELOG.md`](docs/decisions/CHANGELOG.md).

## [Unreleased]

### Fixed

- A `DirtyObservation` taken before a sparse chunk was evicted can no longer
  clear a dirty mark made after it was reloaded. Reloading restarts a
  chunk's `version` at zero, so version equality alone could match across
  two different residency intervals and erase work the observation never
  saw — the one thing the observation protocol promises cannot happen. The
  observation now carries the residency generation and a stale one is
  refused. Always-resident worlds are unaffected.
- `SparseWorld::ensure_resident()` returns an invalid handle for a key
  outside the bounded shape instead of writing past its slot table. It is
  the only residency entry point with no checked counterpart, and the
  directory's direct-slot mode indexes by key, so an out-of-range key wrote
  out of bounds wherever assertions were compiled out. It now behaves like
  `try_chunk()` and `try_meta()`, which already refused such keys.
- `dirty_chunk_domain()` and `active_chunk_domain()` yield chunk keys in
  ascending order on sparse worlds. They previously inherited residency
  order, which depends on load and eviction history rather than on world
  content, so identical content could iterate differently between runs and
  a non-commutative block kernel could produce different results. The
  underlying `dirty_chunks()` and `active_chunks()` scans stay unordered
  and allocation-free; only the domain builders, which already allocate,
  sort.
- Continuous-integration compiler caches no longer collide. Cache restore
  keys match by prefix, so the `dev` namespace also matched the sanitizer,
  cppcheck, and clang-tidy namespaces and restored whichever was written
  most recently. Every namespace is now terminated, and each cache-using
  job reports its own hit rate so a cold rebuild is visible in its log.
- Changes under `include/tess/ops/` select the paired benchmark run again.
  The directory was excluded as nanosecond-scale, which is true of its
  queued and scheduler families but not of the pool executor it also owns,
  whose benchmarks run at millisecond scale.
- The gated benchmark family list has one home. It was maintained by hand
  in the workflow, in CMake, in the contributor guide, and in a test that
  covered five of eighteen families, so a new family could ship a manifest
  and a target and still never gate. CMake now derives the set from the
  threshold targets it defines.
- The advisory clang-tidy profile pins its major version, matching the
  required gate. It installed the unversioned package, so the runner image
  decided which analyser ran and no pull request would have noticed a
  change.
- Every continuous-integration job declares a timeout. They inherited the
  360-minute default, which a hung job would spend at up to ten times the
  base billing rate on the macOS runners.

## [0.12.0] - 2026-08-05

### Added

- Exception-free consumer builds. Clang-family and GCC consumers may compile
  every public header, aggregate, and example with `-fno-exceptions`, and
  native MSVC consumers with `/EHs-c-` and `_HAS_EXCEPTIONS=0`. The installed
  `tess::tess` target stays neutral; `TESS_HAS_EXCEPTIONS` and
  `tess::has_exceptions` report the compiler mode and cannot be overridden.
  Every translation unit in a program must use the same mode. See
  [exception-free builds](docs/architecture/no-exceptions.md).
- Non-throwing capacity entry points for exception-free callers:
  `BlockScratch::reserve_bytes_checked`,
  `WeightedPortalSegmentCache::reserve_segments_checked`,
  `reserve_path_nodes_checked`, and `ClassView::store_checked`, reporting
  through `ReserveStatus` and `PortalSegmentStoreStatus`.
- Explicit no-throw execution aliases `NoThrowWorkerPoolPhaseExecutor` and
  `NoThrowScopedThreadPhaseExecutor`, plus the `ScheduleNoThrowTaskFn` erased
  signature, so an explicitly `noexcept` callback keeps that property through
  the queued, result-channel, schedule, and auto-exec adapters.
- A Conan recipe and a vcpkg checkout overlay alongside the existing
  `FetchContent` and installed-package paths. See
  [packaging](docs/packaging.md).
- Per-tick timing and allocation attribution. Diagnostics-enabled schedules
  time the complete tick and each executed task under its static label,
  duration records carry inclusive allocation and deallocation byte deltas,
  and snapshots retain the newest trace records with a dropped count.
  Diagnostics-off builds retain no timer or attribution code.
- A resolved transition model shared by exact paths, reverse fields,
  multi-goal products, topology, caches, path agents, and movement commit,
  including clearance-preserving diagonal steps, axial-hex adjacency, and
  provider-composed special edges.
- Compile-time compact-cost range assessment and explicit runtime
  `CostOverflow` results.
- Typed queued intents, cooperative generation-stamped async tickets, bounded
  exact event streams, and event/background scheduling adapters.
- Lazy block pipelines and exact allocation-free box, radius, and chunk-span
  queries.
- Deterministic coarse region/portal routes, persistent weighted field
  products, caller-keyed area indexes, tactical assignment, and local move
  coordination.
- Versioned authoritative world archives, an optional Flecs adapter, and
  bounded optional Dear ImGui world inspection/edit-intent helpers.
- An optional stable-C-API WebGPU transport with generation-bearing resources
  and bounded asynchronous readback.
- A network-free external-grid parser and independent oracle harness; external
  corpus acquisition remains gated on documented content rights.

### Changed

- Path results now report their fixed-point cost scale; provider type and
  revision participate in persistent path-product and cache identity.
- **Behavior change:** `collect_planned_dirty` and both partition-collecting
  `merge_planned_dirty` overloads no longer throw `std::length_error` on a
  record-count overflow. They now return the new
  `PlannedDirtyCollectStatus::CapacityExceeded` and
  `PlannedDirtyMergeStatus::CapacityExceeded` values instead, in
  exception-enabled builds as well as exception-free ones. Callers that
  relied on the exception must check the returned status; exhaustive
  `switch` statements over either enum need the new value. `AutoExecTask`
  absorbs the status internally and still publishes every started
  callback's dirty metadata through its allocation-free fallback merge, so
  its observable result is unchanged.
- The consolidated public surface is versioned and released as `v0.12.0`.

### Fixed

- Occupancy-blocked path agents retry retained steps without repeated
  occupancy-blind searches, stop after a bounded retry budget, and surface an
  explicit terminal outcome. The colony demo reports those outcomes.
- Special-transition field products preserve provider costs, transition
  enumeration propagates callback failures, and zero-step agent ticks preserve
  blocked-retry budgets.
- Archive loads invalidate pre-load cache identities, area-index validation is
  constant time, and reentrant queued-work mutation is rejected safely.
- The external-grid harness now parses scenario lengths on Apple libc++ and
  rejects incompatible required-data options before probing the toolchain.
- Persistence decoding, checksum handling, and field validation now compile
  cleanly across the supported GCC, Clang, MSVC, and cppcheck gates.
- The cppcheck gate now bypasses cppcheck 2.21 template-simplifier crashes
  while retaining product-header analysis and compiler test coverage.

### Performance

- Every literal benchmark in a threshold-gated family is covered by a
  calibrated or explicitly labeled bootstrap ceiling; newly covered
  resolved-transition, weighted-product,
  coarse-topology, area-index, and Flecs workloads close the prior gate gaps.
- Default orthogonal unit routes, fields, and product replays retain their
  direct specialized paths while other lattices, step policies, and providers
  use the resolved transition model.
- Indexed axis-neighbor iteration remains inline in hot reconstruction loops,
  and bounded weighted floods hoist per-node bucket work out of their
  per-neighbor loop.
- Fully covered sparse worlds bypass residency hashing on the storage read
  path.
- Default orthogonal distance-field products capture dependencies at
  chunk-frontier level again instead of enumerating exact transitions per
  reached tile, undoing a v0.12 build/store regression.
- The serial-versus-pool dispatch crossover is measured and published rather
  than estimated; the pool's dispatcher no longer shares a CPU with a worker.

## [0.4.0] - 2026-07-20

### Added

- Curated `<tess/pathfinding.h>` and `<tess/simulation.h>` facade headers;
  the existing `<tess/tess.h>` compatibility umbrella remains available.
- A compiled quickstart, tracked installed-package and `FetchContent`
  consumers, and source-backed documentation snippets enforced in CI.
- CI verification that the quickstart's documented output matches the
  compiled binary.
- A strict MkDocs site deployed through GitHub Pages, plus a single-threaded
  interactive pathfinding example compiled with Emscripten 6.0.3.
- Support, security, and structured issue-reporting metadata, a Contributor
  Covenant code of conduct, and weekly Dependabot updates for GitHub Actions
  and pip dependencies.

### Changed

- Package metadata and maintained documentation now report `0.4.0`
  consistently.
- The README now leads with fit and non-fit guidance, a complete runnable
  program, dependency-free example commands, and explicit install-prefix and
  `FetchContent` instructions.

## [0.3.0] - 2026-07-17

### Changed

- BREAKING pre-release hardening of the queued-operation and path-cache
  surfaces: `PlannedOperation` gets checked, immutable construction with
  a world-shape stamp; `ExecutionPhase` becomes a planner-issued,
  generation-stamped capability so hand-built or stale phases cannot
  bypass parallel ownership checks; deferred dirty recording and merge
  return explicit failure results and reject cross-world use; portal
  segment construction and compaction commit transactionally, cache
  budget reductions apply immediately, and result hooks are `noexcept`.
- Version metadata now has one CMake authority that generates the
  installed `tess/version.h`; dependency acquisition is pinned by
  default with hash-verified tooling.
- CMake floor lowered to 3.25 (3.28 and newer keep module-scan
  suppression and fetched-dependency hygiene), and the project declares
  the `3.25...3.28` policy range.
- README restructured as a user-facing overview with a features list, a
  quickstart, and measured performance figures; contributor material
  moved to `CONTRIBUTING.md`.
- Docs indexes lead with maintained material; the TDD archive and
  planning records are marked historical.

### Added

- A `consumer` CMake preset: headers-only configure for installing the
  library with no tests, examples, benchmarks, warnings-as-errors, or
  network fetches.
- An opt-in `tess_docs` Doxygen target (`TESS_BUILD_DOCS=ON`) generating
  a local HTML API reference.
- Top-level `CONTRIBUTING.md` (developer workflow, quality gates,
  benchmark policy) and this `CHANGELOG.md`.
- `docs/getting-started.md`: a tutorial from shapes and schemas to the
  schedule loop and render bridge.
- GitHub Releases published for the existing `v0.1.0` and `v0.2.0` tags.
- Sparse local topology reports `MissingChunk`; stateful transition
  providers expose a monotonic revision.

### Fixed

- Deterministic allocation-failure testing reports itself unavailable
  (and stays inert) under MSVC checked iterators instead of terminating;
  Windows keeps failure coverage in Release.
- Cross-platform warning debt cleared across GCC, Clang, AppleClang, and
  MSVC.

## [0.2.0] - 2026-07-12

### Changed

- ChunkMeta hot/cold SoA split (M5): flag words and dirty bounds moved
  to world-owned columns with new `dirty_flags`, `active_flags`, and
  `dirty_bounds` accessors. Breaking versus the undocumented struct
  layout; minor bump by decision.

### Added

- Per-agent pathing dirt: `PathSubmitScope` plus `PathAgentRoutes`
  retained routes, so one goal re-arm no longer replans the whole batch
  (4.2x on the goal-churn tick benchmark).

### Performance

- The audit remediation stack: de-elided benchmark gates, batch
  grouping and settle-target floods (~118x near-goal), scheduler and
  planner overhead cuts, worker-pool claiming (~2x), and intrusive LRU
  eviction (3.5x).

## [0.1.0] - 2026-07-11

### Added

- The initial pre-stable surface, complete across milestones M0-M15:
  constexpr shapes with one model for 2D, vertical 2D, and 3D;
  chunk-local SoA storage with sparse residency; queued operations with
  result channels and write-policy enforcement; the schedule with
  cadences, budgets, auto-exec, and a selectable parallel phase
  executor; movement classes with per-class topology, transition
  providers, and the region-graph precheck; A* and weighted routing
  with route and field-product caches; distance-field products and the
  byte-budgeted cache; the ECS adapter (EnTT-gated); the versioned
  DeltaFrame render bridge; compile-gated diagnostics with ImGui
  panels; and the GPU backend interface (interface only).
