# tess_portal_memo_test

- `tests/tess_portal_memo_test.cc`: request-scoped portal memo unit
  coverage. The memo's key omits the goal, the world and the movement
  class, so these tests pin the two mechanisms that make the omission
  sound rather than the memo's storage: the generation stamp, which must
  retire every entry when a selection begins, and the RAII selection
  scope, under which a nested selection bypasses the memo instead of
  sharing its generation. Also covers signed six-way direction coding,
  cached failures serving no portal, saturation falling back to the
  uncached call and staying sticky rather than re-walking the table,
  scope restoration when an exception unwinds, and per-thread isolation
  — the last so that replacing the thread-local store with shared state
  is caught rather than silently leaking entries keyed for another goal.
