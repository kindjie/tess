# Design Changelog

Records meaningful design changes from the original TDDs.

## Template

```md
## YYYY-MM-DD - Title

- Changed:
- Reason:
- Affected docs:
- Affected code:
```

## 2026-06-05 - Weighted A* Entry Costs

- Changed: Added a separate `weighted_astar_path` API for positive integral
  entry costs, plus weighted correctness tests and weighted path benchmarks.
- Reason: Weighted terrain is likely to be important, but the existing
  unit-cost fast paths and route reuse proofs do not apply to arbitrary entry
  costs. The weighted API preserves optimal weighted paths while leaving the
  optimized unit-cost path unchanged.
- Affected docs: `docs/architecture/path.md`,
  `docs/planning/benchmark-plan.md`,
  `docs/planning/optimization-log.md`, `tests/AGENTS.md`
- Affected code: `include/tess/path/path.h`, `tests/tess_path_test.cc`,
  `bench/tess_bench.cc`, `bench/thresholds/path.json`

## 2026-06-05 - Symbolicated Benchmark Profiling Workflow

- Changed: Added a `bench-profile` preset and a `tools/profile_benchmark.sh`
  command helper for non-interactive `samply` captures with debug information,
  frame pointers, and presymbolication.
- Reason: Release benchmark profiles were saved without usable symbols and
  `samply record` launched a local viewer process that could outlive the
  profiling run. The profiling workflow now emits a direct shell command to run
  as a separate capture step, producing repeatable saved profiles outside the
  repository and loading them explicitly only when needed.
- Affected docs: `docs/planning/benchmark-plan.md`
- Affected code: `CMakePresets.json`, `tools/profile_benchmark.sh`

## 2026-06-05 - Route Cache Path Reuse

- Changed: Added reusable exact route and same-goal suffix caching for the
  current unit-cost A* path model. Added cache-hit benchmark counters and
  monitored batch benchmarks for repeated route and suffix reuse cases.
- Reason: Many-agent batches can repeat identical routes or ask for suffixes
  of an already-computed route. Reusing cached optimal path spans avoids
  rerunning A* while preserving the general fallback.
- Affected docs: `docs/architecture/path.md`,
  `docs/planning/benchmark-plan.md`,
  `docs/planning/optimization-log.md`, `tests/AGENTS.md`
- Affected code: `include/tess/path/path.h`, `tests/tess_path_test.cc`,
  `bench/tess_bench.cc`, `bench/thresholds/path.json`

## 2026-06-05 - Shared-Goal Distance Fields

- Changed: Added reusable reverse distance-field scratch and shared-goal path
  reconstruction for the current unit-cost passability path model. Added
  many-agent batch benchmarks and reuse counters for unique starts, goals, and
  chunks.
- Reason: Independent A* repeats substantial work for agents sharing goals.
  Reverse distance fields amortize the search across all starts for a goal and
  are a better fit for 100-agent shared-destination workloads.
- Affected docs: `docs/architecture/path.md`,
  `docs/planning/benchmark-plan.md`,
  `docs/planning/optimization-log.md`, `tests/AGENTS.md`
- Affected code: `include/tess/path/path.h`, `tests/tess_path_test.cc`,
  `bench/tess_bench.cc`, `bench/thresholds/path.json`

## 2026-06-05 - Unit-Cost A* Bucket Open Set

- Changed: Replaced the general fallback A* binary heap with a two-bucket
  monotone open set for the current unit-cost Manhattan heuristic path model.
- Reason: With unit-cost axis-adjacent movement and a consistent Manhattan
  heuristic, generated fallback nodes have the current `f` score or `f + 2`.
  The bucket queue removes binary heap maintenance while preserving optimal
  path ordering for this MVP path model.
- Affected docs: `docs/architecture/path.md`,
  `docs/planning/optimization-log.md`
- Affected code: `include/tess/path/path.h`

## 2026-06-05 - Path Direct Fast-Path Prechecks

- Changed: Pathfinding now tries shape-relevant direct Manhattan axis orders,
  simple axis-aligned detours, verified 2D and 3D plane-gap routes, and 2D
  forced-gap sequences before fallback A*, and rejects full axis-plane
  barriers before expanding A* nodes.
