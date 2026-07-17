#pragma once

#include <tess/core/shape.h>
#include <tess/storage/residency.h>

#include <concepts>
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
// The provider type and revision are stamped on the graph at build time
// (mirroring the movement-class stamp). Empty providers have revision zero.
// A stateful provider must expose
// `std::uint64_t transition_revision() const noexcept` and change that value
// whenever its emitted transition set can change. update_region_graph falls
// back to a full rebuild when either stamp differs.
/// Constrains deterministic special-transition providers for a world type.
template <typename P, typename World>
concept TransitionProviderFor = requires(const P& provider, const World& world,
                                         ChunkKey chunk,
                                         void (*sink)(Coord3, Coord3)) {
  provider.for_each_transition(world, chunk, sink);
} && (std::is_empty_v<P> || requires(const P& provider) {
                                  {
                                    provider.transition_revision()
                                  } noexcept -> std::same_as<std::uint64_t>;
                                });

namespace detail {

// Revision helper shared by graph construction and incremental updates.
// Empty providers compile to the constant zero; stateful providers pay one
// cheap revision read per graph build/update, never per transition.
template <typename P>
[[nodiscard]] constexpr auto transition_provider_revision(
    const P& provider) noexcept -> std::uint64_t {
  if constexpr (std::is_empty_v<P>) {
    (void)provider;
    return 0;
  } else {
    return provider.transition_revision();
  }
}

}  // namespace detail

// The default provider: no transitions beyond the built-in face adjacency
// the region graph already pairs. Building with it is byte-identical to the
// providerless build.
/// Supplies no special transitions beyond ordinary face adjacency.
struct AdjacentTransitions {
  template <typename World, typename Sink>
  void for_each_transition(const World& /*world*/, ChunkKey /*chunk*/,
                           Sink&& /*sink*/) const noexcept {}
};

// Direction a stair's landing lies in, stored as the integral value of the
// stair field (0 keeps "no stair" as the zero-initialized default).
/// Encodes the horizontal component of a one-level stair transition.
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
// from the landing's chunk -- the foot's chunk, its +z face neighbor (the
// landing rose off the top layer), or a sideways face neighbor (the landing
// stepped off the x/y edge at a lower local z). Whether either endpoint is
// traversable stays a movement-class question -- the graph builders drop
// transitions whose endpoints hold no region label, so stair edges are
// automatically per-class. A stair field value outside the StairDirection
// range reads as None.
//
// Limit: a landing must share the foot's chunk or a face-neighbor chunk
// (the TransitionProviderFor contract). A stair whose landing would cross
// two chunk boundaries at once -- sideways off the chunk's x/y edge AND up
// off its top z layer -- contributes nothing; place the foot so the landing
// stays within face-neighbor range.
/// Emits bidirectional stair transitions encoded by an integral world field.
template <typename StairTag>
struct StairTransitions {
  template <typename World, typename Sink>
  void for_each_transition(const World& world, ChunkKey chunk,
                           Sink&& sink) const {
    using Shape = typename World::shape_type;
    using Traits = ShapeTraits<Shape>;
    static_assert(
        World::schema_type::template contains<StairTag>,
        "StairTransitions references a field absent from the schema.");
    emit_for_feet(world, chunk, chunk, sink);
    // Down transitions originating here belong to stairs whose FOOT lies in
    // a face-neighbor chunk: one z-level below (the landing rose off the
    // foot chunk's top layer) or sideways at the same chunk z (the landing
    // stepped off the foot chunk's x/y edge with local z below the top).
    const auto coord_of_chunk = chunk_coord<Shape>(chunk);
    const auto scan_foot_chunk = [&](ChunkCoord3 neighbor) {
      const auto key = chunk_key<Shape>(neighbor);
      if constexpr (std::is_same_v<typename World::residency_type,
                                   SparseResident>) {
        // A non-resident foot chunk holds no readable stairs; the landing
        // tile sits on this chunk's boundary toward it, whose boundary exit
        // already marks the region as reaching missing topology.
        if (!world.is_resident(key)) {
          return;
        }
      }
      emit_for_feet(world, key, chunk, sink);
    };
    if (coord_of_chunk.z > 0) {
      auto below = coord_of_chunk;
      --below.z;
      scan_foot_chunk(below);
    }
    if (coord_of_chunk.x > 0) {
      auto west = coord_of_chunk;
      --west.x;
      scan_foot_chunk(west);
    }
    if (coord_of_chunk.x + 1 < Traits::chunk_count_x) {
      auto east = coord_of_chunk;
      ++east.x;
      scan_foot_chunk(east);
    }
    if (coord_of_chunk.y > 0) {
      auto south = coord_of_chunk;
      --south.y;
      scan_foot_chunk(south);
    }
    if (coord_of_chunk.y + 1 < Traits::chunk_count_y) {
      auto north = coord_of_chunk;
      ++north.y;
      scan_foot_chunk(north);
    }
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
      // Out-of-range values read as None. Compare BEFORE any narrowing: a
      // wider field's 257 must not wrap into PositiveX, and a negative value
      // must not wrap into the valid range.
      if constexpr (std::is_signed_v<std::remove_cvref_t<decltype(value)>>) {
        if (value <= 0) {
          continue;
        }
      } else if (value == 0) {
        continue;
      }
      if (static_cast<std::uint64_t>(value) >
          static_cast<std::uint64_t>(StairDirection::NegativeY)) {
        continue;
      }
      const auto direction =
          static_cast<StairDirection>(static_cast<std::uint8_t>(value));
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
