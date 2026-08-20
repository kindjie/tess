#pragma once

#include <tess/core/shape.h>
#include <tess/storage/metadata_types.h>

#include <bit>
#include <cstdint>
#include <limits>

namespace tess {

/** Work-participation activity derived from a chunk's active mask. */
enum class ChunkActivity : std::uint8_t {
  Sleeping,
  Active,
};
static_assert(sizeof(ChunkActivity) == sizeof(std::uint8_t));

/**
 * Cold metadata for one resident chunk.
 *
 * Dirty and active masks and dirty bounds live in world-owned parallel arrays
 * for cache-efficient scans. Read and mutate those values through `World`; a
 * `ChunkMeta` reference alone does not expose the complete chunk state. Sparse
 * world eviction invalidates references to this object.
 */
struct ChunkMeta {
  ContentVersion content_version{};
  TopologyVersion topology_version{};
  std::uint32_t entity_count = 0;
};

/**
 * Generation-stamped snapshot returned by `World::observe_dirty()`.
 *
 * Pass it to `World::clear_dirty_observed()` after rebuilding derived state.
 * The clear succeeds only if no later dirty mark changed the content version,
 * so a maintenance pass cannot erase intervening marks.
 *
 * `residency_generation` scopes the observation to one residency interval. A
 * sparse world restarts a rematerialized chunk's content version at zero, so
 * content version equality alone would let an observation taken before an
 * eviction match a mark made after rematerialization and clear work it never
 * saw. Always-resident worlds never evict and leave this invalid on both sides.
 */
struct DirtyObservation {
  DirtyMask mask{};
  Box3 bounds{};
  ContentVersion content_version{};
  ResidencyGeneration residency_generation{};
};

namespace detail {

[[nodiscard]] constexpr std::uint32_t popcount(ActiveMask mask) noexcept {
  // Single POPCNT/CNT instruction instead of the old 32-iteration bit
  // loop; runs on every occupancy/state edit.
  return static_cast<std::uint32_t>(std::popcount(mask.value));
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
// both maintain identical dirty-mask, active-mask, and content-version
// semantics. The mask word and bounds live in the worlds' SoA columns (see
// ChunkMeta's comment), so the helpers take them by reference alongside the
// residual struct.

inline void meta_mark_dirty(DirtyMask& dirty_mask, Box3& dirty_bounds,
                            ChunkMeta& meta, DirtyMask mask,
                            Box3 bounds) noexcept {
  if (mask.empty()) {
    return;
  }
  if (dirty_mask.empty()) {
    dirty_bounds = bounds;
  } else {
    dirty_bounds = union_box(dirty_bounds, bounds);
  }
  dirty_mask |= mask;
  ++meta.content_version;
}

inline void meta_mark_content_changed(ChunkMeta& meta) noexcept {
  ++meta.content_version;
}

inline void meta_clear_dirty(DirtyMask& dirty_mask, Box3& dirty_bounds,
                             DirtyMask mask) noexcept {
  if (mask.empty()) {
    return;
  }
  dirty_mask &= ~mask;
  if (dirty_mask.empty()) {
    dirty_bounds = {};
  }
}

[[nodiscard]] inline DirtyObservation meta_observe_dirty(
    DirtyMask dirty_mask, Box3 dirty_bounds, const ChunkMeta& meta,
    DirtyMask mask, ResidencyGeneration residency_generation = {}) noexcept {
  return DirtyObservation{
      dirty_mask & mask,
      dirty_bounds,
      meta.content_version,
      residency_generation,
  };
}

inline bool meta_clear_dirty_observed(
    DirtyMask& dirty_mask, Box3& dirty_bounds, const ChunkMeta& meta,
    DirtyObservation observed,
    ResidencyGeneration residency_generation = {}) noexcept {
  // A rematerialized sparse chunk restarts its content version at zero, so an
  // observation from an earlier residency interval can compare equal to a
  // mark made after rematerialization. Reject it before the content-version
  // check.
  if (residency_generation != observed.residency_generation) {
    return false;
  }
  if (meta.content_version != observed.content_version) {
    return false;
  }
  meta_clear_dirty(dirty_mask, dirty_bounds, observed.mask);
  return true;
}

inline void meta_mark_active(ActiveMask& active_mask,
                             ActiveMask mask) noexcept {
  if (mask.empty()) {
    return;
  }
  active_mask |= mask;
}

inline void meta_clear_active(ActiveMask& active_mask,
                              ActiveMask mask) noexcept {
  if (mask.empty()) {
    return;
  }
  active_mask &= ~mask;
}

}  // namespace detail

}  // namespace tess
