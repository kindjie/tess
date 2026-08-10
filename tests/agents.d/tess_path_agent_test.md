# tess_path_agent_test

- `tess_path_agent_test`: verifies the public path-agent wrapper, including
  goal assignment, runtime-backed request/result processing, tile-by-tile
  advancement and arrival that resets a preserved blocked streak,
  conservative reprocessing after world edits,
  invalid/unreachable goal handling, weighted shared-goal processing,
  allocation-free warm unit, unit field-product, and weighted agent batches,
  the phase lifecycle (goal set/clear transitions, transient movement
  failures keeping Found status while entering Blocked, structural failures
  turning terminally Unreachable), movement validation rejection statuses
  including stale topology/version branches, `record_movement_failure`
  bucketing every `MovementStatus` into its dedicated counter, the
  transient-versus-terminal `is_transient_movement_failure` classification
  of every status, disappearing special-provider edges classified as stale
  topology, provider exceptions propagating through validation and commit
  without changing occupancy, reservations, or dirty metadata, and
  overflow-safe `manhattan_distance` at `int64` extremes. The warm no-alloc
  batches also pin submitted/found stats so a skipped frame cannot pass as
  allocation-free.
