# tess_path_agent_tick_test

- `tess_path_agent_tick_test`: pins dirty-gated planning and retained-route
  movement through the minimal tick wrapper. Transient occupancy waits consume
  a bounded step budget without repeated searches; callers can preserve the
  legacy terminal outcome or keep a geometrically reachable agent blocked
  without inventing `NoPath`. The deterministic recovery schedule caps due
  probes, backs them off across the full `uint32_t` delay range, resets after
  progress even when the agent ends the tick blocked again, and supports
  independent concurrent owners. The exact replan queue deduplicates requests,
  safely drains an unreserved single request and redundant empty pop, preserves
  a blocked retry streak until movement proves progress, and applies only its
  deterministic per-call budget; independent queue owners also run concurrently
  under TSan. Each individual queue remains externally synchronized. Zero-step
  ticks preserve the retry budget, while boxed-in goals under the legacy policy
  eventually become terminal and stop consuming processing. Weighted drains
  can derive deterministic per-agent equal-cost tie seeds without changing the
  bounded FIFO contract. The generic callback drain owns only queue and agent
  lifecycle: it copies borrowed results immediately, commits earlier FIFO
  entries across a later callback exception, leaves the throwing entry intact,
  and stays allocation-free after caller reservation. A warm clean tick must
  still advance every agent while allocating nothing and skipping path
  processing.
