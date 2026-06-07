#pragma once

#include <tess/core/shape.h>

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

    const auto end_x = detail::axis_end(meta.dirty_bounds.origin.x,
                                        meta.dirty_bounds.extent.x);
    const auto end_y = detail::axis_end(meta.dirty_bounds.origin.y,
                                        meta.dirty_bounds.extent.y);
    const auto end_z = detail::axis_end(meta.dirty_bounds.origin.z,
                                        meta.dirty_bounds.extent.z);
    for (auto z = meta.dirty_bounds.origin.z; z < end_z; ++z) {
      for (auto y = meta.dirty_bounds.origin.y; y < end_y; ++y) {
        for (auto x = meta.dirty_bounds.origin.x; x < end_x; ++x) {
          const auto coord = Coord3{x, y, z};
          if (!contains<Shape>(coord)) {
            continue;
          }
          const auto resolved = world.resolve(coord);
          if (resolved.chunk_key != chunk_key) {
            continue;
          }
          out.push_back(RenderTileDelta{
              coord,
              chunk_key,
              resolved.local_tile_id,
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
