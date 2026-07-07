#pragma once

#include <tess/core/shape.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace tess {

struct RenderTileDelta {
  Coord3 coord{};
  ChunkKey chunk_key{};
  LocalTileId local_tile_id{};
  std::uint32_t dirty_flags = 0;
  std::uint32_t chunk_version = 0;
};

namespace detail {

[[nodiscard]] constexpr auto axis_end(std::int64_t origin,
                                      std::uint64_t extent) noexcept
    -> std::int64_t {
  return origin + static_cast<std::int64_t>(extent);
}

}  // namespace detail

template <typename World>
void collect_render_tile_deltas(const World& world, std::uint32_t dirty_mask,
                                std::vector<RenderTileDelta>& out) {
  using Shape = World::shape_type;
  if (dirty_mask == 0) {
    return;
  }

  for (std::uint64_t key = 0; key < World::chunk_count; ++key) {
    const auto chunk_key = ChunkKey{key};
    const auto& meta = world.meta(chunk_key);
    const auto flags = meta.field_dirty_flags & dirty_mask;
    if (flags == 0) {
      continue;
    }

    // Clip the dirty bounds to this chunk's own world-space box before the
    // per-tile loop. Every tile in the clipped box is inside the shape and
    // resolves to this chunk, so no per-tile filtering is needed.
    using Traits = ShapeTraits<Shape>;
    const auto chunk = chunk_coord<Shape>(chunk_key);
    const auto chunk_begin_x =
        static_cast<std::int64_t>(chunk.x * Traits::chunk.x);
    const auto chunk_begin_y =
        static_cast<std::int64_t>(chunk.y * Traits::chunk.y);
    const auto chunk_begin_z =
        static_cast<std::int64_t>(chunk.z * Traits::chunk.z);
    const auto begin_x = std::max(meta.dirty_bounds.origin.x, chunk_begin_x);
    const auto begin_y = std::max(meta.dirty_bounds.origin.y, chunk_begin_y);
    const auto begin_z = std::max(meta.dirty_bounds.origin.z, chunk_begin_z);
    const auto end_x =
        std::min(detail::axis_end(meta.dirty_bounds.origin.x,
                                  meta.dirty_bounds.extent.x),
                 chunk_begin_x + static_cast<std::int64_t>(Traits::chunk.x));
    const auto end_y =
        std::min(detail::axis_end(meta.dirty_bounds.origin.y,
                                  meta.dirty_bounds.extent.y),
                 chunk_begin_y + static_cast<std::int64_t>(Traits::chunk.y));
    const auto end_z =
        std::min(detail::axis_end(meta.dirty_bounds.origin.z,
                                  meta.dirty_bounds.extent.z),
                 chunk_begin_z + static_cast<std::int64_t>(Traits::chunk.z));
    for (auto z = begin_z; z < end_z; ++z) {
      for (auto y = begin_y; y < end_y; ++y) {
        for (auto x = begin_x; x < end_x; ++x) {
          const auto coord = Coord3{x, y, z};
          out.push_back(RenderTileDelta{
              coord,
              chunk_key,
              local_tile_id<Shape>(local_coord<Shape>(coord)),
              flags,
              meta.version,
          });
        }
      }
    }
  }
}

template <typename World>
[[nodiscard]] auto render_tile_deltas(const World& world,
                                      std::uint32_t dirty_mask)
    -> std::vector<RenderTileDelta> {
  std::vector<RenderTileDelta> deltas;
  collect_render_tile_deltas(world, dirty_mask, deltas);
  return deltas;
}

template <typename World>
void clear_render_delta_dirty(World& world, std::uint32_t dirty_mask) noexcept {
  if (dirty_mask == 0) {
    return;
  }
  for (std::uint64_t key = 0; key < World::chunk_count; ++key) {
    world.clear_dirty(ChunkKey{key}, dirty_mask);
  }
}

}  // namespace tess
