# tess_sparse_stream_test

- `tess_sparse_stream_test`: the S3 sparse-streaming scenario
  (`tests/sparse_stream_harness.h`) searching the S1 terrain in a
  `SparseResidentWorld` under budget fractions of the world's chunks,
  streaming chunks in and retrying on `PathStatus::Indeterminate`
  against a dense reference, in two strategies: stop at the first
  definitive answer, or stream on until optimality can be certified.
  Pins fully-resident equivalence, and stream-and-retry convergence: the loop streams past the first
  definitive answer until nothing further could change it, and a
  certified answer must equal the dense optimum exactly. Where the
  budget cannot certify, it pins soundness instead (an uncertified
  cost is an upper bound, never below the optimum; never a path the
  dense world denies; never a NoPath contradicting it; a loop that
  gives up reports Indeterminate; any other definitive status fails),
  with a witness test proving the bound is exercised rather than
  vacuous — stopping at the first answer reports strictly longer
  routes even at full budget, which is the measured cost of not
  certifying (optimization log). Also the budget ceiling at every step, a streaming golden
  across three budget fractions (the relationship is not monotone, so
  the counts are pinned rather than compared), section 3.3's identities
  over residency admission, coalesced hits and eviction checked against
  the world's own resident count, and determinism. About 1.3 s in Debug
  and 3 s under ASan.
