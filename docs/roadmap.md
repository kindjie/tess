# Scope and roadmap

What is released, what has landed only on `main`, what remains release-gated,
and what tess will not become. The [support policy](support.md) governs
stability; architecture documents describe the current checkout, the
[completion record][completion-plan] preserves the historical v0.5-v0.12
sequence, and the [release process][release-process] owns the path to 1.0.

## Released in v0.12.0

`v0.12.0` provides the following tested machinery. The
[Concepts](architecture/README.md) pages document the current checkout and may
also contain main-only additions identified below: compile-time shapes and
field schemas, dense and sparse residency, queued operations with result
channels, the block kernel layer, topology and the reachability precheck, A*
with movement classes, weighted batches, distance-field products and caches,
the schedule loop, the EnTT, Flecs, and custom-ECS adapters, DeltaFrame render
bridging, the production worker-pool phase executor, the GPU descriptor
interface and optional WebGPU transport, and compile-gated diagnostics.
Resolved regular and provider-composed transitions drive exact paths, fields,
topology, caches, agents, and movement commit across orthogonal,
clearance-preserving diagonal, and axial-hex worlds. `OperationBatch` also carries
typed intent batches and their
version/invalidation policy; cooperative tickets resume budgeted work across
ticks; and exact event streams drive coalesced OnEvent schedule cadences.
Block-resolved lazy pipelines fuse adapters into explicit terminals, while
exact box, Euclidean-radius, and chunk-local span queries emit allocation-free
x-runs. Experimental immediate, FIFO, and queued-coalescing maintenance
backends remain opt-in and are not integrated into storage.
Region graphs also reconstruct shortest coarse region/portal routes and chunk
corridors. Dense weighted multi-goal products are versioned, byte-budgeted,
provider-aware, and reusable across runtime calls through an opt-in policy.
Caller-keyed area indexes derive stable area summaries and adjacency from
region graphs. Deterministic tactical assignment and local move coordination
provide capacity-aware claims, caller-ranked destination reservations, and
coordinate congestion summaries without owning game semantics or steering.
Two movement-commit tiers resolve contention between agents whose routes
were planned independently: joint movement admits a tick's moves together
so agents never stack, with a caller-selected swap policy deciding whether
a mutually blocked pair may exchange tiles, and the opt-in PIBT tier
(priority inheritance with backtracking) additionally lets a blocked agent
yield off its route, which resolves a head-on that the default
swap-forbidding policy leaves blocked.
Versioned world archives persist caller-selected authoritative scalar fields,
stable chunk metadata, and compatibility identities in canonical
little-endian form. Exact loads preflight corruption and compatibility,
surface schema changes as explicit migration requirements, and invalidate
derived products. Optional Dear ImGui helpers inspect dense and sparse world
state and return caller-applied boolean field intents without introducing a
core UI dependency or direct editor mutation.

## Landed on main, not yet released

These changes are available in the current source checkout but not in the
`v0.12.0` release. Consumers of a released package must not assume they exist.
The [unreleased changelog fragments][unreleased-fragments] are the complete
change inventory; the bullets here summarize the release-shaping additions
without maintaining a second exhaustive list.

- The breaking API cleanups documented for the final pre-1.0 release have
  landed. The [1.0 upgrade guide](upgrade-1.0.md) describes the principal
  migration themes; consult the unreleased fragments for every change.
- `World::mark_content_changed` can invalidate version-keyed derived state
  without scheduling dirty-mask work, and path-overlay collection can include
  retained and queue-produced routes whose runtime tickets are no longer
  directly usable.
- Stable maintenance task, budget, result, handle, immediate-execution,
  structural-backend, and external fixed-slot chunk-adapter spellings now live
  under `tess::maintenance`. The adapter exercises dense and sparse residency
  without integrating handles into world storage. Portable evidence found no
  M3 regression but a material Steam Deck regression for dirty-bit scheduling,
  so dirty-bit, FIFO, queued-coalescing, and the virtual scheduler remain
  explicit `tess::experimental` machinery.
- Budgeted-progress benchmark infrastructure, controlled-hardware campaign
  tooling, and further diagnostics and correctness fixes have landed. The
  [budgeted-progress record][budgeted-progress] preserves its staged design
  and acceptance evidence.

Release notes are assembled from the repository's unreleased fragments when a
release is prepared; source presence alone does not make a change released.

## Release-gated next steps

- Publish `v0.13.0` only after the breaking API changes and upgrade guide are
  complete, the stable maintenance API and external chunk adapter land, and
  the exact release commit passes release-mode CI.
- After `v0.13.0`, complete the bounded pathfinding, movement, execution, and
  synthesis dispositions in the
  [v0.13-to-v1.0 execution plan][pre-rc-plan]. Accepted implementations land
  before downstream evaluation; rejection or explicit deferral completes an
  optional experiment.
