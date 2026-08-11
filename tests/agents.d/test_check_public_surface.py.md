# test_check_public_surface.py

- `tests/test_check_public_surface.py`: pins the required public-surface
  manifest gate's namespace-scope extraction, exclusions, diagnostics, and
  real-tree completeness. It inventories public types and free functions, not
  members or `detail`/`internal` symbols.
