# Design Changelog

Records meaningful design changes from the original TDDs.

## Template

```md
## YYYY-MM-DD - Title

- Changed:
- Reason:
- Affected docs:
- Affected code:
```

## 2026-07-10 - StairTransitions vertical provider (M6, S5 slice 8)

- Added: `StairTransitions<StairTag>` in `topology/transition_provider.h` --
  an integral field holds a `StairDirection`, marking the tile as the foot
  of a stair whose landing is one step sideways and one z-level up. The
  offset form is deliberate: two stacked passable tiles are already six-axis
  adjacent, so only an offset stair adds connectivity. Both directions are
  emitted, each from the chunk owning its origin tile (down from the
  landing's chunk -- the foot's chunk or its +z face neighbor), keeping
  incremental re-derivation exact. Endpoint traversability stays a
  movement-class question (label filter), so stair edges are per-class. A
  landing that would cross two chunk boundaries at once (sideways off the
  x/y edge AND up off the top z layer) violates the face-neighbor contract
  and contributes nothing, documented as the placement limit. On sparse
  worlds a foot in a non-resident chunk needs no special handling: the
  landing tile's own -z boundary exit already marks its region as reaching
  missing topology.
- Reason: S5 slice 8 -- the concrete vertical provider closing M6's
  stairs/ladders deliverable on the S5.7 contract, with no byte-identity
  shield (new code), so connectivity, per-class filtering, incremental
  equality, and the diagonal limit are all pinned by property tests.
- Affected docs: `architecture/topology.md`, `architecture/surface.json`,
  `tests/AGENTS.md`.
- Affected code: `topology/transition_provider.h`;
  `tests/tess_topology_movement_test.cc` (two-level fixture, both-direction
  reachability, Builder-only stair, incremental == full across stair edits,
  diagonal skip, same-chunk landing).

## 2026-07-10 - Transition providers for the region graph (M6, S5 slice 7)

- Added: `include/tess/topology/transition_provider.h` -- the
  `TransitionProviderFor<P, World>` concept and the `AdjacentTransitions`
  default. A provider contributes EXTRA directed tile-to-tile transitions
  (stairs, ladders) enumerated once per chunk; `build_region_graph` /
  `update_region_graph` take an optional trailing provider and append one
  directed `RegionPortal` per transition whose endpoints both resolve to
  labeled regions (provider edges are automatically per-class). The landing
  tile must stay within the origin chunk or a face neighbor -- the exact
  range incremental updates re-derive -- asserted in debug builds. The
  provider TYPE is stamped on the graph beside the movement class
  (`matches_provider`); an update with a different provider type falls back
  to a full rebuild. On sparse worlds a transition landing in a non-resident
  chunk marks its origin region as reaching missing topology, so reachability
  degrades to Indeterminate, never a wrong Unreachable.
- Reason: S5 slice 7 -- M6's special-transitions contract. The default
  provider keeps every existing build byte-identical (pinned by test) while
  giving the vertical stair provider (next slice) a sound, incremental-safe
  extension point.
- Affected docs: `architecture/topology.md`, `architecture/surface.json`,
  `tests/AGENTS.md`.
- Affected code: new `topology/transition_provider.h`;
  `topology/topology.h` (provider stamp, per-chunk provider portals,
  reaches-missing pass), `tess.h`, `CMakeLists.txt`;
  `tests/tess_topology_movement_test.cc` (default identity, bridge portals,
  incremental == full, provider-mismatch rebuild, sparse Indeterminate).

## 2026-07-09 - Class-aware agent tick and the runtime class binding (M6, S5 slice 6)

- Changed: the agent pipeline speaks classes end to end.
  `process_unit_path_agents`, `advance_path_agents_with_movement`,
  `tick_unit_path_agents[_with_movement]`, and
  `PathRequestRuntime::process_unit_cached` take `ClassOrTag`;
  `process_weighted_path_agents`, `tick_weighted_path_agents[_with_movement]`,
  and `process_weighted_batch` gain `<World, Class, MaxCost>` forms in which
  ONE movement class drives the search, the precheck, and commit validation,
  while the legacy `<PassableTag, CostTag>` overloads keep the historical
  semantics (LegacyWeighted search; precheck on passability only, matching
  tag-built graphs). `PathRequestRuntime` now binds itself to the movement
  class of each unit process call: the unit route cache keys entries on
  (start, goal) plus a world-version fingerprint and nothing on the class, so
  a rebind clears the unit caches (correct even on misuse) and counts in the
  new `PathRuntimeStats::class_cache_invalidations`; one runtime per
  (world, class) is the PERF contract, not a correctness precondition.
- Reason: S5 slice 6 -- without the binding, a runtime reused across classes
  would serve one class's cached route to another (same start/goal, same
  world version, different passability), the exact cross-class collision the
  graph stamp closes for prechecks. Pinned by test: a Walker-cached 21-step
  detour is never served to a Builder asking the same (start, goal), which
  gets its 7-step route through the wall instead.
- Affected docs: `architecture/path.md`, `architecture/simulation.md`,
  `tests/AGENTS.md`.
- Affected code: `path/path_runtime.h` (class binding, weighted impl split),
  `sim/path_agent.h`, `sim/path_agent_tick.h`;
  `tests/tess_path_movement_class_test.cc` (stale-route regression,
  per-class tick divergence with zero movement rejections).

## 2026-07-09 - Class-aware movement commit validation (M6, S5 slice 5)

- Changed: `validate_movement_intent` / `commit_movement_intent` take a
  movement class OR a raw passable tag (`ClassOrTag`, normalized exactly as
  in astar_path) instead of a bare `PassableTag`. Each endpoint's passability
  is evaluated on its own resolved page -- from and to may live on different
  pages -- replacing the coord-scope `field<PassableTag>` point reads; the
  identity class performs the same resolve+field reads the legacy code did.
- Reason: S5 slice 5 -- plan == commit. A* (slice 2) and the region graph
  (slice 3) already speak the class vocabulary; commit validation was the
  remaining seam still hard-wired to a single global passability, which would
  let a Builder plan a step through a construction site and then have the
  commit reject it. Pinned by test: every step weighted A* accepts for a
  class validates as Moved for that same class, and BlockedFrom/BlockedTo are
  per class on both endpoints.
- Affected docs: `architecture/simulation.md`, `tests/AGENTS.md`.
- Affected code: `sim/movement.h`;
  `tests/tess_path_movement_class_test.cc` (plan==commit property,
  per-class block statuses).

## 2026-07-09 - Precheck class agreement through the graph stamp (M6, S5 slice 4)

- Changed: `precheck_path<ClassOrTag>(graph, world, start, goal, scratch)` --
  the movement class the search uses is now the explicit first template
  argument, and the gate checks `is_region_graph_fresh_for<ClassOrTag>`
  instead of the classless freshness: a graph labeled for a different
  movement class (or predating any stamp) reports `GraphStale` and degrades
  to running A*. `PathRequestRuntime::precheck_prepass<ClassOrTag>` threads
  the class from `process_unit_cached` / `process_weighted_batch` (the
  weighted batch prechecks on PASSABILITY only, matching the legacy weighted
  asymmetry it searches with).
- Reason: S5 slice 4 -- closes the documented precondition hole: the graph
  type encodes only residency, so before the stamp a graph built over a
  different passability compiled fine and its definitive `Unreachable` could
  prune a route the search's own class could walk (the one way the gate could
  turn a solvable query into a wrong failure). That agreement is now enforced
  at runtime, not delegated to the caller.
- Affected docs: `architecture/path.md`, `tests/AGENTS.md`.
- Affected code: `path/precheck.h`, `path/path_runtime.h`,
  `bench/tess_topology_bench.cc` (explicit class argument);
  `tests/tess_path_precheck_test.cc` (wrong-class -> GraphStale, per-class
  rule-out), `tests/tess_path_precheck_runtime_test.cc` (Builder falls back
  to A* under a walker-stamped graph, Builder-stamped graph rules out without
  searching).

## 2026-07-09 - Per-class region labeling and the graph class stamp (M6, S5 slice 3)

- Changed: `build_local_chunk_topology`, `build_region_graph`, and
  `update_region_graph` take a movement class OR a raw passable tag. The
  identity class floods the raw `field_span` exactly as the legacy single-tag
  build did (byte-identical labels and codegen, pinned by test); a composed
  class evaluates its predicate on the resolved page per tile. Portal pairing
  is untouched -- it queries labels, so per-class labels yield per-class
  portals automatically. `RegionGraphT` gains a movement-class stamp
  (`built_class_`, a `core/tag_identity.h` token of the NORMALIZED class)
  recorded at build time and mirrored on `matches_shape`: a stamp mismatch in
  `update_region_graph` forces a full rebuild with the requested class's
  labels, the public `matches_class<ClassOrTag>()` reports the binding, and
  the new `is_region_graph_fresh_for<ClassOrTag>(world, graph)` is the
  class-aware freshness form (later slices route precheck through it so a
  wrong-class graph degrades to GraphStale, never a wrong Unreachable).
- Reason: S5 slice 3 -- the same vocabulary that drives search (slice 2) now
  drives labeling, so a Walker graph and a Builder graph over one world are
  first-class. The stamp closes the documented precheck precondition gap: the
  graph type encodes neither shape nor class, so binding both at build time is
  what keeps a definitive Unreachable trustworthy.
- Affected docs: `architecture/topology.md`, `architecture/surface.json`,
  `tests/AGENTS.md`.
- Affected code: `topology/topology.h`; new
  `tests/tess_topology_movement_test.cc` (identity byte-identity,
  Walker/Builder divergence + Builder-only bridge, per-class incremental ==
  full, stamp-mismatch rebuild, per-class freshness, alloc-free warm
  relabel), `tests/CMakeLists.txt`; `bench/tess_topology_bench.cc` +
  `bench/thresholds/topology.json` (composed-class 512x512 labeling gate;
  measured on par with the raw scan locally, bootstrap threshold 3x the
  identity gate pending CI calibration).

## 2026-07-09 - Movement classes through the A* leaves and weighted cores (M6, S5 slice 2)

