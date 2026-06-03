# TDD: ECS Integration

## 1. Summary

This document defines integration with EnTT and custom ECS/game object systems.

The substrate does not own game entities. ECS systems inspect game state, enqueue tile/path/movement/query intents, and consume result channels after the queued planner/executor runs.

Tiles are not ECS entities. Dense tile data remains in the substrate.

## 2. Goals

- Keep core ECS-agnostic.
- Provide v1 EnTT adapter.
- Provide custom minimal adapter example.
- Defer Flecs.
- Keep dense tile data separate from ECS entities.
- Support batched AI/path/work/movement queries.
- Preserve ECS structural mutation rules.
- Keep tile occupancy/spatial indices synchronized with ECS Position.
- Avoid per-entity/per-tile allocation in hot paths.
- Support game-defined component types through concepts/adapters.

## 3. Non-goals

- No ECS implementation in core.
- No ownership of EnTT registries.
- No pawn AI behavior implementation.
- No game content definitions.
- No direct structural ECS mutation during tile block jobs.
- No Flecs adapter in v1.
- No entity-per-tile storage.

## 4. Ownership model

ECS owns entities, components, AI, inventory, jobs, health, factions, buildings, items, combat state.

Tile substrate owns tile fields, chunk storage, topology, path products, distance/flow products, occupancy fields, spatial index, queued planner, result channels.

Shared boundary:

- EntityHandle
- TileKey
- Position adapter
- movement intents
- reservations
- path requests/results
- render deltas

## 5. Entity handle adapter

Core uses opaque EntityHandle. Adapters convert entt::entity or custom ids.

Requirements:

- stable during queued execution
- comparable/hashable
- invalid/null representation
- generation/version support preferred
- debug name lookup optional

## 6. Position concept

Games define their own Position component. Adapter requires get/set TileKey or Coord3.

Support direct component shapes and custom adapters.

## 7. Integration pattern

Each tick:

1. ECS systems inspect components.
2. ECS systems enqueue intents into FrameOps.
3. Planner/executor runs.
4. Result channels are consumed.
5. ECS components update at safe phases.
6. Structural mutations are deferred.
7. Tile occupancy/spatial index updates commit through substrate movement ops.

## 8. Movement integration

Movement is intent-based. MoveEntityRequest validates source, destination, passability, occupancy, reservation, entity validity, and path/topology version. Commit updates occupancy, spatial index, dirty masks, render deltas, and ECS Position through result application.

## 9. Path request integration

AI systems enqueue PathRequest batches. Path-following systems read PathState and emit movement intents. Result consumers update Movement/PathState or AI fallback state.

## 10. Nearest target and work query integration

Substrate provides reachability/cost products and result channels. Game provides target validity, priorities, permissions, faction rules, and job semantics.

## 11. Reservations

Reservation authority can remain game-owned but substrate-aware. The substrate supports hooks, policies, final validation, and result channels.

## 12. Spatial index integration

Provide spatial index for entities inhabiting tiles/chunks:

- insert
- remove
- move
- entities in tile/chunk/box/radius
- per-chunk buckets

Position and spatial index must stay synchronized through movement commits.

## 13. Teleport and external position changes

Use explicit intents: spawn, despawn, teleport, force_move. Avoid silent direct component mutation.

## 14. EnTT adapter

v1 optional module:

- EntityHandle conversion
- Position adapter concept
- view-to-request collection helpers
- result application helpers
- movement intent helpers
- optional diagnostics integration

Generic EnTT entity/component inspection can rely on existing EnTT/ImGui ecosystem where appropriate.

## 15. Flecs

Flecs is future work. Core must not make assumptions that block a later Flecs adapter.

## 16. Custom ECS

Game provides EntityHandle adapter, Position adapter, entity iteration, result consumption, reservation adapter if needed, and debug name adapter if desired.

## 17. AI behavior convenience layer

Optional later layer may reduce boilerplate. Core should first support explicit batch collection.

## 18. Tactical query integration

Future Tactical Queries and Assignment module integrates here for cover/firing/retreat/rally positions, scarce position assignment, reservation, and path generation.

## 19. Render delta integration

ECS movement/spawn/despawn changes produce render events. Renderer owns presentation state.

## 20. Threading and determinism

ECS intent collection may be parallel if ECS permits. Substrate execution does not mutate ECS directly. Result application happens at safe phases.

Deterministic mode requires stable ECS iteration or explicit sorting.

## 21. Diagnostics

Report path requests emitted, movement intents, stale entity results, Position/spatial mismatches, result backlog, reservation conflicts, tactical contention, and unsafe structural mutation.

## 22. Performance concerns

- batch requests, avoid one allocation per entity
- stable sorting may cost; benchmark
- use movement intents as authoritative sync points
- avoid per-tile entity joins when sparse
- use arenas/pools/result channels

## 23. Tests

Test adapters, movement, path results, stale results, destroyed entities, teleports, reservations, EnTT collection/application, custom adapter, and spatial index consistency.

## 24. Benchmarks

- request collection for 1k/10k/100k entities
- result application
- movement commit throughput
- spatial index update
- deterministic sorting overhead
- EnTT adapter overhead
- reservation conflict throughput

## 25. Acceptance criteria

- Core is ECS-agnostic.
- EnTT adapter is optional and v1-supported.
- Custom adapter example exists.
- ECS systems can enqueue path, nearest, movement, and work intents.
- Result channels update ECS components safely.
- Position and spatial index stay synchronized.
- Structural ECS changes are deferred.
- Flecs remains possible but deferred.
