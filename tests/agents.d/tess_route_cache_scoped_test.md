# tess_route_cache_scoped_test

- `tess_route_cache_scoped_test`: verifies the unit route cache's opt-in
  `ScopedFeasible` staleness mode — off-footprint edits keep entries serving
  (survival and revalidation stats), crossed-chunk edits retire entries with
  the recomputed replacement reachable past the tombstone, dead suffix-slot
  occupants overwritten in place by the next covering store (sized so no
  index growth bypasses the branch), retirement-triggered compaction during
  a serve staying reference-safe at an entry cap of one, a seeded 200-step
  toggling-edit property holding every served route legal at truthful cost,
  non-Found results cached per epoch and retired by an unrelated-chunk
  change (whole-world, not endpoint, sensitivity), blocking-only edit
  sequences serving fresh-optimal costs, oversized dependency footprints
  skipped without evicting residents plus the aggregate dependency budget
  invalidating like the other caps, mode flips dropping all entries, and
  identical (call, edit) sequences producing identical per-call
  (status, cost, stats-delta) traces.
