- `IntentPayloadView` gains `holds<T>()` and `bound()`, and `as<T>()` now
  asserts `holds<T>()` instead of answering a wrong-type query with an empty
  span. That span was also what a correctly-typed empty batch returned and
  what an operation carrying no payload returned, so a caller that asked for
  the wrong type processed nothing every frame with no signal to distinguish
  it from a quiet frame. Consumers dispatch on `QueuedOperation::kind`, which
  fixes the type, so a mismatch is a caller bug rather than a condition to
  branch on; `holds<T>()` answers the question where it is genuinely open,
  and `bound()` separates "no payload" from "empty batch".
- Source-compatible, behaviour-breaking in one direction only: a build with
  assertions enabled now aborts where it previously returned an empty span.
  With assertions compiled out the empty-span fallback remains, so a release
  consumer keeps the old behaviour rather than reading one type's bytes as
  another's. Both halves are pinned — the abort by death tests, the fallback
  by the NDEBUG cell that compiles the same source.
- `holds<T>()` documents what its identity token actually guarantees: the
  address is unique per type within one binary image, so a payload built in
  another image does not hold its own type here. `bound()` documents what it
  cannot do — this is a non-owning view, and a dangling `data` pointer is
  indistinguishable from a live one.
- `ResumableWorkQueue`'s five mutators keep their `bool` and gain
  documentation of what it means: whether the call changed the state. The
  three causes behind `false` are already separable through `state(ticket)`
  (`Unbound` for an unknown or retired ticket, the settled state for one
  some earlier call terminalized), and reentrant mutation during `advance()`
  is a contract violation rather than an outcome. `mark_stale_if_version`
  adds a fourth and ordinary case — the version already matches — which is a
  no-change result, not a failure; a caller that retries on it retries
  forever.
