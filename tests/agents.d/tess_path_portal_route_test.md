# tess_path_portal_route_test

- `tess_path_portal_route_test`: pins chunk-portal candidate selection and
  contiguous route assembly. The blocked-seam fixture is deliberate: every
  axis-order candidate fails and only the greedy interleaving succeeds. A
  heuristic miss or failed candidate segment is `NoCandidate` and must clear
  the partially assembled path. An isolated preferred seam proves that exact
  A* can still find a route after the heuristic candidate fails. Original
  endpoint validation matches exact weighted A*, and every stitched builder
  rejects an aggregate cost at the reserved infinity sentinel.
