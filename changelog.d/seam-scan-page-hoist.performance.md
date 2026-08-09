- Portal-route seam scans resolve their two chunk pages once and walk
  local tile ids instead of running a full coordinate resolution per seam
  tile. Portal selection is identical — iteration order, scoring,
  tie-breaking, and scan accounting are unchanged, and the per-tile loop
  remains the authority for out-of-shape or non-resident chunks. On the
  Steam Deck the goal-churn portal tick drops 21% and candidate selection
  11%; Apple M3 is flat, plausibly because the wider core already hides
  the resolve arithmetic behind the seam-tile loads.
