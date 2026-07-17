# TDD: Initial Milestone / Implementation Plan

> **Status: COMPLETE (2026-07-11).** Every milestone M0-M15 shipped; the
> cross-cutting acceptance criteria in section 8 are pinned by named
> tests, and the required deliverables in section 7 are all present.
> `docs/decisions/CHANGELOG.md` records the per-stage design deltas from
> this plan, and `docs/architecture/` documents what was actually built.
> This document is preserved as the planning record, not maintained.
>
> **Versioning note:** The completed milestone maps to pre-stable `v0.1.0`;
> no stable compatibility promise was made.

## 1. Summary

This document defines the initial implementation plan.

The initial milestone should produce a usable prototype with constexpr shapes,
3D-internal coordinates, chunk-local SoA storage, queued ops, planner/executor
foundation, scheduler, topology/local region foundation, A*, distance fields,
EnTT integration, render deltas, diagnostics, and benchmarks.

## 2. Initial milestone goals

- Establish architecture and type system.
- Prove 2D, vertical 2D, and 3D share one model.
- Prove single-chunk 2D low overhead.
- Prove queued operation flow.
- Prove topology prechecks reduce failed path cost.
- Prove shared fields can beat repeated A*.
- Prove diagnostics explain planner decisions.
- Prove EnTT integration.
- Prove render deltas avoid snapshots.
- Establish benchmarks.

## 3. Initial milestone non-goals

- No dynamic world shape.
- No unbounded coordinate space.
- No production GPU backend.
- No Flecs adapter.
- No tactical query/assignment module.
- No complete crowd simulation.
- No full renderer/editor.
- No full save-game system beyond shape/schema metadata.

## 4. Suggested repo layout

- include/tess/core, storage, queued, sim, topo, path, fields, ecs/entt, delta, block, pipe, gpu, diag, debug/imgui, unsafe
- src/
- examples/
- bench/
- tests/
- docs/
- tools/

## 5. Testable MVP checkpoint

The first testable MVP is narrower than the full milestone vision. It should
prove an end-to-end synchronous flow over always-resident storage:

- create a compile-time world and typed tile fields;
- enqueue and plan field update work;
- execute planned work through serial block views;
- update dirty metadata from declared dirty masks;
- run a deterministic unit-cost A* query over a boolean-like passability field;
- cover top-down 2D, vertical 2D, and small 3D coordinates;
- keep repeated hot execution/path queries allocation-free after scratch or
  plan warmup;
- expose benchmark smoke tests and disabled timing thresholds for new queue and
  path benchmarks.

MVP non-goals: sparse residency, scheduler-owned execution, barrier insertion,
portal graphs, topology prechecks, weighted movement, distance fields, ECS,
render deltas, GPU backends, save/load, and production timing gates.

## 6. Milestones

### M0 scaffolding

CMake, Clang warning profile, tests, benchmarks, sanitizers, CI, docs.

### M1 shape, coordinates, keys

Extent3, Coord2/3, Box3, ChunkCoord3, LocalTileId, ChunkKey, TileKey, ShapeTraits, key packing, validation.

Acceptance: top-down 2D, vertical 2D, true 3D, huge bounded shapes, u64/u128 inference.

### M2 field schema and chunk storage

FieldTag, FieldSchema, typed handles, ChunkPage, ChunkDirectory, AlwaysResident, minimal SparseResident states, SoA spans, dirty/active masks.

### M3 block kernel executor

BlockDomain, BlockCtx, ChunkView, parallel_for_blocks, write policies, worker scratch, diagnostics.

### M4 queued operations foundation

FrameOps, OpHandle, schemas, domains, planner, execution plan, phases/barriers, results, source_location.

### M5 simulation scheduler

SimClock, fixed tick, schedule, phases, every_tick, every_N, dirty tasks, background, result hooks, sim speed multiplier.

### M6 topology local regions

Passability, movement vocabulary DSL, MovementClass, TransitionProvider, local connected components, local_region_id, dirty versions.

### M7 portal graph foundation

Boundary exits, adjacent portals, region graph, reachability, missing chunk status.

### M8 A* pathfinding core

PathRequest, PathTicket, PathResult, PathScratch, A*, topology precheck, reusable scratch, deterministic tie-breaking, PathView.

### M9 distance fields and nearest query

GoalSet, DistanceFieldProduct, BFS/Dijkstra, byte-budgeted LRU field cache, nearest target query, field planner heuristic.

### M10 EnTT integration

EnTT adapter, EntityHandle conversion, Position adapter, request collection, result application, custom ECS minimal example. Flecs deferred.

### M11 render delta bridge

DeltaFrame, versions, entity deltas, tile changes, baselines, coalescing, path overlays, high-speed handling.

### M12 diagnostics and tooling

WarningSink, TraceBuffer, stats, planner trace, path/topology/field/storage diagnostics, benchmark exports, optional ImGui skeleton.

### M13 GPU interface only

GpuBackend concept, capabilities, mirror descriptors, upload/dispatch/readback descriptors, NoGpuBackend, mock backend.

### M14 benchmark suite

Shape/key, storage, block, planner, scheduler, topology, pathfinding, fields, cache, ECS, render delta, diagnostics.

### M15 polish and examples

2D colony, vertical 2D ant-farm, 3D stair pathing, EnTT pawn movement, render delta consumer, path overlay.

## 7. Initial milestone deliverables

Required:

- constexpr shape/key system
- chunked storage
- typed fields
- block executor
- queued ops/planner foundation
- scheduler with sim speed
- local topology + portal graph foundation
- A*
- distance field + nearest query
- product cache with byte-budgeted LRU
- EnTT adapter
- render delta bridge
- diagnostics
- benchmark suite
- GPU backend concepts
- standalone docs

Optional:

- Dear ImGui panels
- weighted Dijkstra beyond minimal
- simple congestion field
- path cache
- Hilbert benchmark
- mock GPU tests

Deferred:

- Flecs
- real GPU backend
- tactical query/assignment
- crowd/local steering
- room system
- full save/load migration
- editor/tool integration

## 8. Cross-cutting acceptance criteria

Performance:

- no per-path allocation after warmup
- no per-field-frontier allocation after warmup
- no hidden full-world scans on huge worlds
- chunk-local contiguous spans
- bounded planner overhead
- product cache respects byte budget
- diagnostics compile down

Correctness:

- shape/key round trips
- topology reachability matches references
- path steps legal
- fields decrease toward goals
- render delta replay matches projected state
- ECS Position and spatial index stay synchronized

Usability:

- queued intents are common path
- compile-time errors understandable
- diagnostics explain planner decisions
- examples concrete

## 9. Future TDDs

- Tactical Queries and Assignment
- Flecs Integration
- Real GPU Backend
- Room/Area System
- Crowd Movement / Local Steering
- Save/Load and Migration
- Editor/Tooling Integration
- Hilbert/Morton Ordering Study
- Field Planner Heuristics Study
