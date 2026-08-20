# tess_assert_test

- `tess_assert_test`: verifies the `TESS_ASSERT`/`TESS_ASSERT_MSG` debug
  precondition policy — death tests for out-of-shape coordinates and
  out-of-range keys on unchecked accessors (`World::resolve`, `World::chunk`,
  `World::meta`, `tile_key`),
  `TESS_ASSERT_MSG` aborting with the caller's custom message and passing
  silently when the condition holds, that the disabled forms do not evaluate
  their conditions, and that guarded accessors stay `noexcept`. It also
  covers runtime contracts checked unconditionally rather than under
  `TESS_ASSERT`: path-result ticket publication, Schedule ids/lifecycle and
  result accounting, queue setup and reentrant mutation (including tick
  observation), EventStream setup/assignment, raw async callbacks, and
  content/topology/residency version exhaustion. The
  same source also proves over-budget async progress settles `Failed` rather
  than asserting.
  `ResultChannel::value_for` is hardened the same way but is NOT covered
  here: it is a private producer hook reachable only from the friended
  execute wrappers, so no test can call it without becoming a friend.
- `IntentPayloadView::as<T>` is covered here rather than beside the rest of
  the payload-view tests because its two configurations disagree and both
  are contract: asserts on, a wrong-type or unbound read aborts; asserts
  off, it falls back to an empty span. Only this source is compiled both
  ways (`tess_assert_ndebug_test`), so the release half is unobservable
  anywhere else.
