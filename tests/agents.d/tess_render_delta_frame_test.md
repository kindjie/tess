# tess_render_delta_frame_test

- `tess_render_delta_frame_test`: verifies the M11 DeltaFrame bridge
  core: version semantics (empty publishes never bump; state-carrying
  publishes bump by one; the applicability truth table including the
  fresh-consumer-only-baseline and truncation-is-a-structural-gap
  rules); tile encoding (per-tile records matching the legacy
  `render_tile_deltas` set below the threshold, clipped box records
  above it and at threshold zero, box degradation without truncation
  when tile storage cannot hold a chunk); observed-generation dirty
  clearing (only the collected mask is consumed; a racing mark between
  observe and clear is never lost); entity recording (last-writer-wins
  move coalescing with tick stamps, barrier splits, the coalescing
  window closing at publish, per-step records with coalescing disabled,
  capacity overflow truncating the frame); `clear()` poisoning the
  stream until a baseline; baselines dropping pending entity records;
  full-scope baselines covering every chunk, consuming the mask, and
  healing the stream (with an overflowing baseline truncated and never
  applicable -- the truth table pins truncation defeating baselines);
  overlay staging copying nodes at call time with full-replacement
  per-frame semantics and overflow dropping the overlay rather than the
  frame; the section-8 acceptance through `render_delta_replay.h`'s
  RenderReplayGrid consumer model (invalidation apply re-reading the
  current world, a shadow entity map, the version contract enforced):
  randomized seeded scripts replaying per-tick, as coalesced eight-tick
  frames (Speed4x/backlog shape, coalesced entity records landing on
  final tiles), and through a lossy consumer that drops frames, detects
  the gap, resyncs via a full baseline plus entity re-snapshot, and
  reconverges; a sparse resident-set replay (no-evict script; residency
  records deferred); and an allocation-free steady-state collection
  cycle.
