- `DeltaCollector` is now move-only. A copy silently duplicated all five
  published and pending buffer pairs, and left two collectors each
  believing they were the sole owner clearing the dirty bits they
  collected. Collection *consumes* those bits, so a second collector over
  the same world observes nothing and publishes an empty frame that still
  advances its own version — a consumer on that chain misses every
  invalidation with no signal. Nothing in the tree copied one, so this
  breaks no existing code.
- It stays movable, because factories return a collector by value.
  Declaring the copy operations — even as deleted — suppresses the
  implicit move operations, so those are defaulted explicitly rather than
  left silently absent; a test pins both halves. Moving still invalidates
  a live `DeltaFrame`, whose spans then point into buffers the destination
  owns, and the lifetime contract says so.
- A moved-from collector now behaves as if `clear()` had been called on
  it: its next publish is forced truncated, so a consumer on its chain
  resyncs rather than accepting an empty frame as an applicable no-op.
  Deleting the copy operations alone would have *relocated* the hazard
  rather than removed it — the defaulted move transfers the buffers but
  copies the protocol scalars, so a moved-from collector kept its version
  and baseline flag. Review reproduced the consequence: re-reserve the
  source, let the destination collect first, and the source publishes an
  applicable, untruncated, empty frame on a chain that still looks
  continuous.
- The poison lives in the move operations of a one-member sentinel rather
  than in a hand-written `DeltaCollector` move, so both enclosing moves
  stay `= default` and every member — including any added later —
  participates memberwise as usual. Hand-writing the enclosing move would
  have silently dropped a future member, which is the same class of silent
  failure as the bug.
- Bounds, stated so the fix is not oversold: this closes the moved-from
  chain-continuity hole only. Two live collectors clearing one world's
  dirty bits remains the sole-clearing-owner contract, and no type-level
  check can enforce it.
- Reuse a moved-from collector by assigning a fresh one to it, or by
  `clear()` and a baseline collection. `reserve()` looks like a reset and
  is not one.
