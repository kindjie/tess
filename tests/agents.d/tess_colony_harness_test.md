# tess_colony_harness_test

- `tess_colony_harness_test`: the S2 colony macro-harness
  (`tests/colony_harness.h`) driving 100 agents with goals through the
  production stack — schedule loop, auto-exec queued ops with a result
  channel, weighted path agents with movement, incremental region-graph
  topology, and render-delta publishes — over terrain raster-scaled
  from the S1 logical map. Pins forward progress and the exact churn
  accounting (one queued operation per distinct chunk, acked tiles,
  replan passes, delta publishes), the quiet no-churn contrast, and the
  metamorphic gates: serial == pool (with `pool_phases` asserted so the
  comparison cannot pass on an idle task), worker-count invariance,
  chunk-size invariance, field-payload-width invariance, clearing the
  runtime caches, and incremental topology == a fresh rebuild both
  after every churn event (before agents move) and at end of run. Also
  pins the flow-accounting admission and retention identities with
  agents still in flight, and a serial-only outcome golden (steps,
  arrivals, terminal buckets) that catches drift moving every
  configuration equally. About 3 s in Debug and 14 s under ASan.
