# test_branding_assets.py

- `tests/test_branding_assets.py`: static asset and browser-demo contract
  coverage. Assertions intentionally inspect source text so hook-backstop CI
  catches contract drift without requiring Emscripten or a browser; keep
  literal checks synchronized with the templates and configuration they pin.
