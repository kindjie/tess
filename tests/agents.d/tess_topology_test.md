# tess_topology_test

- `tess_topology_test`: verifies local chunk-region labeling, blocked-tile
  region rejection, boundary exits, invalid chunks, inter-chunk portal pairing,
  reachability, axial-hex local, diagonal-chunk, and provider-seam
  connectivity, and top-down 2D, vertical 2D, and 3D degenerate-axis behavior.
  Reachability coverage includes same-region, multi-hop, disconnected, enclosed,
  blocked-seam, invalid endpoint, and vertical 2D cases. It also verifies
  region bounds for known 2D and 3D layouts, Z-face portal pairing across
  stacked chunks, multi-region-per-chunk seam pairing, the checked 1-based
  `region(id)` accessor, the dense region index (`region_count`,
  `region_index`, sentinel rejection), CSR reachability parity against a
  portal-scan reference BFS on a seeded multi-chunk maze including visited
  counts, and `update_region_graph` equivalence with full rebuilds: empty
  dirty-set no-op, invalid dirty-chunk rejection without mutation,
  allocation-failure preservation before incremental mutation,
  revision-invalidated clearing after incremental mutation begins, and
  full-build allocation failure clearing every partial label/index and its
  freshness stamp,
  single-chunk and two-chunk seam edits, all-chunks-dirty rebuilds, and 40
  seeded single-tile edits compared graph-for-graph (regions, portals,
  region contents, and reachability probes) after every edit.
