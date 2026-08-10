# tess_path_movement_class_test

- `tess_path_movement_class_test`: verifies movement classes threaded through
  the A* leaves and weighted cores (S5.2): the `WalkableField` identity class
  matches the raw-tag unit search node-for-node on a serpentine maze,
  `LegacyWeighted<PassableTag, CostTag>` matches the tag-pair weighted search
  and weighted distance field exactly (statuses, costs, expansion counts,
  paths), a Walker routes around a construction wall the Builder cuts through
  while resolved diagonal and axial-hex models return exact scaled costs,
  provider-aware unit and weighted A* use stair edges that undercut the
  regular lattice route, provider-aware reverse fields reconstruct the same
  edge across unbounded, boxed, and bounded entry points and reject
  providerless or equal-revision/different-instance reads; reverse stair
  probing conservatively reports a missing potential foot under sparse
  `Indeterminate`; and the same stair is rejected without the provider but
  accepted by provider-aware movement commit. A
  provider edge parallel to a blocked regular diagonal remains legal from
  planning through commit; missing provider topology outranks a blocked
  regular edge, while a legal regular edge skips provider enumeration.
  Route-cache
  model binding preserves diagonal scales, bypasses invalid non-unit suffix
  arithmetic, and invalidates when a provider type, live instance, or revision
  binding changes,
  while unit and weighted runtime, retained-route agent, and tick entry points
  retain provider semantics from planning through commit,
  result aggregate initialization retains a default scale, and an
  unrepresentable exact cost reports `CostOverflow`. An obstructed axial-hex
  unit search is checked against an independent six-direction BFS oracle, and
  movement commit rejects regular zero-entry-cost destinations omitted by
  planning, provider edges cannot override that destination sentinel, and
  legal parallel provider edges remain available on valid endpoints,
  (fixed build price via `SelectCost`), unit A* accepts classes for
  passability only, class-driven weighted searches keep the sparse
  missing-chunk contract (blocked by default, `Indeterminate` on request),
  plan == commit (S5.5): every step weighted A* accepts for a class
  validates as `Moved` through `validate_movement_intent` for that same
  class, with `BlockedFrom`/`BlockedTo` per class on both endpoints; and the
  runtime class binding (S5.6): a `PathRequestRuntime` rebound to another
  class clears its unit caches instead of serving the previous class's
  cached `(start, goal)` route (counted in `class_cache_invalidations`),
  the class-form weighted ticks route and commit per class (Builder
  crosses the wall the Walker detours around; neither ever has a movement
  step rejected, since commit validates with the class the plan used), and
  a policy-triggered cache clear inside a process call never leaves the
  class binding unbound (the next class still gets its own route), and a
  DIRECT `cached_astar_path` caller alternating classes on one
  `RouteCacheScratch` is never served the other class's route (the cache
  binds itself per call; rebinds counted in `class_rebinds`).
