# tess_maintenance_test

- `tess_maintenance_test`: pins scheduling, progress, concurrency, failure,
  shutdown, and allocation contracts for the experimental immediate, FIFO,
  queued-coalescing, and registered dirty-bit backends. Immediate
  self-scheduling must remain constant-stack and allocation-free. Dirty-bit
  setup publication, generation-safe world clearing, canonical archive
  equivalence, claimed-task retention, and concurrent producer/drain behavior
  are explicit contracts; exceptions consume only the failing request for
  caller-controlled retry.
