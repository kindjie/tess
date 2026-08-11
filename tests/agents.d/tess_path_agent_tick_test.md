# tess_path_agent_tick_test

- `tess_path_agent_tick_test`: pins dirty-gated planning and retained-route
  movement through the minimal tick wrapper. Transient occupancy waits consume
  a bounded step budget without repeated searches; zero-step ticks preserve
  that budget, while boxed-in goals eventually become terminal and stop
  consuming processing. A warm clean tick must still advance every agent while
  allocating nothing and skipping path processing.
