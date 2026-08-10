# test_header_compile_cost.py

- `tests/test_header_compile_cost.py`: pytest coverage for the repeatable
  syntax-only public-header compile-cost tool. It pins the source and generated
  include paths, compiler command, elapsed-sample collection, compiler-error
  preservation, configured-header reuse, source-template fallback generation,
  and invalid repetition handling without making timing-dependent assertions
  in CI.
