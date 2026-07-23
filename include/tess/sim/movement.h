#pragma once

#include <tess/core/shape.h>
#include <tess/storage/chunk_meta.h>
#include <tess/storage/residency.h>
#include <tess/topology/movement_class.h>
#include <tess/topology/transition_model.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace tess {

/// Classifies a movement commit or the reason validation rejected it.
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

/// Holds optional optimistic-concurrency versions for both movement endpoints.
struct MovementVersionCheck {
  std::optional<std::uint32_t> from_chunk_version;
  std::optional<std::uint32_t> to_chunk_version;
  std::optional<std::uint32_t> from_topology_version;
  std::optional<std::uint32_t> to_topology_version;
};

/// Describes an adjacent move and any versions it expects to remain current.
struct MovementIntent {
  Coord3 from{};
  Coord3 to{};
  MovementVersionCheck versions{};
};

/// Reports the movement status together with the requested endpoints.
struct MovementResult {
  MovementStatus status = MovementStatus::Moved;
  Coord3 from{};
  Coord3 to{};
};

/// Aggregates rejected movement attempts by retry-relevant category.
struct MovementFailureCounts {
  std::size_t invalid = 0;
  std::size_t blocked = 0;
  std::size_t occupied = 0;
  std::size_t reserved = 0;
  std::size_t stale_version = 0;
  std::size_t stale_topology = 0;
};

/// Increments the category corresponding to a non-success movement status.
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
/// Returns whether retrying after world state changes may allow the move.
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

namespace detail {

[[nodiscard]] inline auto movement_versions_match_meta(
    const ChunkMeta& from_meta, const ChunkMeta& to_meta,
    const MovementVersionCheck& versions) noexcept -> MovementStatus {
  if (versions.from_chunk_version.has_value() &&
      from_meta.version != *versions.from_chunk_version) {
    return MovementStatus::StaleVersion;
  }
  if (versions.to_chunk_version.has_value() &&
      to_meta.version != *versions.to_chunk_version) {
    return MovementStatus::StaleVersion;
  }
  if (versions.from_topology_version.has_value() &&
      from_meta.topology_version != *versions.from_topology_version) {
    return MovementStatus::StaleTopology;
  }
  if (versions.to_topology_version.has_value() &&
      to_meta.topology_version != *versions.to_topology_version) {
    return MovementStatus::StaleTopology;
  }
  return MovementStatus::Moved;
}

[[nodiscard]] constexpr auto has_version_expectations(
    const MovementVersionCheck& versions) noexcept -> bool {
  return versions.from_chunk_version.has_value() ||
         versions.to_chunk_version.has_value() ||
         versions.from_topology_version.has_value() ||
         versions.to_topology_version.has_value();
}

}  // namespace detail

/// Returns whether every optional version in `intent` still matches.
template <typename World>
[[nodiscard]] auto movement_versions_match(const World& world,
                                           MovementIntent intent) noexcept
    -> MovementStatus {
  if constexpr (std::is_same_v<typename World::residency_type,
                               SparseResident>) {
    // A non-resident chunk has no version snapshot to compare against;
    // treat it as stale so the move is rejected rather than reading meta()
    // out of bounds. This holds even with no expectations set, so the
    // fast path below cannot change sparse semantics.
    const auto from = world.resolve(intent.from);
    const auto to = world.resolve(intent.to);
    if (!world.is_resident(from.chunk_key) ||
        !world.is_resident(to.chunk_key)) {
      return MovementStatus::StaleVersion;
    }
    if (!detail::has_version_expectations(intent.versions)) {
      return MovementStatus::Moved;
    }
    return detail::movement_versions_match_meta(
        world.meta(from.chunk_key), world.meta(to.chunk_key), intent.versions);
  } else {
    // The movement-scheduler path submits intents with no version
    // expectations at all; resolving both endpoints and reading two metas
    // just to compare nothing was pure per-step waste (audit 2026-07-11
    // M7).
    if (!detail::has_version_expectations(intent.versions)) {
      return MovementStatus::Moved;
    }
    const auto from = world.resolve(intent.from);
    const auto to = world.resolve(intent.to);
    return detail::movement_versions_match_meta(
        world.meta(from.chunk_key), world.meta(to.chunk_key), intent.versions);
  }
}

