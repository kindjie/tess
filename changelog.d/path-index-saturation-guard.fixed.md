- A saturated `PathAgentState::path_index` no longer restarts a consumed
  route. `path_index` is a public field with no enforced range, and the
  five stable advance paths plus the experimental replan-selection
  helper computed `path_index + 1` before their bound check; at
  `SIZE_MAX` that wraps to zero, which compares as in range. An advance
  would step the agent onto `route[0]`, and the helper would rescan a
  route its own comment calls fully consumed.
