#pragma once

#include <tess/core/shape.h>
#include <tess/storage/world.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace tess {

enum class WritePolicy {
  ReadOnly,
  UniquePerTile,
  UniquePerChunk,
  Unsafe,
};

constexpr bool is_valid_write_policy(WritePolicy policy) noexcept {
  switch (policy) {
    case WritePolicy::ReadOnly:
    case WritePolicy::UniquePerTile:
    case WritePolicy::UniquePerChunk:
    case WritePolicy::Unsafe:
      return true;
  }
  return false;
}

class ChunkDomain {
 public:
  constexpr ChunkDomain() noexcept = default;

  constexpr explicit ChunkDomain(std::span<const ChunkKey> keys) noexcept
      : keys_(keys) {}

  constexpr auto keys() const noexcept -> std::span<const ChunkKey> {
    return keys_;
  }

  constexpr auto begin() const noexcept { return keys_.begin(); }

  constexpr auto end() const noexcept { return keys_.end(); }

  constexpr auto size() const noexcept -> std::size_t { return keys_.size(); }

  constexpr bool empty() const noexcept { return keys_.empty(); }

 private:
  std::span<const ChunkKey> keys_;
};

constexpr auto chunk_domain(std::span<const ChunkKey> keys) noexcept
    -> ChunkDomain {
  return ChunkDomain{keys};
}

inline auto explicit_chunk_domain(std::span<const ChunkKey> keys)
    -> std::vector<ChunkKey> {
  std::vector<ChunkKey> domain{keys.begin(), keys.end()};
  std::sort(domain.begin(), domain.end(),
            [](ChunkKey lhs, ChunkKey rhs) { return lhs.value < rhs.value; });
  return domain;
}

template <typename World>
auto dirty_chunk_domain(const World& world, std::uint32_t flags)
    -> std::vector<ChunkKey> {
  return world.dirty_chunks(flags);
}

template <typename World>
auto active_chunk_domain(const World& world, std::uint32_t flags)
    -> std::vector<ChunkKey> {
  return world.active_chunks(flags);
}

template <typename World>
class ChunkView {
 public:
  using world_type = std::remove_reference_t<World>;
  using mutable_world_type = std::remove_cv_t<world_type>;
  using shape_type = typename mutable_world_type::shape_type;
  using page_type =
      std::conditional_t<std::is_const_v<world_type>,
                         const typename mutable_world_type::page_type,
                         typename mutable_world_type::page_type>;
  using meta_type = std::conditional_t<std::is_const_v<world_type>,
                                       const ChunkMeta, ChunkMeta>;

  constexpr ChunkView(world_type& world, ChunkKey key) noexcept
      : page_(&world.chunk(key)),
        meta_(&world.meta(key)),
        key_(key),
        coord_(chunk_coord<shape_type>(key)),
        bounds_(chunk_bounds(coord_)) {}

  constexpr auto page() const noexcept -> page_type& { return *page_; }

  constexpr auto meta() const noexcept -> meta_type& { return *meta_; }

  constexpr auto key() const noexcept -> ChunkKey { return key_; }

  constexpr auto coord() const noexcept -> ChunkCoord3 { return coord_; }

  constexpr auto bounds() const noexcept -> Box3 { return bounds_; }

  template <typename Tag>
  constexpr auto field_span() const noexcept {
    return page_->template field_span<Tag>();
  }

 private:
  static constexpr auto chunk_bounds(ChunkCoord3 coord) noexcept -> Box3 {
    const auto chunk = ShapeTraits<shape_type>::chunk;
    return Box3{
        Coord3{
            static_cast<std::int64_t>(coord.x * chunk.x),
            static_cast<std::int64_t>(coord.y * chunk.y),
            static_cast<std::int64_t>(coord.z * chunk.z),
        },
        chunk,
    };
  }

  page_type* page_;
  meta_type* meta_;
  ChunkKey key_;
  ChunkCoord3 coord_;
  Box3 bounds_;
};

template <typename World, typename Fn>
constexpr void for_each_chunk(World& world, ChunkDomain domain,
                              WritePolicy policy, Fn&& fn) {
  assert(is_valid_write_policy(policy));
  (void)policy;
  for (const auto key : domain) {
    std::invoke(std::forward<Fn>(fn), ChunkView<World>{world, key});
  }
}

}  // namespace tess
