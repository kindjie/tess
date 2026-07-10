#pragma once

#include <tess/topology/topology.h>

#include <cstdint>

namespace tess {

// Outcome of a topology precheck: a cheap region-graph reachability query run
// before A* so a definitively unreachable goal is rejected without expanding
// the grid. Only `Unreachable` licenses skipping A* -- every other value is
// "inconclusive, run A*" -- so the precheck can never turn a solvable query
// into a wrong failure (see precheck_rules_out_path).
enum class PrecheckStatus : std::uint8_t {
  // The graph admits a region path from start to goal; run A* to realize it.
  Reachable,
  // The graph definitively rules out any route within known topology. This is
  // the ONLY status that lets the caller skip A*.
  Unreachable,
  // The search reached the edge of the resident set (a boundary exit into a
  // non-resident chunk): a route through the unloaded region cannot be ruled
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
[[nodiscard]] constexpr bool precheck_rules_out_path(
    PrecheckStatus status) noexcept {
  return status == PrecheckStatus::Unreachable;
}

// Cheap pre-A* topology gate. Consults `graph` (built over `world`) for whether
// `start` can reach `goal` through region connectivity, WITHOUT expanding the
// grid. Staleness is resolved first and conservatively: an empty graph is
// NoGraph and a graph that no longer matches the world is GraphStale -- both
// BEFORE calling reachable(), because a stale graph can otherwise return a
// definitive but wrong Unreachable from an outdated snapshot. `scratch` is
// caller-owned and reused across queries (allocation-free once warm); it must
// not be shared across concurrent queries.
//
// `ClassOrTag` (explicit first template argument; `World` stays deduced) is
// the movement class the SEARCH uses -- a raw passable tag normalizes to its
// WalkableField identity, exactly as in astar_path. The historical
// precondition that the graph be built over the same passability is now
// ENFORCED through the graph's class stamp: a graph built for a different
// movement class (or predating any stamp) reports GraphStale via
// is_region_graph_fresh_for, so it degrades to running A* rather than letting
// `Unreachable` prune a route the search's own class could walk. Cost
// weighting remains irrelevant (weights only order passable tiles).
template <typename ClassOrTag, typename World>
[[nodiscard]] auto precheck_path(
    const RegionGraphT<typename World::residency_type>& graph,
    const World& world, Coord3 start, Coord3 goal, RegionGraphScratch& scratch)
    -> PrecheckStatus {
  if (graph.local_topologies().empty()) {
    return PrecheckStatus::NoGraph;
  }
  if (!is_region_graph_fresh_for<ClassOrTag>(world, graph)) {
    return PrecheckStatus::GraphStale;
  }
  const auto result =
      reachable<typename World::shape_type>(graph, start, goal, scratch);
  switch (result.status) {
    case ReachabilityStatus::Reachable:
      return PrecheckStatus::Reachable;
    case ReachabilityStatus::Unreachable:
      return PrecheckStatus::Unreachable;
    case ReachabilityStatus::Indeterminate:
      return PrecheckStatus::MissingChunk;
    case ReachabilityStatus::InvalidStart:
      return PrecheckStatus::InvalidStart;
    case ReachabilityStatus::InvalidGoal:
      return PrecheckStatus::InvalidGoal;
  }
  return PrecheckStatus::NoGraph;  // unreachable: all statuses handled above
}

}  // namespace tess
