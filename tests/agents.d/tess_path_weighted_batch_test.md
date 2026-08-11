# tess_path_weighted_batch_test

- `tess_path_weighted_batch_test`: pins grouped weighted batching against the
  per-request A* oracle, including exact statistics and allocation-free warm
  reuse. Failed groups retain per-member endpoint precedence; an invalid start
  must not inherit the goal's failure. A realized bucket overflow falls directly
  back per member and withdraws the partial field's replay stamp, avoiding a
  second full flood.
