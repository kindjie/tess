# tess_path_portal_route_test

- `tess_path_portal_route_test`: pins chunk-portal candidate selection and
  contiguous route assembly. The blocked-seam fixture is deliberate: every
  axis-order candidate fails and only the greedy interleaving succeeds. A
  failed segment must clear the partially assembled path.
