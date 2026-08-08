# Changelog

Notable, release-facing changes to `tess`. All `0.x` releases are
pre-stable: minor versions may change public APIs and data layouts
without compatibility shims. The format loosely follows
[Keep a Changelog](https://keepachangelog.com/); design-level decisions
and their rationale are recorded separately in
[`docs/decisions/CHANGELOG.md`](docs/decisions/CHANGELOG.md).

## [Unreleased]

### Added

- Opt-in scoped staleness for the unit route cache:
  `PathRuntimeCachePolicy::unit_route_staleness =
  UnitRouteStaleness::ScopedFeasible` keeps cached routes whose chunk
  footprint an edit did not touch, instead of dropping the whole cache on
  any world change. Surviving routes are legal with a truthful cost and
  were optimal when stored; an edit elsewhere that opens a shortcut can
  leave a served route suboptimal until it is retired (blocking-only edit
  sequences preserve optimality). Applies to unit-cost movement without
  special transitions on dense worlds; other models and sparse worlds keep
  today's exact whole-cache behavior. The default is unchanged. Direct
  `RouteCacheScratch` users get the same machinery via `set_staleness` and
  `refresh_if_world_changed`. On the profiled steady off-route edit shape,
  the world-edit agent tick drops from ~415 us to ~130 us on the
  calibration machine; two new benchmark cells pin the survival steady
  state and the forced retire-every-tick worst case.

### Performance

- Per-frame queued planning is measured. `queued/plan_frame_256` and
  `queued/plan_frame_4096` time `plan_operations` plus
  `plan_parallel_execution_phases`, which nothing timed before: every
  other queued benchmark plans outside its measured loop, and the one
  in-loop planner call plans a single operation. Two sizes are registered
  so growth reads as a shape rather than a point. First readings, on an
  Apple M3 Max: **59.6 us at 256 operations and 23.4 ms at 4096** — 16x the
  operations for 392x the time. No fix is in this change; the measurement
  comes first so the fix has before-and-after evidence.
- Field-product cache scanning is measured against resident entry count.
  `fields/cache_scan_entries_8` and `_128` hold per-store work identical —
  same world, same goal cardinality, same product build — and differ only
  in how many entries are resident when the cache's linear scans run. A
  miss-and-store walks three of them (lookup, the store's existing-key
  scan, then eviction), all linear in entry count, so the delta is their
  aggregate rather than eviction alone. The family's other cache
  benchmarks hold about two entries, so those scans never had more than
  two candidates to compare. The 128-entry variant is registered as a
  paired sentinel: the scans are only about 7% of each reading, so the
  bootstrap ceiling gives trend visibility rather than a complexity gate,
  and the paired run's relative effect floor is what can actually see a
  scan regression.

### Fixed

- `EventStream` retirement no longer wraps a flow accountant's outstanding
  count. It subtracted its batch size directly where every other
  terminalization site routes through the zero-floored
  `record_left_outstanding`, so a shared accountant whose other flow
  terminalized first, or a `reset()` between publish and retire, drove the
  unsigned counter to about 2^64 and took the inventory and retention
  identities with it. Diagnostics only; simulation results were unaffected.
- `save_world_archive` writes `lattice_version` at a fixed width, and a
  lattice whose version cannot fit the header's 32-bit field is now a
  compile error rather than a file that saves `Ok` and never loads. The
  constant's own type decided the field width, and `LatticeType` requires
  only convertibility to `uint32`, so a custom lattice declaring a
  `uint64` version produced a header four bytes too long. Casting the
  write alone was not enough: a version above the 32-bit range would still
  have saved, because the truncated stored value can never equal the
  full-width trait the load compares it against. Both shipped lattices are
  unaffected.
- The A* fast paths saturate their route-cost arithmetic instead of
  wrapping, matching `best_chunk_portal`. Reaching the wrap needs distances
  beyond 2^32 tiles, so this is symmetry rather than a live defect.
- `DeltaCollector` sizes its coalescing table from the realized entity
  capacity rather than the requested one. Both probe loops rely on a null
  slot to terminate, and `reserve(n)` only guarantees `capacity() >= n`, so
  an implementation that over-allocated could fill the table and spin
  forever. No shipped standard library does; the guard is unconditional now
  rather than resting on that.
- A transition provider that emits a transition whose source lies outside
  the chunk it was asked about no longer grows the portal set without
  bound. Incremental removal keys on the source's chunk, so such a portal
  was never erased while every update touching that chunk appended it
  again, and incremental output diverged from a full rebuild. Previously
  only asserted, so the divergence was live precisely in builds with
  assertions compiled out; the transition is now dropped in every build.
- `ResolvedTransitionModel::for_each_dependency_chunk` rejects an
  out-of-world origin instead of emitting an out-of-range `ChunkKey`,
  matching the forward and reverse probes.
- PIBT priority inheritance no longer displaces an agent that has arrived
  or ended at `Unreachable`. The priority loop skips such agents and the
  apply pass checks the same condition before touching a stay-put agent,
  but inheritance reached them through the occupant path: passing traffic
  shoved them off their tile and rewrote them to `Blocked`, restarting a
  lifecycle documented as terminal. A second failure for one admission
  then broke the flow-accounting retention identity permanently. They are
  now treated like an agent standing on impassable terrain — the tile is
  claimed so later deciders are turned away, and the inheriting agent
  backtracks to its next candidate.
- A goalless agent standing on the origin tile no longer registers an
  arrival. `clear_path_agent_goal` zeroes `goal`, so the comparison could
  succeed for a journey that was never admitted, inflating `completed`.
- A `DirtyObservation` taken before a sparse chunk was evicted can no longer
  clear a dirty mark made after it was reloaded. Reloading restarts a
  chunk's `version` at zero, so version equality alone could match across
  two different residency intervals and erase work the observation never
  saw — the one thing the observation protocol promises cannot happen. The
  observation now carries the residency generation and a stale one is
  refused. Always-resident worlds are unaffected.
- `SparseWorld::ensure_resident()` returns an invalid handle for a key
  outside the bounded shape instead of writing past its slot table. It is
  the only residency entry point with no checked counterpart, and the
  directory's direct-slot mode indexes by key, so an out-of-range key wrote
  out of bounds wherever assertions were compiled out. It now behaves like
  `try_chunk()` and `try_meta()`, which already refused such keys.
- `dirty_chunk_domain()` and `active_chunk_domain()` yield chunk keys in
  ascending order on sparse worlds. They previously inherited residency
  order, which depends on load and eviction history rather than on world
  content, so identical content could iterate differently between runs and
  a non-commutative block kernel could produce different results. The
  underlying scans stay unordered; only the domain builders, which already
  allocate a vector, sort.
- Continuous-integration compiler caches no longer collide. Cache restore
  keys match by prefix, so the `dev` namespace also matched the sanitizer,
  cppcheck, and clang-tidy namespaces and restored whichever was written
  most recently. Every namespace is now terminated, and each cache-using
  job reports its own hit rate so a cold rebuild is visible in its log.
- Changes under `include/tess/ops/` select the paired benchmark run again.
  The directory was excluded as nanosecond-scale, which is true of its
  queued and scheduler families but not of the pool executor it also owns,
  whose benchmarks run at millisecond scale.
- The gated benchmark family list has one home. It was maintained by hand
  in the workflow, in CMake, in the contributor guide, and in a test that
  covered five of eighteen families, so a new family could ship a manifest
  and a target and still never gate. CMake now derives the set from the
  threshold targets it defines.
- The advisory clang-tidy profile pins its major version, matching the
  required gate. It installed the unversioned package, so the runner image
  decided which analyser ran and no pull request would have noticed a
  change.
- Every continuous-integration job declares a timeout. They inherited the
  360-minute default, which a hung job would spend at up to ten times the
  base billing rate on the macOS runners.

### Documentation

- Documented that a raw field write does not make a region graph stale.
  Only `mark_topology_dirty` and `mark_topology_rebuilt` advance the
  topology version that freshness compares, so editing a field a movement
  class or its provider reads — opening a wall, placing a stair — leaves a
  built graph reporting fresh, and `precheck_path` can then return a
  definitive, wrong `Unreachable` that makes a caller skip a search which
  would have succeeded. The obligation is now stated on `precheck_path`,
  on `StairTransitions`, and in the topology architecture note.
  `precheck.h` previously said a graph that no longer matches the world
  reports `GraphStale`, which overstated what the check can detect.
- Corrected the robotics use case: `examples/stairs_3d.cc` demolishes its
  stair with a direct field write and hands the affected chunk key to
  `update_region_graph`. It was described as a queued edit that marks the
  region dirty, which is machinery that example does not use.

## [0.12.0] - 2026-08-05

### Added

- Exception-free consumer builds. Clang-family and GCC consumers may compile
  every public header, aggregate, and example with `-fno-exceptions`, and
  native MSVC consumers with `/EHs-c-` and `_HAS_EXCEPTIONS=0`. The installed
  `tess::tess` target stays neutral; `TESS_HAS_EXCEPTIONS` and
  `tess::has_exceptions` report the compiler mode and cannot be overridden.
  Every translation unit in a program must use the same mode. See
  [exception-free builds](docs/architecture/no-exceptions.md).
- Non-throwing capacity entry points for exception-free callers:
  `BlockScratch::reserve_bytes_checked`,
  `WeightedPortalSegmentCache::reserve_segments_checked`,
  `reserve_path_nodes_checked`, and `ClassView::store_checked`, reporting
  through `ReserveStatus` and `PortalSegmentStoreStatus`.
- Explicit no-throw execution aliases `NoThrowWorkerPoolPhaseExecutor` and
  `NoThrowScopedThreadPhaseExecutor`, plus the `ScheduleNoThrowTaskFn` erased
  signature, so an explicitly `noexcept` callback keeps that property through
  the queued, result-channel, schedule, and auto-exec adapters.
- A Conan recipe and a vcpkg checkout overlay alongside the existing
  `FetchContent` and installed-package paths. See
  [packaging](docs/packaging.md).
- Per-tick timing and allocation attribution. Diagnostics-enabled schedules
  time the complete tick and each executed task under its static label,
  duration records carry inclusive allocation and deallocation byte deltas,
  and snapshots retain the newest trace records with a dropped count.
  Diagnostics-off builds retain no timer or attribution code.
- A resolved transition model shared by exact paths, reverse fields,
  multi-goal products, topology, caches, path agents, and movement commit,
  including clearance-preserving diagonal steps, axial-hex adjacency, and
  provider-composed special edges.
- Compile-time compact-cost range assessment and explicit runtime
  `CostOverflow` results.
- Typed queued intents, cooperative generation-stamped async tickets, bounded
  exact event streams, and event/background scheduling adapters.
- Lazy block pipelines and exact allocation-free box, radius, and chunk-span
  queries.
- Deterministic coarse region/portal routes, persistent weighted field
  products, caller-keyed area indexes, tactical assignment, and local move
  coordination.
- Versioned authoritative world archives, an optional Flecs adapter, and
  bounded optional Dear ImGui world inspection/edit-intent helpers.
- An optional stable-C-API WebGPU transport with generation-bearing resources
  and bounded asynchronous readback.
- A network-free external-grid parser and independent oracle harness; external
  corpus acquisition remains gated on documented content rights.

### Changed

- Path results now report their fixed-point cost scale; provider type and
  revision participate in persistent path-product and cache identity.
- **Behavior change:** `collect_planned_dirty` and both partition-collecting
  `merge_planned_dirty` overloads no longer throw `std::length_error` on a
  record-count overflow. They now return the new
  `PlannedDirtyCollectStatus::CapacityExceeded` and
  `PlannedDirtyMergeStatus::CapacityExceeded` values instead, in
  exception-enabled builds as well as exception-free ones. Callers that
  relied on the exception must check the returned status; exhaustive
  `switch` statements over either enum need the new value. `AutoExecTask`
  absorbs the status internally and still publishes every started
  callback's dirty metadata through its allocation-free fallback merge, so
  its observable result is unchanged.
- The consolidated public surface is versioned and released as `v0.12.0`.

### Fixed

- Occupancy-blocked path agents retry retained steps without repeated
  occupancy-blind searches, stop after a bounded retry budget, and surface an
  explicit terminal outcome. The colony demo reports those outcomes.
- Special-transition field products preserve provider costs, transition
  enumeration propagates callback failures, and zero-step agent ticks preserve
  blocked-retry budgets.
- Archive loads invalidate pre-load cache identities, area-index validation is
  constant time, and reentrant queued-work mutation is rejected safely.
- The external-grid harness now parses scenario lengths on Apple libc++ and
  rejects incompatible required-data options before probing the toolchain.
- Persistence decoding, checksum handling, and field validation now compile
  cleanly across the supported GCC, Clang, MSVC, and cppcheck gates.
- The cppcheck gate now bypasses cppcheck 2.21 template-simplifier crashes
  while retaining product-header analysis and compiler test coverage.

### Performance

- Every literal benchmark in a threshold-gated family is covered by a
  calibrated or explicitly labeled bootstrap ceiling; newly covered
  resolved-transition, weighted-product,
  coarse-topology, area-index, and Flecs workloads close the prior gate gaps.
- Default orthogonal unit routes, fields, and product replays retain their
  direct specialized paths while other lattices, step policies, and providers
  use the resolved transition model.
- Indexed axis-neighbor iteration remains inline in hot reconstruction loops,
  and bounded weighted floods hoist per-node bucket work out of their
  per-neighbor loop.
- Fully covered sparse worlds bypass residency hashing on the storage read
  path.
- Default orthogonal distance-field products capture dependencies at
  chunk-frontier level again instead of enumerating exact transitions per
  reached tile, undoing a v0.12 build/store regression.
- The serial-versus-pool dispatch crossover is measured and published rather
  than estimated; the pool's dispatcher no longer shares a CPU with a worker.

## [0.4.0] - 2026-07-20

### Added

- Curated `<tess/pathfinding.h>` and `<tess/simulation.h>` facade headers;
  the existing `<tess/tess.h>` compatibility umbrella remains available.
- A compiled quickstart, tracked installed-package and `FetchContent`
  consumers, and source-backed documentation snippets enforced in CI.
- CI verification that the quickstart's documented output matches the
  compiled binary.
- A strict MkDocs site deployed through GitHub Pages, plus a single-threaded
  interactive pathfinding example compiled with Emscripten 6.0.3.
- Support, security, and structured issue-reporting metadata, a Contributor
  Covenant code of conduct, and weekly Dependabot updates for GitHub Actions
  and pip dependencies.

### Changed

- Package metadata and maintained documentation now report `0.4.0`
  consistently.
- The README now leads with fit and non-fit guidance, a complete runnable
  program, dependency-free example commands, and explicit install-prefix and
  `FetchContent` instructions.

## [0.3.0] - 2026-07-17

### Changed

- BREAKING pre-release hardening of the queued-operation and path-cache
  surfaces: `PlannedOperation` gets checked, immutable construction with
  a world-shape stamp; `ExecutionPhase` becomes a planner-issued,
  generation-stamped capability so hand-built or stale phases cannot
  bypass parallel ownership checks; deferred dirty recording and merge
  return explicit failure results and reject cross-world use; portal
  segment construction and compaction commit transactionally, cache
  budget reductions apply immediately, and result hooks are `noexcept`.
- Version metadata now has one CMake authority that generates the
  installed `tess/version.h`; dependency acquisition is pinned by
  default with hash-verified tooling.
- CMake floor lowered to 3.25 (3.28 and newer keep module-scan
  suppression and fetched-dependency hygiene), and the project declares
  the `3.25...3.28` policy range.
- README restructured as a user-facing overview with a features list, a
  quickstart, and measured performance figures; contributor material
  moved to `CONTRIBUTING.md`.
- Docs indexes lead with maintained material; the TDD archive and
  planning records are marked historical.

### Added

- A `consumer` CMake preset: headers-only configure for installing the
  library with no tests, examples, benchmarks, warnings-as-errors, or
  network fetches.
- An opt-in `tess_docs` Doxygen target (`TESS_BUILD_DOCS=ON`) generating
  a local HTML API reference.
- Top-level `CONTRIBUTING.md` (developer workflow, quality gates,
  benchmark policy) and this `CHANGELOG.md`.
- `docs/getting-started.md`: a tutorial from shapes and schemas to the
  schedule loop and render bridge.
- GitHub Releases published for the existing `v0.1.0` and `v0.2.0` tags.
- Sparse local topology reports `MissingChunk`; stateful transition
  providers expose a monotonic revision.

### Fixed

- Deterministic allocation-failure testing reports itself unavailable
  (and stays inert) under MSVC checked iterators instead of terminating;
  Windows keeps failure coverage in Release.
- Cross-platform warning debt cleared across GCC, Clang, AppleClang, and
  MSVC.

## [0.2.0] - 2026-07-12

### Changed

- ChunkMeta hot/cold SoA split (M5): flag words and dirty bounds moved
  to world-owned columns with new `dirty_flags`, `active_flags`, and
  `dirty_bounds` accessors. Breaking versus the undocumented struct
  layout; minor bump by decision.

### Added

- Per-agent pathing dirt: `PathSubmitScope` plus `PathAgentRoutes`
  retained routes, so one goal re-arm no longer replans the whole batch
  (4.2x on the goal-churn tick benchmark).

### Performance

- The audit remediation stack: de-elided benchmark gates, batch
  grouping and settle-target floods (~118x near-goal), scheduler and
  planner overhead cuts, worker-pool claiming (~2x), and intrusive LRU
  eviction (3.5x).

## [0.1.0] - 2026-07-11

### Added

- The initial pre-stable surface, complete across milestones M0-M15:
  constexpr shapes with one model for 2D, vertical 2D, and 3D;
  chunk-local SoA storage with sparse residency; queued operations with
  result channels and write-policy enforcement; the schedule with
  cadences, budgets, auto-exec, and a selectable parallel phase
  executor; movement classes with per-class topology, transition
  providers, and the region-graph precheck; A* and weighted routing
  with route and field-product caches; distance-field products and the
  byte-budgeted cache; the ECS adapter (EnTT-gated); the versioned
  DeltaFrame render bridge; compile-gated diagnostics with ImGui
  panels; and the GPU backend interface (interface only).
