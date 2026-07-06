# Architecture

This directory contains maintained architecture documentation for the current
implementation.

The codebase has an implemented synchronous MVP foundation, with larger async,
residency, ECS, GPU, and topology-aware routing work still deferred. The
historical TDD archive provides broader design intent:

- [Project design](../tdd/project-design.md)
- [Shape, coordinate, and key system](../tdd/core-shape-coordinate-key-system.md)
- [Chunk storage](../tdd/core-chunk-storage.md)
- [Queued operations and planner](../tdd/queued-operations-and-planner.md)
- [Simulation scheduler](../tdd/simulation-scheduler.md)
- [Topology and region graph](../tdd/topology-and-region-graph.md)
- [Pathfinding core](../tdd/pathfinding-core.md)
- [Flow and distance fields](../tdd/flow-distance-fields.md)
- [ECS integration](../tdd/ecs-integration.md)
- [Render delta bridge](../tdd/render-delta-presentation-bridge.md)
- [Block kernel pipeline](../tdd/block-kernel-pipeline.md)
- [GPU backend interface](../tdd/gpu-backend-interface.md)
- [Diagnostics and tooling](../tdd/diagnostics-and-tooling.md)
- [Modern C++ safety](../tdd/modern-cpp-compile-time-safety.md)

C++ implementation work follows the active [style policy](../style.md).

Maintained notes for implemented areas:

- [Storage foundation](storage.md)
- [Block foundation](block.md)
- [Queued operations foundation](queued-operations.md)
- [Topology foundation](topology.md)
- [Path foundation](path.md)
- [Simulation integration MVP](simulation.md)
