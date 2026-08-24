- Documented the caller-side refresh obligation on the
  `cached_astar_path` declarations: direct adopters must run
  `UnitRouteCache::refresh_if_world_changed` after world edits, or a
  cache hit can serve the pre-edit route (found by the RC-1 downstream
  evaluation).
