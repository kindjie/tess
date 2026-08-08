- The budgeted-progress suite gains its colony-derived cells:
  incremental region-graph updates over four toggling dirty chunks per
  event, and the queued one-op-per-chunk update path through
  AutoExecTask planning, execution, and dirty merge — both reusing the
  colony harness's deterministic map, cost, and churn machinery, with
  pre-timing equivalence against a fresh topology rebuild and
  byte-exact toggle restoration.
