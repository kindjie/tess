# tess_path_view_test

- `tess_path_view_test`: verifies the non-owning `PathView` handed out by
  `PathResult`: a default view is empty, a view mirrors its underlying nodes
  (size, front/back, indexing, iteration, `data()` identity) without copying,
  `span()` recovers the raw span, `suffix(offset)` returns the remaining path
  sharing the same storage (composing across suffixes) and bounds-clamps at or
  past the end, view and suffix operations allocate nothing, and a real
  `astar_path` result exposes a walkable suffix into its scratch storage.
