# tess_path_precheck_test

- `tess_path_precheck_test`: verifies the pre-A* topology gate `precheck_path`
  and `precheck_rules_out_path`: a reachable goal within a connected region, an
  `Unreachable` verdict across a walled chunk boundary (the only status that
  licenses skipping A*), out-of-bounds and walled starts reported as
  `InvalidStart` (never ruling out), an out-of-bounds goal reported as
  `InvalidGoal`, an
  unbuilt graph as `NoGraph`, a post-build topology edit degrading to
  `GraphStale` rather than a wrong `Unreachable`, a sparse corridor exiting into
  a non-resident chunk reported as `MissingChunk`, an allocation-free warm
  precheck query, and class agreement (S5.4): a walker-labeled graph queried
  for a Builder class is `GraphStale` (never the walker's wrong
  `Unreachable` across a construction bridge), while a Builder-stamped graph
  answers `Reachable`/`Unreachable` per class and reads `GraphStale` for the
  raw walker tag. Stateful provider instance/revision mismatches likewise
  return `GraphStale`.
