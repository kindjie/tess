# RC-1 downstream evaluation: retained evidence

The complete proposed stable surface, evaluated in one substantial
anonymized downstream plus one targeted consumer for the surfaces the
first does not reach, recorded as one evaluation per the plan.

## Relationship disclosure, first

The primary downstream is a co-developed reference consumer: it evolved
alongside this library, its maintainer is this library's maintainer, and
it has shaped the surface it evaluates. That is admissible under the
plan and it bounds what this gate establishes: findings only an
unfamiliar reader can produce -- unclear naming, docs that assume
context, missing affordances nobody inside asked for -- are OUTSIDE its
reach. The plan's cheap supplement (one outside read of the public
surface and upgrade guide, no integration) remains OPEN and is
deliberately left to an actual outside reader rather than simulated.

## Status of the primary downstream (disclosed mid-evaluation)

Between the plan's coverage snapshot and this evaluation, the
downstream project deliberately retired the very harness that supplied
its tess coverage (its decision D-0003, 2026-08-19): the 2D game-state,
topology, agents, and tests were removed from its maintained
implementation -- preserved through git history -- and replaced by a
minimal storage-contract probe while the project pivots to a different
prototype roadmap. The migration below therefore exercised that
harness AT ITS FINAL STATE, as the largest available body of real
downstream usage, and its result grades the upgrade guide; it was not
merged into the downstream's live line (the migration PR was closed
against the retirement decision, branch retained). Consequences stated
plainly: the coverage measured in this record describes the retired
harness, no live external consumer currently tracks the proposed
surface beyond that probe, and the outside cold-read (F6) plus the
repository's own examples and demos remain the living coverage.

## The upgrade leg (the largest single finding)

The reference consumer predated the v0.13 terminology migration and did
not compile against the proposed surface: seventeen-plus distinct
compile errors across its core, tests, and renderer. The migration was
performed strictly through `docs/upgrade-1.0.md`, and the guide earns a
strong grade: EVERY failing spelling was covered by a guide bullet,
including the subtle semantic ones (`last_result` presence semantics;
`RemainBlocked` as the new exhaustion default with the explicit
`MarkUnreachable` recipe; `AssumeImpassable` for policy-relative
definite verdicts; the movement-class composition replacing the removed
passable/cost template pair). The consumer's suite is green against the
proposed surface after the migration (70/70).

Three RECIPE gaps -- cases the guide names but does not show the
consumer-side pattern for -- are recorded as deferred documentation
improvements, not blockers:

1. `DeltaFrame` members: collector-issued frames no longer default
   construct, so a consumer that stores "the last published frame" as a
   member needs `std::optional` plus accessor calls; the guide documents
   the accessors but not the storage recipe.
2. `build_region_graph` returns the status-free `RegionGraphBuildResult`
   while `update_region_graph` returns `TopologyBuildResult`; a consumer
   unifying both in one stored member must bridge the shapes by hand.
3. Under `MarkUnreachable`, `last_result` is CLEARED at the terminal
   transition ("a retry clock is not a path search"), so pre-migration
   assertions of `status == NoPath` become assertions of absence -- the
   guide states the clearing but a migration example would have saved a
   round trip.

## Coverage, measured rather than assumed

The plan characterized the consumer as strong on maintenance and
worker-pool, light on sparse/persistence, and barely touching
pathfinding. Measurement INVERTS most of that, and the correction is
part of this record: the consumer exercises pathfinding heavily
(region graphs and incremental updates, topology prechecks with graph
gating, weighted movement classes, the full EnTT agent lifecycle and
tick pipeline, delta-frame rendering, operation batches, worker-pool
executors, path invalidation through `mark_pathing_dirty` and cache
policies, plus a soak test) and does not touch maintenance adapters,
persistence archives, the unit route cache, versioned-edit
invalidation (`mark_content_changed` / `observe_dirty`), or
distance-field products at all.

The targeted consumer (`programs.md`, `targeted.txt`) closes exactly
that measured gap as an adopter against public headers: the unit route
cache under version-marked edits; maintenance marks, budgets, and
flush points with derived products; a persistence schema round trip;
and sparse residency search-policy verdicts (`ReportIndeterminate`
default vs explicit `AssumeImpassable`). All contracts held.

## Findings ledger

- **F1 (fixed, consumer-side):** the consumer lagged the v0.13
  migration entirely; migrated per the guide, suite green.
- **F2 (accepted fix, this PR):** a direct `cached_astar_path` +
  `UnitRouteCache` adopter who follows the versioned-edit contract
  (`field` write + `mark_content_changed`) still receives the PRE-EDIT
  route -- through a now-closed wall -- unless they also call
  `refresh_if_world_changed`. The obligation IS documented, but only
  on the definition in `route_cache.h` ("staleness detection is the
  caller's job"); the declarations in `path.h` -- the first thing a
  header reader or IDE hover reaches, and where this adopter read --
  say nothing, and `PathRequestRuntime` performs the refresh for its
  own users, which hides the sharpness from anyone reading
  runtime-based examples. Accepted fix: the declaration site now
  carries the contract note and points at the full statement. The API
  is unchanged (folding the refresh into every call would charge every
  lookup for the O(chunk_count) version scan by design).
- **F3 (recorded, not acted on):** the build/update region-graph
  result-type asymmetry (guide gap 2 above); unifying the result types
  is an API change deliberately not taken during the compatibility
  freeze.
- **F4 (recorded, not acted on):** the plan's own coverage
  characterization of the consumer was stale; corrected here by
  measurement rather than silently.
- **F5 (deferred docs):** the three guide recipe gaps above.
- **F6 (open):** the outside cold-read supplement, deliberately left
  to a human outside reader.

## Deferrals

No breaking finding surfaced: nothing in the migration or the targeted
consumer required a surface change, so the "final breaking release"
claim stands untouched. The deferred items are the documentation
improvements (F5), the result-type unification (F3), and the outside
read (F6).