- Changed: the pathfinding passability/cost leaves (`detail::is_passable`,
  `is_passable_index`, `tile_entry_cost_index`) now evaluate a movement class
  at the resolved (page, tile) seam. The passability leaves accept a class OR
  a raw tag (normalized through `movement_class_of`, so every legacy
  `<World, Tag>` call site compiles unchanged and stays byte-identical via the
  `WalkableField` identity); the cost leaf requires an actual class, because a
  raw tag would normalize to the unit-cost identity and silently discard a
  legacy `CostTag`. The weighted family -- `weighted_astar_path`,
  `build_weighted_distance_field`, `build_weighted_distance_field_in_box`,
  `build_bounded_weighted_distance_field`, `weighted_distance_field_path`,
  `weighted_path_batch` -- gained single-`Class` cores; the historical
  `<PassableTag, CostTag>` overloads remain as thin forwarders through
  `movement::LegacyWeighted` (exact semantics, including the cost-agnostic
  passability asymmetry). `tag_identity` hoisted from a private
  `FieldProductCache` member to `include/tess/core/tag_identity.h`
  (`tess::detail`) so later slices can stamp graphs and runtimes with a class
  identity. Diagnostics placement is unchanged (`path_cost_read` in the cost
  leaf, `path_passability_check` at callers), so counters do not drift.
- Reason: S5 slice 2 -- one vocabulary must drive search so per-class region
  labeling (slice 3) and precheck/commit agreement (slices 4-5) can share it.
  Fusing the pair into one class also removes the latent trap of mixing a
  passability tag and a cost tag from different logical classes. The
  route-product/portal-route family and the path runtime still carry tag
  pairs and forward through the legacy overloads; they convert with the
  runtime in slice 6.
- Affected docs: `architecture/path.md`, `tests/AGENTS.md`; changelog entries
  from 2026-07-06 moved to `CHANGELOG-archive.md` (token cap).
- Affected code: `path/path.h`, `path/detail/astar.h`,
  `path/detail/weighted_batch.h`, `path/distance_field_box.h`,
  `path/field_product_cache.h`, new `core/tag_identity.h`, `CMakeLists.txt`;
  new `tests/tess_path_movement_class_test.cc` (identity and LegacyWeighted
  node-for-node equivalence, Walker/Builder divergence, sparse missing-chunk
  contract), `tests/CMakeLists.txt`.

## 2026-07-09 - Movement vocabulary DSL (M6, S5 slice 1)

- Added: `include/tess/topology/movement_class.h` (namespace `tess::movement`) --
  a compile-time DSL where a `MovementClass<PassExpr, CostExpr>` fuses a
  passability predicate and an entry-cost expression composed from typed-field
  leaves. Boolean terms `Field`/`NotZero`/`Not`/`AllOf`/`AnyOf`; cost
  expressions `UnitCost`/`ConstantCost`/`FieldCost`/`SelectCost` with a
  `normalize_cost` byte-exact to the weighted A* leaf; identity classes
  `WalkableField`/`WalkableCostField`/`LegacyWeighted`; the `MovementClassFor`
  concept and `movement_class_of` tag/class normalization.
- Reason: first slice of the M6 movement-vocabulary close (S5). Labeling,
  pathfinding, and commit validation currently each bake in a single global
  passability (a raw `PassableTag`); this vocabulary is the shared, allocation-
  free, constexpr foundation later slices thread through region labeling,
  precheck, the weighted agent tick, and `sim/movement.h` so plan and commit
  agree. Leaves read the constexpr `ChunkPage::field<Tag>` at the (page, tile)
  seam because world-scope accessors are not constexpr, and the whole predicate
  inlines to the same ops a hand-written cast emits. `WalkableField` is a
  distinct struct (not an alias) carrying the raw tag + a `field_span` fast path
  so the identity class stays byte-identical to today's single-field scan. Pure
  vocabulary; no wiring yet.
- Affected docs: `architecture/topology.md`, `architecture/surface.json`.
- Affected code: new `topology/movement_class.h`, `tess.h`, `CMakeLists.txt`;
  new `tests/tess_movement_class_test.cc`, `tests/CMakeLists.txt`,
  `tests/AGENTS.md`.

## 2026-07-09 - Path product/cache invalidation contracts (audit-2 W-A)

- Changed: (1) Route, portal-route, and distance-field products treat an empty
  dependency set as invalid, and every builder now captures dependencies for
  non-Found results (`capture_all`), so a cached failure can never replay
  after a world edit that might change the answer. (2)
  `build_distance_field_product` additionally depends on the face neighbors
  of every touched chunk, covering the blocked frontier: an edit that opens a
  fully-sealed chunk now invalidates the product. (3)
  `RouteCacheScratch::store` skips a single route larger than the node cap
  (new `stats().oversized_skips`) instead of invalidating resident entries
  and storing it anyway, and cap value 0 now disables storage rather than
  meaning "unlimited", matching the portal segment cache. (4)
  `build_weighted_portal_route_product` accepts its own `waypoints()` span
  (rebuild-from-own-product no longer reads a cleared vector). (5)
  `PathTicket.generation` widened to 64 bits so retained tickets cannot alias
  across generation wraparound. (6) Documented: the caller-owned staleness
  contract of `cached_astar_path`, the one-cache/runtime-per-world identity
  contract, and the chunk-portal route builder's heuristic (non-authoritative)
  NoPath tier.
- Changed (review follow-up): the multi-agent review of this branch found
  the segment-cache overload of `build_weighted_portal_route_product` had
  been missed (same self-alias UB -- ASan-confirmed -- and missing failure
  capture); the alias guard now covers all product-owned spans (including a
  previously returned `PathResult.path`) via a shared `stash_if_owned`
  helper. It also flagged two quadratic passes and an over-broad capture,
  fixed as: `capture_all` appends directly (O(chunk_count)), the
  blocked-frontier pass dedupes through a scratch seen-set +
  `add_chunk_unique` (linear), and InvalidStart/InvalidGoal products now
  depend only on the offending in-bounds tiles' chunks instead of every
  chunk (out-of-bounds failures carry no dependencies and are permanently
  invalid -- callers pay only the cheap bounds rejection on rebuild).
  `reserve_dependencies` docs now state the chunk_count bound.
- Reason: second audit (2026-07-09) findings H1, H2, M3, M4, M5, M6 and cache
  lows -- failure products carried empty dependency sets that validated
  vacuously forever, and unreached chunks never invalidated field products,
  so removing a wall could leave agents permanently pathless.
- Affected docs: `planning/audit-2026-07-09.md`,
  `planning/audit-2026-07-09-remediation.md`, `decisions/CHANGELOG.md`.
- Affected code: `path/path.h`, `path/portal_route.h`,
  `path/field_product_cache.h`, `path/route_cache.h`, `path/path_runtime.h`,
  `tests/tess_path_test.cc`, `tests/tess_path_cache_test.cc`,
  `tests/tess_path_runtime_test.cc`.
## 2026-07-09 - Executor dispatch guards and partial-plan dirty (audit-2 W-B)

- Changed: (1) `WorkerPoolPhaseExecutor` now documents and enforces its
  single-dispatch contract -- a `dispatch_active_` flag maintained under the
  existing dispatch mutex makes nested or concurrent `for_each_operation`, and
  `reserve_operations` during a dispatch, fail fast via `TESS_ASSERT` in debug
  builds (release builds compile the check out and keep zero overhead). (2)
  `execute_plan` / `execute_plan_deferred_dirty` now include the chunks written
  before an abort in the returned `chunk_count`, and the scheduler tick marks
  pathing dirty whenever any chunk was written, so a plan aborted partway by a
  `PolicyMismatch` no longer leaves path caches stale over already-mutated
  passability. (3) A blocked movement step no longer consumes re-path budget;
  only `prepare_path_agent_processing` counts attempts, so
  `max_blocked_retries = N` grants exactly N re-path attempts (previously a
  movement-blocked cycle was double-counted), and the budget semantics --
  including the by-design indefinite re-path loop against a permanently parked
  blocker -- are documented at `max_blocked_retries`. (4) Thread-spawn loops in
  both executors now join already-started threads and rethrow if a
  `std::thread` constructor throws mid-spawn instead of terminating via a
  joinable-thread unwind; `ScopedThreadPhaseExecutor` documents its no-throw
  callback requirement. (5) `FixedStepAccumulator` clamps the ~1 ulp negative
  bank left by the rounded-division one-tick borrow.
- Reason: second-audit findings H3/M2 (nested/concurrent dispatch deadlocks
  and use-after-realloc were silent), M1 (partially executed plans skipped the
  pathing-dirty mark), and the workstream's low-severity items.
- Affected docs: `decisions/CHANGELOG.md`.
- Affected code: `ops/phase_executor.h`, `ops/queued.h`, `sim/scheduler.h`,
  `sim/path_agent.h`, `sim/path_agent_tick.h`, `sim/time.h`,
  `tests/tess_phase_executor_test.cc`, `tests/tess_sim_scheduler_test.cc`,
  `tests/tess_path_agent_tick_test.cc`, `tests/tess_path_agent_test.cc`,
  `tests/tess_path_runtime_sparse_test.cc`.
## 2026-07-09 - Topology index/shape hardening (audit-2 W-C)

- Changed: (1) `RegionGraph::region_index` guards are wrap-proof -- the chunk
  guard no longer adds 1 before comparing (the out-of-world sentinel ChunkKey
  wrapped past it into an OOB read) and the offset arithmetic is 64-bit (a
  region id near 2^32 wrapped back to a valid-but-wrong index). (2) Region
  graphs now record their build shape (chunk-grid and chunk tile extents);
  `update_region_graph` fully rebuilds and `is_region_graph_fresh` reports
  stale on any mismatch, instead of the chunk-count-only check that let
  equal-count shape mismatches incremental-patch onto wrong adjacency.
  (3) `ShapeTraits` gains division-based (wrap-proof) compile-time overflow
  asserts and an int64 bound on size axes. (4) `detail::box_axis_end` and the
  region-bounds union saturate at int64 max instead of wrapping on extents
  >= 2^63. (5) Documented `ChunkView::is_boundary` degenerate-axis semantics
  and the sparse one-graph-per-world staleness contract.
