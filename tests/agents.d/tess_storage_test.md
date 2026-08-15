# tess_storage_test

- `tess_storage_test`: pins typed field storage, chunk metadata, coordinate and
  span access, dirty bounds, and allocation-free hot paths for dense worlds and
  resident pages. Generation-stamped dirty clearing must preserve a mark that
  lands after observation. Content-version changes must invalidate an
  earlier observation without changing dirty, topology, or active metadata,
  and remain allocation-free.
