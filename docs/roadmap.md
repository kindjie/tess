# Scope and roadmap

What is shipped, what is designed but deferred, and what tess will not
become. The [support policy](support.md) governs stability; the archived
[TDD documents][tdd-index] record full design rationale.

## Shipped

Every concept page under [Concepts](architecture/README.md) documents
shipped, tested machinery: compile-time shapes and field schemas, dense
and sparse residency, queued operations with result channels, the block
kernel layer, topology and the reachability precheck, A* with movement
classes, weighted batches, distance-field products and caches, the
schedule loop, the EnTT and custom-ECS adapters, DeltaFrame render
bridging, the GPU descriptor interface, and compile-gated diagnostics.

## Designed but deferred

Each item below has a design document but **no shipped implementation**.
Do not build agent code that assumes these APIs exist.

- **Flow, congestion, and influence fields** ([TDD][tdd-flow]) — only
  distance fields and nearest-target queries shipped. Today's fallback
  for congestion-aware routing: write congestion from your simulation
  into a cost field and route through a weighted movement class.
- **Production parallel executor** ([addendum][tdd-concurrent]) — the
  scoped-thread and worker-pool executors are documented prototypes;
  every published benchmark median is single-threaded. Declaring honest
  `WritePolicy` values today licenses parallel execution later without
  operation-code changes.
- **Coalescing maintenance scheduler** ([addendum][tdd-work]) — dirty
  flags on chunk metadata are the shipped baseline.
- **Real GPU backend** ([TDD][tdd-gpu]) — the descriptor/concept layer
  shipped; no device backend exists, and the CPU stays authoritative.
- **Flecs adapter** ([TDD][tdd-ecs]) — EnTT and the custom-adapter
  concepts shipped; Flecs did not.
- **Crowd movement and local steering** ([project design][tdd-project])
  — pathing returns per-agent optimal routes; it does not spread or
  queue crowds.
- **Layout and span-query experiments** ([addendum][tdd-layout]) —
  row-major-in-chunk storage is the shipped baseline; the proposed
  span/bitset/summary APIs are gated on benchmark evidence.
- **Save/load and migration, room/area systems, editor tooling**
  ([project design][tdd-project]) — out of the current cycle entirely.

## Out of scope

tess is not a renderer, physics engine, navigation-mesh generator, or
drop-in ECS, and does not intend to become one. It supplies the spatial
substrate; the application owns meaning, entities, and presentation.

[tdd-index]: https://github.com/kindjie/tess/blob/main/docs/tdd/README.md
[tdd-flow]: https://github.com/kindjie/tess/blob/main/docs/tdd/flow-distance-fields.md
[tdd-concurrent]: https://github.com/kindjie/tess/blob/main/docs/tdd/tdd_addendum_concurrent_tile_world.md
[tdd-work]: https://github.com/kindjie/tess/blob/main/docs/tdd/tdd_addendum_work_contracts.md
[tdd-gpu]: https://github.com/kindjie/tess/blob/main/docs/tdd/gpu-backend-interface.md
[tdd-ecs]: https://github.com/kindjie/tess/blob/main/docs/tdd/ecs-integration.md
[tdd-project]: https://github.com/kindjie/tess/blob/main/docs/tdd/project-design.md
[tdd-layout]: https://github.com/kindjie/tess/blob/main/docs/tdd/tdd_addendum_tile_layout_bench_takeaways.md
