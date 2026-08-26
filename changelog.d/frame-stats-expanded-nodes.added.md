- `PathAgentFrameStats` now reports `expanded_nodes`, the total search nodes
  expanded by the completed results a processing or replan-drain call applied.
  This gives callers a deterministic work meter: planning can be budgeted by
  search effort per tick where a wall-clock budget would break replay.
