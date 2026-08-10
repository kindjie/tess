# tess_allocation_counter_test

- `tess_allocation_counter_test`: verifies the scoped allocation counter
  itself — plain/array, aligned, nothrow, and aligned-nothrow `operator new`
  forms are counted with byte totals; supported configurations reject selected
  throwing, nothrow, and aligned allocations; unsupported guards are inert;
  construction resets counters; and a fatal gtest failure inside a counting
  scope disables counting during unwind instead of poisoning later
  assertions.
