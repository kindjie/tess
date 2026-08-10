# tess_path_portal_route_test

- `tess_path_portal_route_test`: verifies chunk-portal route product corner
  cases: invalid endpoints reported before any candidate search, sealed
  start chunks yielding NoPath after all seven candidates (six axis orders
  plus greedy) fail, segment failure clearing the partially assembled path,
  a blocked-seam layout where only the greedy interleaved candidate finds a
  route (axis-order candidates all fail), and a multi-seam L-shaped route
  crossing several chunk boundaries with a contiguous stitched path.
