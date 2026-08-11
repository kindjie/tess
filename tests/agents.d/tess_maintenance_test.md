# tess_maintenance_test

- `tess_maintenance_test`: pins scheduling, progress, concurrency, failure,
  shutdown, and allocation contracts for the experimental immediate, FIFO, and
  coalescing backends. Immediate self-scheduling must remain constant-stack and
  allocation-free; exceptions are consumed only for caller-controlled retry.