- Reason: Uniform-cost direct paths and fully separating blocked planes can be
  resolved exactly without general A* search. Axis-aligned one-tile parallel
  detours and verified routes through a passable plane gap are also optimal
  under the current unit-cost movement model. Forced single-gap barriers have
  fixed crossings, while non-matching cases preserve normal A* fallback
  behavior.
- Affected docs: `docs/architecture/path.md`,
  `docs/planning/benchmark-plan.md`,
  `docs/planning/optimization-log.md`
- Affected code: `include/tess/path/path.h`, `tests/tess_path_test.cc`,
  `bench/tess_bench.cc`

## 2026-06-05 - Testable MVP Scope

- Changed: Added an explicit MVP checkpoint that narrows the first end-to-end
  prototype to always-resident queued execution plus minimal unit-cost A*
  pathfinding.
- Reason: The full v1 milestone remains useful design intent, but it is too
  broad to serve as the first testable implementation target.
- Affected docs: `docs/planning/v1-milestone-plan.md`
- Affected code: none

## 2026-06-05 - One Millisecond Benchmark Investigation Gate

- Changed: Current benchmark threshold scaffolds now enforce a 1 ms CPU-time
  ceiling for each named benchmark while leaving real-time limits unset. The
  path threshold set includes 64x64, 512x512, and 1024x1024 open-world A*
  benchmarks in addition to the cheap smoke path.
- Reason: Any operation taking longer than 1 ms should be investigated, and the
  benchmark policy should encode that expectation directly.
- Affected docs: `docs/planning/benchmark-plan.md`
- Affected code: `bench/thresholds/key-conversions.json`,
  `bench/thresholds/storage.json`, `bench/thresholds/block.json`,
  `bench/thresholds/queued.json`, `bench/thresholds/path.json`

## 2026-06-05 - Path Benchmark Profiling Counters

- Changed: `PathResult` now reports expanded and reached node counts, and path
  benchmarks publish cost, path-node, expanded-node, and reached-node counters.
- Reason: Large-world path timing needs to distinguish graph-work growth from
  per-node overhead. The 1024x1024 open-grid profile currently points to heap
  maintenance, passability/world lookup, and 2D use of six-axis neighbor
  generation as the first bottlenecks.
- Affected docs: `docs/architecture/path.md`,
  `docs/planning/benchmark-plan.md`
- Affected code: `include/tess/path/path.h`, `bench/tess_bench.cc`,
  `tests/tess_path_test.cc`

## 2026-06-05 - Queued Execution Bridge

- Changed: Added explicit execution helpers for planned queued operations and
  plans. Execution runs caller callbacks through policy-typed serial block
  contexts and marks visited chunks dirty from declared dirty masks.
- Reason: The queued planner needed a minimal synchronous execution bridge
  before scheduler-owned execution, barriers, result channels, or async work.
- Affected docs: `docs/architecture/queued-operations.md`,
  `docs/planning/benchmark-plan.md`, `tests/AGENTS.md`
- Affected code: `include/tess/ops/queued.h`, `tests/tess_queued_test.cc`,
  `bench/tess_bench.cc`, `bench/CMakeLists.txt`,
  `bench/thresholds/queued.json`

## 2026-06-05 - MVP Path Foundation

- Changed: Added a minimal always-resident A* path API over boolean-like typed
  passability fields, reusable `PathScratch`, path tests, benchmark coverage,
  and equal-score tie-breaking that prefers deeper paths to avoid open-grid
  wavefront expansion. Path scratch now clears only nodes touched by the prior
  query instead of resetting dense arrays for the whole world on every query.
- Reason: The first MVP needs a concrete path query that proves top-down 2D,
  vertical 2D, and shallow 3D share the existing coordinate/storage model
  before topology prechecks, portal graphs, weighted movement, or distance
  fields are introduced.
- Affected docs: `docs/architecture/path.md`, `docs/architecture/README.md`,
  `docs/planning/benchmark-plan.md`, `tests/AGENTS.md`
- Affected code: `include/tess/path/path.h`, `include/tess/tess.h`,
  `CMakeLists.txt`, `tests/tess_path_test.cc`, `tests/CMakeLists.txt`,
  `examples/mvp_path.cc`, `examples/CMakeLists.txt`, `bench/tess_bench.cc`,
  `bench/CMakeLists.txt`, `bench/thresholds/path.json`

