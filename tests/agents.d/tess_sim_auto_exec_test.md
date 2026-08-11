# tess_sim_auto_exec_test

- `tess_sim_auto_exec_test`: pins the scheduled auto-exec pipeline against its
  manual equivalent and across serial and pooled execution. Dirty metadata is
  merged per phase: merging only the last phase would lose earlier writes in a
  write-then-read split. On a throwing kernel, started work remains
  conservatively dirty, completions clear, queued operations remain recoverable,
  and the next run cannot drain ghost results. This also runs under TSan.
