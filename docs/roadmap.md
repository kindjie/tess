# Scope and roadmap

What is shipped, what remains planned, and what tess will not become. The
[support policy](support.md) governs stability; the archived
[TDD documents][tdd-index] record design rationale, while the maintained
[completion plan][completion-plan] owns sequencing and status.

## Shipped

Every concept page under [Concepts](architecture/README.md) documents
shipped, tested machinery: compile-time shapes and field schemas, dense
and sparse residency, queued operations with result channels, the block
kernel layer, topology and the reachability precheck, A* with movement
classes, weighted batches, distance-field products and caches, the
schedule loop, the EnTT and custom-ECS adapters, DeltaFrame render
bridging, the production worker-pool phase executor, the GPU descriptor
interface, and compile-gated diagnostics.

## Planned extensions

The foundations named below may exist, but the extensions themselves are not
shipped. Do not build consumer code that assumes these APIs exist until the
item moves to **Shipped**.

- **Resolved transitions, diagonal movement, and axial-hex worlds**
  ([TDD][tdd-lattice]) — current paths, topology, fields, and movement use
  orthogonal face adjacency, while special providers affect topology only.
- **General queued intents and resumable work** ([queued TDD][tdd-queued]) —
  queued field updates, synchronous typed results, phase planning, and the
  production pool shipped. Typed path, field-product, topology, movement,
  residency, and render intents plus async/budgeted tickets did not.
- **Event scheduling and persistent maintenance**
  ([scheduler TDD][tdd-scheduler], [maintenance addendum][tdd-work]) — fixed
  cadences, dirty/manual triggers, and deterministic background continuation
  shipped. Event streams and coalescing maintenance handles did not.
- **Block pipelines and spatial query acceleration**
  ([block TDD][tdd-block], [layout addendum][tdd-layout]) — resolved chunk
  views and serial block iteration shipped. Block-lazy pipelines, box/radius
  spans, predicate bitsets, summaries, halos, and layout experiments did not.
- **Hierarchical topology and path policy** ([path TDD][tdd-path]) — local
  regions, portals, exact caches, weighted batches, and topology prechecks
  shipped. Coarse hierarchy, corridor selection, weighted field products, and
  richer runtime strategy remain planned.

- **Flow, congestion, and influence fields** ([TDD][tdd-flow]) — only
  distance fields and nearest-target queries shipped. Today's fallback
  for congestion-aware routing: write congestion from your simulation
  into a cost field and route through a weighted movement class.
- **Real GPU backend** ([TDD][tdd-gpu]) — the descriptor/concept layer
  shipped; no device backend exists, and the CPU stays authoritative.
- **Flecs adapter** ([TDD][tdd-ecs]) — EnTT and the custom-adapter
  concepts shipped; Flecs did not.
- **Crowd movement and local steering** ([project design][tdd-project])
  — pathing returns per-agent optimal routes; it does not spread or
  queue crowds.
- **Tactical assignment, room/area systems, save/load and migration, and
  editor integration** ([project design][tdd-project]) — these later phases
  provide substrate and optional tooling only; game-specific meaning remains
  application-owned.

## Out of scope

tess is not a renderer, physics engine, navigation-mesh generator, or
drop-in ECS, and does not intend to become one. It supplies the spatial
substrate; the application owns meaning, entities, and presentation.

[tdd-index]: https://github.com/kindjie/tess/blob/main/docs/tdd/README.md
[completion-plan]: https://github.com/kindjie/tess/blob/main/docs/planning/roadmap-completion.md
[tdd-lattice]: https://github.com/kindjie/tess/blob/main/docs/tdd/lattice-and-transition-model.md
[tdd-queued]: https://github.com/kindjie/tess/blob/main/docs/tdd/queued-operations-and-planner.md
[tdd-scheduler]: https://github.com/kindjie/tess/blob/main/docs/tdd/simulation-scheduler.md
[tdd-block]: https://github.com/kindjie/tess/blob/main/docs/tdd/block-kernel-pipeline.md
[tdd-path]: https://github.com/kindjie/tess/blob/main/docs/tdd/pathfinding-core.md
[tdd-flow]: https://github.com/kindjie/tess/blob/main/docs/tdd/flow-distance-fields.md
[tdd-work]: https://github.com/kindjie/tess/blob/main/docs/tdd/tdd_addendum_work_contracts.md
[tdd-gpu]: https://github.com/kindjie/tess/blob/main/docs/tdd/gpu-backend-interface.md
[tdd-ecs]: https://github.com/kindjie/tess/blob/main/docs/tdd/ecs-integration.md
[tdd-project]: https://github.com/kindjie/tess/blob/main/docs/tdd/project-design.md
[tdd-layout]: https://github.com/kindjie/tess/blob/main/docs/tdd/tdd_addendum_tile_layout_bench_takeaways.md
