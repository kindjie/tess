#pragma once

#include <tess/core/shape.h>
#include <tess/storage/residency.h>
#include <tess/storage/world.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
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
  // Saturating: an unguarded origin + int64(extent) is UB for huge
  // caller-supplied extents (audit 2026-07-11 C1); share chunk_meta's
  // guarded helper.
  return box_axis_end(origin, extent);
}

// Emits render-tile deltas for one chunk. Shared by the dense (all chunks) and
// sparse (resident set) iteration in collect_render_tile_deltas, so the
// per-chunk logic stays single-sourced.
template <typename World>
void emit_chunk_render_deltas(const World& world, ChunkKey chunk_key,
                              std::uint32_t dirty_mask,
                              std::vector<RenderTileDelta>& out) {
  using Shape = typename World::shape_type;
  // One observation carries the masked flags, bounds, and version -- the
  // flag word and bounds live in the world's SoA columns, not ChunkMeta
  // (audit 2026-07-11 M5).
  const auto observed = world.observe_dirty(chunk_key, dirty_mask);
  const auto flags = observed.flags;
  if (flags == 0) {
    return;
  }
  const auto& dirty_bounds = observed.bounds;

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
  const auto begin_x = std::max(dirty_bounds.origin.x, chunk_begin_x);
  const auto begin_y = std::max(dirty_bounds.origin.y, chunk_begin_y);
  const auto begin_z = std::max(dirty_bounds.origin.z, chunk_begin_z);
  const auto end_x =
      std::min(detail::axis_end(dirty_bounds.origin.x, dirty_bounds.extent.x),
               chunk_begin_x + static_cast<std::int64_t>(Traits::chunk.x));
  const auto end_y =
      std::min(detail::axis_end(dirty_bounds.origin.y, dirty_bounds.extent.y),
               chunk_begin_y + static_cast<std::int64_t>(Traits::chunk.y));
  const auto end_z =
      std::min(detail::axis_end(dirty_bounds.origin.z, dirty_bounds.extent.z),
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
            observed.version,
        });
      }
    }
  }
}

}  // namespace detail

template <typename World>
void collect_render_tile_deltas(const World& world, std::uint32_t dirty_mask,
                                std::vector<RenderTileDelta>& out) {
  if (dirty_mask == 0) {
    return;
  }

  // Dense scans every chunk; sparse scans only the resident set (a non-resident
  // chunk holds no data and cannot be dirty, so this misses no delta and never
  // reads a non-resident slot / runs a full chunk_count scan).
  if constexpr (std::is_same_v<typename World::residency_type,
                               AlwaysResident>) {
    for (std::uint64_t key = 0; key < World::chunk_count; ++key) {
      detail::emit_chunk_render_deltas(world, ChunkKey{key}, dirty_mask, out);
    }
  } else {
    for (const auto chunk_key : world.resident_chunk_keys()) {
      detail::emit_chunk_render_deltas(world, chunk_key, dirty_mask, out);
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
  if constexpr (std::is_same_v<typename World::residency_type,
                               AlwaysResident>) {
    for (std::uint64_t key = 0; key < World::chunk_count; ++key) {
      world.clear_dirty(ChunkKey{key}, dirty_mask);
    }
  } else {
    // Only resident chunks can carry dirty state; clear_dirty on a non-resident
    // key would assert.
    for (const auto chunk_key : world.resident_chunk_keys()) {
      world.clear_dirty(chunk_key, dirty_mask);
    }
  }
}

}  // namespace tess
