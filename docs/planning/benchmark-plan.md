# TDD: Benchmark Plan

## 1. Summary

This document defines the benchmark plan for the tile/path simulation substrate.

The benchmark suite validates design choices, catches regressions, and determines defaults for chunk sizes, ordering, topology, pathfinding, field reuse, memory allocation, diagnostics overhead, and sparse-world behavior.

## 2. Goals

- Measure realistic simulation/path workloads.
- Compare chunk shapes and layouts.
- Compare dense vs dirty/sparse updates.
- Compare A* vs shared fields.
- Measure topology and portal graph costs.
- Measure scheduler/planner overhead.
- Measure allocation and product cache behavior.
- Measure 2D, vertical 2D, and 3D cases.
- Measure huge bounded sparse worlds without full allocation.
- Support Clang-first local and CI execution.

## 3. Benchmark principles

Report elapsed time, p50/p95/p99, tiles/chunks/nodes, bytes, allocations, peak memory, cache reuse, planner/executor time, and correctness checks.

## 4. Shape matrix

Minimum shapes:

- SingleChunk2D: 1024x1024x1, chunk 1024x1024x1
- Chunked2D: 4096x4096x1, chunk 64x64x1
- Vertical2D_XZ: 1024x1x1024, chunk 64x1x64 or 64x1x16
- Vertical2D_YZ: 1x1024x1024, chunk 1x64x64 or 1x64x16
- Chunked3D_Shallow: 1024x1024x64, chunk 32x32x4
- Chunked3D_Deeper: 1024x1024x128, chunk 32x32x8
- HugeSparse3D: 1,000,000 x 1,000,000 x 256, chunk 32x32x4, sparse subset

## 5. Chunk shape comparison

Compare 2D, vertical 2D, and 3D chunk shapes for field iteration, topology, pathing, fields, boundary overhead, dirty granularity, memory, scheduling.

## 6. Chunk ordering comparison

Compare row-major, Morton, Hilbert if implemented, and workload-specific active/visible/dirty priority order.

## 7. Storage benchmarks

- field iteration
- coordinate/key math
- chunk directory lookup
- dirty/resident iteration
- sparse mask operations

## 8. Planner benchmarks

- simple ops
- many tiny ops
- path requests
- nearest queries
- dirty/sparse domain expansion
- result channels
- hazard validation
- diagnostics levels

## 9. Scheduler benchmarks

- task counts
- cadence dispatch
- dirty/event dispatch
- background continuations
- variable sim speed 1x/2x/5x
- overload policy

## 10. Topology benchmarks

- local chunk topology
- wall/door/stair edits
- vertical 2D
- 3D
- portal graph build/repair
- reachability
- multiple movement classes

## 11. Pathfinding benchmarks

- short/medium/long paths
- failed with/without topology precheck
- open/maze/rooms
- vertical 2D
- 3D stairwell
- unique/shared goals
- bottlenecks
- missing chunks
- open set comparisons
- scratch reuse

## 12. Flow/distance/influence benchmarks

- reverse BFS/Dijkstra
- bucket vs heap
- chunk-window/region/corridor/sparse fields
- shared goal reuse
- congestion fields
- influence/danger fields

## 13. Product cache benchmarks

- FieldProductCache
- PathProductCache
- byte-budgeted LRU
- weighted LRU if implemented
- invalidation
- pins
- thrash/reuse

## 14. Memory/allocation benchmarks

Strictly check no allocations after warmup for hot path/field builds. Measure scratch, arenas, frontiers, product caches, result channels, render deltas.

## 15. Render delta benchmarks

- entity moves
- dirty sparse/box/chunk encodings
- baseline visible region
- missed delta/resync
- high sim speed coalescing
- vertical 2D regions

## 16. ECS benchmarks

v1: EnTT and custom minimal adapter.

Measure request collection, result application, movement commit, spatial index update, deterministic sorting.

## 17. Diagnostics/tooling benchmarks

Measure disabled/counters/warnings/trace/stacktrace, ImGui panel draw, export cost, memory stats.

## 18. GPU placeholders

Mock backend for planner/backend choice, upload batch construction, handle lifecycle, fallback, stale readback, cache eviction. Real GPU benchmarks future.

## 19. Scenario benchmarks

- Early colony
- Growing colony
- Late colony
- Combat
- Disaster
- 3D fortress
- Vertical 2D ant farm

## 20. Correctness validation

Benchmarks must check correctness: legal paths, decreasing distance fields, topology reference matches, dirty matches dense reference, fused equals materialized, delta replay matches projected state.

## 21. Reproducibility

Record commit, compiler, flags, CPU, OS, build mode, shape, chunk size, field schema, random seed, thread count, diagnostic level.

## 22. Output formats

Console summary, JSON, CSV. Optional Chrome trace/flamegraph markers and plot scripts.

## 23. CI strategy

- correctness benchmarks with sanitizers
- performance smoke
- full performance suite manual/scheduled

## 24. Acceptance criteria

Benchmarks cover storage, planner, scheduler, topology, pathing, fields, caches, ECS, render deltas, diagnostics, shapes, allocation, and correctness. Results are reproducible and exportable.
