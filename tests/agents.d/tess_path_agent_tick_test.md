# tess_path_agent_tick_test

- `tess_path_agent_tick_test`: pins dirty-gated planning and retained-route
  movement through the minimal tick wrapper. Transient occupancy waits consume
  a bounded step budget without repeated searches; callers can preserve the
  legacy terminal outcome or keep a geometrically reachable agent blocked
  without inventing `NoPath`. The deterministic recovery schedule caps due
  probes, backs them off, resets after progress, and supports independent
  concurrent owners. The exact replan queue deduplicates requests and applies
  only its deterministic per-call budget; independent queue owners also run
  concurrently under TSan. Each individual queue remains externally
  synchronized. Zero-step ticks preserve the retry budget, while boxed-in
  goals under the legacy policy eventually become terminal and stop consuming
  processing. A warm clean tick must still advance every agent while
  allocating nothing and skipping path processing.
