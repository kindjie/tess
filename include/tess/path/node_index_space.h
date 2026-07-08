#pragma once

#include <tess/core/shape.h>
#include <tess/storage/world.h>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace tess::detail {

// Maps a global tile index (0..tile_count) to a compact node-array offset and
// reports the node-array capacity a search must allocate. For the dense
// AlwaysResident world the map is the identity and the capacity is the whole
// tile count, so A* node storage and indexing are byte-identical to indexing
// the arrays by the raw tile index. The sparse specialization (added in a
// later slice) remaps through the resident chunk directory so node arrays are
// bounded by the residency budget rather than the global tile count, letting
// A* run over worlds far too large to index densely.
template <typename World, typename Residency = typename World::residency_type>
struct NodeIndexSpace;

template <typename World>
struct NodeIndexSpace<World, AlwaysResident> {
  explicit NodeIndexSpace(const World& /*world*/) noexcept {}

  [[nodiscard]] constexpr std::size_t offset(
      std::uint64_t index) const noexcept {
    return static_cast<std::size_t>(index);
  }

  [[nodiscard]] constexpr std::size_t capacity_hint() const noexcept {
    static_assert(World::chunk_count <=
                  std::numeric_limits<std::size_t>::max() /
                      World::local_tile_count);
    return static_cast<std::size_t>(World::chunk_count *
                                    World::local_tile_count);
  }
};

}  // namespace tess::detail