// `ClassOrTag` is the mover's movement class OR a raw passable tag (normalized
// exactly as in astar_path), so plan and commit share one vocabulary: every
// step A* accepted for a class passes validation for that same class. The
// from- and to-tiles may live on different pages; each is resolved and the
// class predicate evaluated on its own page.

namespace detail {

// Validation core: returns the resolved endpoints alongside the result so
// commit_movement_intent reuses them for its field writes and dirty marks
// instead of re-resolving the same coordinates 4-7x per committed step
// (audit 2026-07-11 M7). Resolved tiles are meaningful only when
// result.status == Moved.
template <typename World, typename ClassOrTag, typename OccupancyTag,
          typename ReservationTag, typename Provider>
[[nodiscard]] auto validate_movement_intent_resolved(
    const World& world, MovementIntent intent,
    const Provider& provider) noexcept {
  using Class = movement::movement_class_of<ClassOrTag>;
  using Model = ResolvedTransitionModel<World, Class, Provider>;
  using Resolved = ResolvedTile<typename World::shape_type>;
  struct Validated {
    MovementResult result;
    Resolved from;
    Resolved to;
  };
  const auto fail = [&](MovementStatus status) {
    return Validated{MovementResult{status, intent.from, intent.to}, Resolved{},
                     Resolved{}};
  };
  const auto resolved_from = world.try_resolve(intent.from);
  if (!resolved_from.has_value()) {
    return fail(MovementStatus::InvalidFrom);
  }
  const auto resolved_to = world.try_resolve(intent.to);
  if (!resolved_to.has_value()) {
    return fail(MovementStatus::InvalidTo);
  }
  if constexpr (std::is_same_v<typename World::residency_type,
                               SparseResident>) {
    // try_resolve is containment-only, so an in-bounds but non-resident
    // endpoint passes the checks above. A non-resident chunk carries no data,
    // but this is a TRANSIENT condition -- the chunk may be reloaded -- so
    // return StaleVersion (a transient failure, matching
    // movement_versions_match for the identical condition) rather than a
    // terminal InvalidFrom/InvalidTo. That routes the agent lifecycle to
    // re-plan against the now-changed residency instead of permanently
    // stranding it at Unreachable, and it still short-circuits before the
    // unchecked accessors below so we never read a non-resident slot out of
    // bounds. Ordinary LRU eviction of a chunk under a Following agent must
    // not read as a permanent caller bug.
    if (!world.is_resident(resolved_from->chunk_key) ||
        !world.is_resident(resolved_to->chunk_key)) {
      return fail(MovementStatus::StaleVersion);
    }
  }
  const auto& from_page = world.chunk(resolved_from->chunk_key);
  const auto& to_page = world.chunk(resolved_to->chunk_key);
  if (!Class::passable(from_page, resolved_from->local_tile_id)) {
    return fail(MovementStatus::BlockedFrom);
  }
  if (!Class::passable(to_page, resolved_to->local_tile_id)) {
    return fail(MovementStatus::BlockedTo);
  }
  auto transition_availability = TransitionAvailability::Blocked;
  auto is_candidate = Model::is_regular_candidate(intent.from, intent.to);
  if (is_candidate) {
    transition_availability =
        Model::regular_availability(world, intent.from, intent.to);
  } else {
    const auto model = Model{provider};
    model.for_each_forward(
        world, intent.from,
        detail::transition_index<typename World::shape_type>(intent.from),
        [&](auto probe) {
          if (probe.to == intent.to) {
            is_candidate = true;
            transition_availability = probe.availability;
          }
        });
  }
  if (!is_candidate) {
    return fail(MovementStatus::NotAdjacent);
  }
  if (transition_availability == TransitionAvailability::MissingTopology) {
    return fail(MovementStatus::StaleVersion);
  }
  if (transition_availability != TransitionAvailability::Legal) {
    return fail(MovementStatus::BlockedTo);
  }
  if (static_cast<bool>(
          to_page.template field<OccupancyTag>(resolved_to->local_tile_id))) {
    return fail(MovementStatus::Occupied);
  }
  if (static_cast<bool>(
          to_page.template field<ReservationTag>(resolved_to->local_tile_id))) {
    return fail(MovementStatus::Reserved);
  }

  // Residency (sparse) was checked above, so the meta reads are safe for
  // both world kinds; with no expectations the compares are skipped
  // entirely.
  if (detail::has_version_expectations(intent.versions)) {
    const auto version_status = detail::movement_versions_match_meta(
        world.meta(resolved_from->chunk_key),
        world.meta(resolved_to->chunk_key), intent.versions);
    if (version_status != MovementStatus::Moved) {
      return fail(version_status);
    }
  }
  return Validated{
      MovementResult{MovementStatus::Moved, intent.from, intent.to},
      *resolved_from, *resolved_to};
}

}  // namespace detail

