# tess_sparse_stream_test

- `tess_sparse_stream_test`: pins streaming search against a dense reference
  under several residency budgets. A certified result must equal the dense
  optimum; an uncertified result is only a sound upper bound and may not
  contradict dense reachability. The witness is load-bearing: stopping at the
  first definitive answer yields strictly longer routes even at full budget,
  proving certification is not vacuous. Budget-fraction counts are golden,
  not monotonic comparisons, because the relationship is not monotone.
