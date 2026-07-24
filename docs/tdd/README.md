# TDD Archive

These technical design documents capture design intent at the time they were
written. They are useful for rationale and tradeoffs, but they are not the
authoritative source for current implementation behavior once code exists.
Historical `v1` scope labels in these documents mean the initial prototype
milestone, which was released as pre-stable `v0.1.0`; they do not describe a
stable 1.x compatibility promise.

When implementation diverges from a TDD:

- update the maintained architecture docs if the public design changed;
- add a design changelog entry explaining the divergence;
- optionally add a short note at the top of the affected TDD pointing to the
  newer source of truth.

## Documents

- [Project design](project-design.md)
- [Core shape, coordinate, and key system](core-shape-coordinate-key-system.md)
- [Core chunk storage](core-chunk-storage.md)
- [Queued operations and planner](queued-operations-and-planner.md)
- [Simulation scheduler](simulation-scheduler.md)
- [Topology and region graph](topology-and-region-graph.md)
- [Pathfinding core](pathfinding-core.md)
- [Flow and distance fields](flow-distance-fields.md)
- [ECS integration](ecs-integration.md)
- [Render delta / presentation bridge](render-delta-presentation-bridge.md)
- [Block kernel / pipeline](block-kernel-pipeline.md)
- [GPU backend interface](gpu-backend-interface.md)
- [Diagnostics and tooling](diagnostics-and-tooling.md)
- [Modern C++ / compile-time safety](modern-cpp-compile-time-safety.md)
- [Lattices and the resolved transition model](lattice-and-transition-model.md)
- [External grid benchmark data and the scenario oracle][benchmark-data]
  (network-free harness shipped; external acquisition gated)
- [Concurrent tile-world execution and maintenance][concurrent-tile-world]
- [Work Contracts evaluation and integration][work-contracts] (proposed
  addendum)
- [Tile layout benchmark findings and library integration][tile-layout]
  (proposed addendum)

[benchmark-data]: grid-benchmark-data-and-scenario-oracle.md
[concurrent-tile-world]: tdd_addendum_concurrent_tile_world.md
[work-contracts]: tdd_addendum_work_contracts.md
[tile-layout]: tdd_addendum_tile_layout_bench_takeaways.md
