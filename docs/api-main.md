# tess API reference

This reference documents the supported C++ API generated from the installed
headers. Use the version-relative navigation above to return to the authored
documentation, its learning paths, or the reference map.

tess is pre-1.0; see the
[stability policy](../support/).

## Core worlds

- [`tess::Shape`](@ref tess::Shape) fixes the world, chunk, and lattice
  dimensions at compile time.
- [`tess::FieldSchema`](@ref tess::FieldSchema) declares the typed fields in
  each chunk page.
- [`tess::World`](@ref tess::World) combines shape, schema, and dense or sparse
  residency.

## Pathfinding

- [`tess::PathRequest`](@ref tess::PathRequest) carries one start and goal.
- [`tess::PathScratch`](@ref tess::PathScratch) owns reusable A* work storage.
- [`tess::DistanceFieldProduct`](@ref tess::DistanceFieldProduct) retains a
  dense multi-goal distance product for reuse.

## Simulation

- [`tess::OperationBatch`](@ref tess::OperationBatch) collects declared world
  edits for validation and execution.
- [`tess::Schedule`](@ref tess::Schedule) runs deterministic phase-ordered
  tasks.
- [`tess::DeltaFrame`](@ref tess::DeltaFrame) publishes versioned changes to a
  consumer-owned presentation layer.

## Diagnostics

- [`tess::diagnostics::DiagnosticsSnapshot`][api-diagnostics-snapshot]
  captures path, queued-phase, timing, trace, and allocation state.
- [`tess::diagnostics::FlowAccounting`][api-flow-accounting] accounts
  lifecycle admission, retention, and terminal outcomes.

## Optional integrations

- [`tess::EnttTilePositionAdapter`](@ref tess::EnttTilePositionAdapter) and
  [`tess::FlecsTilePositionAdapter`](@ref tess::FlecsTilePositionAdapter) bind
  external entity storage without transferring ownership.
- [`tess::gpu::WebGpuBackend`](@ref tess::gpu::WebGpuBackend) implements the
  optional GPU transport boundary; pathfinding remains on the CPU.

[api-diagnostics-snapshot]: @ref tess::diagnostics::DiagnosticsSnapshot
[api-flow-accounting]: @ref tess::diagnostics::FlowAccounting
