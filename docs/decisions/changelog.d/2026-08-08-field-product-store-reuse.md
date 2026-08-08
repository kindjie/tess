## 2026-08-08 - Field-product stores return what they stored

- Fixed: `FieldProductCache::store` took the product by move, so the
  caller's member was left empty and the next
  `build_distance_field_product` ran `distance_.assign` against a
  zero-capacity vector — a fresh world-sized allocation on every world
  edit, cache eviction or provider revision change. The previous comment
  argued only that the moved-from state was never observed, which was true
  and beside the point: the capacity was gone, so
  `reserve_unit_field_product_nodes` helped only the first build.
- Fixed: the caller then called `lookup` to recover the pointer the store
  already had. That rescanned every entry, reconstructing the `Model`
  inside the loop, and counted a HIT — so every miss-then-build published
  one cache hit for work the cache had not reused, in the very counters
  the benchmarks report as evidence.
- Changed: `store_reusing` and `store_weighted_reusing` return the stored
  product and leave the caller holding whatever storage the cache
  displaced (the replaced entry's buffers, or an evicted one's). The
  rvalue `store` overloads keep their `bool` contract as thin wrappers.
- Scope, recorded because the first draft did not state it: a store only
  hands storage back when it displaces something. A same-key replacement
  always does, and an admission does once the byte budget forces an
  eviction, but an admission into a cache still under budget displaces
  nothing. A world edit or provider revision change produces a new key, so
  it is an admission -- meaning a runtime keeps reallocating the
  world-sized array until its product cache reaches budget, and only then
  stops. The removed relookup is saved on every build regardless. Review
  raised this against a draft that read as though every rebuild was
  covered; the allocation test exercises the displacing path only, which
  is what the claim is now limited to.
- Recorded: "existing callers are unaffected" was too strong, and a review
  pass proved why. The displaced product is handed back with its CONTENTS,
  not just its storage, so before the follow-up fix an evicting store left
  the caller's argument reporting `Found`, carrying another key's goals,
  and passing `is_valid` — strictly worse than the old moved-from state,
  which failed validity and hit the size guard. Both displacing paths now
  `clear()` the argument, which is noexcept and retains capacity, so the
  performance goal is unaffected and the argument can never be observed as
  a valid wrong-goal product.
- Fixed before merge: the replace path read `entries_[i]` AFTER eviction,
  and eviction erases from that vector — so evicting anything below `i`
  shifted it and the store returned a different entry, or indexed past the
  end. Proven twice by execution: a three-entry case returned the wrong
  key's product, and a two-entry case tripped an AddressSanitizer
  container-overflow. The pointer is now read before eviction. Not
  reachable through `PathRequestRuntime`, whose stores always take the
  append path, but live for any direct consumer of the public cache.
- Evidence is an allocation count rather than a timing, because the claim
  is about allocation and the machine was too loaded for a trustworthy
  benchmark. Mutation-verified: reverting only the hand-back gives five
  allocations where the fix gives zero.
- Recorded: two existing tests were asserting the inflated counter —
  `hits >= 1` after a single build, which only the store's own relookup
  could satisfy. They now assert that a build is not a hit and that a
  genuine reuse is exactly one. That they had to change is the clearest
  evidence the finding was real.
