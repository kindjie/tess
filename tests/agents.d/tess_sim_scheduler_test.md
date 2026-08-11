# tess_sim_scheduler_test

- `tess_sim_scheduler_test`: pins the fixed-step simulation integration across
  movement, queued edits, path invalidation, render deltas, and unit and
  weighted scheduling. A rejected plan reports planned-but-unexecuted work and
  leaves the world untouched, but agents still tick.
