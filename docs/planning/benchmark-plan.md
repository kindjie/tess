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

Measure request collection, result application, movement commit, spatial index
update, deterministic sorting.

## 17. Diagnostics/tooling benchmarks

Measure disabled/counters/warnings/trace/stacktrace, ImGui panel draw, export cost, memory stats.

## 18. GPU placeholders

Mock backend for planner/backend choice, upload batch construction, handle
lifecycle, fallback, stale readback, cache eviction. Real GPU benchmarks future.

## 19. Scenario benchmarks

- Early colony
- Growing colony
- Late colony
- Combat
- Disaster
- 3D fortress
- Vertical 2D ant farm

## 20. Correctness validation

Benchmarks must check correctness: legal paths, decreasing distance fields,
topology reference matches, dirty matches dense reference, fused equals
materialized, delta replay matches projected state.

## 21. Reproducibility

Record commit, compiler, flags, CPU, OS, build mode, shape, chunk size, field
schema, random seed, thread count, diagnostic level.

## 22. Output formats

Console summary, JSON, CSV. Optional Chrome trace/flamegraph markers and plot scripts.

## 23. Profiling workflow

Use the `bench-profile` preset for local native profiler captures. It builds the
benchmark binary with optimized code, debug information, and frame pointers so
sampled profiles can be symbolicated without changing the normal `bench`
regression preset.

Build the profiling binary first, then capture the default 1024x1024 A*
benchmark without starting a local profiler viewer:

```sh
cmake --preset bench-profile
cmake --build --preset bench-profile --target tess_bench
tools/profile_benchmark.sh \
  --output "$TMPDIR/tess-astar-1024-profile.json.gz"
# Run the printed samply command as its own separate shell command.
```

The helper prints a `samply record --save-only` command with presymbolication
enabled, plus the expected profile, symbol sidecar, and CLI summary paths. Pass
an output path in a scratch directory so generated profiles and symbol sidecars
do not land in the repository. Run the printed command as its own separate shell
command so `samply` is launched directly by the shell. In managed macOS sandbox
environments, launching `samply` from an intermediate helper process, or
immediately after CMake in the same shell command, can fail with
`Unknown(1100)`.

Samply writes presymbolicated native symbols to a sibling `.syms.json` sidecar,
for example `tess-profile.json.gz` produces `tess-profile.json.syms.json`.
Seeing `symbolicated:false` in the main Gecko profile JSON is expected in this
workflow; use the sidecar when producing CLI summaries:

```sh
tools/profile_symbol_summary.py \
  "$TMPDIR/tess-astar-1024-profile.json.gz" \
  "$TMPDIR/tess-astar-1024-profile.json.syms.json" \
  --include-regex 'tess::|PathScratch'
```

Load the saved profile explicitly when an interactive view is needed:

```sh
samply load "$TMPDIR/tess-astar-1024-profile.json.gz"
```

Pass a different benchmark filter or extra Google Benchmark arguments with:

```sh
tools/profile_benchmark.sh --filter 'path/.*' -- --benchmark_repetitions=1
```

## 24. Regression thresholds

Key conversion benchmarks have threshold scaffolding in
`bench/thresholds/key-conversions.json`. Storage benchmarks have matching
threshold scaffolding in `bench/thresholds/storage.json`. Block benchmarks
have matching threshold scaffolding in `bench/thresholds/block.json`,
including scratch-specific names for `block/scratch_allocate_u32` and
`block/context_scratch_tile_iteration_2d`. Queued execution benchmarks have
matching threshold scaffolding in `bench/thresholds/queued.json`, and MVP path
benchmarks have matching threshold scaffolding in `bench/thresholds/path.json`.
The path set includes a cheap smoke path plus 64x64, 512x512, and 1024x1024
open-world A* scaling paths intended to catch unrealistic path-core overhead.
Path benchmarks also publish user counters for cost, path nodes, expanded
nodes, and reached nodes so timing changes can be correlated with graph work.
Additional A* investigation benchmarks cover short/medium/long 512x512 open
paths, wall-gap detours, failed wall-separated paths, striped mazes, and
100-request batches. They also include alternate direct-axis-order and
axis-aligned one-tile detour cases for uniform-cost fast paths. The wall-gap
case exercises the exact top-down 2D single-plane gap precheck, and striped
maze cases exercise dynamic forced-gap sequence handling. These are profiling
cases first; keep only stable, short-running path cases in threshold JSON until
same-runner variance is calibrated.

Run the current scaffolds with:

```sh
cmake --build --preset bench --target tess_bench_key_thresholds
cmake --build --preset bench --target tess_bench_storage_thresholds
cmake --build --preset bench --target tess_bench_block_thresholds
cmake --build --preset bench --target tess_bench_queued_thresholds
cmake --build --preset bench --target tess_bench_path_thresholds
```

Benchmark CPU time over 1 ms is an investigation trigger for the current MVP
suite. Threshold JSON entries use `max_cpu_time_ns: 1000000` to enforce that
upper bound while leaving real-time limits unset until same-runner variance is
better understood. CI also collects non-gating repeated benchmark samples with:

```sh
cmake --build --preset bench --target tess_bench_ci_baselines
```

Use the pinned `ubuntu-24.04` CI runner artifacts as the primary calibration
source for limits that will gate CI. After several same-runner baseline runs,
tighten stable benchmark thresholds below the 1 ms investigation ceiling where
useful. Prefer `max_cpu_time_ns` for single-thread microbenchmarks. Leave
real-time limits at `null` until their variance is understood.

After downloading CI baseline artifacts, summarize candidate CPU-time limits
with:

```sh
tools/benchmark_baseline_summary.py path/to/*.json
```

Review coefficient of variation and outliers before copying suggested values
into threshold JSON. The suggestions use maximum observed CPU time plus
headroom; they are starting points, not automatic gates.

Generate the README-visible trend snapshot and detailed local HTML report with:

```sh
tools/benchmark_trends.py path/to/benchmark-baselines-* \
  --out build/bench/benchmark-trends.html \
  --snapshot-svg docs/assets/benchmark-trends.svg \
  --summary-md docs/performance.md
```

Refresh the tracked SVG intentionally when thresholds change, benchmark
workloads change, or a milestone/release lands. The SVG label must include the
source CI run, commit, and Pacific-time collection timestamp so stale snapshots
are obvious.

## 24. CI strategy

- correctness benchmarks with sanitizers
- performance smoke
- full performance suite manual/scheduled

## 25. Acceptance criteria

Benchmarks cover storage, planner, scheduler, topology, pathing, fields,
caches, ECS, render deltas, diagnostics, shapes, allocation, and correctness.
Results are reproducible and exportable.
