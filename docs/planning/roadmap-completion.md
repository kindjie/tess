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
| v0.7 | Maintenance, block pipelines, query acceleration | Active |
| v0.8 | Hierarchical topology and spatial products | Planned |
| v0.9 | Areas, tactical assignment, crowd coordination | Planned |
| v0.10 | Persistence, Flecs adapter, optional ImGui editor tools | Planned |
| v0.11 | Optional WebGPU backend | Planned |
| v0.12 | Consolidation, compatibility, performance, backlog closure | Planned |

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