template <typename World, typename ClassOrTag, typename OccupancyTag,
          typename ReservationTag>
/// Validates bounds, adjacency, topology, occupancy, and expected versions.
///
/// This function does not mutate `world`; success does not reserve the target.
[[nodiscard]] auto validate_movement_intent(const World& world,
                                            MovementIntent intent) noexcept
    -> MovementResult {
  return detail::validate_movement_intent_resolved<World, ClassOrTag,
                                                   OccupancyTag, ReservationTag,
                                                   AdjacentTransitions>(
             world, intent, AdjacentTransitions{})
      .result;
}

template <typename World, typename ClassOrTag, typename OccupancyTag,
          typename ReservationTag, typename Provider>
/// Validates a movement intent against regular and provider-supplied edges.
[[nodiscard]] auto validate_movement_intent(const World& world,
                                            MovementIntent intent,
                                            const Provider& provider) noexcept
    -> MovementResult {
  return detail::validate_movement_intent_resolved<
             World, ClassOrTag, OccupancyTag, ReservationTag, Provider>(
             world, intent, provider)
      .result;
}

template <typename World, typename ClassOrTag, typename OccupancyTag,
          typename ReservationTag>
/// Validates and atomically applies a movement intent to `world`.
///
/// On failure the world is unchanged. Success updates occupancy and, when
/// requested, the dirty metadata maintained by the world.
auto commit_movement_intent(World& world, MovementIntent intent,
                            std::uint32_t dirty_mask = 0) noexcept
    -> MovementResult {
  const auto validated = detail::validate_movement_intent_resolved<
      World, ClassOrTag, OccupancyTag, ReservationTag, AdjacentTransitions>(
      world, intent, AdjacentTransitions{});
  if (validated.result.status != MovementStatus::Moved) {
    return validated.result;
  }

  auto& from_page = world.chunk(validated.from.chunk_key);
  auto& to_page = world.chunk(validated.to.chunk_key);
  from_page.template field<OccupancyTag>(validated.from.local_tile_id) = false;
  to_page.template field<OccupancyTag>(validated.to.local_tile_id) = true;
  to_page.template field<ReservationTag>(validated.to.local_tile_id) = false;
  if (dirty_mask != 0) {
    world.mark_dirty(validated.from.chunk_key, dirty_mask,
                     Box3{intent.from, Extent3{1, 1, 1}});
    world.mark_dirty(validated.to.chunk_key, dirty_mask,
                     Box3{intent.to, Extent3{1, 1, 1}});
  }
  return validated.result;
}

template <typename World, typename ClassOrTag, typename OccupancyTag,
          typename ReservationTag, typename Provider>
/// Validates and commits movement through a provider-supplied edge.
auto commit_movement_intent(World& world, MovementIntent intent,
                            std::uint32_t dirty_mask,
                            const Provider& provider) noexcept
    -> MovementResult {
  const auto validated =
      detail::validate_movement_intent_resolved<World, ClassOrTag, OccupancyTag,
                                                ReservationTag, Provider>(
          world, intent, provider);
  if (validated.result.status != MovementStatus::Moved) {
    return validated.result;
  }

  auto& from_page = world.chunk(validated.from.chunk_key);
  auto& to_page = world.chunk(validated.to.chunk_key);
  from_page.template field<OccupancyTag>(validated.from.local_tile_id) = false;
  to_page.template field<OccupancyTag>(validated.to.local_tile_id) = true;
  to_page.template field<ReservationTag>(validated.to.local_tile_id) = false;
  if (dirty_mask != 0) {
    world.mark_dirty(validated.from.chunk_key, dirty_mask,
                     Box3{intent.from, Extent3{1, 1, 1}});
    world.mark_dirty(validated.to.chunk_key, dirty_mask,
                     Box3{intent.to, Extent3{1, 1, 1}});
  }
  return validated.result;
}

}  // namespace tess
