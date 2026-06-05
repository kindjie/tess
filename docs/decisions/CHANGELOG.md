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
