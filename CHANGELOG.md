# Changelog

Notable, release-facing changes to `tess`. All `0.x` releases are
pre-stable: minor versions may change public APIs and data layouts
without compatibility shims. The format loosely follows
[Keep a Changelog](https://keepachangelog.com/); design-level decisions
and their rationale are recorded separately in
[`docs/decisions/CHANGELOG.md`](docs/decisions/CHANGELOG.md).

## [Unreleased]

### Added

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
- The source version is now the unreleased `v0.12.0` development API; the
  latest supported release tag remains `v0.4.0`.

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
  calibrated ceiling; newly covered resolved-transition, weighted-product,
  coarse-topology, area-index, and Flecs workloads close the prior gate gaps.
- Default orthogonal unit routes, fields, and product replays retain their
  direct specialized paths while other lattices, step policies, and providers
  use the resolved transition model.

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
