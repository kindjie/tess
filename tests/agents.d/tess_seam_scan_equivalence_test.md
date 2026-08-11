# tess_seam_scan_equivalence_test

- `tess_seam_scan_equivalence_test`: differential oracle for the page-hoisted
  seam fast path across every adjacent pair of an asymmetric 3D shape. The
  equal-score cases pin authoritative loop nesting because filtered tie sets
  are not rectangular and ordinary pattern tests cannot catch a nesting swap.
  Missing sparse pages take the generic fallback. Passability-event accounting
  is owned by `tess_diagnostics_enabled_test`.
