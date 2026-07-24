# GPU Backends

The GPU layer in `include/tess/gpu/` keeps tess's deterministic CPU results
authoritative while allowing derived work to be submitted to an optional
accelerator. Descriptors and the `GpuBackend` concept remain dependency-free.
`WebGpuBackend` is an independently gated implementation using the stable
WebGPU C API; CPU-only consumers do not include or link a GPU dependency.
Everything lives in `tess::gpu`.

## Public Surface

- `GpuFieldFormat` is the storage format of one field's per-tile value,
  derived from the schema value type (unsigned/signed 8-64 bit and F32; bool
  maps to U8, its storage size).
- `FieldMirrorDesc` describes one field mirrored to the GPU:
  `field_index`, `format`, `value_bytes`, `tiles_per_chunk`,
  `bytes_per_chunk`, `chunk_count`, and `total_bytes()`. A dense mirror stores
  chunk-key-major field slices.
- `field_mirror_desc<World, Tag>()` computes that description from
  compile-time layout facts. Unsupported value types and overflowing dense
  shapes fail at compile time. Selective sparse mirrors can reuse the same
  descriptors with caller-selected offsets.
- `UploadDesc` identifies one live chunk field span and its mirror-buffer
  destination. `upload_desc<Tag>(world, chunk_key)` derives it; the span stays
  valid only until the world mutates or evicts that chunk.
- `GpuProductHandle` combines a caller product key with a backend generation.
  Dispatch and readback descriptors carry both values, so stale handles fail
  after unregister and slot reuse.
- `DispatchDesc` identifies a product, input field, chunk count, and workgroup
  count. `ReadbackPolicy` and `ReadbackDesc` make result transfer explicit:
  `None`, `Summary`, `SelectedTiles`, `SelectedPath`, or explicit/debug-only
  `FullField`.
- `GpuCapabilities` reports compute, asynchronous dispatch/readback, buffer
  and dispatch limits, and alignment. All-false/zero means never choose GPU.
- `GpuBackend` is the compile-time backend concept. Its `upload`, `dispatch`,
  and `readback` methods return whether work was accepted and submitted, not
  whether asynchronous device work has completed. Refusal always leaves the
  caller free to execute the authoritative CPU path.
- `NoGpuBackend` is the default dependency-free implementation and refuses
  all operations.

## Optional WebGPU Backend

Define `TESS_ENABLE_WEBGPU`, include the consumer's stable
`<webgpu/webgpu.h>` C header first, and then include
`<tess/gpu/webgpu_backend.h>`. `WebGpuBackend` retains a supplied device and
queue. `WebGpuBackendConfig` sets its budgets, `WebGpuProductDesc` registers
provider resources, and `WebGpuReadbackStatus` reports callback completion.
The backend exposes these bounded setup and execution operations:

- Field registration creates a storage/copy-destination buffer. Uploads use
  `wgpuQueueWriteBuffer` and enforce the WebGPU four-byte offset and size
  alignment rules. Wrapped mirror totals and byte counts that cannot narrow to
  the host API's `size_t` are rejected.
- Product registration accepts consumer-created pipelines, bind groups, and
  a bounded source range for readback. Registration returns a generation
  handle; unregister/reuse invalidates old descriptors.
- Dispatch validates the product generation, field, chunk budget, and total
  workgroup-X count before encoding a real compute pass and submitting it to
  the queue. The configured X limit defaults to WebGPU's guaranteed 65,535.
- Readback allocates one map-read staging buffer per accepted request, encodes
  a source copy, submits, and reports completion through
  `WebGpuReadbackCallback`. In-flight request count and total bytes are
  bounded. Each accepted operation owns its staging resource until the map
  callback, including if the backend object is destroyed first. Stable-C
  spontaneous delivery may invoke the application callback inline or on an
  arbitrary thread. Its mapped data is callback-scoped, userdata must be
  synchronized, and it must not re-enter this backend or call WebGPU functions
  that are not explicitly documented as spontaneous-callback-safe. Because an
  accepted callback may outlive product unregistration or the backend, the
  consumer must revalidate its generation and authoritative world version
  before applying derived bytes.
- Device loss and explicit notification disable further GPU submissions.
  Full-field readback is disabled unless the configuration opts in.

Pipelines, shader meaning, and bind-group layouts remain algorithm/provider
responsibilities. This keeps tess from inventing a universal shader ABI and
keeps GPU products derived: simulation code must validate or recompute any
gameplay-exact answer on the CPU.

## Testing

`tests/gpu_mock_backend.h` exercises descriptor ordering without a device.
`tess_webgpu_backend_test` uses an API-matching fake stable C device to test
resource ownership, generation invalidation, bounded asynchronous readback,
disabled configuration, and device loss. The documentation build also
compiles and runs a browser smoke example with Emdawnwebgpu's exact pinned
port. A browser without an adapter is an explicit unsupported result; a
device or backend failure, including an initialization or readback timeout, is
not. There is no timing gate until measurements can be calibrated across a
representative browser/GPU matrix.
