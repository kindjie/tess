# tess_residency_test

- `tess_residency_test`: pins the byte-budgeted sparse world, residency
  generations, LRU directory behavior, resident-only queries, and warm slot
  reuse. The roughly 30-billion-chunk shape must remain empty at construction,
  proving storage scales with residency rather than shape. Reloading an evicted
  key gets a strictly newer generation and zeroed data. The suite also fixes
  weighted-field status precedence when missing topology and cost overflow
  occur in one flood. Content-version changes preserve residency,
  dirty, topology, active, and warm-allocation state.
