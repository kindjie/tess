# tess_gpu_interface_test

- `tess_gpu_interface_test`: pins the interface-only GPU contract: backend
  concepts and refusal behavior, exact world-to-mirror layout, dense and sparse
  upload descriptors, and ordered mock upload/dispatch/readback operations.
  Refused mock operations must leave no record behind.
