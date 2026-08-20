#pragma once

#include <tess/path/request.h>
#include <tess/topology/topology.h>

#include <cstdint>

namespace tess {

// Outcome of a topology precheck: a cheap region-graph reachability query run
// before A* so a definitively unreachable goal is rejected without expanding
// the grid. Only `Unreachable` licenses skipping A* -- every other value is
// "inconclusive, run A*" -- so the precheck can never turn a solvable query
// into a wrong failure (see precheck_rules_out_path).
/// Classifies a conservative topology precheck before authoritative search.
enum class PrecheckStatus : std::uint8_t {
  // The graph admits a region path from start to goal; run A* to realize it.
  Reachable,
  // The graph definitively rules out any route within known topology. This is
  // the ONLY status that lets the caller skip A*.
  Unreachable,
  // The search reached the edge of the resident set (a boundary exit into a
  // non-resident chunk): a route through the non-resident region cannot be
  // ruled
  // out, so run A*. Sparse worlds only.
  MissingChunk,
  // Start not resolvable in the graph; run A* (it is authoritative on the
  // start tile's validity/passability).
  InvalidStart,
  // Goal not resolvable in the graph; run A*.
  InvalidGoal,
  // The graph no longer matches the world (a topology edit or residency change
  // since it was built); run A* rather than trust a stale snapshot.
  GraphStale,
  // No built graph was supplied; run A*.
  NoGraph,
};

// True iff the precheck definitively established that no path exists, so the
// caller may skip A* entirely. Every other status means "run A*".
/// Returns whether `status` alone proves that no path exists.
[[nodiscard]] constexpr bool precheck_rules_out_path(
    PrecheckStatus status) noexcept {
  return status == PrecheckStatus::Unreachable;
}

// Cheap pre-A* topology gate. Consults `graph` (built over `world`) for whether
// `start` can reach `goal` through region connectivity, WITHOUT expanding the
// grid. Detectable staleness is resolved first and conservatively: an empty
// graph is NoGraph, and a graph whose recorded stamps no longer match the
// world is GraphStale -- both BEFORE calling reachable(), because a stale
// graph can otherwise return a definitive but wrong Unreachable from an
// outdated snapshot. `scratch` is caller-owned and reused across queries
// (allocation-free once warm); it must not be shared across concurrent
// queries.
//
// STALENESS IS DETECTED, NOT INFERRED. The freshness check compares recorded
// chunk topology versions, residency generations, the shape, and the class
// and provider stamps. A raw field write bumps none of those: only
// `mark_topology_dirty` and `mark_topology_rebuilt` advance
// `topology_version`. So editing a field that a movement class or its
// provider reads -- opening a wall, placing a stair -- leaves this reporting
// a fresh graph, and a caller acting on `precheck_rules_out_path` skips a
// search that would have succeeded. Mark every chunk whose transitions the
// edit can change topology-dirty afterwards -- for an arbitrary provider
// that is not necessarily just the edited tile's chunk; see
// `docs/architecture/topology.md`. The built-in
// `StairTransitions` cannot compensate through provider stamps either: it is
// an empty type, so its instance identity is always null and its revision
// always zero.
//
// `ClassOrTag` (explicit first template argument; `World` stays deduced) is
// the movement class the SEARCH uses -- a raw passable tag normalizes to its
// UnitCostFieldMovement identity, exactly as in astar_path. The historical
// precondition that the graph be built over the same passability is now
// ENFORCED through the graph's class stamp: a graph built for a different
// movement class (or predating any stamp) reports GraphStale via
// is_region_graph_fresh_for, so it degrades to running A* rather than letting
// `Unreachable` prune a route the search's own class could walk. Cost
// weighting remains irrelevant (weights only order passable tiles).
/// Runs a provider-aware conservative reachability check before grid search.
///
/// Only `Unreachable` proves failure. Every other result requires the caller
/// to run authoritative pathfinding. The graph must match the exact provider
/// instance and revision; a mismatch returns `GraphStale`. The caller owns and
/// synchronizes scratch. `missing_chunk_policy` matches authoritative search:
/// the default-facing policy reports `MissingChunk`, while
/// `AssumeImpassable` can make a resident-set boundary definitively
/// `Unreachable` and classifies non-resident endpoints as invalid.
template <typename ClassOrTag, typename World, typename Provider>
[[nodiscard]] auto precheck_path(
    const RegionGraphT<typename World::residency_type>& graph,
    const World& world, PathRequest request, RegionGraphScratch& scratch,
    MissingChunkPolicy missing_chunk_policy, const Provider& provider)
    -> PrecheckStatus {
  if (graph.local_topologies().empty()) {
    return PrecheckStatus::NoGraph;
  }
  if (!is_region_graph_fresh_for<ClassOrTag>(world, graph) ||
      !graph.matches_provider(provider)) {
    return PrecheckStatus::GraphStale;
  }
  const auto result =
      reachable<typename World::shape_type>(graph, request, scratch);
  switch (result.status) {
    case ReachabilityStatus::Reachable:
      return PrecheckStatus::Reachable;
    case ReachabilityStatus::Unreachable:
      return PrecheckStatus::Unreachable;
    case ReachabilityStatus::Indeterminate:
      if (missing_chunk_policy == MissingChunkPolicy::ReportIndeterminate) {
        return PrecheckStatus::MissingChunk;
      }
      if constexpr (!std::is_same_v<typename World::residency_type,
                                    AlwaysResident>) {
        using Shape = typename World::shape_type;
        if (contains<Shape>(request.start) &&
            world.try_chunk(chunk_key<Shape>(
                chunk_coord<Shape>(request.start))) == nullptr) {
          return PrecheckStatus::InvalidStart;
        }
        if (contains<Shape>(request.goal) &&
            world.try_chunk(chunk_key<Shape>(
                chunk_coord<Shape>(request.goal))) == nullptr) {
          return PrecheckStatus::InvalidGoal;
        }
      }
      return PrecheckStatus::Unreachable;
    case ReachabilityStatus::InvalidStart:
      return PrecheckStatus::InvalidStart;
    case ReachabilityStatus::InvalidGoal:
      return PrecheckStatus::InvalidGoal;
  }
  return PrecheckStatus::NoGraph;  // unreachable: all statuses handled above
}

/// Runs the conservative precheck for ordinary adjacent transitions.
///
/// Sparse callers receive `MissingChunk` by default; pass
/// `AssumeImpassable` only when the corresponding policy-relative conclusion
/// is acceptable.
template <typename ClassOrTag, typename World>
[[nodiscard]] auto precheck_path(
    const RegionGraphT<typename World::residency_type>& graph,
    const World& world, PathRequest request, RegionGraphScratch& scratch,
    MissingChunkPolicy missing_chunk_policy =
        MissingChunkPolicy::ReportIndeterminate) -> PrecheckStatus {
  return precheck_path<ClassOrTag>(graph, world, request, scratch,
                                   missing_chunk_policy, AdjacentTransitions{});
}

}  // namespace tess