## 2026-06-05 - Queued Operations Foundation

- Changed: Added a public `FrameOps` queue, minimal chunk-domain descriptors,
  deterministic operation handles/ids, and a planner scaffold that validates
  write policies/domains and expands domains into ordered chunk-key vectors.
- Reason: M4 needs a stable queued-intent foundation before adding executors,
  scheduler integration, topology/path systems, richer diagnostics, result
  channels, or work-contract-style maintenance.
- Affected docs: `docs/architecture/queued-operations.md`,
  `docs/architecture/README.md`, `tests/AGENTS.md`
- Affected code: `include/tess/ops/queued.h`, `include/tess/tess.h`,
  `tests/tess_queued_test.cc`, `tests/CMakeLists.txt`

## 2026-06-05 - Queued Operation Diagnostics

- Changed: Added structured operation failure reasons, diagnostic access
  metadata, invalid explicit-chunk detail, and deterministic report lookup and
  count helpers.
- Reason: The queued planner needs testable diagnostics and a hazard-metadata
  foothold before adding executors, barriers, batching, or scheduler behavior.
- Affected docs: `docs/architecture/queued-operations.md`, `tests/AGENTS.md`
- Affected code: `include/tess/ops/queued.h`, `tests/tess_queued_test.cc`

## 2026-06-05 - Planned Operation Block Adapter

- Changed: Added non-owning adapters from successful planned operations to
  `ChunkDomain` and policy-typed `BlockCtx` instances.
- Reason: Planned chunk work needs a practical bridge to the existing serial
  block API before introducing executors, queued callbacks, barriers, or
  scheduler behavior.
- Affected docs: `docs/architecture/queued-operations.md`, `tests/AGENTS.md`
- Affected code: `include/tess/ops/queued.h`, `tests/tess_queued_test.cc`

## 2026-06-05 - Queued Field Access Metadata

- Changed: Added untyped field access masks to queued, planned, and reported
  operations, plus validation rejecting read-only operations with write masks.
- Reason: Planner diagnostics and future hazard checks need explicit
  read/write metadata before typed field binding, callbacks, barriers, or
  executors are introduced.
- Affected docs: `docs/architecture/queued-operations.md`, `tests/AGENTS.md`
- Affected code: `include/tess/ops/queued.h`, `tests/tess_queued_test.cc`

## 2026-06-05 - Basic Queued Hazard Validation

- Changed: Added deterministic field-mask hazard validation across overlapping
  planned chunk domains. Later conflicting operations are rejected with
  conflict handle/id and conflict-mask diagnostics.
- Reason: The queued planner should catch obvious write/write and read/write
  conflicts before adding barriers, reordering, batching, or execution.
- Affected docs: `docs/architecture/queued-operations.md`, `tests/AGENTS.md`
- Affected code: `include/tess/ops/queued.h`, `tests/tess_queued_test.cc`

## 2026-06-01 - Documentation Model

- Changed: TDDs are treated as historical design intent, while maintained
  architecture docs track current implementation behavior.
- Reason: TDDs are likely to drift as implementation validates and revises the
  original design.
- Affected docs: `docs/README.md`, `docs/architecture/README.md`,
  `docs/tdd/README.md`
- Affected code: none

## 2026-06-04 - Initial Chunk Key Ordering

- Changed: The first implemented `ChunkKey` layout uses row-major chunk
  ordering instead of the Morton ordering preferred by the historical TDD.
- Reason: Row-major ordering keeps the M1 coordinate/key API simple and
  testable while chunk-order benchmarks are still pending.
- Affected docs: `docs/tdd/core-shape-coordinate-key-system.md`
- Affected code: `include/tess/core/shape.h`, `tests/tess_shape_test.cc`

## 2026-06-04 - Key Conversion Performance Gates

- Changed: Added key conversion benchmarks, zero-allocation/noexcept tests, and
  disabled benchmark threshold scaffolding for future regression gates.
- Reason: M1 key conversion is hot-path foundation, but wall-clock thresholds
  should wait until same-machine baselines are stable.
