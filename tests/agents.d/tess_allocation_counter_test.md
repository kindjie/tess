# tess_allocation_counter_test

- `tess_allocation_counter_test`: pins allocation counting and failure
  injection across the complete supported `operator new` surface. The fatal
  gtest case is load-bearing: it proves scope unwinding disables counting so
  one failed assertion cannot poison later tests.
