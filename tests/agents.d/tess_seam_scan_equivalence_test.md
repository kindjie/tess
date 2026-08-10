# tess_seam_scan_equivalence_test

- `tess_seam_scan_equivalence_test`: differential oracle for
  `detail::best_chunk_portal`'s page-hoisted seam fast path — compares
  found/portal/scan_tiles against a per-tile-resolve reference on a 3D
  asymmetric shape (4x8x2-tile chunks, 2x2x2 grid) across all 24 adjacent
  chunk pairs with all-pass, all-blocked, and seeded-random patterns;
  exact single portals asserted at both scan extremes of every pair;
  equal-score incomparable crossings pinning the authoritative loop
  nesting per axis (tie sets after passability filtering are not
  rectangles, so pattern tests alone cannot catch a nesting swap);
  sparse worlds with a missing chunk taking the generic fallback;
  past-the-top out-of-shape neighbors scanning the full seam and finding
  nothing; and wrapped (below-zero) unsigned chunk coordinates reading as
  non-adjacent. The fast path's passability-event short-circuit
  accounting (one event per scanned pair, a second only when the source
  passes) is asserted in `tess_diagnostics_enabled_test`.
