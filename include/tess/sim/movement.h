#pragma once

#include <tess/core/shape.h>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace tess {

enum class MovementStatus : std::uint8_t {
  Moved,
  InvalidFrom,
  InvalidTo,
  NotAdjacent,
  BlockedFrom,
  BlockedTo,
  Occupied,
  Reserved,
  StaleVersion,
  StaleTopology,
};
static_assert(sizeof(MovementStatus) == sizeof(std::uint8_t));

struct MovementVersionCheck {
  std::optional<std::uint32_t> from_chunk_version;
  std::optional<std::uint32_t> to_chunk_version;
  std::optional<std::uint32_t> from_topology_version;
  std::optional<std::uint32_t> to_topology_version;
};

struct MovementIntent {
  Coord3 from{};
  Coord3 to{};
  MovementVersionCheck versions{};
};

struct MovementResult {
  MovementStatus status = MovementStatus::Moved;
  Coord3 from{};
  Coord3 to{};
};

struct MovementFailureCounts {
  std::size_t invalid = 0;
  std::size_t blocked = 0;
  std::size_t occupied = 0;
  std::size_t reserved = 0;
  std::size_t stale_version = 0;
  std::size_t stale_topology = 0;
};

inline void record_movement_failure(MovementFailureCounts& counts,
                                    MovementStatus status) noexcept {
  switch (status) {
    case MovementStatus::Moved:
      return;
    case MovementStatus::InvalidFrom:
    case MovementStatus::InvalidTo:
    case MovementStatus::NotAdjacent:
      ++counts.invalid;
      return;
    case MovementStatus::BlockedFrom:
    case MovementStatus::BlockedTo:
      ++counts.blocked;
      return;
    case MovementStatus::Occupied:
      ++counts.occupied;
      return;
    case MovementStatus::Reserved:
      ++counts.reserved;
      return;
    case MovementStatus::StaleVersion:
      ++counts.stale_version;
      return;
    case MovementStatus::StaleTopology:
      ++counts.stale_topology;
      return;
  }
}

// Transient failures describe a world state that can legitimately change
// under a routed agent (another agent passing through, a fresh wall, a
// stale version guard); callers should re-path and retry. The remaining
// failures (invalid endpoints, non-adjacent steps) indicate a caller bug
// and are terminal.
[[nodiscard]] constexpr auto is_transient_movement_failure(
    MovementStatus status) noexcept -> bool {
  switch (status) {
    case MovementStatus::BlockedFrom:
    case MovementStatus::BlockedTo:
    case MovementStatus::Occupied:
    case MovementStatus::Reserved:
    case MovementStatus::StaleVersion:
    case MovementStatus::StaleTopology:
      return true;
    case MovementStatus::Moved:
    case MovementStatus::InvalidFrom:
    case MovementStatus::InvalidTo:
    case MovementStatus::NotAdjacent:
      return false;
  }
  return false;
}

template <typename World>
[[nodiscard]] auto movement_versions_match(const World& world,
                                           MovementIntent intent) noexcept
    -> MovementStatus {
  const auto from = world.resolve(intent.from);
  const auto to = world.resolve(intent.to);
  const auto& from_meta = world.meta(from.chunk_key);
  const auto& to_meta = world.meta(to.chunk_key);

  if (intent.versions.from_chunk_version.has_value() &&
      from_meta.version != *intent.versions.from_chunk_version) {
    return MovementStatus::StaleVersion;
  }
  if (intent.versions.to_chunk_version.has_value() &&
      to_meta.version != *intent.versions.to_chunk_version) {
    return MovementStatus::StaleVersion;
  }
  if (intent.versions.from_topology_version.has_value() &&
      from_meta.topology_version != *intent.versions.from_topology_version) {
    return MovementStatus::StaleTopology;
  }
  if (intent.versions.to_topology_version.has_value() &&
      to_meta.topology_version != *intent.versions.to_topology_version) {
    return MovementStatus::StaleTopology;
  }
  return MovementStatus::Moved;
}

template <typename World, typename PassableTag, typename OccupancyTag,
          typename ReservationTag>
[[nodiscard]] auto validate_movement_intent(const World& world,
                                            MovementIntent intent) noexcept
    -> MovementResult {
  if (!world.try_resolve(intent.from).has_value()) {
    return MovementResult{MovementStatus::InvalidFrom, intent.from, intent.to};
  }
  if (!world.try_resolve(intent.to).has_value()) {
    return MovementResult{MovementStatus::InvalidTo, intent.from, intent.to};
  }
  if (manhattan_distance(intent.from, intent.to) != 1) {
    return MovementResult{MovementStatus::NotAdjacent, intent.from, intent.to};
  }
  if (!static_cast<bool>(world.template field<PassableTag>(intent.from))) {
    return MovementResult{MovementStatus::BlockedFrom, intent.from, intent.to};
  }
  if (!static_cast<bool>(world.template field<PassableTag>(intent.to))) {
    return MovementResult{MovementStatus::BlockedTo, intent.from, intent.to};
  }
  if (static_cast<bool>(world.template field<OccupancyTag>(intent.to))) {
    return MovementResult{MovementStatus::Occupied, intent.from, intent.to};
  }
  if (static_cast<bool>(world.template field<ReservationTag>(intent.to))) {
    return MovementResult{MovementStatus::Reserved, intent.from, intent.to};
  }

  const auto version_status = movement_versions_match(world, intent);
  if (version_status != MovementStatus::Moved) {
    return MovementResult{version_status, intent.from, intent.to};
  }
  return MovementResult{MovementStatus::Moved, intent.from, intent.to};
}

template <typename World, typename PassableTag, typename OccupancyTag,
          typename ReservationTag>
auto commit_movement_intent(World& world, MovementIntent intent,
                            std::uint32_t dirty_mask = 0) noexcept
    -> MovementResult {
  const auto result = validate_movement_intent<World, PassableTag, OccupancyTag,
                                               ReservationTag>(world, intent);
  if (result.status != MovementStatus::Moved) {
    return result;
  }

  world.template field<OccupancyTag>(intent.from) = false;
  world.template field<OccupancyTag>(intent.to) = true;
  world.template field<ReservationTag>(intent.to) = false;
  if (dirty_mask != 0) {
    world.mark_dirty(world.resolve(intent.from).chunk_key, dirty_mask,
                     Box3{intent.from, Extent3{1, 1, 1}});
    world.mark_dirty(world.resolve(intent.to).chunk_key, dirty_mask,
                     Box3{intent.to, Extent3{1, 1, 1}});
  }
  return result;
}

}  // namespace tess
