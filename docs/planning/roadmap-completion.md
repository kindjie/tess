# Roadmap Completion Plan

Status: active. This is the maintained sequencing and completion record for
the post-v0.4 roadmap. Architecture documents and code remain authoritative
for shipped behavior; archived TDDs provide rationale and acceptance criteria.

## Delivery Rules

- Deliver one test-first vertical slice per pull request.
- Keep incomplete public APIs in `tess::experimental` until their release gate
  passes.
- Preserve the dependency-free CPU core and authoritative deterministic state.
- Treat an evidence-backed rejection as completion for optional experiments.
- Update the roadmap, architecture, decision log, tests, and benchmarks in the
  same slice as behavior changes.

## Release Sequence

| Release | Capability | Status |
| --- | --- | --- |
| v0.5 | Resolved transitions, diagonals, axial hex, special edges | Complete |
| v0.6 | Queued intents, async work, event scheduling | Complete |
| v0.7 | Maintenance, block pipelines, query acceleration | Complete |
| v0.8 | Hierarchical topology and spatial products | Complete |
| v0.9 | Areas, tactical assignment, crowd coordination | Complete |
| v0.10 | Persistence, Flecs adapter, optional ImGui editor tools | Complete |
| v0.11 | Optional WebGPU backend | Complete |
| v0.12 | Consolidation, compatibility, performance, backlog closure | Active |

### Observed v0.12 Release Blocker

The web colony demo can enter a stable partial-arrival state after a narrow
bottleneck is painted, while tick cost rises into tens of milliseconds for
roughly 900 agents. The current lifecycle replans every blocked agent, A*
intentionally ignores occupancy, and a `Found` replan resets retry accounting
before the same occupied next step can fail again. v0.12 must add a seeded
bottleneck regression that proves bounded planning cost and eventual progress
or an explicit terminal outcome; a visually stationary infinite
`Found -> Occupied -> replan` loop is not releasable behavior.

### v0.5 Completion

Orthogonal, diagonal, axial-hex, and provider-composed models now drive exact
paths, reverse fields, multi-goal products, topology, cache identity, runtime
batches, retained agent routes, and movement validation. Provider costs and
revisions are model inputs, compact-cost risk has a compile-time assessment
and explicit runtime overflow status, and the historical orthogonal benchmark
gate remains within its calibrated thresholds.

### v0.6 Completion

`FrameOps` now expresses typed path, nearest-target, field-product, movement,
topology, residency, dirty-mark, and render-delta batches while retaining
planner-visible versions, invalidations, backend eligibility, exactness,
priority, budget, domain, and access policy. Cooperative generation-stamped
tickets expose the complete immediate/pending/ready/failure/cancellation/
supersession/staleness lifecycle and resume in FIFO order under deterministic
item budgets. The schedule adds coalesced OnEvent cadences backed by exact,
bounded tick-stamped event streams, plus a direct Background adapter for
resumable queues. Warm event and continuation paths allocate nothing after
reserve, and scheduler benchmarks cover both additions.

### v0.7 Completion

Block-resolved sources now compose lazy filter, map, flat-map, for-each,
reduce, bounded collection, and explicitly allocating terminals with optional
caller-owned diagnostics. Exact allocation-free box, Euclidean-radius, and
chunk-local span emitters clip and order consistently across top-down,
vertical, 3D, and very wide shapes; 100,000 seeded equivalence cases and local
benchmarks satisfy the historical promotion gate. Immediate, FIFO, and
coalescing derived-maintenance backends pass the correctness experiment in an
optional namespace, but coalescing fails the mandatory sparse-overhead gate
and is therefore not integrated into storage. Predicate bitsets and summaries
remain evidence-backed deferrals rather than speculative schema additions.

### v0.8 Completion

