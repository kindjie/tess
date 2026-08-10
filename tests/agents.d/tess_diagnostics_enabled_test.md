# tess_diagnostics_enabled_test

- `tess_diagnostics_enabled_test`: verifies public diagnostic macros evaluate
  exactly once when `TESS_ENABLE_DIAGNOSTICS` is defined, and that scoped path
  and queued phase counters record generic diagnostic events including weighted
  cost reads and queued partitioned execution. Exceptional dirty coalescing
  counts every input record while counting each merged chunk once. It also
  links diagnostic
  allocation hooks and verifies scoped allocation counters observe global
  `new`/`delete` (via sanitizer malloc/free hooks under ASan/TSan). It
  hosts the serpentine-maze mutation guards: unit and weighted searches on
  the `path_test_util.h` fixtures must record `heap_pushes > 0`, which
  permanently fails if a future fast path learns to answer the mazes
  without the heap loop; the unit maze additionally pins the
  initialization, start/goal endpoint-check, and closed-neighbor counters.
