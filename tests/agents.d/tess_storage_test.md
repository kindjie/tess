# tess_storage_test

- `tess_storage_test`: pins typed field storage, chunk metadata, coordinate and
  span access, dirty bounds, and allocation-free hot paths for dense worlds and
  resident pages. Dense whole-field traversal allocates no world storage and
  deliberately leaves dirty, active, topology, and content versions untouched;
  a throwing value assignment leaves only the visited prefix written. Sparse
  worlds intentionally expose no whole-field fill. Top-down world access
  accepts `Coord2` through the canonical z-zero lift.
  Generation-stamped dirty clearing must preserve a mark that
  lands after observation. Content-version changes must invalidate an
  earlier observation without changing dirty, topology, or active metadata,
  and remain allocation-free.
