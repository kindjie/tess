# tess_path_precheck_runtime_test

- `tess_path_precheck_runtime_test`: verifies the optional precheck gate wired
  into `PathRequestRuntime` and the agent tick. A goal sealed off by an
  enclosing wall (not a full-axis barrier, so A*'s dense fast-path cannot rule
  it out) is resolved to `NoPath` with zero expanded nodes and counted in
  `precheck_ruled_out` when a graph is supplied, while the same request without
  a graph floods A* (expanded nodes > 0) and a reachable goal still runs A*; a
  post-build topology edit degrades to A* (`GraphStale`) instead of a wrong
  verdict; a mixed weighted batch proves the survivor partition scatters results
  back to their original slots; the warm unit rule-out path is allocation-free;
  and the weighted and unit ticks surface `precheck_ruled_out` through
  `PathAgentFrameStats` while the ruled-out agent never advances. Class
  agreement (S5.4): a walker-stamped graph supplied to a Builder-class
  `process_unit_cached` rules nothing out (GraphStale) and the Builder's own
  A* routes through the construction gap, while a Builder-stamped graph rules
  out a genuinely sealed goal without searching.
