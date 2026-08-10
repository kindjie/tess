# tess_sim_scheduler_test

- `tess_sim_scheduler_test`: verifies the simulation integration slice,
  including movement intent validation and commit, fixed-step accumulator
  pause/speed/clamp behavior with exact interpolation alpha values at known
  accumulator states, render-delta collection from dirty bounds including
  chunk-border and out-of-shape clipping, scheduler-driven render-dirty
  clearing after collection, queued-edit pathing invalidation with reroute to
  arrival around the edited tile, rejected-plan early return that reports
  planned-but-not-executed operations while leaving the world untouched and
  still ticking agents, unit and weighted movement scheduler occupancy
  commits, weighted cost-band detour routing, and movement dirty-mask
  metadata interplay for nonzero and zero masks.
