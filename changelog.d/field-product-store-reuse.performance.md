- Field-product stores no longer discard the caller's buffers or rescan the
  cache to recover what they just stored. `store` took the product by move,
  leaving the caller's member empty, so the next rebuild reallocated a
  world-sized distance array — about 1 MiB on a 512x512 world, on every
  world edit, cache eviction or provider revision change. The caller then
  had to `lookup` the product back, which rescanned every entry and
  recorded a cache HIT for work the cache had not reused, inflating the
  published hit rate by one on every build. `store_reusing` returns what it
  stored and hands the caller the displaced entry's storage, cleared. The
  rvalue `store` overloads keep their `bool` contract; their argument is
  still left empty, though on a displacing store it now owns the displaced
  buffers rather than its own. Measured on a 64-tile world: rebuilding
  after a store went from five allocations to zero.
