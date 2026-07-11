#pragma once

#include <tess/core/shape.h>
#include <tess/storage/chunk_page.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

// M13 GPU descriptors: the plain data a compile-time-polymorphic backend
// consumes, derived from the field schema and chunk layout. Interface
// only in v1 -- no backend implementation ships, CPU stays authoritative
// for gameplay state, and GPU products are derived/cached/versioned by a
// future backend without redesigning core (the acceptance bar of the
// gpu-backend-interface TDD). Nothing here touches a GPU API or adds a
// dependency; descriptors are byte-level facts about storage tess
// already owns.
namespace tess::gpu {

// Storage format of one field's per-tile value, derived from the
// schema's value type. Signedness is preserved so kernels can bind
// typed buffers; bool maps to U8 (its storage size).
enum class GpuFieldFormat : std::uint8_t {
  U8,
  U16,
  U32,
  U64,
  I8,
  I16,
  I32,
  I64,
  F32,
};

namespace detail {

template <typename Value>
[[nodiscard]] constexpr auto field_format() noexcept -> GpuFieldFormat {
  static_assert(std::is_arithmetic_v<Value>,
                "GPU mirrors support arithmetic field values only");
  if constexpr (std::is_same_v<Value, float>) {
    return GpuFieldFormat::F32;
  } else if constexpr (std::is_signed_v<Value>) {
    if constexpr (sizeof(Value) == 1) {
      return GpuFieldFormat::I8;
    } else if constexpr (sizeof(Value) == 2) {
      return GpuFieldFormat::I16;
    } else if constexpr (sizeof(Value) == 4) {
      return GpuFieldFormat::I32;
    } else {
      return GpuFieldFormat::I64;
    }
  } else {
    if constexpr (sizeof(Value) == 1) {
      return GpuFieldFormat::U8;
    } else if constexpr (sizeof(Value) == 2) {
      return GpuFieldFormat::U16;
    } else if constexpr (sizeof(Value) == 4) {
      return GpuFieldFormat::U32;
    } else {
      return GpuFieldFormat::U64;
    }
  }
}

}  // namespace detail

// Byte-level description of one field mirrored to the GPU: enough for a
// backend to size one buffer holding `chunk_count` chunk-contiguous
// slices of `bytes_per_chunk` (tess pages are SoA per chunk, so a
// chunk's field values are one contiguous run).
struct FieldMirrorDesc {
  std::uint32_t field_index = 0;
  GpuFieldFormat format = GpuFieldFormat::U8;
  std::uint32_t value_bytes = 0;
  std::uint64_t tiles_per_chunk = 0;
  std::uint64_t bytes_per_chunk = 0;
  std::uint64_t chunk_count = 0;

  [[nodiscard]] constexpr auto total_bytes() const noexcept -> std::uint64_t {
    return bytes_per_chunk * chunk_count;
  }

  friend constexpr auto operator==(const FieldMirrorDesc&,
                                   const FieldMirrorDesc&) noexcept
      -> bool = default;
};

// The mirror description for `Tag` in `World`, computed entirely from
// compile-time layout facts.
template <typename World, typename Tag>
[[nodiscard]] constexpr auto field_mirror_desc() noexcept -> FieldMirrorDesc {
  using Schema = typename World::schema_type;
  using Value = typename Schema::template value_type<Tag>;
  using Traits = ShapeTraits<typename World::shape_type>;
  FieldMirrorDesc desc;
  desc.field_index = static_cast<std::uint32_t>(Schema::template index<Tag>);
  desc.format = detail::field_format<Value>();
  desc.value_bytes = static_cast<std::uint32_t>(sizeof(Value));
  desc.tiles_per_chunk = Traits::local_tile_count;
  desc.bytes_per_chunk = Traits::local_tile_count * sizeof(Value);
  desc.chunk_count = Traits::chunk_count;
  return desc;
}

// One chunk's worth of one field, staged for upload. `data` points into
// the live page (valid until the world mutates or evicts the chunk);
// `buffer_offset` is the destination slice in the mirror buffer, laid
// out chunk-key-major.
struct UploadDesc {
  ChunkKey chunk_key{};
  std::uint32_t field_index = 0;
  std::uint64_t buffer_offset = 0;
  std::uint64_t byte_size = 0;
  const void* data = nullptr;
};

// The upload description for one resident chunk of `Tag`. Sparse worlds:
// callers pass resident keys only (the same contract as every other
// chunk accessor).
template <typename Tag, typename World>
[[nodiscard]] auto upload_desc(const World& world, ChunkKey chunk_key) noexcept
    -> UploadDesc {
  const auto span = world.template field_span<Tag>(chunk_key);
  constexpr auto desc = field_mirror_desc<World, Tag>();
  UploadDesc upload;
  upload.chunk_key = chunk_key;
  upload.field_index = desc.field_index;
  upload.buffer_offset = chunk_key.value * desc.bytes_per_chunk;
  upload.byte_size = span.size_bytes();
  upload.data = span.data();
  return upload;
}

// One kernel dispatch over a mirrored product: which product, how many
// chunks it covers, and a per-chunk workgroup hint. Deliberately
// abstraction-free -- a real backend maps this onto its own pipeline
// and binding model.
struct DispatchDesc {
  std::uint64_t product_key = 0;
  std::uint32_t input_field_index = 0;
  std::uint64_t chunk_count = 0;
  std::uint32_t workgroups_per_chunk = 1;
};

// Explicit readback policy (TDD section 12): no full readback by
// default; Summary is the expected steady-state shape.
enum class ReadbackPolicy : std::uint8_t {
  None,
  Summary,
  SelectedTiles,
  // Debug/explicit only.
  FullField,
};

struct ReadbackDesc {
  std::uint64_t product_key = 0;
  ReadbackPolicy policy = ReadbackPolicy::None;
  std::uint64_t byte_size = 0;
};

}  // namespace tess::gpu
