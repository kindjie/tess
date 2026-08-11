# tess_path_precheck_runtime_test

- `tess_path_precheck_runtime_test`: pins the optional topology precheck in the
  runtime and agent tick, including weighted survivor scattering and movement-
  class agreement. The sealed-goal fixture avoids a full-axis barrier so A*'s
  own dense fast path cannot prove the result; only the graph may produce zero
  expansions. A stale or wrong-class graph falls back to A* rather than
  returning another class's verdict.
