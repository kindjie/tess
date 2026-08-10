# tess_path_product_test

- `tess_path_product_test`: additionally verifies resolved-model parity for
  diagonal and axial-hex products, including fixed-point cost scale and
  rejection when a product is read through another model, plus normalized
  raw-tag/class cache identity. Provider products follow stair edges and both
  product readers, cache stores, and cache lookups reject a providerless model
  or changed stateful-provider revision. It also verifies the replay-product
  invalidation contracts: products with empty dependency sets are never
  valid, failure (NoPath) route and portal-route products capture every
  chunk version so any world edit invalidates the replayed failure,
  distance-field products depend on the blocked frontier (face neighbors of
  touched chunks) so opening a fully-sealed chunk invalidates them, and
  rebuilding a portal route product from its own `waypoints()` span is safe.
