#pragma once

#include <tess/core/shape.h>

#include <bit>
#include <cstdint>
#include <limits>

namespace tess {

enum class ChunkState : std::uint8_t {
  ResidentSleeping,
  ResidentActive,
};
static_assert(sizeof(ChunkState) == sizeof(std::uint8_t));

struct ChunkMeta {
  ChunkState state = ChunkState::ResidentSleeping;
  std::uint32_t version = 0;
  std::uint32_t topology_version = 0;
  std::uint32_t field_dirty_flags = 0;
  std::uint32_t active_flags = 0;
  Box3 dirty_bounds{};
  std::uint32_t active_count = 0;
  std::uint32_t entity_count = 0;
};

// Generation-stamped snapshot of one chunk's dirty state, taken by
// World::observe_dirty and consumed by World::clear_dirty_observed. A
// maintenance pass observes before rebuilding derived state; the paired
// clear succeeds only while the observed generation is still current, so
// marks that land during the rebuild are never lost.
struct DirtyObservation {
  std::uint32_t flags = 0;
  Box3 bounds{};
  std::uint32_t version = 0;
};

namespace detail {

[[nodiscard]] constexpr std::uint32_t popcount(std::uint32_t flags) noexcept {
  // Single POPCNT/CNT instruction instead of the old 32-iteration bit
  // loop; runs on every occupancy/state edit (audit 2026-07-11 low).
  return static_cast<std::uint32_t>(std::popcount(flags));
}

// An extent >= 2^63 would flip the int64 cast negative (and a large origin
// plus extent would overflow), corrupting dirty-bounds unions; saturate the
// axis end at the int64 maximum instead.
[[nodiscard]] constexpr std::int64_t box_axis_end(
    std::int64_t origin, std::uint64_t extent) noexcept {
  constexpr auto max = std::numeric_limits<std::int64_t>::max();
  if (extent > static_cast<std::uint64_t>(max)) {
    return max;
  }
  const auto delta = static_cast<std::int64_t>(extent);
  return origin > max - delta ? max : origin + delta;
}

[[nodiscard]] constexpr std::int64_t box_min(std::int64_t lhs,
                                             std::int64_t rhs) noexcept {
  return lhs < rhs ? lhs : rhs;
}

[[nodiscard]] constexpr std::int64_t box_max(std::int64_t lhs,
                                             std::int64_t rhs) noexcept {
  return lhs < rhs ? rhs : lhs;
}

[[nodiscard]] constexpr Box3 union_box(Box3 lhs, Box3 rhs) noexcept {
  const auto min_x = box_min(lhs.origin.x, rhs.origin.x);
  const auto min_y = box_min(lhs.origin.y, rhs.origin.y);
  const auto min_z = box_min(lhs.origin.z, rhs.origin.z);
  const auto max_x = box_max(box_axis_end(lhs.origin.x, lhs.extent.x),
                             box_axis_end(rhs.origin.x, rhs.extent.x));
  const auto max_y = box_max(box_axis_end(lhs.origin.y, lhs.extent.y),
                             box_axis_end(rhs.origin.y, rhs.extent.y));
  const auto max_z = box_max(box_axis_end(lhs.origin.z, lhs.extent.z),
                             box_axis_end(rhs.origin.z, rhs.extent.z));
  return Box3{
      Coord3{min_x, min_y, min_z},
      // max >= min on every axis; abs_delta subtracts in unsigned space, so a
      // saturated end paired with a negative origin cannot overflow int64.
      Extent3{
          abs_delta(max_x, min_x),
          abs_delta(max_y, min_y),
          abs_delta(max_z, min_z),
      },
  };
}

// Mutation helpers shared by the AlwaysResident and SparseResident worlds so
// both maintain identical dirty/active/version semantics on a ChunkMeta.

inline void meta_mark_dirty(ChunkMeta& meta, std::uint32_t flags,
                            Box3 bounds) noexcept {
  if (flags == 0) {
    return;
  }
  if (meta.field_dirty_flags == 0) {
    meta.dirty_bounds = bounds;
  } else {
    meta.dirty_bounds = union_box(meta.dirty_bounds, bounds);
  }
  meta.field_dirty_flags |= flags;
  ++meta.version;
}

inline void meta_clear_dirty(ChunkMeta& meta, std::uint32_t flags) noexcept {
  if (flags == 0) {
    return;
  }
  meta.field_dirty_flags &= ~flags;
  if (meta.field_dirty_flags == 0) {
    meta.dirty_bounds = {};
  }
}

[[nodiscard]] inline DirtyObservation meta_observe_dirty(
    const ChunkMeta& meta, std::uint32_t flags) noexcept {
  return DirtyObservation{
      meta.field_dirty_flags & flags,
      meta.dirty_bounds,
      meta.version,
  };
}

inline bool meta_clear_dirty_observed(ChunkMeta& meta,
                                      DirtyObservation observed) noexcept {
  if (meta.version != observed.version) {
    return false;
  }
  meta_clear_dirty(meta, observed.flags);
  return true;
}

inline void meta_mark_active(ChunkMeta& meta, std::uint32_t flags) noexcept {
  if (flags == 0) {
    return;
  }
  const auto before = meta.active_flags;
  meta.active_flags |= flags;
  meta.active_count = popcount(meta.active_flags);
  if (before == 0 && meta.active_flags != 0) {
    meta.state = ChunkState::ResidentActive;
  }
}

inline void meta_clear_active(ChunkMeta& meta, std::uint32_t flags) noexcept {
  if (flags == 0) {
    return;
  }
  meta.active_flags &= ~flags;
  meta.active_count = popcount(meta.active_flags);
  if (meta.active_flags == 0) {
    meta.state = ChunkState::ResidentSleeping;
  }
}

}  // namespace detail

}  // namespace tess
