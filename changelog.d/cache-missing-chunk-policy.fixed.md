- `cached_astar_path` accepts a `MissingChunkPolicy`. It previously
  hardcoded the impassable assumption with no override, so on a sparse world the
  cached call returned `NoPath` where the identical uncached call returned
  `Indeterminate` — adding a route cache for performance changed
  correctness semantics. `PathStatus::Indeterminate` exists specifically
  so a caller never mistakes "not searched" for "no route exists". The
  parameter now defaults to `ReportIndeterminate`, matching the safer default
  across public sparse-aware path entry points.
- The policy binds to the whole cache rather than joining the entry key,
  matching the existing `bind_class` and `bind_provider` precedent: a
  lookup under a different policy drops the cache and counts a
  `policy_rebinds` in `stats()`. Widening the key instead would duplicate
  `Found` entries, which are policy-invariant and are the entire
  suffix-index substrate, for a distinction that only affects the terminal
  status of searches that exhausted the resident set.
- The binding is normalized on dense worlds. No chunk can be missing under
  `AlwaysResident`, so the policy cannot change any answer there, and
  binding the caller's value would make a generic caller that alternates
  policies drop the cache for nothing. `policy_rebinds` is therefore
  always zero on a dense world.
- `Indeterminate` results stay cacheable. They are deterministic within a
  residency epoch — any evict, reload, or in-place edit changes the
  content-version fingerprint and drops the cache before a serve — and they
  come from the most expensive searches, so refusing to cache them would
  have traded a correctness trap for a silent performance one, aimed at
  exactly the callers who opted into correctness.
- `weighted_path_batch` accepts and preserves the same policy. When a shared
  reverse field touches a missing chunk, reached members still return `Found`;
  only unreached members fall back to independent A* classification. Grouping
  therefore cannot change a request's status.