- Reason: audit findings M7 (region-index wraparound), M8 (shape-mismatch
  incremental patching leaving dangling portal targets), and the related
  topology/shape/meta low-severity items.
- Affected docs: `decisions/CHANGELOG.md`.
- Affected code: `topology/topology.h`, `core/shape.h`,
  `storage/chunk_meta.h`, `block/block.h`, `tests/tess_topology_test.cc`,
  `tests/tess_topology_sparse_test.cc`, `tests/tess_storage_test.cc`.
## 2026-07-09 - Diagnostics alloc-hook and capture-contract fixes (audit-2 W-D)

- Changed: three second-audit fixes on the diagnostics layer. (1) M9: the
  benchmark/test allocation hooks no longer record a deallocation for a null
  pointer -- `operator delete(nullptr)` / `free(nullptr)` are legal no-ops, so
  counting them skewed the deallocations/allocations balance; both the plain
  operator new/delete branch and the sanitizer free hook now null-check before
  recording. (2) M10: the sanitizer-hook branch is excluded on Windows
  (`!defined(_MSC_VER)`, covering MSVC and clang-cl) because MSVC
  /fsanitize=address defines `__SANITIZE_ADDRESS__` but its ASan runtime never
  calls the `__sanitizer_*` hooks and `<pthread.h>` does not exist there.
  (3) M11: documented the threading contract of `capture_timing` /
  `capture_diagnostics` (unsynchronized reads: capture on the recording thread
  or externally synchronize; only the returned snapshot is safe to share) with
  matching notes in trace.h and the ImGui panels header. Docs only, no
  behavior change.
- Reason: findings M9-M11 of the 2026-07-09 second audit.
- Affected docs: `decisions/CHANGELOG.md`.
- Affected code: `bench/tess_diagnostics_alloc_hooks.cc`,
  `diagnostics/export.h`, `diagnostics/trace.h`, `debug/imgui/panels.h`,
  `tests/tess_diagnostics_enabled_test.cc`.
## 2026-07-09 - Test hardening: flake guards, format checking, coverage gaps (audit-2 W-E)

- Changed: test-only hardening from the second audit; no library behavior
  changes. (1) The worker-pool warm no-alloc test tolerates one-time lazy
  runtime allocations on live pool workers by requiring only the last of
  several warm dispatches to be allocation-free (the counter is
  process-global). (2) The ImGui stub's `Text` now carries real ImGui's
  printf-format attribute so `-Wformat` checks panel format strings under the
  `-Werror` presets. (3) The two multi-worker rendezvous spins in the queued
  tests are bounded (30s, clear failure message) instead of hanging to the
  ctest timeout on regression. (4) New coverage: `PrecheckStatus::InvalidStart`
  (out-of-bounds and walled starts), `MovementFailureCounts`
  reserved/stale buckets plus the full `is_transient_movement_failure`
  classification, the four previously unasserted `PathCounters` fields
  (initializations, start/goal passability checks, closed neighbors) via both
  direct events and a real A* maze, a `TESS_ASSERT_MSG` death test, and a
  `UInt128` negative-int constructor death test. (5) Weak assertions
  strengthened: the smoke test pins the released version against a
  hand-maintained literal mirror of the CMake project version instead of
  comparing macros to themselves (a `tess.h` bump that forgets the test now
  fails; a CMake-only bump remains undetectable there), and the warm
  no-alloc agent/tick tests now also pin observable work (submitted/found
  stats, skipped processing with advancement) so a no-op cannot pass. The
  allocation counter documents its relaxed-ordering under-count caveat.
- Reason: second-audit findings M14/M15 plus grep-verified untested surfaces
  and tautological or effect-blind assertions; each weakness could hide a real
  regression (latent flake, unchecked format strings, suite-long hangs,
  silently skipped work).
- Affected docs: `tests/AGENTS.md`.
- Affected code: tests only — `tests/tess_phase_executor_test.cc`,
  `tests/imgui_stub/imgui.h`, `tests/tess_queued_test.cc`,
  `tests/tess_path_precheck_test.cc`, `tests/tess_path_agent_test.cc`,
  `tests/tess_path_agent_tick_test.cc`, `tests/tess_diagnostics_enabled_test.cc`,
  `tests/tess_assert_test.cc`, `tests/tess_shape_test.cc`,
  `tests/tess_smoke.cc`, `tests/allocation_counter.cc`.
## 2026-07-09 - CI and tooling audit remediation (audit-2 W-F)

- Changed: five infrastructure fixes from the 2026-07-09 second audit.
  (1) The `parallel/*` benchmark family is now executed in CI: a
  `tess_bench_parallel_smoke` CTest in the style of the other family smokes,
  and a `parallel/.*` command in `tess_bench_ci_baselines` so baseline
  artifacts accumulate samples. Threshold gating is deliberately deferred:
  repo policy calibrates ceilings from CI baseline artifacts (~3x observed
  maximum), none exist yet for this family, and each gating target enumerates
  an explicit thresholds file, so omitting `bench/thresholds/parallel.json`
  defers the gate without code changes. (2) `tools/benchmark_trends.py`
  discovers every result JSON in a baseline artifact (excluding
  `metadata.json`) instead of reading only block/storage/key, and an
  explicit `--benchmark` selector that matches nothing is now an error
  instead of a silent "n/a"; `tools/benchmark_artifact_metadata.py` gained
  its first tests. (3) `tools/benchmark_thresholds.py` rejects unknown
  per-benchmark limit keys against an allowlist so a typo cannot silently
  disable a gate. (4) A public-safety deny pattern for the private
  downstream consumer's name (split-string style, case-insensitive) so the
  hooks catch reintroduction. (5) Doc drift from the 2026-07-07 gate
  promotions: the Windows MSVC job and public-surface manifest check are
  described as required gates everywhere, the README threshold-suite list
  includes `topology` and `diagnostics`, the advisory GCC compile job is
  listed, stale advisory comments in the workflow were removed or dated,
  and small fixes (path.json note provenance, deck help env vars,
  `.pytest_cache/` ignore, `actions/cache@v4` in the dependency inventory).
- Reason: the audit found compiled-but-never-run benchmarks, tools that
  silently ignored data or typos, a policy without an enforcing pattern,
  and documentation contradicting the actual CI gate status. All fixes are
  infrastructure-only; no tess API or runtime behavior changed.
- Affected docs: `README.md`, `tests/AGENTS.md`, `dependencies.md`,
  `decisions/CHANGELOG.md`.
- Affected code: `bench/CMakeLists.txt`, `bench/thresholds/path.json`,
  `tools/benchmark_trends.py`, `tools/benchmark_artifact_metadata.py`,
  `tools/benchmark_thresholds.py`, `tools/check_public_surface.py`,
  `tools/git_hooks.py`, `tools/steamdeck/deck`, `tests/test_git_hooks.py`,
  `tests/test_benchmark_tools.py`, `.github/workflows/ci.yml`,
  `.gitignore`.

## 2026-07-09 - TraceBuffer pinned to its storage (M12, S4)

- Changed: `diagnostics::TraceBuffer` is now non-copyable and non-movable (all
  four special members deleted).
- Reason: the buffer owns its ring metadata and per-category timing
  accumulators but only references caller storage through a `std::span`. An
  implicit value copy produced two buffers with independent metadata over the
  same backing array, so passing a buffer by value into a helper that installs
  `ScopedTrace` would collect records the caller's original buffer never sees --
  `capture_diagnostics` would silently miss them. Pinning the buffer to its
  storage makes that misuse a compile error; every caller already constructs it
  in place and passes it by reference, so nothing else changed. Flagged by the
  Codex connector review of PR #8.
- Affected docs: `decisions/CHANGELOG.md`.
- Affected code: `diagnostics/trace.h`, `tests/tess_diagnostics_trace_test.cc`.

## 2026-07-09 - Diagnostics review follow-up (M12, S4)

- Changed: three review-feedback fixes on the S4 diagnostics branch. (1) Removed
  the private downstream consumer's name from the new diagnostics docs, using
  generic "downstream consumer/adoption" wording instead -- tracked content is
  treated as public per `AGENTS.md`. (2) Documented the optional, consumer-
  provided Dear ImGui integration dependency that `debug/imgui/panels.h`
  targets. (3) Wired the diagnostics benchmark threshold target into CI so
  `bench/thresholds/diagnostics.json` actually gates regressions.
- Reason: the ImGui-panels and diagnostics slices named the private consumer in
  a repository intended to be public, added an optional dependency without the
  required entry in `docs/dependencies.md`, and shipped a threshold file that no
  CI step exercised (a silent no-gate). All three are addressed here without
  changing any tess API or runtime behavior.
- Affected docs: `architecture/diagnostics.md`, `decisions/CHANGELOG.md`,
  `dependencies.md`.
- Affected code: `.github/workflows/ci.yml`.

## 2026-07-09 - Diagnostics ImGui Panels (M12, S4 slice 5)

- Added (new header `debug/imgui/panels.h`, doubly gated by `TESS_ENABLE_IMGUI`
  && `TESS_ENABLE_DIAGNOSTICS`): reference Dear ImGui panels over the export
  snapshots -- `draw_timing_panel`, `draw_path_counters_panel`,
  `draw_queued_counters_panel`, `draw_allocation_counters_panel`, the composite
  `draw_diagnostics_panel`, and the `category_name` label helper. tess core
  never fetches or links ImGui; the consumer defines the gates on its own
  target and includes `<imgui.h>` before the header (a `#error` enforces the
  order). Only Text/TextUnformatted/Separator are used, with portable
  `unsigned long long` printf casts. Not included by `tess.h`.
- Reason: fifth slice of the M12 diagnostics close (S4) -- the ImGui skeleton.
  The panels are the reference renderer a downstream overlay adopts in the final
  slice. tess validates the header against a minimal ImGui stub
  (`tests/imgui_stub/imgui.h`, `tess_diagnostics_panels_test`) so a panel bug is
  caught in tess CI, not only in the consumer; the stub mirrors the real API for
  the three primitives used.
