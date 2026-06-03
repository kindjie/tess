# TDD: Render Delta / Presentation Bridge

## 1. Summary

This document defines the bridge between fixed-tick simulation and rendering.

The substrate does not own rendering, animation, sprites, materials, cameras, or presentation state. It publishes compact, versioned deltas and optional baselines. The renderer maintains its own presentation world.

## 2. Goals

- Decouple fixed-TPS simulation from variable-FPS rendering.
- Avoid copying the full world every tick.
- Publish compact entity/tile/chunk deltas.
- Support interpolation/smoothing through deltas/events.
- Support resync baselines.
- Support 2D, vertical 2D, and 3D shapes.
- Support sparse worlds and visible-region culling.
- Support tile dirty regions by chunk/field.
- Support entity motion deltas and planned path overlays.

## 3. Non-goals

- No renderer implementation.
- No animation graph.
- No sprite/mesh/material ownership.
- No camera/culling implementation beyond data hooks.
- No mandatory previous/current full snapshots.
- No mandatory interpolation policy.
- No ECS ownership.
- No GPU render backend in v1.
- No direct rendering from mutable simulation state.

## 4. Core concepts

- DeltaFrame
- RenderBaseline
- EntityMotionDelta
- TileFieldChanged
- VisualEvent
- PathOverlayDelta
- PublishMode
- RenderVersion

## 5. Ownership model

Simulation/ECS owns authoritative state. Tile substrate owns delta generation helpers and metadata. Renderer owns presentation state and interpolation.

## 6. Delta frame

DeltaFrame includes from_version, to_version, sim_tick, sequence, changed chunks, field changes, entity changes, visual events, and optional debug metadata.

Consumer must have `current_version == frame.from_version` or request baseline.

## 7. Baseline / resync

Used when renderer loads late, misses frames, camera jumps, delta stream overflows, debug attaches, save/load occurs, or version mismatch occurs.

Scopes: whole world only for small worlds, visible region, Box3, chunk set, entity set, z slice/range, debug selection.

## 8. Field publish modes

- None
- ViewCurrent
- DirtyDelta
- DoubleBuffered
- RendererOwned

Defaults:

- terrain: DirtyDelta or ViewCurrent
- passability: None/debug
- occupancy: DirtyDelta
- temperature/fog/light visuals: RendererOwned or DoubleBuffered
- debug overlays: DirtyDelta/debug

## 9. Entity deltas

EntityMoved includes entity, from/to TileKey, from/to position, tick, visual duration hint, motion style, flags.

Renderer can turn this into base + delta and apply its own interpolation/easing.

## 10. Interpolation model

Substrate supports interpolation but does not own policy.

Global fixed tick alpha:

```text
alpha = remaining_accumulator / fixed_dt
```

Per-entity visual transitions may use start time, duration, easing, snap/teleport flags.

Do not force all fields/entities to use the same alpha.

## 11. Variable simulation speed

At high sim speed, multiple sim ticks may happen before a render frame. The render bridge must handle delta backlog, coalescing, and baseline fallback.

Rules:

- fixed_dt does not change
- consumer may apply multiple DeltaFrames before drawing
- safe deltas may coalesce
- teleport/snap/noncoalescible events are preserved
- missed sequence/version requires baseline

## 12. Coalescing

Coalesce dirty rects, repeated field changes, and compatible entity moves when safe. Do not coalesce important visible events, teleport/snap, spawn/despawn effects, or noncoalescible events.

## 13. Tile field changes

Encodings:

- DirtyChunk
- DirtyBox
- SparseTiles
- RLE
- RendererOwnedInvalidation

Encoding heuristic chooses sparse, box, chunk, or renderer-owned invalidation based on change distribution.

## 14. Sparse/visible worlds

Publish deltas for visible/relevant chunks by default. Renderer controls visible domains. Substrate provides hooks to publish for visible chunks, box, explicit chunks, or debug selection.

## 15. Path overlays

The path system exposes versioned PathView. Render/debug code may request overlay deltas for selected entities or tickets.

PathOverlayDelta includes entity, ticket, path version, copied/path-view tile sequence or segment list, style, and lifetime policy.

Overlays must not retain raw path spans beyond product lifetime unless copied.

## 16. Degenerate dimensions

Delta model uses Box3/TileKey for all shapes. Top-down 2D has depth 1. Vertical 2D preserves x/z or y/z coordinates. Renderer decides projection.

## 17. Memory management

Rules:

- no mandatory full snapshots
- no copying all fields every tick
- no copying all ECS components
- no per-tile allocation in dirty publication
- use arenas/ring buffers
- let renderer own persistent memory

## 18. Lifetime safety

DeltaFrames are immutable once published. Handles include versions/generations. Long-lived references must be copied or retained explicitly.

## 19. Diagnostics

Report frames, bytes, entity moves, field changes, coalescing, baselines, version mismatches, missed frames, memory high-water, and full snapshot warnings.

## 20. Tests

Test ordered deltas, version mismatch, baseline reset, movement/spawn/despawn/teleport, dirty encodings, vertical 2D, high-speed coalescing, path overlays, and delta replay correctness.

## 21. Benchmarks

- entity move deltas
- dirty sparse/box/chunk encodings
- high sim speed coalescing
- baseline visible region
- sparse visible chunks
- ring buffer reuse

## 22. Acceptance criteria

- No full snapshots by default.
- DeltaFrames are versioned and immutable.
- Consumers detect missed deltas.
- Entity movement is compact.
- Tile changes support multiple encodings.
- Path overlays are exposed safely.
- 2D, vertical 2D, and 3D dirty regions work.
- Renderer owns presentation state.
