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
schedule loop, the EnTT, Flecs, and custom-ECS adapters, DeltaFrame render
bridging, the production worker-pool phase executor, the GPU descriptor
interface and optional WebGPU transport, and compile-gated diagnostics.
Resolved regular and
provider-composed transitions drive exact paths, fields, topology, caches,
agents, and movement commit across orthogonal, clearance-preserving diagonal,
and axial-hex worlds. `FrameOps` also carries typed intent batches and their
version/invalidation policy; cooperative tickets resume budgeted work across
ticks; and exact event streams drive coalesced OnEvent schedule cadences.
Block-resolved lazy pipelines fuse adapters into explicit terminals, while
exact box, Euclidean-radius, and chunk-local span queries emit allocation-free
x-runs. Experimental maintenance backends are available for evaluation but
are not integrated into storage because the coalescing prototype failed its
sparse-overhead promotion gate.
Region graphs also reconstruct shortest coarse region/portal routes and chunk
corridors. Dense weighted multi-goal products are versioned, byte-budgeted,
provider-aware, and reusable across runtime calls through an opt-in policy.
Caller-keyed area indexes derive stable area summaries and adjacency from
region graphs. Deterministic tactical assignment and local move coordination
provide capacity-aware claims, caller-ranked destination reservations, and
coordinate congestion summaries without owning game semantics or steering.
Versioned world archives persist caller-selected authoritative scalar fields,
stable chunk metadata, and compatibility identities in canonical
little-endian form. Exact loads preflight corruption and compatibility,
surface schema changes as explicit migration requirements, and invalidate
derived products. Optional Dear ImGui helpers inspect dense and sparse world
state and return caller-applied boolean field intents without introducing a
core UI dependency or direct editor mutation.

## Planned extensions

The foundations named below may exist, but the extensions themselves are not
shipped. Do not build consumer code that assumes these APIs exist until the
item moves to **Shipped**.

- **Persistent maintenance**
  ([scheduler TDD][tdd-scheduler], [maintenance addendum][tdd-work]) — fixed
  cadences, dirty/manual/event triggers, exact event streams, and deterministic
  background continuation shipped. Experimental immediate, FIFO, and
  coalescing backends also shipped for evaluation, but coalescing maintenance
  handles are not integrated into storage.
- **Further spatial query acceleration**
  ([block TDD][tdd-block], [layout addendum][tdd-layout]) — resolved block-lazy
  pipelines and exact box/radius/chunk spans shipped. Predicate bitsets,
  summaries, halos, and alternate layout experiments did not meet or have not
  yet been evaluated against their separate promotion gates.
- **Flow, congestion, and influence fields** ([TDD][tdd-flow]) — only
  distance fields, weighted persistent products, nearest-target queries, and
  coarse chunk corridors shipped. Today's fallback
  for congestion-aware routing: write congestion from your simulation
  into a cost field and route through a weighted movement class.
- **Additional GPU algorithms** ([TDD][tdd-gpu]) — the descriptor/concept
  layer and optional stable-C-API WebGPU transport shipped. Consumers still
  provide algorithm pipelines and bindings, and the CPU stays authoritative.
- **Continuous crowd steering** ([project design][tdd-project]) — local
  reservation and congestion arbitration shipped, but tess does not perform
  velocity-obstacle steering, formation control, or globally optimal
  multi-agent pathfinding.
- **Sparse backing-store persistence and full editor integration**
  ([project design][tdd-project]) — dense and resident-set world archives
  and bounded optional ImGui substrate tools shipped. Durable non-resident
  chunk storage, picking, undo, general reflection, and game-specific meaning
  remain application-owned.
- **External grid benchmark data and the scenario oracle**
  ([TDD][tdd-benchdata]) — a harness-only design for community grid maps
  and scenario optima as opt-in fixtures and calibrated benchmarks. Strict
  parsers, inline fixtures, independent reference search, oracle bounds, and
  opt-in skip/strict behavior shipped network-free. External acquisition
  remains gated on documented content rights.

## Out of scope

tess is not a renderer, physics engine, navigation-mesh generator, or
drop-in ECS, and does not intend to become one. It supplies the spatial
substrate; the application owns meaning, entities, and presentation.

[tdd-index]: https://github.com/kindjie/tess/blob/main/docs/tdd/README.md
[completion-plan]: https://github.com/kindjie/tess/blob/main/docs/planning/roadmap-completion.md
[tdd-scheduler]: https://github.com/kindjie/tess/blob/main/docs/tdd/simulation-scheduler.md
[tdd-block]: https://github.com/kindjie/tess/blob/main/docs/tdd/block-kernel-pipeline.md
[tdd-benchdata]: https://github.com/kindjie/tess/blob/main/docs/tdd/grid-benchmark-data-and-scenario-oracle.md
[tdd-flow]: https://github.com/kindjie/tess/blob/main/docs/tdd/flow-distance-fields.md
[tdd-work]: https://github.com/kindjie/tess/blob/main/docs/tdd/tdd_addendum_work_contracts.md
[tdd-gpu]: https://github.com/kindjie/tess/blob/main/docs/tdd/gpu-backend-interface.md
[tdd-project]: https://github.com/kindjie/tess/blob/main/docs/tdd/project-design.md
[tdd-layout]: https://github.com/kindjie/tess/blob/main/docs/tdd/tdd_addendum_tile_layout_bench_takeaways.md
