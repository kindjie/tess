- Observers that hand out a span or reference into their own storage are
  now lvalue-only, so the dangling expressions they permitted are compile
  errors instead of undefined behaviour:
  - `OwnedChunkDomain::view/keys/begin/end` —
    `explicit_chunk_domain(keys).view()` returned a span into a vector
    destroyed at the end of the full expression. The deleted
    `chunk_domain(OwnedChunkDomain&&)` overload made this worse rather
    than better: a caller who hit its error would "fix" it by adding
    `.view()`, trading a compile error for undefined behaviour.
  - `ExecutionReport::plan/operations` and `ExecutionPlan::operations` —
    `plan_operations` returns by value, so the idiomatic-looking
    `for (const auto& op : plan_operations(world, ops).plan().operations())`
    iterated freed memory. `ExecutionPhase` already had a generation
    check, which made these unprotected accessors read as safe by
    comparison.
  Source-breaking only for code that was already dangling. `size()` and
  `empty()` return values and stay callable on a temporary.
- `explicit_chunk_domain`'s Doxygen claimed it "Copies, sorts, and
  deduplicates". It sorts and stops. `architecture/block.md` was always
  right, and the queued-operation layer deduplicates its own domains
  independently, so nothing depended on the promise — but a caller who
  believed it would have passed duplicates and got them back. The comment
  now says what the body does; the behaviour is unchanged deliberately,
  since deduplicating would be a silent semantic change.
- Not included: `DeltaFrame`. Holding one across a `publish()` aliases the
  vectors that become the pending accumulator, so the caller reads torn
  mid-tick data with no sanitizer signal. The audit proposed making it
  move-only, which does not fix it — a moved-into frame held across
  `publish()` tears identically. The fix is the generation gating
  `ExecutionPlan` already uses, which turns an aggregate of five spans
  into an accessor-gated view, and that is its own change.
