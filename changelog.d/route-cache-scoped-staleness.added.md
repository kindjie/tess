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
  `UnitRouteCache` users get the same machinery via `set_staleness` and
  `refresh_if_world_changed`. On the profiled steady off-route edit shape,
  the world-edit agent tick drops from ~415 us to ~130 us on the
  calibration machine; two new benchmark cells pin the survival steady
  state and the forced retire-every-tick worst case.
