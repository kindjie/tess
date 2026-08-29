# tess_replan_selection_test

- `tess_replan_selection_test.cc` — the experimental scoped-replan
  selection query (`tess::experimental::request_replans_for_route_
  crossings`): requests replans only for agents whose remaining
  retained route crosses a caller-nominated tile. Fixtures pin the
  contract directly: crossing vs non-crossing routes, neither the
  consumed prefix nor the occupied tile counting while the next step
  does (a tile's cost is paid on entering it, so the occupied tile
  cannot change the remaining route), goalless/unreachable/routeless
  agents skipped, missing route entries contributing nothing, queue
  deduplication, ascending-index drain order, and the
  first-hit-stops-consultation guarantee. Pure fixtures, no world.
