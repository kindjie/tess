## 2026-08-08 - Field-product stores hand their displaced buffers back

- Area: `FieldProductCache::store` and the two `PathRequestRuntime` call
  sites that follow it (`path_runtime.h:586`, `:797`). Follow-up to the
  2026-08-07 instrument entry below, though not to the part of it that the
  `fields/cache_scan_entries_*` pair measures.
- Hypothesis: taking the product by move left the caller's member with no
  capacity, so the next `build_distance_field_product` reallocated a
  world-sized distance array; handing the displaced entry's storage back
  should make the rebuild allocation-free.
- Evidence: accepted, on an allocation count rather than a timing. On a
  64-tile world a rebuild after a displacing store went from five
  allocations to zero, counted with `ScopedAllocationCounter`.
  Mutation-verified: reverting only the hand-back restores the five.
  Timing was not used because the machine was too loaded during this work
  to produce a trustworthy reading, and the claim is about allocation
  rather than scan cost.
- Scope: the hand-back only fires on a store that displaces something -- a
  same-key replacement, or an admission the byte budget makes evict. An
  admission into a cache still under budget displaces nothing, so a
  runtime keeps reallocating until its product cache is full. A world edit
  or provider revision change produces a new key and is therefore an
  admission. This was stated too broadly in the first draft and is
  corrected here.
- Also fixed, and independent of the above: the call sites used to `lookup`
  the product straight back after storing it, which rescanned every entry,
  reconstructed the transition `Model` inside the loop, and recorded a
  cache HIT for work the cache had not reused. That inflated the published
  hit rate by one on every build, in the counters the benchmarks report as
  evidence. Two existing tests were asserting the inflated value. The store
  now returns what it stored, so the relookup is gone on every build, not
  only on displacing ones.
- Risk: the displaced product is handed back with its contents, not just
  its storage. Before the pre-merge fix that closed it, an evicting store
  left the caller's argument reporting `Found` with another key's goals --
  strictly worse than the old moved-from state. Both displacing paths now
  `clear()` the argument, which is noexcept and retains capacity. A second
  pre-merge fix corrected a read of `entries_[i]` after eviction had
  already shifted the vector.
