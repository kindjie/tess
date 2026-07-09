#pragma once

#include <tess/core/assert.h>
#include <tess/core/shape.h>
#include <tess/storage/chunk_meta.h>
#include <tess/storage/chunk_page.h>

#include <cstddef>
#include <cstdint>
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
    metadata_.reserve(static_cast<std::size_t>(chunk_count));
    for (std::uint64_t key = 0; key < chunk_count; ++key) {
      const auto chunk = ChunkKey{key};
      pages_.emplace_back(chunk, chunk_coord<Shape>(chunk));
      metadata_.emplace_back();
    }
  }

  [[nodiscard]] auto chunks() noexcept -> std::span<page_type> {
    return {pages_.data(), pages_.size()};
  }

  [[nodiscard]] auto chunks() const noexcept -> std::span<const page_type> {
    return {pages_.data(), pages_.size()};
  }

  [[nodiscard]] auto chunk(ChunkKey key) noexcept -> page_type& {
    TESS_ASSERT(key.value < chunk_count);
    return pages_[static_cast<std::size_t>(key.value)];
  }

  [[nodiscard]] auto chunk(ChunkKey key) const noexcept -> const page_type& {
    TESS_ASSERT(key.value < chunk_count);
    return pages_[static_cast<std::size_t>(key.value)];
  }

  [[nodiscard]] auto chunk(ChunkCoord3 coord) noexcept -> page_type& {
    return chunk(chunk_key<Shape>(coord));
  }

  [[nodiscard]] auto chunk(ChunkCoord3 coord) const noexcept
      -> const page_type& {
    return chunk(chunk_key<Shape>(coord));
  }

  [[nodiscard]] auto try_chunk(ChunkKey key) noexcept -> page_type* {
    if (key.value >= chunk_count) {
      return nullptr;
    }
    return &chunk(key);
  }

  [[nodiscard]] auto try_chunk(ChunkKey key) const noexcept
      -> const page_type* {
    if (key.value >= chunk_count) {
      return nullptr;
    }
    return &chunk(key);
  }

  [[nodiscard]] auto try_chunk(ChunkCoord3 coord) noexcept -> page_type* {
    if (!contains_chunk(coord)) {
      return nullptr;
    }
    return &chunk(coord);
  }

  [[nodiscard]] auto try_chunk(ChunkCoord3 coord) const noexcept
      -> const page_type* {
    if (!contains_chunk(coord)) {
      return nullptr;
    }
    return &chunk(coord);
  }

  [[nodiscard]] auto meta(ChunkKey key) noexcept -> ChunkMeta& {
    TESS_ASSERT(key.value < chunk_count);
    return metadata_[static_cast<std::size_t>(key.value)];
  }

  [[nodiscard]] auto meta(ChunkKey key) const noexcept -> const ChunkMeta& {
    TESS_ASSERT(key.value < chunk_count);
    return metadata_[static_cast<std::size_t>(key.value)];
  }

  [[nodiscard]] auto meta(ChunkCoord3 coord) noexcept -> ChunkMeta& {
    return meta(chunk_key<Shape>(coord));
  }

  [[nodiscard]] auto meta(ChunkCoord3 coord) const noexcept
      -> const ChunkMeta& {
    return meta(chunk_key<Shape>(coord));
  }

  [[nodiscard]] auto try_meta(ChunkKey key) noexcept -> ChunkMeta* {
    if (key.value >= chunk_count) {
      return nullptr;
    }
    return &meta(key);
  }

  [[nodiscard]] auto try_meta(ChunkKey key) const noexcept -> const ChunkMeta* {
    if (key.value >= chunk_count) {
      return nullptr;
    }
    return &meta(key);
  }

  [[nodiscard]] auto try_meta(ChunkCoord3 coord) noexcept -> ChunkMeta* {
    if (!contains_chunk(coord)) {
      return nullptr;
    }
    return &meta(coord);
  }

  [[nodiscard]] auto try_meta(ChunkCoord3 coord) const noexcept
      -> const ChunkMeta* {
    if (!contains_chunk(coord)) {
      return nullptr;
    }
    return &meta(coord);
  }

  [[nodiscard]] auto chunk_state(ChunkKey key) const noexcept -> ChunkState {
    return meta(key).state;
  }

  [[nodiscard]] auto chunk_state(ChunkCoord3 coord) const noexcept
      -> ChunkState {
    return meta(coord).state;
  }

  void set_chunk_state(ChunkKey key, ChunkState state) noexcept {
    meta(key).state = state;
  }

  void mark_dirty(ChunkKey key, std::uint32_t flags, Box3 bounds) noexcept {
    detail::meta_mark_dirty(meta(key), flags, bounds);
  }

  void mark_topology_dirty(ChunkKey key, std::uint32_t flags,
                           Box3 bounds) noexcept {
    if (flags == 0) {
      return;
    }
    mark_dirty(key, flags, bounds);
    ++meta(key).topology_version;
  }

  void mark_topology_rebuilt(ChunkKey key) noexcept {
    ++meta(key).topology_version;
  }

  void clear_dirty(ChunkKey key, std::uint32_t flags) noexcept {
    detail::meta_clear_dirty(meta(key), flags);
  }

  [[nodiscard]] auto observe_dirty(ChunkKey key,
                                   std::uint32_t flags) const noexcept
      -> DirtyObservation {
    return detail::meta_observe_dirty(meta(key), flags);
  }

  // Clears exactly the observed flags iff the chunk's dirty generation still
  // matches the observation. Any mark_dirty after the observation advances
  // the generation, so a stale clear leaves every flag and bound in place
  // and returns false; the caller re-observes and rebuilds.
  bool clear_dirty_observed(ChunkKey key, DirtyObservation observed) noexcept {
    return detail::meta_clear_dirty_observed(meta(key), observed);
  }

  void mark_active(ChunkKey key, std::uint32_t flags) noexcept {
    detail::meta_mark_active(meta(key), flags);
  }

  void clear_active(ChunkKey key, std::uint32_t flags) noexcept {
    detail::meta_clear_active(meta(key), flags);
  }

  void collect_dirty_chunks(std::uint32_t flags,
                            std::vector<ChunkKey>& out) const {
    collect_matching_chunks(flags, &ChunkMeta::field_dirty_flags, out);
  }

  void collect_active_chunks(std::uint32_t flags,
                             std::vector<ChunkKey>& out) const {
    collect_matching_chunks(flags, &ChunkMeta::active_flags, out);
  }

  [[nodiscard]] auto dirty_chunks(std::uint32_t flags) const
      -> std::vector<ChunkKey> {
    std::vector<ChunkKey> chunks;
    collect_dirty_chunks(flags, chunks);
    return chunks;
  }

  [[nodiscard]] auto active_chunks(std::uint32_t flags) const
      -> std::vector<ChunkKey> {
    std::vector<ChunkKey> chunks;
    collect_active_chunks(flags, chunks);
    return chunks;
  }

  [[nodiscard]] auto resolve(Coord3 coord) const noexcept
      -> ResolvedTile<Shape> {
    TESS_ASSERT(contains<Shape>(coord));
    const auto chunk_coord_value = chunk_coord<Shape>(coord);
    return ResolvedTile<Shape>{
        chunk_key<Shape>(chunk_coord_value),
        local_tile_id<Shape>(local_coord<Shape>(coord)),
    };
  }

  [[nodiscard]] auto try_resolve(Coord3 coord) const noexcept
      -> std::optional<ResolvedTile<Shape>> {
    if (!contains<Shape>(coord)) {
      return std::nullopt;
    }
    return resolve(coord);
  }

  template <typename Tag>
  [[nodiscard]] auto field(Coord3 coord) noexcept
      -> Schema::template value_type<Tag>& {
    const auto resolved = resolve(coord);
    return chunk(resolved.chunk_key)
        .template field<Tag>(resolved.local_tile_id);
  }

  template <typename Tag>
  [[nodiscard]] auto field(Coord3 coord) const noexcept
      -> const Schema::template value_type<Tag>& {
    const auto resolved = resolve(coord);
    return chunk(resolved.chunk_key)
        .template field<Tag>(resolved.local_tile_id);
  }

  template <typename Tag>
  [[nodiscard]] auto try_field(Coord3 coord) noexcept
      -> Schema::template value_type<Tag>* {
    const auto resolved = try_resolve(coord);
    if (!resolved.has_value()) {
      return nullptr;
    }
    return &chunk(resolved->chunk_key)
                .template field<Tag>(resolved->local_tile_id);
  }

  template <typename Tag>
  [[nodiscard]] auto try_field(Coord3 coord) const noexcept
      -> const Schema::template value_type<Tag>* {
    const auto resolved = try_resolve(coord);
    if (!resolved.has_value()) {
      return nullptr;
    }
    return &chunk(resolved->chunk_key)
                .template field<Tag>(resolved->local_tile_id);
  }

  template <typename Tag>
  [[nodiscard]] auto field_span(ChunkKey key) noexcept {
    return chunk(key).template field_span<Tag>();
  }

  template <typename Tag>
  [[nodiscard]] auto field_span(ChunkKey key) const noexcept {
    return chunk(key).template field_span<Tag>();
  }

 private:
  static constexpr bool contains_chunk(ChunkCoord3 coord) noexcept {
    using Traits = ShapeTraits<Shape>;
    return coord.x < Traits::chunk_count_x && coord.y < Traits::chunk_count_y &&
           coord.z < Traits::chunk_count_z;
  }

  void collect_matching_chunks(std::uint32_t flags,
                               std::uint32_t ChunkMeta::* member,
                               std::vector<ChunkKey>& out) const {
    for (std::uint64_t key = 0; key < chunk_count; ++key) {
      if ((metadata_[static_cast<std::size_t>(key)].*member & flags) != 0) {
        out.push_back(ChunkKey{key});
      }
    }
  }

  std::vector<page_type> pages_;
  std::vector<ChunkMeta> metadata_;
};

template <typename Shape, typename Schema>
using AlwaysResidentWorld = World<Shape, Schema, AlwaysResident>;

}  // namespace tess
