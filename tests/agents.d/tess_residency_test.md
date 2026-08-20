# tess_residency_test

- `tess_residency_test`: pins the byte-budgeted sparse world, residency
  generations, LRU directory behavior, resident-only queries, and warm slot
  reuse. The roughly 30-billion-chunk shape must remain empty at construction,
  proving storage scales with residency rather than shape. Rematerializing an evicted
  key gets a strictly newer generation and value-initialized field data,
  including accepted aggregates with nonzero defaults. The suite also fixes
  weighted-field status precedence when missing topology and cost overflow
  occur in one flood. Unit, weighted, bounded, and boxed field readers preserve
  an indeterminate build for unreached and non-resident starts.
  Content-version changes preserve residency,
  dirty, topology, active, and warm-allocation state.
