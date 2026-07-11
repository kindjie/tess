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

- [Shape, coordinate, and key foundation](shape.md)
- [Storage foundation](storage.md)
- [Block foundation](block.md)
- [Queued operations foundation](queued-operations.md)
- [Topology foundation](topology.md)
- [Path foundation](path.md)
- [Simulation integration MVP](simulation.md)
- [Diagnostics foundation](diagnostics.md)
- [ECS integration](ecs.md)
- [GPU backend interface](gpu.md)

The umbrella header `tess/tess.h` exports the whole public surface plus the
`tess::version` / `tess::library_version` constant.

[surface.json](surface.json) maps each maintained doc to the public symbol
names it documents; `tools/check_public_surface.py` compares that manifest
against the headers in `TESS_PUBLIC_HEADERS` and reports undocumented public
symbols (advisory in CI's hooks-backstop job).