- Affected docs: `architecture/diagnostics.md`, `architecture/surface.json`.
- Affected code: new `debug/imgui/panels.h`, `tests/imgui_stub/imgui.h`,
  `tests/tess_diagnostics_panels_test.cc`, `CMakeLists.txt`,
  `tests/CMakeLists.txt`.

## 2026-07-09 - Diagnostics Benchmark Family (M12, S4 slice 4)

- Added: `bench/tess_diagnostics_bench.cc` and `bench/thresholds/diagnostics.json`,
  a gated `diagnostics/` benchmark family compiled only into the
  diagnostics-enabled `tess_bench_diagnostics` binary (the source is an empty TU
  without `TESS_ENABLE_DIAGNOSTICS`). Benches: `diagnostics/trace_record`,
  `diagnostics/record_timing`, `diagnostics/scoped_timer`,
  `diagnostics/warning_sink`. New `tess_bench_diagnostics_thresholds` target, a
  CI baseline command, and a `tess_bench_diagnostics_family_smoke` CTest.
- Reason: fourth slice of the M12 diagnostics close (S4). Measures the enabled
  overhead of the trace/timer/warning primitives directly (local observations
  ~0.3-32 ns, far under the loose bootstrap ceilings). The compile-down side is
  proven structurally: the default `tess_bench` compiles every diagnostic macro
  to nothing, so its `queued/`/`path/` families are the zero-overhead baseline
  and still pass unchanged with the planner-trace macros present.
- Affected docs: `planning/benchmark-plan.md`.
- Affected code: new `bench/tess_diagnostics_bench.cc`,
  `bench/thresholds/diagnostics.json`, `bench/CMakeLists.txt`.

## 2026-07-09 - Planner Trace and Snapshot Export (M12, S4 slice 3)

- Added (ops/queued.h): planner-trace instrumentation. `plan_operations` (now
  an index loop) records `invalid_write_policy` / `invalid_field_access` /
  `invalid_domain` / `conflict` / `planned` per operation, and
  `plan_parallel_execution_phases` records `unsupported_write_policy` /
  `new_phase` / `merged` per operation, all under the `Planner` trace category
  via `TESS_DIAG_TRACE_VALUE` (value = operation/phase index). Macros only:
  byte-identical planning when diagnostics are off.
- Added (diagnostics, new header `diagnostics/export.h`, gated by
  `TESS_ENABLE_DIAGNOSTICS`): `TimingSnapshot` (a copy of every category's
  `TraceCategoryStats` with a Count-guarding `category()` accessor),
  `DiagnosticsSnapshot` (path/allocation/queued counters + timing), and the
  `capture_timing` / `capture_diagnostics` free functions that assemble them as
  pure copies decoupled from the live sinks.
- Reason: third slice of the M12 diagnostics close (S4). The planner was the one
  major queued-ops surface with zero instrumentation; routing its decisions
  through the trace buffer gives the profiling panels a real event log, and the
  export snapshots are the plain structs the ImGui panels and the downstream
  adoption render. The `plan_operations` loop switched to an index form purely
  to expose the operation index as the trace value; behavior is unchanged (the
  full 514-test suite passes).
- Affected docs: `architecture/diagnostics.md`, `architecture/surface.json`.
- Affected code: `ops/queued.h`, new `diagnostics/export.h`, `tess.h`,
  `CMakeLists.txt`; extended test `tess_diagnostics_trace_test.cc`.

## 2026-07-09 - Diagnostics Trace Buffer and Timers (M12, S4 slice 2)

- Added (diagnostics, new header `diagnostics/trace.h`, gated by
  `TESS_ENABLE_DIAGNOSTICS`): a structured event log and timing capture.
  `TraceCategory` (coarse origin tag with a `Count` sizing sentinel),
  `TraceRecord` (category + non-owning `label` + `value` + monotonic
  `sequence`), `TraceCategoryStats` (samples/total/min/max ns), and
  `TraceBuffer` -- a caller-owned ring over a `std::span<TraceRecord>` with an
  inline per-category timing accumulator (allocation-free; empty span drops all
  records; overflow overwrites oldest and counts `dropped()`). `ScopedTrace`
  installs the thread's active buffer; `trace_event` and new
  `TESS_DIAG_TRACE` / `TESS_DIAG_TRACE_VALUE` macros route to it (and compile to
  nothing when off). `ScopedTimer` is a `steady_clock` RAII timer that binds to
  the buffer active at construction and, on destruction, folds the elapsed
  nanoseconds into the category accumulator and appends a duration record.
- Reason: second slice of the M12 diagnostics close (S4). Trace records and
  timers are the profiling substrate the ImGui panels and the downstream adoption
  render, and the planner-trace slice records against them. `ScopedTimer`
  captures the active buffer at construction (not destruction) so nested scopes
  and destruction order cannot misattribute a timed span. The `label`
  non-owning contract mirrors `Warning::message`/`PathView`.
- Affected docs: `architecture/diagnostics.md`, `architecture/surface.json`.
- Affected code: new `diagnostics/trace.h`, `diagnostics/diagnostics.h` (the two
  new trace macros), `tess.h`, `CMakeLists.txt`; extended tests
  `tess_diagnostics_trace_test.cc` and `tess_diagnostics_default_test.cc`.

## 2026-07-09 - Diagnostics Warning Sink (M12, S4 slice 1)

- Added (diagnostics, new header `diagnostics/warning_sink.h`, gated by
  `TESS_ENABLE_DIAGNOSTICS`): a `Warning` record (a `WarningCategory` origin
  tag, a non-owning `std::string_view message`, a numeric `detail`, and a
  defaulting `std::source_location`), the `WarningSink` concept
  (`noexcept warn(const Warning&)`), `NullWarningSink` (discards), and
  `BufferedWarningSink<Capacity>` -- a caller-owned fixed-capacity ring with
  inline storage (allocation-free `warn()`), oldest-first indexing, and a
  `dropped()` overflow count.
- Reason: first slice of the M12 diagnostics close (S4). A warning channel is
  a foundational primitive that later stages consume (queued-ops result
  reasons in S6, scheduler budget signals in S7); landing it early and
  independently keeps the trace/timer slice focused. The `message` non-owning
  contract mirrors `PathView`: warnings are copied by value but never own their
  text, so the sink stays allocation-free.
- Affected docs: `architecture/diagnostics.md`, `architecture/surface.json`.
- Affected code: new `diagnostics/warning_sink.h`, `tess.h`, `CMakeLists.txt`;
  new test `tess_diagnostics_trace_test.cc`.

## 2026-07-09 - PathView Non-Owning Path View (M8)

- Added (path, new header `path/path_view.h`): `PathView`, a non-owning view
  over a path's coordinates. `PathResult::path` changes type from
  `std::span<const Coord3>` to `PathView` (an intentional but source-compatible
  break -- see below). PathView offers read-only span parity (`size`, `empty`,
  `operator[]`, `front`, `back`, `begin`/`end`, `data`), `span()` to recover the
  raw `std::span`, and `suffix(offset)`: the bounds-clamped remaining path from a
  walked index, sharing the same storage without copying. It carries the same
  lifetime contract as the span it wraps (valid until the A* scratch or runtime
  node buffer it views is reused).
- Reason: closes the M8 `PathView` deliverable and gives consumers a named path
  type plus the one operation real consumers need -- "the remaining path from
  index N" (an agent advancing along `path_index`, an overlay drawing the rest
  of a route). PathView is constructible from a `std::span` or a
  `std::vector<Coord3>`, so every existing result-construction site
  (`PathResult{..., scratch.path_}`) compiles unchanged, and its span-parity API
  means the ~250 span-style `.path` reads across the codebase compile unchanged;
  the only edited call site extracts a raw span via `.path.span()`. Scoped to
  `PathResult`; `DistanceFieldResult` (no path) and the route-product/waypoint
  spans are deliberately left as plain spans.
- Affected docs: `architecture/path.md`, `architecture/surface.json`.
- Affected code: new `path/path_view.h`, `path/path.h`,
  `path/portal_segment_cache.h`, `tess.h`, `CMakeLists.txt`; new test
  `tess_path_view_test.cc`.

## 2026-07-09 - Topology Benchmark Family

- Added: `bench/tess_topology_bench.cc` and `bench/thresholds/topology.json`, a
  new gated `topology/` benchmark family covering region-graph build over an
  open 512x512 world, single-chunk incremental update, a far reachability query,
  and `precheck_path` on both a reachable and a sealed (enclosed-goal)
  destination. Wired into `tess_bench` / `tess_bench_diagnostics`, the
  `tess_bench_topology_thresholds` gate target, the non-gating CI baseline
  collection, a smoke test, and a new CI "Topology benchmark thresholds" step.
- Headline: `topology/precheck_unreachable_512x512` floods only the region graph
  (~256 regions) to prove a goal unreachable, versus `path/astar_plane_gap_miss_512x512`
  which floods the full tile component (~262k tiles) for the same NoPath verdict
  -- the cost the precheck short-circuits.
- Reason: the topology precheck (S3) needs a regression guard, and M14 tracks a
  topology bench family. Per the benchmark policy, initial `max_cpu_time_ns`
  ceilings are deliberately generous documentation bounds (not tuned gates) to
  avoid false failures on slower/noisier CI runners; they are tightened once in
  the S11 consolidation from accumulated CI baselines.
- Affected code: `bench/tess_topology_bench.cc`, `bench/thresholds/topology.json`,
  `bench/CMakeLists.txt`, `.github/workflows/ci.yml`.

## 2026-07-09 - Topology Precheck Wired Into the Path Runtime

- Added (path runtime / sim): `PathRequestRuntime::process_unit_cached` and
  `process_weighted_batch`, the `process_*_path_agents` helpers, and all four
  `tick_*_path_agents` entry points now take an optional trailing
  `const RegionGraphT<World::residency_type>*` (default `nullptr`). When a graph
  is supplied, a pre-A* pass resolves every request `precheck_path` proves
  `Unreachable` to `NoPath` (zero expanded nodes) without searching, counting
  them in the new `PathRuntimeStats::precheck_ruled_out` /
  `PathAgentFrameStats::precheck_ruled_out` -- a SUBSET of `no_path`, so
  aggregate failure counts are unchanged. The unit path skips ruled-out requests
  in its search loop; the weighted path runs the monolithic `weighted_path_batch`
  over only the survivors and scatters results back to their original slots.
