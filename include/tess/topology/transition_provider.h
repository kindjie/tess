#pragma once

#include <tess/core/shape.h>
#include <tess/storage/residency.h>

#include <cstdint>
#include <type_traits>

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

// Direction a stair's landing lies in, stored as the integral value of the
// stair field (0 keeps "no stair" as the zero-initialized default).
enum class StairDirection : std::uint8_t {
  None = 0,
  PositiveX = 1,
  NegativeX = 2,
  PositiveY = 3,
  NegativeY = 4,
};

// Vertical provider: an integral `StairTag` field holds a StairDirection.
// A non-None value marks the tile as the FOOT of a stair whose landing is
// one step in that direction AND one z-level up -- deliberately offset,
// because two vertically stacked passable tiles are already six-axis
// adjacent, so a same-column "stair" would add nothing. Each stair
// contributes both directions, each emitted from the chunk owning its
// origin tile: the up transition from the foot's chunk, the down transition
// from the landing's chunk (which is the foot's chunk or its +z face
// neighbor). Whether either endpoint is traversable stays a movement-class
// question -- the graph builders drop transitions whose endpoints hold no
// region label, so stair edges are automatically per-class.
//
// Limit: a landing must share the foot's chunk or a face-neighbor chunk
// (the TransitionProviderFor contract). A stair whose landing would cross
// two chunk boundaries at once -- sideways off the chunk's x/y edge AND up
// off its top z layer -- contributes nothing; place the foot so the landing
// stays within face-neighbor range.
template <typename StairTag>
struct StairTransitions {
  template <typename World, typename Sink>
  void for_each_transition(const World& world, ChunkKey chunk,
                           Sink&& sink) const {
    using Shape = typename World::shape_type;
    static_assert(
        World::schema_type::template contains<StairTag>,
        "StairTransitions references a field absent from the schema.");
    emit_for_feet(world, chunk, chunk, sink);
    // Down transitions land here from stairs whose foot is one z-level
    // below, in the -z neighbor chunk.
    const auto coord_of_chunk = chunk_coord<Shape>(chunk);
    if (coord_of_chunk.z == 0) {
      return;
    }
    auto below = coord_of_chunk;
    --below.z;
    const auto below_key = chunk_key<Shape>(below);
    if constexpr (std::is_same_v<typename World::residency_type,
                                 SparseResident>) {
      // A non-resident foot chunk holds no readable stairs; the landing tile
      // sits on this chunk's bottom layer, whose boundary exit toward the
      // missing chunk already marks the region as reaching missing topology.
      if (!world.is_resident(below_key)) {
        return;
      }
    }
    emit_for_feet(world, below_key, chunk, sink);
  }

 private:
  // Emits the transitions of every stair FOOT in `foot_chunk` whose origin
  // tile lies in `origin_chunk`: the up transition when the foot is the
  // origin, the down transition when the landing is.
  template <typename World, typename Sink>
  static void emit_for_feet(const World& world, ChunkKey foot_chunk,
                            ChunkKey origin_chunk, Sink&& sink) {
    using Shape = typename World::shape_type;
    using Traits = ShapeTraits<Shape>;
    const auto& page = world.chunk(foot_chunk);
    const auto origin = chunk_coord<Shape>(foot_chunk);
    const auto base = Coord3{
        static_cast<std::int64_t>(origin.x * Traits::chunk.x),
        static_cast<std::int64_t>(origin.y * Traits::chunk.y),
        static_cast<std::int64_t>(origin.z * Traits::chunk.z),
    };
    for (std::uint64_t raw_id = 0; raw_id < Traits::local_tile_count;
         ++raw_id) {
      const auto value = page.template field<StairTag>(LocalTileId{raw_id});
      static_assert(std::is_integral_v<std::remove_cvref_t<decltype(value)>>,
                    "StairTransitions requires an integral stair field.");
      const auto direction = static_cast<StairDirection>(value);
      if (direction == StairDirection::None) {
        continue;
      }
      const auto local = local_coord_of<Shape>(raw_id);
      const auto foot =
          Coord3{base.x + local.x, base.y + local.y, base.z + local.z};
      auto landing = foot;
      ++landing.z;
      switch (direction) {
        case StairDirection::PositiveX:
          ++landing.x;
          break;
        case StairDirection::NegativeX:
          --landing.x;
          break;
        case StairDirection::PositiveY:
          ++landing.y;
          break;
        case StairDirection::NegativeY:
          --landing.y;
          break;
        case StairDirection::None:
          continue;
      }
      if (!contains<Shape>(landing)) {
        continue;
      }
      // Diagonal chunk crossings (sideways AND up at once) violate the
      // face-neighbor contract; such a stair contributes nothing.
      const auto foot_chunk_coord = chunk_coord<Shape>(foot);
      const auto landing_chunk_coord = chunk_coord<Shape>(landing);
      const auto dx = foot_chunk_coord.x != landing_chunk_coord.x ? 1 : 0;
      const auto dy = foot_chunk_coord.y != landing_chunk_coord.y ? 1 : 0;
      const auto dz = foot_chunk_coord.z != landing_chunk_coord.z ? 1 : 0;
      if (dx + dy + dz > 1) {
        continue;
      }
      const auto landing_chunk = chunk_key<Shape>(landing_chunk_coord);
      if (foot_chunk.value == origin_chunk.value) {
        sink(foot, landing);  // up, originating at the foot
        if (landing_chunk.value == origin_chunk.value) {
          sink(landing, foot);  // down, landing shares the chunk
        }
      } else if (landing_chunk.value == origin_chunk.value) {
        sink(landing, foot);  // down, originating at the landing
      }
    }
  }

  template <typename Shape>
  [[nodiscard]] static auto local_coord_of(std::uint64_t raw_id) noexcept
      -> Coord3 {
    const auto chunk = ShapeTraits<Shape>::chunk;
    const auto xy = static_cast<std::uint64_t>(chunk.x) * chunk.y;
    const auto z = raw_id / xy;
    const auto rest = raw_id % xy;
    return Coord3{
        static_cast<std::int64_t>(rest % chunk.x),
        static_cast<std::int64_t>(rest / chunk.x),
        static_cast<std::int64_t>(z),
    };
  }
};

}  // namespace tess
