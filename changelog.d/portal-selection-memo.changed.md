- Chunk-portal route selection now memoizes seam queries within a single
  selection, so the six axis orders and the greedy walk no longer re-walk
  the same chunk seam from the same tile. Route choice is unchanged; the
  `portal.scan_tiles` counter falls by roughly two thirds because that
  work no longer happens.