- Reason: closes the runtime half of the M8 precheck gap. `nullptr` (the
  default) is byte-identical to the previous behavior, so this is opt-in and
  additive. Precondition: the graph must be built over the same `PassableTag`
  the search uses; freshness is checked (`is_region_graph_fresh`) and a stale
  graph degrades to running A*, so the gate can only prune provably-unreachable
  goals, never turn a solvable query into a wrong failure.
- Affected docs: `architecture/path.md`, `architecture/simulation.md`.
- Affected code: `path/path_runtime.h`, `sim/path_agent.h`,
  `sim/path_agent_tick.h`; new test `tess_path_precheck_runtime_test.cc`.
  The downstream adoption (passing its `RegionGraph` to the weighted tick and
  deleting its bespoke reachability gating) lands in a later S3 slice.

## 2026-07-08 - Topology Precheck (Pre-A* Reachability Gate)

- Added (topology): `is_region_graph_fresh(world, graph)` -- a const,
  allocation-free predicate reporting whether a built `RegionGraph` still
  matches the world (dense: per-chunk topology version; sparse: also the frozen
  residency snapshot, generation checked before touching `meta()`). Recomputes
  the staleness test `update_region_graph` applies internally.
- Added (path, new header `path/precheck.h`): `precheck_path(...)` ->
  `PrecheckStatus` {`Reachable`, `Unreachable`, `MissingChunk`, `InvalidStart`,
  `InvalidGoal`, `GraphStale`, `NoGraph`} and `precheck_rules_out_path`. A
  cheap region-graph reachability gate run before A* (M8). Only `Unreachable`
  licenses skipping A*; every other status runs A*. Staleness is resolved
  first: empty graph -> `NoGraph`, stale graph -> `GraphStale`, both decided
  BEFORE `reachable()`, so a stale snapshot can never yield a wrong definitive
  `Unreachable`. Sparse `Indeterminate` (non-resident boundary exit) maps to
  `MissingChunk`. Reuses a caller-owned `RegionGraphScratch`; allocation-free
  warm. Can only prune provably-unreachable goals, never cause a wrong failure.
- Affected docs: `architecture/topology.md`, `architecture/path.md`,
  `architecture/surface.json`.
- Affected code: `topology/topology.h`, new `path/precheck.h`, `tess.h`,
  `CMakeLists.txt`; tests `tess_topology_test.cc`,
  `tess_topology_sparse_test.cc`, new `tess_path_precheck_test.cc`. Runtime
  wiring and the downstream adoption land in later S3 slices.

## 2026-07-08 - Sparse Residency Pre-Merge Review Fixes

Three defects found by the S2 pre-merge review (independent Opus + Fable-5
workflow and a cross-lab codex pass), fixed on the branch before merge.

- Changed (movement, MAJOR): `validate_movement_intent` (`sim/movement.h`) now
  returns transient `StaleVersion` for a non-resident endpoint on a sparse
  world, not terminal `InvalidFrom`/`InvalidTo`. `Invalid*` is not in
  `is_transient_movement_failure`, so the agent lifecycle treated ordinary LRU
  eviction of a chunk under a Following agent as a permanent caller bug and
  stranded the agent at `Unreachable` (retries unused, never re-pathed) --
  contradicting the function's own comment and `movement_versions_match`, which
  already returned `StaleVersion` for the identical condition. Now both agree
  and the agent re-plans against the changed residency.
