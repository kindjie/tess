# TDD: Diagnostics and Tooling

## 1. Summary

This document defines diagnostics, tracing, warnings, debug inspection, benchmark reporting, and optional Dear ImGui tooling.

Diagnostics are first-class. The system must explain planner decisions, performance hazards, stale data, memory usage, path failures, topology rebuilds, field cache behavior, chunk residency, and integration mistakes.

Core diagnostics are UI-agnostic. Dear ImGui support is optional.

## 2. Goals

- Make planner decisions explainable.
- Detect slow or unsafe API usage.
- Report allocation, scratch usage, product cache usage, and fragmentation risks.
- Provide visibility into pathfinding, topology, fields, scheduling, storage, render deltas, ECS, and GPU.
- Support source_location.
- Support optional stack traces.
- Support optional Dear ImGui panels.
- Integrate with EnTT debug workflows without core EnTT dependency.
- Keep diagnostics cheap to disable.

## 3. Non-goals

- No mandatory UI, ImGui, or EnTT dependency.
- No renderer/editor implementation.
- No always-on expensive tracing in release.
- No replacement for external profilers.

## 4. Core concepts

- DiagnosticEvent
- Warning
- Trace
- PlanDump
- Stats
- DebugView
- WarningSink
- TraceBuffer
- ImGuiPanel

## 5. Diagnostic levels

- Disabled
- CountersOnly
- Warnings
- Trace
- DebugHeavy

Disabled diagnostics should compile out where possible.

## 6. Source locations and stack traces

Every queued operation records source location in debug/profile configurations. Stack traces are optional and captured only for expensive/worrisome events.

## 7. Warning system

Warnings are structured with code, severity, source location, tick, affected object, message, suggested fix, repeat count, and suppression key.

Warning categories include storage, planner, path, topology, fields/products, scheduler, render delta, ECS, and GPU.

## 8. Planner trace

Trace records submitted operations, grouping, domains, chunks/blocks, hazards, barriers, backend choices, algorithms, products, deferrals, warnings, estimates, and actual costs.

It should answer why a plan chose A*, field, rebuild, barrier, deferral, or fallback.

## 9. Execution stats

Per tick and per phase stats include ops, phases, barriers, jobs, blocks, chunks, tiles, allocations, scratch high-water, product cache bytes, path requests, fields, topology rebuilds, render delta bytes, warnings, timings, and load imbalance.

## 10. Memory diagnostics

Report world bytes, bytes per field/chunk, masks, topology, path scratch, field/path product caches, render delta buffers, GPU mirrors/products, transient arena high-water, allocations, evictions, and pins.

## 11. Product cache diagnostics

Report max/current bytes, products, hits, misses, evictions, invalidations, stale handles, pins, LRU/MRU summaries, and product keys/versions/bounds/reuse/build time.

## 12. Path diagnostics and overlays

Path diagnostics report request batches, topology prechecks, A* expansions, cache/field usage, missing chunks, budget exhaustion, stale tickets, and p95/p99 latency.

PathView exposes current planned path for debug/UI overlays.

## 13. Topology, field, scheduler, delta, ECS, GPU diagnostics

Each subsystem exposes structured debug views and warnings. EnTT adapter can link selected entities to existing EnTT/ImGui inspectors when available.

## 14. Dear ImGui module

Optional module: `tiles::debug::imgui`.

Panels:

- World Overview
- Chunk Inspector
- Tile Inspector
- Planner Trace
- Path Debug
- Topology Debug
- Field/Product Cache
- Scheduler
- Render Delta
- GPU

The substrate provides data; ImGui renders it.

## 15. Debug overlays

Overlay data includes planned paths, explored nodes, topology regions, portals, dirty chunks, active chunks, flow directions, distance values, congestion pressure, render dirty regions, and chunk residency states.

Renderer chooses how to draw overlays.

## 16. Structured export

Diagnostics export JSON/CSV for plan dumps, tick stats, benchmarks, warnings, memory reports, and product cache state.

## 17. Deterministic diagnostics

Stable diagnostic subsets should be comparable in tests. Avoid comparing wall-clock timing, addresses, thread order, or stack traces.

## 18. API sketch

WarningSink, TraceBuffer, Diagnostics, and optional ImGui draw functions.

## 19. Performance constraints

- disabled diagnostics compile out
- no heap allocation for disabled diagnostics
- source_location is cheap
- stacktrace opt-in/debug-only
- trace buffers fixed capacity/ring
- repeated warnings aggregate

## 20. Tests

Test warning emission/suppression, trace recording, memory stats, source_location, disabled diagnostics, stale handles, repeated warning aggregation, planner/path/topology/field/delta diagnostics, ImGui compile when enabled, and golden structured exports.

## 21. Benchmarks

- disabled overhead
- counters/warnings/trace overhead
- source_location
- stacktrace
- ImGui panel draw cost
- trace export
- memory stats collection
- binary size impact

## 22. Acceptance criteria

- Diagnostics can compile down near-zero when disabled.
- Planner decisions are explainable.
- Structured warnings cover common hazards.
- Memory/product cache usage is visible.
- Optional Dear ImGui module renders substrate-specific panels.
- EnTT generic entity inspection can rely on existing ecosystem tools.
- Offline export and regression tests are supported.
