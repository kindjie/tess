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