Region graphs now reconstruct deterministic shortest region/portal routes,
ordered unique chunk corridors, and clipped conservative bounds across dense
and sparse resident worlds. Weighted multi-goal distance products run through
the complete resolved transition model, replay exact paths and nearest-target
answers, validate chunk and provider revisions, and share the byte-budgeted
LRU cache. The path runtime can opt into persistent unit or weighted products
for repeated, spatially distributed goals while retaining route-cache and
bounded-field/A* fallbacks. Warm builders and coarse reconstruction allocate
nothing after reserve; benchmarks cover coarse routes, product builds, and
replay.

### v0.9 Completion

Caller-defined area indexes now derive stable summaries and adjacency from
region graphs without owning room meaning. Deterministic greedy tactical
assignment accepts caller feasibility and score policies, respects candidate
capacities, orders scarce claims by priority and stable ID, and allocates
nothing after explicit scratch reservation. Deterministic local move
coordination arbitrates caller-generated options, exposes congestion, and
returns reservations that remain subject to normal movement-intent validation.
It is allocation-free after reserve and resolves a representative 1,000-agent,
4,000-option workload in about 0.36 ms locally. Globally optimal multi-agent
pathfinding and continuous steering remain out of scope.

### v0.10 Completion

Versioned world archives now encode caller-selected authoritative scalar
fields in canonical little-endian chunk order with whole-body checksums.
Inspection and loading classify shape, lattice, key-layout, residency, schema,
field, capacity, corruption, and explicit migration boundaries before target
mutation. Loaded chunks invalidate derived state instead of restoring caches.
Sparse archives cover the resident working set because sparse worlds do not
yet own a non-resident backing store. The optional Flecs 4.1 adapter now
provides the same deterministic two-phase path-agent and lifecycle contract as
EnTT while retaining full generation-bearing entity IDs. Its query is
persistent, warm ticks allocate nothing, and the core remains dependency-free.
Optional ImGui helpers now expose dense/sparse world overview, selected-chunk
inspection, and caller-applied boolean field edit intents. The helpers accept
const worlds and never load residency or directly mutate state. Picking,
undo/redo, generalized reflection, and game-specific editor behavior remain
application-owned, preserving the historical no-editor-framework boundary.

### v0.11 Completion

The dependency-free descriptor concept now has an independently gated
`WebGpuBackend` built on the stable WebGPU C API. Explicit field mirrors and
generation-bearing products bind consumer-owned compute pipelines without
inventing a shader ABI. Dispatches validate resources before submission;
bounded per-request staging buffers deliver asynchronous summaries safely
even if the backend object is destroyed. Disabled configuration and device
loss refuse work for CPU fallback. The exact pinned Emdawnwebgpu browser build
executes an upload/compute/readback smoke path where WebGPU is available and
reports unsupported environments distinctly from execution failures.

### Cross-cutting benchmark harness

The external grid benchmark design now has a network-free first phase: a
strict map/scenario parser, compile-time-shape loader, independent orthogonal
and `RequireBothClear` reference search, derived 128/181 oracle intervals, a
schema-pinned empty manifest, and an opt-in driver that skips locally but
fails in required-data mode. Its cache-verifier readiness gate is deliberately
false. External manifest entries, fetching, cache verification, and
data-backed CI remain gated until individual-content rights are documented.

## Cross-Cutting Acceptance

- Serial and parallel authoritative outputs match at synchronization points.
- Warm path, field, planner, executor, and maintenance hot paths do not
  allocate after explicit reserve or setup.
- Dense, sparse, top-down, vertical, 3D, diagonal, and hex configurations use
  the same public model where applicable.
- CPU-only, no-ECS, no-ImGui, and no-GPU consumers keep compiling without
  optional dependencies.
- Performance promotions use repository-owned benchmarks and the calibration
  process in `benchmark-plan.md`; rejected experiments are recorded in
  `optimization-log.md`.

## Explicit Non-Goals

Runtime-sized or unbounded worlds, rendering, physics, navigation-mesh
generation, continuous steering, game-specific AI meaning, and a standalone
editor remain outside tess.
