# tess_ecs_adapter_test

- `tess_ecs_adapter_test`: verifies the dependency-free ECS layer (M10):
  `EntityHandle` null/equality semantics; `TileOccupancyIndex`
  insert/erase/move basics, null/structural uniqueness refusal, backward-shift
  erase keeping probe chains intact under a dense interleaved-erasure
  sweep, and allocation-free operation after `reserve`; concept
  `static_assert`s (`EntityHandleAdapter`, `PositionAdapter`,
  `PathAgentSource`, `PathAgentSink`) against deliberately non-ECS fakes;
  `advance_path_agents_with_index` keeping field and index synchronized
  through commits and arrivals while leaving both untouched on transient
  and structural failures; and the `tick_ecs_*` pipeline over a
  plain-array store -- arrival with per-tick sync invariants, dirty-gated
  exactly-once processing (quiet ticks move without processing; re-arm
  processes once more), off-board entries excluded from collection, the
  weighted tag-pair form, and an allocation-free steady state.
