# GPU Backend Interface

The M13 layer: interface only, in `include/tess/gpu/`. GPU execution is
optional and unimplemented in the current pre-1.0 release -- CPU stays
authoritative for every
gameplay-exact result, GPU products are derived/cached/versioned by a
future backend, and the acceptance bar is that a real backend can be
added later without redesigning core. Nothing here touches a GPU API or
adds a dependency; descriptors are byte-level facts about storage tess
already owns, and the backend seam is compile-time polymorphic (a
concept, never virtual dispatch). Everything lives in `tess::gpu`.

## Public Surface

- `GpuFieldFormat` is the storage format of one field's per-tile value,
  derived from the schema's value type (unsigned/signed 8-64 bit and
  F32; bool maps to U8, its storage size).
- `FieldMirrorDesc` describes one field mirrored to the GPU:
  `field_index`, `format`, `value_bytes`, `tiles_per_chunk`,
  `bytes_per_chunk`, `chunk_count`, and `total_bytes()`. tess pages are
  SoA per chunk, so a chunk's field values are one contiguous run and a
  mirror buffer is `chunk_count` chunk-key-major slices.
- `field_mirror_desc<World, Tag>()` computes the mirror description
  entirely from compile-time layout facts (constexpr). Value types are
  integral or float32 (a `double` field fails to compile rather than
  receive a lying format), and the dense mirror's byte counts are
  compile-time proven to fit `std::uint64_t` -- shapes whose dense
  mirror cannot be described fail to compile instead of wrapping.
  The description is the MAXIMAL dense mirror for dense/bounded worlds
  (the current consumer's case); selective sparse mirrors -- the TDD's
  GpuMirror tracking chosen chunk copies -- are future work that reuses
  these structs with differently-computed offsets.
- `UploadDesc` stages one chunk's worth of one field: the live page span
  (`data`/`byte_size`, valid until the world mutates or evicts the
  chunk) and the destination `buffer_offset` in the chunk-key-major
  mirror. `upload_desc<Tag>(world, chunk_key)` derives it; sparse worlds
  pass resident keys only, the standing accessor contract.
- `DispatchDesc` is one kernel dispatch over a mirrored product
  (`product_key`, `input_field_index`, `chunk_count`,
  `workgroups_per_chunk`) -- deliberately abstraction-free; a real
  backend maps it onto its own pipeline and binding model.
- `ReadbackPolicy` / `ReadbackDesc` make readback explicit: `None`,
  `Summary` (the steady-state shape), `SelectedTiles`, `SelectedPath`,
  and `FullField` (debug/explicit only). No full readback by default.
- `GpuCapabilities` is what the device can do (`compute`,
  `async_dispatch`, `async_readback`, `max_buffer_bytes`,
  `max_dispatch_chunks`, `buffer_alignment`); a planner checks these
  before ever selecting GPU. All-false/zero means never-choose-GPU.
- `GpuBackend` is the backend concept: noexcept `capabilities()`, plus
  `upload`/`dispatch`/`readback` returning `bool` -- `false` is a
  refusal (missing capability, exhausted budget, lost device) and the
  caller falls back to the authoritative CPU path.
- `NoGpuBackend` is the default backend: reports no capabilities and
  refuses every operation, so CPU-only builds compile untouched and
  carry zero GPU obligations.

The current concept is deliberately synchronous-bool only: the fence and
completion-collection surface named by the TDD arrives with the first
real backend, as a non-breaking refinement of this concept (the
`async_dispatch`/`async_readback` capability flags reserve the space).

## Testing

`tests/gpu_mock_backend.h` provides the test-only `MockGpuBackend`: it
satisfies the concept, enforces its configured capabilities, and records
the call sequence so tests assert upload -> dispatch -> readback
ordering and payloads. Benchmarks are deliberately absent while no real
backend exists (the
benchmark plan's mock-backend note): there is no execution to measure,
and gating descriptor construction would gate arithmetic.
