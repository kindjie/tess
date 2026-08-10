# tess_gpu_interface_test

- `tess_gpu_interface_test`: verifies the M13 interface-only GPU layer:
  both shipped backends satisfy the `GpuBackend` concept at compile
  time; `NoGpuBackend` reports empty capabilities and refuses every
  operation; `FieldMirrorDesc` matches the live world's SoA layout
  (tiles/bytes per chunk against real spans, per-field indices, and the
  U32/I16/U8-for-bool/F32 format mapping, constexpr-checked);
  `upload_desc` points at the chunk's live span with chunk-key-major
  buffer offsets, on dense and sparse (resident) worlds; and the
  test-only `MockGpuBackend` (tests/gpu_mock_backend.h) records the
  upload -> dispatch -> readback sequence in order while refusing
  operations beyond its configured capabilities (no compute, oversized
  buffers/dispatches, explicit None readbacks) without recording them.
