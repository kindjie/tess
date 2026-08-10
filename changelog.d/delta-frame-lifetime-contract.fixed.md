- `DeltaFrame`'s documented lifetime was wrong in both directions and is
  corrected. It said a frame is valid "until the next mutating call on the
  collector (`begin_tick` / `record_*` / `collect_*` / `publish` /
  `clear`)". In fact `begin_tick`, `record_*`, `collect_*` and `clear()`
  fill or reset the *pending* buffers and never touch the published ones —
  which is the point of the swap in `publish()`, so the next frame can be
  recorded while the current one is applied — and `reserve()`, which
  re-reserves the published vectors and can reallocate them, was missing
  from the list entirely. The spans are valid until the next `publish()`
  or `reserve()`, and until the collector is assigned to or moved from —
  `DeltaCollector` keeps its compiler-generated copy and move operations,
  which replace or empty the published vectors. `header` is a value copy
  and outlives all of it. The same stale contract appeared a second time on
  `DeltaCollector`'s own Doxygen and is corrected there too.
- The comment now also states the hazard it only implied: holding a frame
  across a `publish()` is outside the contract, and the resulting stale
  spans carry `first_tile`/`first_node` indices that can run past the
  `tiles`/`overlay_nodes` they index, so a consumer reads out of bounds
  rather than merely reading inconsistent data. Enforcing this is tracked
  as the remaining half of audit finding API3; this change makes the
  documented hazard accurate, which is the prerequisite for deciding
  whether to enforce it.
- A new `tess_delta_frame_lifetime_test` pins the narrowed contract, since
  a comment cannot fail.