- Affected docs: `docs/planning/benchmark-plan.md`
- Affected code: `bench/tess_bench.cc`, `bench/CMakeLists.txt`,
  `tools/benchmark_thresholds.py`, `tests/tess_shape_test.cc`

## 2026-06-04 - Storage Performance Gate Scaffold

- Changed: Added disabled threshold scaffolding for storage benchmarks,
  including chunk page access, field iteration, and always-resident world
  lookup/iteration benchmarks.
- Reason: Storage hot paths should have named regression gates, but hard
  wall-clock limits should wait for stable same-machine baselines.
- Affected docs: `README.md`, `docs/planning/benchmark-plan.md`
- Affected code: `bench/CMakeLists.txt`, `bench/thresholds/storage.json`

## 2026-06-04 - Always-Resident Chunk Metadata

- Changed: Always-resident worlds now own per-chunk metadata with sleeping and
  active states plus raw dirty/active flag tracking.
- Reason: Planner and block domains need chunk-level dirty/active discovery
  without scanning tile fields.
- Affected docs: `docs/architecture/storage.md`
- Affected code: `include/tess/storage/world.h`, `tests/tess_storage_test.cc`,
  `bench/tess_bench.cc`

## 2026-06-04 - Minimal Serial Block Domains

- Changed: Added a public `tess::block` foundation with chunk-domain builders,
  const-correct `ChunkView`, and serial `for_each_chunk` execution over
  always-resident worlds.
- Reason: M3 needs deterministic block/domain execution before adding planner
  integration, diagnostics, scratch storage, or external scheduler backends.
- Affected docs: `docs/architecture/block.md`, `docs/dependencies.md`
- Affected code: `include/tess/block/block.h`, `include/tess/tess.h`,
  `tests/tess_block_test.cc`, `bench/tess_bench.cc`

## 2026-06-04 - Chunk-Local Tile Iteration

- Changed: `ChunkView` now exposes local tile coordinate/id conversion,
  current-chunk world coordinate conversion, and allocation-free serial
  `for_each_tile` traversal in ascending `LocalTileId` order.
- Reason: Block executors need deterministic chunk-local tile traversal before
  adding planners, parallel scheduling, scratch storage, or diagnostics.
- Affected docs: `docs/architecture/block.md`, `tests/AGENTS.md`
- Affected code: `include/tess/block/block.h`, `tests/tess_block_test.cc`,
  `bench/tess_bench.cc`, `bench/CMakeLists.txt`

## 2026-06-04 - Chunk Boundary Helpers

- Changed: `ChunkView` now exposes local bounds, signed local-candidate
  validation/conversion, non-degenerate boundary/interior predicates, and
  signed local-candidate world coordinate conversion.
- Reason: Topology and path systems need allocation-free helpers to identify
  current-chunk candidates before movement rules, transitions, halos, scratch
  storage, diagnostics, or parallel execution are introduced.
- Affected docs: `docs/architecture/block.md`, `tests/AGENTS.md`
- Affected code: `include/tess/block/block.h`, `tests/tess_block_test.cc`,
  `bench/tess_bench.cc`, `bench/CMakeLists.txt`

## 2026-06-04 - BlockCtx Foundation

- Changed: Added `BlockCtx` as a non-owning serial block execution context over
  a world, chunk domain, and write policy. Existing `for_each_chunk` now
  delegates through the context.
- Reason: M3 needs an explicit context object before adding planner phases,
  scratch storage, diagnostics, scheduling, or policy enforcement.
- Affected docs: `docs/architecture/block.md`, `tests/AGENTS.md`
- Affected code: `include/tess/block/block.h`, `tests/tess_block_test.cc`,
  `bench/tess_bench.cc`, `bench/CMakeLists.txt`

## 2026-06-04 - CI Benchmark Baseline Collection

- Changed: Added block benchmark threshold scaffolding, non-gating CI baseline
  JSON collection for key, storage, and block benchmark groups, a baseline
  summary helper for threshold calibration, and README-visible benchmark trend
  snapshot generation.
- Reason: Timing limits should be calibrated from repeated samples on the
  pinned CI runner family that will enforce them, not from developer machines.
- Affected docs: `README.md`, `docs/dependencies.md`, `docs/performance.md`,
  `docs/planning/benchmark-plan.md`
