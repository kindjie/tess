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
  `DeltaCollector`'s own Doxygen and a third time in
  `architecture/simulation.md` — the maintained page `surface.json` maps
  `DeltaFrame` to, so the one a consumer is actually sent to. All three are
  corrected.
- The two consumer-facing pages that teach the render bridge —
  `guide/presentation.md` and `getting-started.md` — now state the
  borrowing contract at all. Neither did. The guide recommends this branch
  for "different cadence, thread, or process" and for network mirrors,
  which is exactly the reader who would hold a frame across a publish, so
  it now says plainly that what crosses a thread or socket is applied
  shadow state or a copy of the records, never the `DeltaFrame`, whose
  spans point into memory the simulation thread is about to refill.
- The comment now also states the hazard it only implied: holding a frame
  across a `publish()` is outside the contract, and the resulting stale
  spans carry `first_tile`/`first_node` indices that can run past the
  `tiles`/`overlay_nodes` they index — and past the allocation itself if
  `reserve()` reallocated. In steady state the swap and clear never
  deallocate, so the read stays inside a live allocation; calling that
  "reads out of bounds" without qualification, as an earlier draft of this
  entry did, overstates it. Enforcing this is tracked
  as the remaining half of audit finding API3; this change makes the
  documented hazard accurate, which is the prerequisite for deciding
  whether to enforce it.
- A new `tess_delta_frame_lifetime_test` pins the narrowed contract, since
  a comment cannot fail.
