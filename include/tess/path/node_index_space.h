#pragma once

#include <tess/core/shape.h>
#include <tess/storage/residency.h>
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
struct NodeIndexSpace {
  static_assert(sizeof(World) == 0,
                "NodeIndexSpace has no specialization for this residency "
                "policy; the SparseResident mapping lands in a later slice.");
};

template <typename World>
struct NodeIndexSpace<World, AlwaysResident> {
  // Every chunk of a dense world is resident, so the map is the identity and
  // A* can index its node arrays by the raw tile index. is_dense lets the
  // shared search code drop residency guards entirely under `if constexpr`,
  // keeping the dense path byte-identical.
  static constexpr bool is_dense = true;

  explicit NodeIndexSpace(const World& /*world*/) noexcept {}

  [[nodiscard]] constexpr bool is_resident_index(
      std::uint64_t /*index*/) const noexcept {
    return true;
  }

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

// Sparse specialization: node-array offsets are bounded by the residency
// budget rather than the global tile count. A tile's offset is its resident
// chunk slot times the per-chunk tile count plus the tile's chunk-local id, so
// the node arrays a search allocates are sized to `capacity * local_tile_count`
// no matter how enormous the shape is. offset must only be called for a
// resident tile (guard with is_resident_index first); the slot mapping is
// stable for the duration of a single search because the world is const.
template <typename World>
struct NodeIndexSpace<World, SparseResident> {
  using shape_type = typename World::shape_type;
  static constexpr bool is_dense = false;
  static constexpr std::uint64_t local_tile_count = World::local_tile_count;

  explicit NodeIndexSpace(const World& world) noexcept : world_(&world) {}

  [[nodiscard]] bool is_resident_index(std::uint64_t index) const noexcept {
    return world_->resident_slot(chunk_key_of(index)) != World::npos_slot;
  }

  [[nodiscard]] std::size_t offset(std::uint64_t index) const noexcept {
    const auto slot = world_->resident_slot(chunk_key_of(index));
    return slot * static_cast<std::size_t>(local_tile_count) + local_of(index);
  }

  [[nodiscard]] std::size_t capacity_hint() const noexcept {
    return world_->capacity() * static_cast<std::size_t>(local_tile_count);
  }

 private:
  using Storage = typename ShapeTraits<shape_type>::TileKeyStorage;

  [[nodiscard]] static ChunkKey chunk_key_of(std::uint64_t index) noexcept {
    return chunk_key<shape_type>(
        TileKey<shape_type>{static_cast<Storage>(index)});
  }

  [[nodiscard]] static std::size_t local_of(std::uint64_t index) noexcept {
    return static_cast<std::size_t>(
        local_tile_id<shape_type>(
            TileKey<shape_type>{static_cast<Storage>(index)})
            .value);
  }

  const World* world_;
};

}  // namespace tess::detail