- Affected code: `.github/workflows/ci.yml`, `bench/CMakeLists.txt`,
  `bench/thresholds/block.json`, `tools/benchmark_artifact_metadata.py`,
  `tools/benchmark_baseline_summary.py`, `tools/benchmark_trends.py`

## 2026-06-04 - ReadOnly Block Policy Enforcement

- Changed: `BlockCtx` is now policy-typed, and `ReadOnly` contexts expose
  const chunk views even for mutable worlds. Added `for_each_chunk<Policy>` for
  policy-typed serial execution while keeping the runtime-policy overload as a
  compatibility path.
- Reason: M3 write policies need a real enforcement foothold without adding
  planner phases, scratch storage, diagnostics, or parallel scheduling.
- Affected docs: `docs/architecture/block.md`, `tests/AGENTS.md`
- Affected code: `include/tess/block/block.h`, `tests/tess_block_test.cc`,
  `bench/tess_bench.cc`

## 2026-06-04 - Caller-Owned Block Scratch

- Changed: Added `BlockScratch` as reusable caller-owned bump storage and
  allowed policy-typed `BlockCtx` instances to carry an optional non-owning
  scratch pointer.
- Reason: M3 block algorithms need allocation-free temporary storage during
  serial chunk and tile iteration before planner-owned arenas, diagnostics,
  worker pools, or parallel scheduling exist.
- Affected docs: `docs/architecture/block.md`, `tests/AGENTS.md`
- Affected code: `include/tess/block/block.h`, `tests/tess_block_test.cc`

## 2026-06-05 - Block Scratch Benchmark Scaffold

- Changed: Added disabled block benchmark threshold entries and benchmark
  workloads for scratch allocation/reset and context scratch tile iteration.
- Reason: Caller-owned scratch needs CI baseline visibility before real
  wall-clock gates can be calibrated.
- Affected docs: `docs/performance.md`, `docs/planning/benchmark-plan.md`
- Affected code: `bench/tess_bench.cc`, `bench/thresholds/block.json`

## 2026-06-05 - Caller-Owned Block Diagnostics

- Changed: Added `BlockDiagnostics` with a scratch allocation failure counter
  and allowed policy-typed `BlockCtx` instances to carry an optional
  non-owning diagnostics pointer.
- Reason: Scratch exhaustion needs an explicit reporting path before
  planner-owned arenas, rich diagnostics, worker pools, or parallel scheduling
  exist.
- Affected docs: `docs/architecture/block.md`, `tests/AGENTS.md`
- Affected code: `include/tess/block/block.h`, `tests/tess_block_test.cc`

## 2026-06-04 - Local Warning and Analysis Presets

- Changed: Added project-local warning flags, warnings-as-errors,
  clang-tidy, and ASan/UBSan CMake options plus presets for tests and
  benchmarks.
- Reason: M0 scaffolding needs repeatable compiler diagnostics and dynamic
  analysis without exporting Tess warning policy to downstream consumers.
- Affected docs: `docs/dependencies.md`, `docs/style.md`
- Affected code: `.clang-tidy`, `CMakeLists.txt`, `CMakePresets.json`,
  `cmake/TessProjectOptions.cmake`, `tests/CMakeLists.txt`,
  `bench/CMakeLists.txt`

## 2026-06-04 - clangd Project Configuration

- Changed: Added `.clangd` to point editor tooling at the default developer
  compilation database and keep clang-tidy checks on clangd's strict fast-check
  filter.
- Reason: clangd needs project configuration plus editor startup flags to
  provide consistent clang-tidy diagnostics while editing.
- Affected docs: `docs/dependencies.md`, `docs/style.md`
- Affected code: `.clangd`

## 2026-06-04 - Opt-In Cppcheck Preset

- Changed: Added a project-local cppcheck CMake option and `dev-cppcheck`
  preset for local test and benchmark targets.
- Reason: cppcheck provides a complementary static-analysis pass without
  exporting project analysis policy to downstream consumers.
- Affected docs: `docs/dependencies.md`, `docs/style.md`
- Affected code: `CMakeLists.txt`, `CMakePresets.json`,
  `cmake/TessProjectOptions.cmake`
