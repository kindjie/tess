# tess_path_product_test

- `tess_path_product_test`: pins resolved-model and provider identity for path
  products and caches, plus replay dependency invalidation. A `NoPath` product
  depends on every chunk because any edit may disprove the failure; a distance
  field also depends on the blocked frontier, so opening a sealed neighbor
  invalidates it. Invalid automatic-portal endpoints depend only on their
  endpoint chunks, so unrelated edits preserve replay while endpoint edits
  invalidate it. Empty dependency sets are never valid, and rebuilding a
  portal product from its own `waypoints()` span must be alias-safe.
