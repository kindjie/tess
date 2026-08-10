# tess_webgpu_backend_test

- `tess_webgpu_backend_test` (`TESS_ENABLE_WEBGPU` on): compiles the optional
  backend against an API-matching stable WebGPU C stub. It verifies device and
  queue ownership, overflow-safe mirror registration, `CopySrc` validation
  for readback products with realistically flagged source buffers,
  rejection of readback size/offset/callback/userdata metadata without a
  source,
  representable uploads,
  real chunk-byte uploads, compute submission only for registered mirrors and
  within the configured workgroup-X limit, generation-stale product rejection,
  source-less and `None` readback refusal, a successful `Summary` baseline,
  matched-registry default refusal and explicit `FullField` opt-in,
  asynchronous summary readback that safely completes after backend
  destruction, inline map completion without post-callback use, null map-future
  cleanup and budget restoration, overlapping readback budget recovery with
  out-of-order map failure, fail-closed reported
  device errors, invalid requests, explicit device-loss fallback, and the
  `GpuBackend` concept. Stub
  handles use scoped C-API release owners so fatal assertions cannot leak
  caller references, stub copy and mapped-read entry points abort on
  out-of-bounds ranges instead of corrupting the test process, and stub enum
  widths intentionally match the stable C ABI.
