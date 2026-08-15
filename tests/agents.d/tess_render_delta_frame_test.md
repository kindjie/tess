# tess_render_delta_frame_test

- `tess_render_delta_frame_test`: pins frame version, applicability, encoding,
  coalescing, baseline, overlay, sparse, replay, and allocation contracts. A
  dirty mark racing observation and clearing must survive. Truncation is a
  structural gap and defeats even a baseline; a lossy consumer must detect the
  gap, resnapshot, and reconverge. Seeded replay validates both per-tick and
  coalesced backlog delivery against the consumer model. Retained-route
  overlays remain valid after `NeedsOnly` invalidates runtime tickets and for
  queue-produced routes; pairing, route coverage, and selected indices are
  asserted preconditions.
