- Added `tess::experimental::request_replans_for_route_crossings`
  (`tess/experimental/path_agent_replan_selection.h`): asks the replan
  queue for exactly the agents whose remaining retained route crosses
  a caller-nominated tile, the scoped-replanning discipline that keeps
  periodic cost-field edits (congestion pricing, tolls, seasonal
  terrain) from replanning every agent. Experimental tier; contract
  pinned by direct fixtures.
