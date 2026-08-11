# tess_webgpu_backend_test

- `tess_webgpu_backend_test` (`TESS_ENABLE_WEBGPU` on): pins the optional
  backend against an ABI-matching stable C stub, including ownership, bounds,
  capabilities, generation, readback and device-loss contracts. Asynchronous
  completion remains safe after backend destruction, inline completion cannot
  touch callback state afterward, and every failure restores readback budget.
  Stub enum widths intentionally match the stable C ABI.
