# tess_block_test

- `tess_block_test`: pins block domains, typed contexts, policy enforcement,
  shape-aware iteration, and allocation-free reuse of caller-owned scratch.
  `BlockScratch` rejects byte-count overflow rather than wrapping to a tiny
  allocation; mixed alignments remain disjoint, and growth preserves
  `used_bytes()` while serving new requests from fresh storage.
