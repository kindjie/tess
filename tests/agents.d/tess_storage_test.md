# tess_storage_test

- `tess_storage_test`: pins typed field storage, chunk metadata, coordinate and
  span access, dirty bounds, and allocation-free hot paths for dense worlds and
  resident pages. Generation-stamped dirty clearing must preserve a mark that
  lands after observation.