- Changed (distance field, MAJOR): the two-call `build_distance_field` /
  `distance_field_path` API (and the weighted / box / bounded variants) now
  carries a residency fingerprint in `DistanceFieldScratch`. A field is indexed
  by resident-slot offset, so an eviction/reload between the paired calls could
  rebind a slot to a different chunk and make the reader return `Found` with a
  path to the wrong coordinate (and across impassable tiles). Each build stamps
  `world.residency_fingerprint()` (new accessor on `SparseResidentWorld`: a
  commutative content hash over the resident set's `(key, resident_slot,
  generation, version)` -- the route cache's fingerprint terms plus the key->slot
  binding, because a distance field is indexed by resident slot where the route
  cache is keyed by coordinate); each reader refuses a mismatch with `NoPath` so
  the caller rebuilds. Because it hashes the resident *state* rather than a
  per-world counter, it catches any eviction, reload, in-place edit, or read
  against a different/copied/swapped world -- including two worlds that reach the
  same resident set with the slots permuted (a bare counter, or a slot-less hash,
  aliases these). Dense worlds never evict, so the stamp/check compile to a
  no-op and stay byte-identical.
- Changed (queued ops, safety): the queued planner *and* executor now
  `static_assert` an `AlwaysResidentWorld` at all three public choke points --
  `plan_operations` (planning; its `expand_domain` `ResidentChunks` case
  enumerates `0..chunk_count`, an OOM on a huge sparse world),
  `try_planned_block_ctx` (execution; every `execute_planned_operation*` and
  batch executor funnels through it, and a hand-built `PlannedOperation` naming
  a non-resident chunk would read its page/meta out of bounds), and
  `merge_planned_dirty` (dirty apply; `world.mark_dirty` on a non-resident chunk
  is out of bounds). Queued ops are not yet sparse-ported, so this fails loudly
  at compile time -- matching every other deferred sparse-unsafe family --
  instead of silently OOMing or reading out of bounds.
- Reason: pre-merge correctness of the headline sparse feature. The movement
  defect is reachable in the shipped agent tick with ordinary input; the
  distance-field and queued defects had no in-tree trigger but are silent
  corruption / OOM footguns for external sparse users.
- Affected code: `include/tess/sim/movement.h`,
  `include/tess/path/{path.h,distance_field_box.h,detail/weighted_batch.h}`,
  `include/tess/storage/sparse_world.h`, `include/tess/ops/queued.h`,
  `tests/{tess_path_runtime_sparse_test.cc,tess_residency_test.cc}`.
- Affected docs: `docs/architecture/path.md`.

## 2026-07-08 - Sparse Render-Delta Collection

- Changed: `collect_render_tile_deltas` and `clear_render_delta_dirty`
  (`render_delta.h`) now iterate the resident set on a sparse world instead of
  scanning `0..chunk_count` and calling unchecked `meta()`/`clear_dirty()` (which
  read a non-resident slot out of bounds under NDEBUG). Dense keeps the exact
  `0..chunk_count` loop behind `if constexpr`; the sparse arm iterates
  `resident_chunk_keys()`. A non-resident chunk holds no data and cannot be
  dirty, so the resident set captures every delta. The per-chunk emit body was
  hoisted into a shared `detail::emit_chunk_render_deltas` helper (behavior
  unchanged, verified by the existing dense scheduler suite).
- Reason: S2 Slice 5b (sparse consumers) -- the render-delta scan is reachable
  from the scheduler wrappers on a sparse world (when `render_dirty_mask != 0`),
  and the downstream sparse adoption uses `render_tile_deltas`. Surfaced by the
  Slice-5a route-cache verification's scope-completeness lens.
- Deferred: the queued-ops planner/commit path (`queued.h` `resident_chunk_keys`
  full scan, `mark_dirty` on try_chunk-validated targets) stays dense-bound --
  it is dormant on the sparse agent/render path and not used downstream, so it
  lands with a later queued-ops sparse slice.
- Affected code: `include/tess/sim/render_delta.h`,
  `tests/tess_path_runtime_sparse_test.cc`.
- Affected docs: `docs/architecture/simulation.md`.

## 2026-07-08 - Sparse Path Runtime (Route Cache + Weighted Batch)

- Changed: the path runtime now runs over sparse (`SparseResidentWorld`) worlds.
  `RouteCacheScratch::world_version_fingerprint` became residency-aware: for a
  sparse world it folds only the resident set (bounded by `resident_count`, never
  `chunk_count`) as an order-independent sum over each chunk's `(key,
  residency_generation, meta().version)` -- version catches in-place edits and the
  world-monotonic `residency_generation` catches evict/reload/swap even when
  `ensure_resident` resets a reloaded chunk's version to 0. The commutative sum is
  invariant to `resident_chunk_keys()` order (eviction swap-with-last reorders it).
  The dense-only `static_assert`s on `cached_astar_path` and `weighted_path_batch`
  were removed (both already delegate to sparse-native primitives), and
  `PathRequestRuntime::process_unit_cached`'s field-product block is now behind
  `if constexpr` so the dense-only `process_repeated_goal_fields` is never
  instantiated for a sparse world. Result: `process_unit_cached`,
  `process_weighted_batch`, and `tick_*_path_agents_with_movement` compile and run
  on sparse worlds; a chunk evicted/reloaded/edited invalidates the whole route
  cache before a stale route is served. The agent movement commit was made
  residency-safe to match: `validate_movement_intent` and `movement_versions_match`
  now guard non-resident endpoints behind `if constexpr` (`try_resolve` is
  containment-only, so an in-bounds non-resident endpoint would otherwise reach an
  unchecked `field()`/`meta()` and read a non-resident slot out of bounds under
  NDEBUG). A move into or out of a non-resident chunk is rejected
  (`InvalidFrom`/`InvalidTo`; `StaleVersion` for the version check) so an agent
  re-plans rather than walking a route across a chunk evicted since planning.
- Reason: S2 Slice 5a -- unblock the path runtime on sparse worlds, the
  dependency the downstream sparse adoption needs. Scope and the
  order-independence requirement came from an adversarial proposer-panel +
  synthesis design review.
- Design: dense codegen byte-identical (the fingerprint's dense arm is the exact
  prior FNV-over-`chunk_count` loop; every sparse branch is behind `if constexpr`).
  Default `MissingChunkPolicy::TreatAsBlocked` on the runtime path is
  correct-but-conservative: a route across a non-resident chunk is `NoPath`, never
  a wrong stale route and never `Indeterminate` (policy threading deferred).
- Deferred (later sparse-cache slice): the distance-field product family, the unit
  field-product cache, `WeightedPortalSegmentCache` / route-portal products (their
  `ChunkVersionDependencies` read `meta().version` and would assert on a
  non-resident key -- they need a residency-tolerant dependency check); and
  threading `MissingChunkPolicy` through the runtime so agents can distinguish
  "unloaded" from "unreachable".
- Still dense-bound (separate sparse-consumer slice, not the path runtime):
  `render_delta.h` (`collect_render_tile_deltas` / `clear_render_delta_dirty` scan
  `0..chunk_count` and call unchecked `meta()`) and the queued-ops commit path
  (`queued.h` `mark_dirty`). The scheduler wrappers `tick_*_movement_scheduler`
  reach the render-delta scan only when `render_dirty_mask != 0` (the default is
  0); the agent tick `tick_*_path_agents_with_movement` itself is sparse-safe.
  These land with the sparse-consumer port that precedes the downstream
  adoption.
- Affected code: `include/tess/path/route_cache.h`,
  `include/tess/path/path_runtime.h`, `include/tess/path/detail/weighted_batch.h`,
  `include/tess/sim/movement.h`, `tests/tess_path_runtime_sparse_test.cc`,
  `tests/CMakeLists.txt`.
- Affected docs: `docs/architecture/path.md`.

## 2026-07-08 - Sparse Topology (RegionGraph) and Reachability Indeterminate

- Changed: `RegionGraph` became a class template `RegionGraphT<Residency>` with
  `using RegionGraph = RegionGraphT<AlwaysResident>` and a new
  `SparseRegionGraph = RegionGraphT<SparseResident>`. A sparse graph builds and
  updates only over a world's resident chunk set (sized by resident count, never
  the total chunk count) and resolves a chunk key to a local index through a
  frozen, sorted key table (`std::lower_bound`), so it is self-contained and
  eviction after the build cannot invalidate it. `reachable` gained
  `ReachabilityStatus::Indeterminate`: a non-resident endpoint, or a BFS that
  exhausts without reaching the goal while touching a region that exits into a
  non-resident chunk, returns `Indeterminate` instead of a wrong `Unreachable`;
  a route found within the resident set still wins, and a fully-resident
  enclosed component is a definite `Unreachable`. `update_region_graph` on a
  sparse world checks a frozen residency snapshot (resident count plus per-key
  generation) and falls back to a full rebuild on any residency change.
- Reason: S2 Slice 4 -- topology must run natively over huge sparsely-resident
  worlds. The chosen representation (class template on residency, world-free
  frozen key table, membership-based missing-frontier bit distinguishing
  "unknown" from "wall") came from an adversarial proposer-panel + synthesis
  design review, which disqualified a runtime-bool design (its world-free
  accessors cannot be `if constexpr`-gated) and corrected a per-chunk vs. world
  residency-generation misreading.
- Design: `Indeterminate`-only `reachable` (no `MissingChunkPolicy` knob this
  slice) fully satisfies "never a wrong Unreachable" and avoids pulling path.h's
  `MissingChunkPolicy` into topology.h. Dense codegen is byte-identical: every
  sparse branch is behind `if constexpr`, and the sparse-only graph state lives
  in a `[[no_unique_address]]` companion that is empty for a dense graph, so the
  `RegionGraph` alias keeps every existing dense call site and layout unchanged.
- Affected docs: `docs/architecture/topology.md`,
  `docs/architecture/surface.json`.
- Affected code: `include/tess/topology/topology.h`,
  `tests/tess_topology_sparse_test.cc`, `tests/CMakeLists.txt`.
- Deferred (Slice 5+): a `MissingChunkPolicy` knob on `reachable`; O(1)
  chunk->local resolution (a graph-owned `ChunkDirectory`) and O(1) staleness
  (a world residency epoch); streaming residency change with a live graph;
  consumer wiring of `Indeterminate`.

## 2026-07-08 - Sparse Weighted Distance-Field Family

- Changed: the weighted single-shot distance-field builders now run natively
  over sparse worlds -- `build_weighted_distance_field` (weighted Dijkstra heap
  flood), `build_weighted_distance_field_in_box` (domain-filtered), and
  `build_bounded_weighted_distance_field` (bucket queue) -- along with the
  `weighted_distance_field_path` reader. Each mirrors the unweighted sparse
  pattern: node arrays are sized by `NodeIndexSpace::capacity_hint`, a
  non-resident goal resolves to `Indeterminate`/`InvalidGoal` by
  `MissingChunkPolicy` before the goal chunk is read, non-resident neighbors are
  skipped before their offset is computed, and a field truncated by a missing
  chunk reports `Indeterminate` under that policy. The box keeps its domain
  filter ahead of the residency check; the bucket builder forwards the policy
  through its over-budget fallback. Each builder gained an optional trailing
  `MissingChunkPolicy` whose default lives on a namespace-scope forward
  declaration. This completes the distance-field conversion for the single-shot
  searches (Slice 3).
- Reason: weighted reverse fields must also flood huge sparsely-resident worlds
  and never report a wrong result across a non-resident boundary. A five-lens
  adversarial stronger-model review (offset safety, mirror fidelity, guard
  completeness, dense byte-identity, and fallback/default-argument correctness)
  returned clean on all lenses.
- Decision: the distance-field PRODUCT family and route/portal products stay
  dense-only. They are persistent, cross-frame cached artifacts indexed by raw
  tile id, so they share the route cache's residency-freeze contract and land
  with the sparse-cache slice rather than here.
- Affected docs: `docs/architecture/path.md`.
- Affected code: `include/tess/path/path.h`,
  `include/tess/path/distance_field_box.h`,
  `include/tess/path/detail/weighted_batch.h`, `tests/tess_residency_test.cc`.
- Still dense-only (`static_assert`-guarded): the distance-field product family
  (`build_distance_field_product`, `distance_field_product_path`,
  `nearest_target`), the weighted route/portal route products, and
  `weighted_path_batch` -- all pending the sparse-cache and topology slices.

## 2026-07-08 - Sparse Weighted A* and Unweighted Distance Field

- Changed: `weighted_astar_path` now runs natively over sparse worlds,
  mirroring `astar_path` exactly -- its unit-cost direct and blocked-axis
  detour fast paths compile out for sparse (`if constexpr (Space::is_dense)`),
  the neighbor loop skips non-resident tiles before computing any node-array
  offset, and non-resident endpoints or an exhausted search that crossed a
  missing chunk return `Indeterminate` under `MissingChunkPolicy`. The
  unweighted distance field is likewise sparse: `build_distance_field` floods
  only the resident set (a field truncated by a non-resident chunk reports
  `Indeterminate` under that policy) and `distance_field_path` is a pure
  gradient reader that stays offset-safe (non-resident start is `InvalidStart`).
  `DistanceFieldScratch::touch_node` gained an offset-taking form (a
  dense-identity 1-arg overload serves the still-guarded callers). Both search
  functions take an optional trailing `MissingChunkPolicy` whose default lives
  on a namespace-scope forward declaration. The A* cores also moved to a new
  `include/tess/path/detail/astar.h` (pure split) to keep `path.h` under the
  24k-token header budget.
- Reason: continue Slice 3 -- weighted search and reverse distance fields must
  also run over huge sparsely-resident worlds and never report a wrong
  `NoPath`/`InvalidStart` across a non-resident boundary. An adversarial
  stronger-model review (mirror fidelity, offset safety, guard completeness,
  dense byte-identity) confirmed the ports and flagged the one missing direct
  guard, now added.
- Affected docs: `docs/architecture/path.md`.
- Affected code: `include/tess/path/detail/astar.h`,
  `include/tess/path/path.h`, `include/tess/path/detail/weighted_batch.h`,
  `include/tess/path/portal_route.h`,
  `include/tess/path/portal_segment_cache.h`,
  `include/tess/path/field_product_cache.h`, `tests/tess_residency_test.cc`.
- Still dense-only (`static_assert`-guarded so sparse misuse is a compile
  error, not silent OOB/OOM): the weighted distance-field family (ported in the
  next entry), the distance-field product family (`build_distance_field_product`,
  `distance_field_product_path`, `nearest_target`), the weighted route/portal
  route products, and `weighted_path_batch` -- all pending later slices.

## 2026-07-08 - Sparse A* Node Mapping and Missing-Chunk Path Policy

- Changed: `tess::detail::NodeIndexSpace` gains a `SparseResident`
  specialization mapping a global tile index to a resident-slot-bounded
  node-array offset (`resident_slot * local_tile_count + local id`), so a search
  over a sparse world allocates node arrays sized to the residency budget, not
  the global tile count. Both specializations expose `is_dense` and
  `is_resident_index`; `World<...,SparseResident>` exposes
  `resident_slot`/`npos_slot`. New path vocabulary: `PathStatus::Indeterminate`
  and `MissingChunkPolicy{TreatAsBlocked, Indeterminate}`. `astar_path` is now
  sparse-capable: its pre-A* fast-path scan is compiled out for sparse worlds
  (it cannot answer definitely across a non-resident chunk), the neighbor loop
  skips non-resident neighbors before computing any offset, non-resident
  start/goal and an exhausted search that touched a missing chunk return
  `Indeterminate` under that policy rather than a wrong `InvalidStart`/
  `InvalidGoal`/`NoPath`. `is_passable_index` treats a non-resident chunk as
  impassable. Dense codegen is unchanged (every sparse branch is behind
  `if constexpr (!is_dense)`); the new status flows through `PathRuntimeStats`
  and `PathAgentFrameStats` with a dedicated `indeterminate` bucket.
  `weighted_astar_path`, `build_distance_field`, and
  `build_weighted_distance_field` are `static_assert`-guarded dense-only in this
  commit; the weighted A* and unweighted distance-field ports land in the entry
  above.
- Reason: Slice 3 of the full-sparse stage: A* must run natively over huge
  sparsely-resident worlds and must never report a wrong `NoPath` across a
  non-resident boundary. Design (dense-only fast-path scan vs. surgical
  gating) reviewed with an independent stronger-model pass, which caught the
  start/goal-residency hole, the weighted clone, and the residency-freeze
  contract.
- Affected docs: `docs/architecture/path.md`, `docs/architecture/surface.json`,
  `docs/decisions/CHANGELOG.md`.
- Affected code: `include/tess/path/node_index_space.h`,
  `include/tess/path/path.h`, `include/tess/path/path_runtime.h`,
  `include/tess/sim/path_agent.h`, `include/tess/storage/sparse_world.h`,
  `tests/tess_residency_test.cc`.

## 2026-07-08 - NodeIndexSpace Trait for A* Node Storage

- Changed: New internal `tess::detail::NodeIndexSpace<World>`
  (`include/tess/path/node_index_space.h`) maps a global tile index to a
  node-array offset and reports the array capacity a search must allocate.
  `astar_path` and `weighted_astar_path` now size and index their
  `PathScratch` node arrays through this trait instead of `tile_count<World>()`
  and raw `static_cast<std::size_t>(index)`. The `AlwaysResident`
  specialization is the identity (`offset(i) == i`, capacity == the whole tile
  count), so the dense search is byte-identical and stays allocation-free after
  warmup. `PathScratch::touch_node` now takes the pre-computed offset alongside
  the global index.
- Reason: Slice 2 of the full-sparse stage. Decoupling A* node storage from the
  global tile count is the prerequisite for running A* over sparse worlds,
  whose global tile index can reach ~1e17 and cannot size a dense array. The
  sparse `NodeIndexSpace` specialization (resident-slot remap, bounded by the
  residency budget) and the missing-chunk path policy land in the next slice;
  this slice ships only the trait and the dense identity so the change is a
  provably behavior-preserving refactor.
- Affected docs: `docs/decisions/CHANGELOG.md`.
- Affected code: `include/tess/path/node_index_space.h` (new),
  `include/tess/path/path.h` (astar_path/weighted_astar_path offset threading,
  PathScratch::touch_node signature), `include/tess/tess.h`, `CMakeLists.txt`.

## 2026-07-08 - Sparse-Resident World (Storage Core)

- Changed: New `tess::World<Shape, Schema, tess::SparseResident>` (alias
  `SparseResidentWorld`) materializes only a byte-budgeted, least-recently-used
  subset of a bounded shape, so worlds spanning trillions of chunks cost only
  their residency budget. New public `tess/storage/residency.h`
  (`SparseResident`, `ResidencyConfig`, `ResidencyHandle`) and
  `tess/storage/sparse_world.h` (the specialization plus an internal
  fixed-capacity `ChunkDirectory` with backward-shift deletion). `ChunkMeta`,
  `ChunkState`, `DirtyObservation`, and every `ChunkMeta` mutation
  (dirty/active/version) moved to a new shared `tess/storage/chunk_meta.h`, so
  the dense and sparse worlds share one metadata implementation. Residency is
  explicit (`ensure_resident`/`touch`/`evict`) with world-monotonic per-load
  generations that invalidate stale handles across eviction; `is_resident`
  distinguishes `Missing` in-bounds chunks from out-of-bounds; `try_chunk`/
  `try_meta`/`try_field` are the residency-tolerant readers. All iteration is
  bounded by the resident set or fixed slot capacity, never `chunk_count`.
- Reason: Milestone M2 requires sparse residency early because it changes the
  storage API that topology, pathing, queued ops, and block views all consume.
  This is Slice 1 of the full-sparse stage: the self-contained storage core the
  later topology/path/consumer slices build on. Extracting the shared metadata
  ops keeps dirty/active/version semantics identical across both worlds
  (guarded by the unchanged dense storage tests).
- Affected docs: `docs/architecture/storage.md` (new Sparse-Resident World
  section, revised Out Of Scope), `docs/architecture/surface.json`,
  `tests/AGENTS.md`.
- Affected code: `include/tess/storage/chunk_meta.h` (new),
  `include/tess/storage/residency.h` (new),
  `include/tess/storage/sparse_world.h` (new),
  `include/tess/storage/world.h` (delegates to shared meta ops),
  `include/tess/tess.h`, `CMakeLists.txt`, `tests/tess_residency_test.cc` (new),
  `tests/CMakeLists.txt`.

## 2026-07-07 - Parallel Benchmark Family and Concurrency Plan

- Changed: New `bench/tess_parallel_bench.cc` compares serial,
  scoped-thread, and worker-pool phase executors on identical partitioned
  one-op-per-chunk phases (dispatch-bound, memory-bound, and compute-bound
  workloads, fixed worker counts, worker/chunk counters). Parallel cases
  are deliberately ungated pending CI baselines. New
  `docs/planning/concurrency-plan.md` records the S1 concurrency-stream
  decisions: planner-anchored write-policy enforcement in v1 (claim-table
  checking deferred to the scheduler stage), internal backends before
  external `work_contract` evaluation, ungated parallel benchmarks, and the
  non-atomic `ChunkMeta` invariant, plus measured dispatch-cost evidence.
- Reason: The concurrent tile-world addendum requires baseline benchmark
  data before thresholds and requires Tess-owned backends to establish
  correctness and performance before any external scheduler dependency is
  considered.
- Affected docs: `docs/planning/concurrency-plan.md`,
  `docs/planning/README.md`
- Affected code: `bench/tess_parallel_bench.cc`, `bench/CMakeLists.txt`

## 2026-07-07 - Persistent Worker-Pool Phase Executor Prototype

- Changed: `tess/ops/phase_executor.h` gains `WorkerPoolPhaseExecutor`, a
  persistent-pool prototype satisfying the `PhaseExecutor` concept: workers
  are created once and reused across phases (no per-phase thread creation),
  jobs are published type-erased under the pool mutex, completion blocks
  until all claimed operations finish and all adopted workers leave the
  claim loop, failures report in operation order, and
  `reserve_operations(count)` makes warm dispatch allocation-free. It does
  not declare `serial_execution_tag` and pairs only with
  `execute_phase_partitioned_dirty_with`.
- Reason: The concurrent tile-world addendum requires a Tess-owned
  persistent pool prototype behind the executor interface before any
  external backend is evaluated; this is the S1 slice of the v1 concurrency
  stream, providing the comparison point for the parallel benchmarks and
  the candidate promoted to production in the scheduler stage (S7).
- Affected docs: `docs/architecture/queued-operations.md`,
  `docs/architecture/surface.json`
- Affected code: `include/tess/ops/phase_executor.h`,
  `tests/tess_phase_executor_test.cc`, `tests/tess_queued_test.cc`,
  `tests/AGENTS.md`

## 2026-07-07 - Phase Executor Contract Promoted to Its Own Public Header

- Changed: The backend-facing executor pieces (`PlannedExecutionStatus`,
  `PlannedExecutionResult`, `ExecutorPhaseRange`, `SerialPhaseExecutor`, the
  `SerialExecutor` concept, `ScopedThreadPhaseExecutor`, and
  `execute_operation_index_range`) moved from `tess/ops/queued.h` into a new
  public header `tess/ops/phase_executor.h`, which also adds a structural
  `PhaseExecutor` concept stating the
  `for_each_operation(first, count, fn) -> PlannedExecutionResult` contract
  and documents the executor thread contract (non-atomic world metadata,
  planner-proven disjoint mutable ownership, caller-owned dirty partitions
  reduced in plan order). `executor_phase_range(ExecutionPhase)` stays in
  `queued.h` as the plan-side bridge. No symbol was renamed.
- Reason: The v1 concurrency stream (plan S1) lands API-shaping work first;
  worker backends must be writable and testable against a small stable
  header without pulling in the planner, and later stages (result channels,
  scheduler auto-exec, production pool) build on the same concept.
- Affected docs: `docs/architecture/queued-operations.md`
- Affected code: `include/tess/ops/phase_executor.h`,
  `include/tess/ops/queued.h`, `include/tess/tess.h`, `CMakeLists.txt`,
  `tests/tess_phase_executor_test.cc`, `tests/CMakeLists.txt`,
  `tests/AGENTS.md`

## 2026-07-07 - Generation-Stamped Dirty Observe/Clear Protocol

- Changed: `World` gains `observe_dirty(key, flags)` returning a
  `DirtyObservation` (observed dirty subset, dirty bounds, chunk version)
  and `clear_dirty_observed(key, observation)`, which clears exactly the
  observed flags only while the chunk's dirty generation still matches and
  otherwise preserves all flags/bounds and returns `false`.
- Reason: The concurrent tile-world addendum requires a generation-aware
  observe/clear protocol before budgeted or concurrent maintenance may clear
  dirty metadata, so rebuilds cannot lose marks that land mid-rebuild. This
  lands the API early in the v1 completion plan (S1) because later scheduler
  and maintenance stages build on it.
- Affected docs: `docs/architecture/storage.md`,
  `docs/architecture/surface.json`
- Affected code: `include/tess/storage/world.h`,
  `tests/tess_storage_test.cc`, `tests/AGENTS.md`

## 2026-07-06 - Docs Audit Sweep Closes Drift and Adds Surface Manifest

- Changed: Final workstream of the 29-part audit remediation; documentation
  drift found "at the seams" is closed and a prevention gate is added.
  `docs/architecture/simulation.md` now documents the full current
  `include/tess/sim/` surface: `SimSchedulerStats`,
  `run_queued_operations`, the four `tick_*_path_agents` /
  `tick_*_path_agents_with_movement` wrappers and their state/options/stats
  types, the path-agent batch helpers, `MovementResult`,
  `MovementVersionCheck`, `MovementFailureCounts` /
  `record_movement_failure` / `is_transient_movement_failure` /
  `movement_versions_match`, the `PathAgentPhase` lifecycle interplay
  across layers, and the fixed-step time types including
  `FixedStepFrame::dropped_seconds`. New maintained docs
  `docs/architecture/shape.md` (Shape/ShapeTraits, coordinate types, key
  packing including the portable `detail::UInt128` and the 64-bit boundary
  guards, the default-member-initializer zero-init guarantee, `contains` /
  `manhattan_distance`, and the `TESS_ASSERT` policy) and
  `docs/architecture/diagnostics.md` (`TESS_ENABLE_DIAGNOSTICS`, event
  macros, scoped counters, and the `thread_local` scope limitation:
  counters do not aggregate across worker threads) join the maintained
  list. `docs/architecture/queued-operations.md` no longer contradicts
  itself about phase sharing: both prose passages now match
  `detail::parallel_phase_conflict` (a mutable operation conflicts with
  any overlapping operation, read-only included), and the
  `OperationFailure` enumerator list is complete (including `None`) and
  well-formed. `docs/tdd/README.md` indexes the Work Contracts and tile
  layout addenda; `docs/README.md` frames the TDD archive as original TDDs
  plus proposed addenda, non-authoritative. `docs/performance.md` marks
  the snapshot stale relative to the 2026-07-06 harness rework and
  threshold changes with an explicit regeneration TODO (no numbers
  fabricated). `README.md` lists both examples and the hooks-backstop CI
  job. Historical corrections: the 2026-06-08 "Concurrent Tile-World TDD
  Split" entry's "Affected code: none" gained a bracketed correction
  (commit 2e22c05 changed public headers), and the 2026-06-05 "One
  Millisecond Benchmark Investigation Gate" entry gained a superseded
  annotation pointing at calibrated per-benchmark ceilings. Prevention
  gate: `docs/architecture/surface.json` maps each maintained doc to the
  public symbol names it documents (233 symbols across 9 docs), and
  `tools/check_public_surface.py` extracts type and free-function names
  from every `TESS_PUBLIC_HEADERS` header (24 headers; simple line-based
  parser with a `detail`/`internal` namespace allowlist) and fails when a
  symbol is missing from the manifest. It runs as an advisory
  (`::warning`) step in the CI hooks-backstop job, with pytest coverage in
  `tests/test_check_public_surface.py` including a completeness test
  against the real tree.
- Reason: A docs audit found the architecture docs had drifted where
  subsystems meet (scheduler/tick/movement seams, phase-conflict rules,
  unindexed addenda, stale benchmark framing), and nothing prevented new
  public symbols from landing undocumented.
- Affected docs: `docs/architecture/simulation.md`,
  `docs/architecture/shape.md` (new), `docs/architecture/diagnostics.md`
  (new), `docs/architecture/queued-operations.md`,
  `docs/architecture/path.md`, `docs/architecture/topology.md`,
  `docs/architecture/block.md`, `docs/architecture/README.md`,
  `docs/architecture/surface.json` (new), `docs/tdd/README.md`,
  `docs/README.md`, `docs/performance.md`, `docs/decisions/CHANGELOG.md`,
  `README.md`, `tests/AGENTS.md`
- Affected code: `tools/check_public_surface.py` (new),
  `tests/test_check_public_surface.py` (new), `.github/workflows/ci.yml`

## 2026-07-06 - CI Gains TSan, macOS, and Advisory MSVC Platform Gates

- Changed: The sanitizer toggle is generalized — `TESS_ENABLE_SANITIZERS`
  now applies the comma-separated `TESS_SANITIZERS` cache string (default
  `address,undefined`, so existing presets are unchanged), with a
  configure-time `FATAL_ERROR` when the list combines `address` and
  `thread` (they cannot link into one binary) and
  `-fno-sanitize-recover=undefined` still applied whenever `undefined` is
  in the list. New `dev-tsan` configure/build/test presets build the suite
  with `-fsanitize=thread` and run ctest with
  `TSAN_OPTIONS=halt_on_error=1 second_deadlock_stack=1`; the preset joins
  the CI quality matrix (no extra apt packages needed). A new `macos` CI
  job on `macos-15` runs the `dev` and `dev-asan` presets plus the install
  smoke test, with no benchmark gates because bench thresholds are
  calibrated on the Linux runner family. A new advisory `windows` CI job
  on `windows-2025` (`continue-on-error: true` during shake-out; flip to
  required after two consecutive green runs on main) builds the
  condition-gated `windows-msvc` preset, runs ctest, and runs the install
  smoke under `shell: bash`. `tools/install_smoke.sh` gained
  `TESS_INSTALL_SMOKE_CONFIG` for multi-config generators: it forwards
  `--config` to `cmake --install` and the consumer build and looks for
  the consumer binary in the per-config subdirectory.
- Reason: The threaded queued executor and scheduler paths had no data
  race gate (a full local dev-tsan run passed 351/351, and a deliberate
  unsynchronized-counter race was verified to fail the gate before being
  discarded), and the library claimed macOS/Windows portability that CI
  never exercised. Platform pinning (`macos-15`, `windows-2025`) follows
  the existing anti-drift policy for `ubuntu-24.04`.
- Affected docs: `README.md`, `docs/dependencies.md`.
- Affected code: `CMakeLists.txt`, `cmake/TessProjectOptions.cmake`,
  `CMakePresets.json`, `.github/workflows/ci.yml`,
  `tools/install_smoke.sh`.
## 2026-07-06 - A* Heap Loops Gain Oracle-Backed Coverage and Mutation Guards

- Changed: Added `tests/tess_path_search_test.cc` (new binary) and
  `tests/path_test_util.h` with serpentine maze fixtures that defeat every
  pre-A* fast path (two parallel two-gap walls with gaps at opposite ends,
  start/goal displaced on both non-degenerate axes), plus independent BFS
  and Dijkstra oracles. An audit had found the unit A* heap loop was never
  reached by any Found-status test — every maze was answered by fast paths
  (heap_pushes = 0 under diagnostics), and pervasive
  `expanded_nodes == path.size()` assertions structurally required this.
  The new tests pin exact optimal costs against the oracles across
  top-down 2D, vertical 2D, and multi-chunk 3D shapes and assert
  `expanded_nodes > path.size()`; `tess_diagnostics_enabled_test` gains
  heap-push mutation guards on the same fixtures. Mutation validation
  (single-gap walls) confirmed the discriminators fail when a fast path
  answers. The heap loops themselves were verified correct: unit banded
  search and weighted heap search both matched the oracles exactly, so no
  library fix was needed. Start == goal now has pinned semantics (Found,
  single-node path, cost 0) across all public path entry points, all of
  which already behaved correctly. The flagship
  `FindsTopDown2DPathAroundBlockedTiles` weak assert
  (`EXPECT_GT(cost, 3u)`) was tightened to the exact optimal cost 9 and
  documented as plane-gap-fast-path coverage.
- Reason: Closes the audit finding that a tie-breaking, band-stride, or
  parent-reconstruction bug in the real search would ship green, and pins
  start == goal semantics (audit H4) against regression.
- Affected docs: `tests/AGENTS.md`
- Affected code: `tests/path_test_util.h`, `tests/tess_path_search_test.cc`,
  `tests/tess_diagnostics_enabled_test.cc`, `tests/tess_path_test.cc`,
  `tests/CMakeLists.txt`
## 2026-07-06 - Benchmark Harness Measures What It Claims and Checks Results

- Changed: Fixed five measurement bugs in the benchmark suite.
  `storage/world_dirty_chunks_iteration` collects into a hoisted reserved
  vector via `collect_dirty_chunks` and iterates the keys (it previously
  timed only the by-value vector allocation and never iterated; 295 ns
  before, 126 ns after on an M-series dev box). `key/coord_from_tile_key_*`
  precompute encoded keys in setup so the loop measures decode only
  (encode was double-counted; 2.67/2.84/2.76 ns before vs 0.68/0.57/0.64 ns
  after for 2d/3d/u128). The field-product cache stale-rejection and LRU
  eviction benchmarks replace per-iteration `PauseTiming`/`ResumeTiming`
  (timer overhead comparable to the measured op) with manual timing plus
  pinned iterations; their gated names gained `/iterations:N/manual_time`
  suffixes and their thresholds moved from `max_cpu_time_ns` to
  `max_real_time_ns` at the 1 ms investigation ceiling because cpu_time now
  includes the untimed cache refill (measured op: ~313 ns stale lookup,
  ~119 ns evicting store, vs ~760/~596 ns reported before). The agent
  clean-tick benchmark keeps its cheap agent reset inside the timed region
  instead of hiding it behind a Pause/Resume pair that cost as much as the
  tick. `path/weighted_chunk_portal_candidates_*` publishes the measured
  scan-tile/waypoint totals instead of a hardcoded formula, and all batch
  benchmarks publish `batch.cost_total`/`batch.expanded_total` aggregates
  instead of the last request's values under single-query counter names
  (which made per-node math ~100x off). Per benchmark-plan sections 14/20,
  benchmark families now validate the last result outside timed regions
  (aborting `bench_check`: Found status, endpoints, legal unit steps onto
  passable tiles, setup-run expected costs, agent frame stats, cache
  outcomes), and `tess_bench_diagnostics` asserts the warm
  `path/astar_open_2d` iteration allocates nothing.
  `tools/benchmark_thresholds.py` now rejects duplicate benchmark names
  (previously a dict comprehension silently kept the last duplicate),
  prefers `--benchmark_repetitions` aggregates keyed by `run_name` (median
  default, `--aggregate` flag), and reports missing/malformed files
  without tracebacks; `tools/benchmark_baseline_summary.py` filters
  aggregates by `run_type` instead of name suffixes and writes real CSV.
  New `tests/test_benchmark_tools.py` covers both tools and runs in the CI
  hooks-backstop job.
- Reason: Q10 audit remediation — several benchmarks measured allocation or
  encode instead of the named operation, per-iteration Google Benchmark
  timer overhead dominated microsecond cache ops, batch counters
  misrepresented totals, thresholds could silently gate on the wrong
  duplicate entry, and the plan-mandated correctness/allocation assertions
  were missing.
- Affected docs: `docs/planning/benchmark-plan.md`, `tests/AGENTS.md`.
- Affected code: `bench/tess_bench.cc`, `bench/tess_path_agent_bench.cc`,
  `bench/tess_path_product_bench.cc`, `bench/tess_path_weighted_bench.cc`,
  `bench/thresholds/path.json`, `tools/benchmark_thresholds.py`,
  `tools/benchmark_baseline_summary.py`, `tests/test_benchmark_tools.py`,
  `.github/workflows/ci.yml`.

## Earlier Entries

Design-changelog entries before 2026-07-07 are archived in
[`CHANGELOG-archive.md`](CHANGELOG-archive.md) to keep this file under the
24k-token per-file limit. New entries go at the top of this file; when it
approaches the limit again, its oldest entries move to the archive.
