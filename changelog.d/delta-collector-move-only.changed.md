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
- A moved-from collector must be destroyed or assigned to, not reused.
  That is narrower than the standard's valid-but-unspecified and narrower
  on purpose: the defaulted move transfers the buffers but copies the
  protocol scalars, so a moved-from collector keeps its version and its
  baseline flag. Re-reserving it and collecting against the same world
  after the destination has collected would find the dirty bits already
  consumed and publish an applicable, untruncated, empty frame whose
  version chain still looks continuous — the same silent-loss shape the
  deleted copy operations caused, reached a different way. It is
  documented rather than enforced because enforcing it means hand-writing
  a twenty-member move so the source can be poisoned, which would silently
  drop any member added later.
