# tess_query_span_test

- `tess_query_span_test`: pins allocation-free x-run queries against reference
  tile sets across supported shapes. Integer-minimum radii and maximum
  radicands exercise overflow boundaries; runs wider than the public 32-bit
  count must split without losing tiles.
