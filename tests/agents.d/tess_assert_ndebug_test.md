# tess_assert_ndebug_test

- `tess_assert_ndebug_test`: the same source compiled with `NDEBUG`. The
  unconditional-precondition death tests in `tess_assert_test` pass
  against either the old assert-gated form or the new one when asserts
  are enabled, so only this cell distinguishes them; it is where those
  tests actually have teeth. The `TESS_ENABLE_ASSERTS` half of the file
  compiles out here.
- It also owns the only observable half of `IntentPayloadView::as<T>`'s
  release contract. The `#else` branch asserts that a wrong-type or
  unbound read falls back to an empty span rather than reinterpreting the
  batch — the debug build aborts before reaching that path, so no other
  target can see it. Both branches are contract; neither alone states it.