- Create `v1.0.0-rc.1` only after adding its immutable compatibility snapshot,
  recording a substantial downstream evaluation of the resulting surface,
  assembling release records, and passing release-mode CI on the exact
  candidate commit.
- Publish `v1.0.0` only after the candidate observation period and the final
  exact-commit release checks described in the release process.

These are evidence gates, not promised dates.

## Future and deferred extensions

The foundations named below may be released or landed on `main`, but the
extensions themselves do not exist as supported APIs. Do not build consumer
code that assumes they do.

- **Persistent maintenance**
  ([scheduler TDD][tdd-scheduler], [maintenance addendum][tdd-work]) — fixed
  cadences, dirty/manual/event triggers, exact event streams, and deterministic
  background continuation are released. Stable maintenance handles, immediate
  execution, structural customization, and an external dense-and-sparse chunk
  adapter have landed for `v0.13.0`. FIFO, queued-coalescing, dirty-bit, and
  the virtual scheduler remain experimental; wider derived-system adapters
  and storage-owned handles remain future work.
- **Further spatial query acceleration**
  ([block TDD][tdd-block], [layout addendum][tdd-layout]) — resolved block-lazy
  pipelines and exact box/radius/chunk spans are released. Predicate bitsets,
  summaries, halos, and alternate layout experiments did not meet or have not
  yet been evaluated against their separate promotion gates.
- **Flow, congestion, and influence fields** ([TDD][tdd-flow]) — only
  distance fields, weighted persistent products, nearest-target queries, and
  coarse chunk corridors are released. Today's fallback
  for congestion-aware routing: write congestion from your simulation
  into a cost field and route through a weighted movement class.
- **Additional GPU algorithms** ([TDD][tdd-gpu]) — the descriptor/concept
  layer and optional stable-C-API WebGPU transport are released. Consumers
  still provide algorithm pipelines and bindings, and the CPU stays
  authoritative.
- **Continuous crowd steering** ([project design][tdd-project]) — local
  reservation and congestion arbitration are released, but tess does not
  perform velocity-obstacle steering, formation control, or globally optimal
  multi-agent pathfinding.
- **Sparse backing-store persistence and full editor integration**
  ([project design][tdd-project]) — dense and resident-set world archives
  and bounded optional ImGui substrate tools are released. Durable non-resident
  chunk storage, picking, undo, general reflection, and game-specific meaning
  remain application-owned.
- **External grid benchmark data and the scenario oracle**
  ([TDD][tdd-benchdata]) — a harness-only design for community grid maps
  and scenario optima as opt-in fixtures and calibrated benchmarks. Strict
  parsers, inline fixtures, independent reference search, oracle bounds, and
  opt-in skip/strict behavior are released network-free. External acquisition
  remains gated on documented content rights.

## Out of scope

tess is not a renderer, physics engine, navigation-mesh generator, or
drop-in ECS, and does not intend to become one. It supplies the spatial
substrate; the application owns meaning, entities, and presentation.

[tdd-index]: https://github.com/kindjie/tess/blob/main/docs/tdd/README.md
[completion-plan]: https://github.com/kindjie/tess/blob/main/docs/planning/roadmap-completion.md
[release-process]: https://github.com/kindjie/tess/blob/main/docs/releasing.md
[unreleased-fragments]: https://github.com/kindjie/tess/tree/main/changelog.d
[budgeted-progress]: https://github.com/kindjie/tess/blob/main/docs/planning/budgeted-progress-benchmarks.md
[pre-rc-plan]: https://github.com/kindjie/tess/blob/main/docs/planning/v0.13-to-v1.0-execution-plan.md
[tdd-scheduler]: https://github.com/kindjie/tess/blob/main/docs/tdd/simulation-scheduler.md
[tdd-block]: https://github.com/kindjie/tess/blob/main/docs/tdd/block-kernel-pipeline.md
[tdd-benchdata]: https://github.com/kindjie/tess/blob/main/docs/tdd/grid-benchmark-data-and-scenario-oracle.md
[tdd-flow]: https://github.com/kindjie/tess/blob/main/docs/tdd/flow-distance-fields.md
[tdd-work]: https://github.com/kindjie/tess/blob/main/docs/tdd/tdd_addendum_work_contracts.md
[tdd-gpu]: https://github.com/kindjie/tess/blob/main/docs/tdd/gpu-backend-interface.md
[tdd-project]: https://github.com/kindjie/tess/blob/main/docs/tdd/project-design.md
[tdd-layout]: https://github.com/kindjie/tess/blob/main/docs/tdd/tdd_addendum_tile_layout_bench_takeaways.md
