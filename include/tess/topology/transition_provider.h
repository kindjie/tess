#pragma once

#include <tess/core/shape.h>

namespace tess {

// A transition provider contributes EXTRA directed tile-to-tile transitions
// to the region graph, beyond the built-in six-axis face adjacency (stairs,
// ladders, and similar special movement for a class). The graph builders
// enumerate a provider once per chunk and append one directed RegionPortal
// per transition whose endpoints both resolve to labeled regions.
//
// Contract:
// - `for_each_transition(world, chunk, sink)` invokes `sink(from, to)` once
//   per directed transition ORIGINATING in `chunk` (`from` must lie in that
//   chunk). Bidirectional passages emit both directions, each from its own
//   chunk's enumeration.
// - `to` must lie in the same chunk or a face-neighbor chunk. Incremental
//   updates re-derive portals only for dirty chunks and their face
//   neighbors, so a longer-range transition would survive, stale, past an
//   edit to its landing chunk (asserted in debug builds).
// - Enumeration must be deterministic for identical world content: the
//   incremental-equals-full-rebuild invariant compares portal order.
// - Transitions whose endpoints are not passable for the graph's movement
//   class contribute nothing (their tiles hold no region label). On a sparse
//   world a transition landing in a NON-RESIDENT chunk marks the origin
//   region as reaching missing topology, so reachability degrades to
//   Indeterminate rather than a wrong Unreachable.
//
// The provider TYPE is stamped on the graph at build time (mirroring the
// movement-class stamp): update_region_graph with a different provider type
// falls back to a full rebuild rather than patching with mismatched edges.
template <typename P, typename World>
concept TransitionProviderFor =
    requires(const P& provider, const World& world, ChunkKey chunk,
             void (*sink)(Coord3, Coord3)) {
      provider.for_each_transition(world, chunk, sink);
    };

// The default provider: no transitions beyond the built-in face adjacency
// the region graph already pairs. Building with it is byte-identical to the
// providerless build.
struct AdjacentTransitions {
  template <typename World, typename Sink>
  void for_each_transition(const World& /*world*/, ChunkKey /*chunk*/,
                           Sink&& /*sink*/) const noexcept {}
};

}  // namespace tess
