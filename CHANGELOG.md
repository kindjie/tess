# Changelog

Notable, release-facing changes to `tess`. All `0.x` releases are
pre-stable: minor versions may change public APIs and data layouts
without compatibility shims. The format loosely follows
[Keep a Changelog](https://keepachangelog.com/); design-level decisions
and their rationale are recorded separately in
[`docs/decisions/CHANGELOG.md`](docs/decisions/CHANGELOG.md).

## [Unreleased]

### Added

- Top-level `CONTRIBUTING.md` (developer workflow, quality gates,
  benchmark policy) and this `CHANGELOG.md`.
- `docs/getting-started.md`: a tutorial from shapes and schemas to the
  schedule loop and render bridge.
- GitHub Releases published for the existing `v0.1.0` and `v0.2.0` tags.

### Changed

- CMake floor lowered to 3.25 (3.28 and newer keep module-scan
  suppression and fetched-dependency hygiene), and the project now
  declares the `3.25...3.28` policy range.
- README restructured as a user-facing overview with a features list
  and quickstart; contributor material moved to `CONTRIBUTING.md`.
- Docs indexes lead with maintained material; the TDD archive and
  planning records are marked historical.

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
