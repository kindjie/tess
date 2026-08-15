- Fix large-agent colony stutter by separating blocked retry exhaustion from
  reachability, adding deterministic backoff/jitter and bounded exact replan
  queues, and sharing an eight-query tick budget in the browser demo.
