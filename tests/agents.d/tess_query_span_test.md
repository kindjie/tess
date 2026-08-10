# tess_query_span_test

- `tess_query_span_test`: verifies exact allocation-free x-run emitters for
  clipped world boxes, Euclidean radii, and chunk-local boxes across top-down,
  vertical, and 3D shapes; deterministic z/y/x ordering; edge clipping; empty
  queries; randomized reference tile-set equivalence for every query kind and
  layout; integer-minimum radius clamping and maximum-radicand integer square
  roots; and splitting runs wider than the public 32-bit span count without
  losing tiles.
