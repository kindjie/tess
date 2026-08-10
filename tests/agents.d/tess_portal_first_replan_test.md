# tess_portal_first_replan_test

- `tess_portal_first_replan_test`: verifies the opt-in `PortalFirst`
  weighted replan strategy — the cache-aware chunk builder matching the
  uncached builder cold and serving every segment from cache warm (zero
  fresh expansions), an accepted singleton staying legal within the
  pinned premium cap with attempt/accept stats, a cap of 1/1 forcing
  premium rejection with byte-identical exact-A* fallback (status, cost,
  cost_scale, expansions, and path), two distinct-goal singletons in one
  batch not aliasing through the shared product workspace, a mixed batch
  conserving the stats identity (attempts == accepted + no_candidates +
  verification_failures + premium_rejections) with multi-goal groups
  bypassed, the default strategy remaining exact with zero portal
  stats, an aggregate segment-cost overflow reported as CostOverflow and
  served by the exact fallback (never Found with an unrepresentable
  cost), a zero premium denominator normalizing to 1 rather than
  silently disabling the cap, out-of-shape endpoints counting no
  attempt, a compile-time-ineligible movement class taking the exact
  path with the ineligible_fallbacks counter visible, and identical
  batch sequences producing identical results and portal stats.
