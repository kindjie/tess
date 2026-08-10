# tess_assert_ndebug_test

- `tess_assert_ndebug_test`: the same source compiled with `NDEBUG`. The
  unconditional-precondition death tests in `tess_assert_test` pass
  against either the old assert-gated form or the new one when asserts
  are enabled, so only this cell distinguishes them; it is where those
  tests actually have teeth. The `TESS_ENABLE_ASSERTS` half of the file
  compiles out here.
