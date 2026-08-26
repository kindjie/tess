# tess_replan_selection_test

- `tess_replan_selection_test.cc` — the experimental scoped-replan
  selection query (`tess::experimental::request_replans_for_route_
  crossings`): requests replans only for agents whose remaining
  retained route crosses a caller-nominated tile. Fixtures pin the
  contract directly: crossing vs non-crossing routes, the consumed
  prefix not counting while the CURRENT tile does (the contract detail
  a stable promotion must settle), goalless/unreachable/routeless
  agents skipped, missing route entries contributing nothing, queue
  deduplication, ascending-index drain order, and the
  first-hit-stops-consultation guarantee. Pure fixtures, no world.
