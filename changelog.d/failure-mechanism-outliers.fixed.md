- `detail::fail_fast` now prints its message unconditionally. It was gated
  on `TESS_ENABLE_DIAGNOSTICS`, so a release consumer — the one least able
  to reproduce the failure under a debugger — got a bare `abort()` with
  nothing naming what went wrong.
- Three preconditions that were `TESS_ASSERT` are now checked in every
  build, because in the builds that compile asserts out each one did
  something worse than nothing. `Schedule::task_stats` returned a
  default-constructed `ScheduleTaskStats`, which is all zeroes and
  therefore identical to what a registered task that has never run
  reports — the caller could not tell a bad id from an idle task.
  `Schedule::set_enabled` silently left the task in its existing state.
  `ResultChannel::value_for` asserted and then indexed anyway, which is
  undefined behaviour precisely where the assert was compiled out.
- `block.h`'s policy-view dispatch fails through `fail_fast` with a
  message instead of `assert(false)` plus a bare `std::abort()`. The
  program still terminated either way — the `std::abort()` was a separate
  unconditional statement, so `NDEBUG` removed the *diagnostic*, not the
  failure. What it removed mattered: under `NDEBUG` the consumer got a
  bare abort naming nothing, and the assertion honoured `NDEBUG` rather
  than `TESS_ENABLE_ASSERTS`, so it disagreed with every other check in
  the library about when it was live. It stays a
  runtime failure rather than becoming a `static_assert`, despite the
  condition being compile-time: the runtime-dispatching `for_each_chunk`
  instantiates this template for all four write policies whichever one the
  caller passes, so a `static_assert` would reject callbacks that accept
  only `ReadOnly` and never reach the branch.
- A new `tess_assert_ndebug_test` target compiles the assert suite with
  `NDEBUG`. Without it the new death tests would have passed against the
  old code too, since with asserts enabled both forms abort — the existing
  NDEBUG contract cell compiles but never runs, so it could not tell them
  apart.
