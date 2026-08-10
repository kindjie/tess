# tess_path_agent_tick_test

- `tess_path_agent_tick_test`: verifies the minimal path-agent tick wrapper,
  including tick advancement, dirty-gated path processing, movement ordering,
  explicit dirty-mark requirements after world edits, dirty reprocessing after
  world edits and goal changes, unreachable goals, weighted shared-goal ticks,
  allocation-free warm clean ticks (pinning that path processing is skipped
  while every agent still advances), two-argument goal assignment processed
  without a manual dirty mark, transiently blocked agents resuming and arriving
  without occupancy-blind re-plans, successful plain-driver progress resetting
  a preserved blocked streak, permanent occupancy or
  reservation exhausting a bounded retained-step wait budget without repeated
  searches while zero-step ticks preserve that budget (including while another
  agent requests a scoped planning pass), a seeded multi-agent
  bottleneck reaching only arrived or explicit terminal outcomes after one
  initial planning pass, mid-route wall
  insertion triggering bounded re-paths, and boxed-in goals exhausting the
  retry budget into terminal Unreachable that stops consuming processing.
