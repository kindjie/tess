#pragma once

#include <tess/core/shape.h>
#include <tess/storage/chunk_page.h>

#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace tess {

struct AlwaysResident {};

template <typename Shape, typename Schema, typename Residency>
class World;

template <typename Shape, typename Schema>
class World<Shape, Schema, AlwaysResident> {
 public:
  using shape_type = Shape;
  using schema_type = Schema;
  using residency_type = AlwaysResident;
  using page_type = ChunkPage<Shape, Schema>;

  static constexpr std::uint64_t chunk_count = ShapeTraits<Shape>::chunk_count;
  static constexpr std::uint64_t local_tile_count =
      ShapeTraits<Shape>::local_tile_count;
  static constexpr std::size_t field_count = Schema::field_count;
  static constexpr std::size_t page_byte_size = page_type::byte_size;
  static constexpr std::size_t storage_byte_size =
      static_cast<std::size_t>(chunk_count) * page_byte_size;

  static_assert(chunk_count <= static_cast<std::uint64_t>(
                                   std::numeric_limits<std::size_t>::max()),
                "AlwaysResident World chunk count must fit std::size_t.");
  static_assert(page_byte_size <= std::numeric_limits<std::size_t>::max() /
                                      static_cast<std::size_t>(chunk_count),
                "AlwaysResident World storage bytes must fit std::size_t.");

  World() {
    pages_.reserve(static_cast<std::size_t>(chunk_count));
    for (std::uint64_t key = 0; key < chunk_count; ++key) {
      const auto chunk = ChunkKey{key};
      pages_.emplace_back(chunk, chunk_coord<Shape>(chunk));
    }
  }

  auto chunks() noexcept -> std::span<page_type> {
    return {pages_.data(), pages_.size()};
  }

  auto chunks() const noexcept -> std::span<const page_type> {
    return {pages_.data(), pages_.size()};
  }

  auto chunk(ChunkKey key) noexcept -> page_type& {
    return pages_[static_cast<std::size_t>(key.value)];
  }

  auto chunk(ChunkKey key) const noexcept -> const page_type& {
    return pages_[static_cast<std::size_t>(key.value)];
  }

  auto chunk(ChunkCoord3 coord) noexcept -> page_type& {
    return chunk(chunk_key<Shape>(coord));
  }

  auto chunk(ChunkCoord3 coord) const noexcept -> const page_type& {
    return chunk(chunk_key<Shape>(coord));
  }

  auto try_chunk(ChunkKey key) noexcept -> page_type* {
    if (key.value >= chunk_count) {
      return nullptr;
    }
    return &chunk(key);
  }

  auto try_chunk(ChunkKey key) const noexcept -> const page_type* {
    if (key.value >= chunk_count) {
      return nullptr;
    }
    return &chunk(key);
  }

  auto try_chunk(ChunkCoord3 coord) noexcept -> page_type* {
    if (!contains_chunk(coord)) {
      return nullptr;
    }
    return &chunk(coord);
  }

  auto try_chunk(ChunkCoord3 coord) const noexcept -> const page_type* {
    if (!contains_chunk(coord)) {
      return nullptr;
    }
    return &chunk(coord);
  }

  auto resolve(Coord3 coord) const noexcept -> ResolvedTile<Shape> {
    const auto chunk_coord_value = chunk_coord<Shape>(coord);
    return ResolvedTile<Shape>{
        chunk_key<Shape>(chunk_coord_value),
        local_tile_id<Shape>(local_coord<Shape>(coord)),
    };
  }

  auto try_resolve(Coord3 coord) const noexcept
      -> std::optional<ResolvedTile<Shape>> {
    if (!contains<Shape>(coord)) {
      return std::nullopt;
    }
    return resolve(coord);
  }

  template <typename Tag>
  auto field(Coord3 coord) noexcept ->
      typename Schema::template value_type<Tag>& {
    const auto resolved = resolve(coord);
    return chunk(resolved.chunk_key)
        .template field<Tag>(resolved.local_tile_id);
  }

  template <typename Tag>
  auto field(Coord3 coord) const noexcept -> const
      typename Schema::template value_type<Tag>& {
    const auto resolved = resolve(coord);
    return chunk(resolved.chunk_key)
        .template field<Tag>(resolved.local_tile_id);
  }

  template <typename Tag>
  auto try_field(Coord3 coord) noexcept ->
      typename Schema::template value_type<Tag>* {
    const auto resolved = try_resolve(coord);
    if (!resolved.has_value()) {
      return nullptr;
    }
    return &chunk(resolved->chunk_key)
                .template field<Tag>(resolved->local_tile_id);
  }

  template <typename Tag>
  auto try_field(Coord3 coord) const noexcept -> const
      typename Schema::template value_type<Tag>* {
    const auto resolved = try_resolve(coord);
    if (!resolved.has_value()) {
      return nullptr;
    }
    return &chunk(resolved->chunk_key)
                .template field<Tag>(resolved->local_tile_id);
  }

  template <typename Tag>
  auto field_span(ChunkKey key) noexcept {
    return chunk(key).template field_span<Tag>();
  }

  template <typename Tag>
  auto field_span(ChunkKey key) const noexcept {
    return chunk(key).template field_span<Tag>();
  }

 private:
  static constexpr bool contains_chunk(ChunkCoord3 coord) noexcept {
    using Traits = ShapeTraits<Shape>;
    return coord.x < Traits::chunk_count_x && coord.y < Traits::chunk_count_y &&
           coord.z < Traits::chunk_count_z;
  }

  std::vector<page_type> pages_;
};

template <typename Shape, typename Schema>
using AlwaysResidentWorld = World<Shape, Schema, AlwaysResident>;

}  // namespace tess
