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
