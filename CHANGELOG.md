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

### Changed

- Path results now report their fixed-point cost scale; provider type and
  revision participate in persistent path-product and cache identity.

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
