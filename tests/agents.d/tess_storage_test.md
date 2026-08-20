# tess_storage_test

- `tess_storage_test`: pins typed field storage, chunk metadata, coordinate and
  span access, dirty bounds, and allocation-free hot paths for dense worlds and
  resident pages. It verifies the `TileFieldValue` boundary, representative
  accepted and rejected values, and explicit non-converting metadata domains.
  Dense whole-field traversal allocates no world storage and deliberately
  leaves dirty/active masks and topology/content versions untouched. Sparse
  worlds intentionally expose no whole-field fill. Chunk activity and active
  category count derive from the active mask. Top-down world access
  accepts `Coord2` through the canonical z-zero lift.
  Generation-stamped dirty clearing must preserve a mark that
  lands after observation. Content-version changes must invalidate an
  earlier observation without changing dirty, topology, or active metadata,
  and remain allocation-free.
