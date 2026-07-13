# Architecture

This directory contains maintained architecture documentation for the current
implementation.

The current pre-1.0 surface includes sparse residency,
queued operations with result channels, the schedule with cadences and the
selectable parallel phase executor, movement classes with per-class topology
and transition providers, A* with the region-graph precheck, distance-field
products and caches, the ECS adapter (EnTT-gated), the versioned DeltaFrame
render bridge, compile-gated diagnostics, and the GPU backend interface
(interface only in the current pre-1.0 release). The historical TDD archive
preserves the original
design intent behind each area:

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

The umbrella header `tess/tess.h` exports the dependency-free core surface plus
the configured `TESS_VERSION_MAJOR`, `TESS_VERSION_MINOR`, and
`TESS_VERSION_PATCH` macros and their typed `tess::version` /
`tess::library_version` representation. Optional integrations
that require consumer-provided EnTT or Dear ImGui declarations are
deliberately not included; consumers include those adapter headers explicitly.

[surface.json](surface.json) maps each maintained doc to the public symbol
names it documents; `tools/check_public_surface.py` compares that manifest
against the headers in `TESS_PUBLIC_HEADERS` and the generated version-header
template. It covers public types, aliases, concepts, constants, free functions,
and macros, and rejects both undocumented declarations and stale manifest
entries. The check is a required gate in CI's hooks-backstop job; the companion
`tools/check_public_docs.py` requires Doxygen comments for namespace-scope API
declarations across every installed header, including public declarations
whose definitions live in an implementation header included by an umbrella.
