# tess_path_search_test

- `tess_path_search_test`: verifies the real A* heap search loops against
  reference oracles. `path_test_util.h` provides shared serpentine maze
  builders (top-down 2D, vertical 2D, and multi-chunk 3D shapes) that
  defeat every pre-A* fast path — two parallel walls with two adjacent
  gaps each, at opposite ends, with start/goal displaced on both
  non-degenerate axes — plus an independent unit-cost BFS oracle and a
  weighted Dijkstra oracle. Serpentine tests pin exact optimal costs
  against the oracles, walk path validity, and assert
  `expanded_nodes > path.size()` (fast paths structurally return
  `expanded_nodes == path.size()`). It also pins start == goal semantics
  (Found, single-node path, cost 0) across every public path entry point:
  unit/weighted/cached A*, plain/weighted/bounded/boxed distance fields,
  distance-field products and `nearest_target`, weighted route and portal
  route products, the weighted batch (astar-fallback and shared-field
  branches), `PathRequestRuntime` unit and weighted processing, and agent
  ticks arriving immediately when the goal equals the position.
