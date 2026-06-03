# TDD: GPU Backend Interface

## 1. Summary

This document defines the optional GPU backend interface.

GPU execution is not required in v1. The design ensures core storage, field products, queued operations, diagnostics, and ownership do not block future GPU acceleration.

CPU remains authoritative. GPU products are derived, cached, and versioned.

## 2. Goals

- Define future GPU backend interfaces without requiring v1 implementation.
- Keep CPU authoritative for topology, occupancy, reservations, and exact gameplay state.
- Support GPU acceleration of dense/chunked derived products.
- Avoid full readbacks by default.
- Support dirty chunk upload.
- Support async dispatch and summary readback.
- Use compile-time polymorphic backend supplied by game/plugin.
- Avoid contaminating CPU hot paths with GPU requirements.

## 3. Non-goals

- No required GPU implementation in v1.
- No GPU-authoritative gameplay.
- No mandatory GPU dependency.
- No full-world GPU mirror by default.
- No full field readback by default.
- No renderer ownership.
- No shader language commitment.
- No replacement for CPU pathfinding.

## 4. Backend model

The library defines concepts. The game or plugin provides a backend type wrapping its chosen graphics/compute abstraction.

```cpp
using Runtime = tiles::Runtime<
  World,
  tiles::gpu::Config<MyGpuBackend, MyGpuAlgorithms>
>;
```

A NoGpuBackend configuration must compile and be the default.

## 5. Backend vs algorithms

Separate:

- GpuDeviceBackend: buffers, uploads, dispatch, readback, fences
- GpuAlgorithmProvider: distance/influence/visibility kernels
- Tile substrate: planning, product cache, versioning, dirty upload selection, readback policy

## 6. Core concepts

- GpuBackend
- GpuCapabilities
- GpuMirror
- GpuField
- UploadBatch
- GpuDispatch
- Readback
- GpuProductHandle
- BackendPolicy

## 7. CPU authority

CPU owns authoritative fields, topology, occupancy, reservations, entity state, path tickets, save/load, deterministic simulation.

GPU may own derived products: distance, flow, reachability, influence, danger, fog/visibility, render/debug, cost transforms.

## 8. Capabilities

Capabilities include compute, storage buffers, async compute/readback, subgroup ops, atomics, 16-bit storage, max buffer/dispatch size, alignment, and supported formats.

Planner checks capabilities before selecting GPU.

## 9. GPU mirror

GpuMirror tracks selected field/chunk copies with CPU/GPU versions, dirty status, format, memory bytes, and residency dependencies.

Modes: None, OnDemand, Persistent, RendererOwned.

## 10. Dirty upload

Uploads are chunk-based and support UploadNow, UploadAsync, Coalesce, DropIfSuperseded.

Avoid full-world upload by default.

## 11. GPU products and dispatch

GpuFieldProduct stores key, kind, bounds/chunk set, format, resource handle, version deps, status, bytes, last used tick, debug name.

Dispatch status includes PendingUpload, Ready, Running, Complete, Failed, Stale, Cancelled.

## 12. Readback policy

Readback is explicit:

- None
- Summary
- SelectedTiles
- SelectedPath
- FullField, debug/explicit only

No full readback by default.

## 13. Candidate algorithms

Good candidates: distance fields, influence, visibility/fog, debug overlays.

Risky candidates: arbitrary per-agent A*, reservation-aware exact paths, branchy movement rules, CPU-immediate decisions requiring sync readback.

## 14. Planner interaction

Use GPU when work is dense/batched, result can be async, readback is small/unneeded, data is already mirrored or upload cost is acceptable.

Use CPU when request is urgent/small/branchy/immediate/deterministic or upload/readback dominates.

## 15. Memory management

Use explicit GPU budgets and byte-budgeted LRU/weighted-LRU product caches.

Avoid unbounded resource creation, staging growth, ownership cycles, and implicit chunk pinning.

## 16. Determinism

Deterministic gameplay must not depend on uncertified nondeterministic GPU products. CPU fallback required for authoritative exact results.

## 17. API sketch

Backend concept includes capabilities, buffer/resource creation, upload, dispatch, readback, completion collection.

Planner represents BackendChoice and reasons.

## 18. Diagnostics

Report backend status, capabilities, mirrors, uploads, dispatches, readbacks, cache bytes, evictions, stale products, fallback reasons, and device errors.

Warnings: full readback, upload cost too high, uncertified deterministic use, cache thrashing, many small dispatches, fallback.

## 19. Tests

Compile-time and mock-backend tests cover disabled builds, invalid requests, deterministic rejection, upload batch creation, handle generation, stale invalidation, readback mismatch, cache eviction, fallback, device loss.

## 20. Benchmarks

Mock backend in v1. Future real benchmarks compare CPU vs GPU fields, upload/readback cost, dense/sparse dispatch, many small vs batched dispatch.

## 21. Acceptance criteria

- GPU interfaces are optional.
- CPU-only builds unaffected.
- Backend is compile-time polymorphic.
- Planner can represent CPU/GPU/hybrid choices.
- Field products can record backend and versions.
- Dirty upload and readback policies are explicit.
- Real backend can be added later without redesigning core.
