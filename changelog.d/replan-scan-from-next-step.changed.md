- `tess::experimental::request_replans_for_route_crossings` now begins its
  scan at the agent's next step rather than at the tile it occupies.
  Movement charges a tile's cost on entering it, so a price rise on the
  occupied tile cannot change the cost of the route that remains, and
  scanning it queued replans that could not improve anything. A route
  that revisits that tile later is still detected, at the later index.
