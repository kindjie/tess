#pragma once

#include <tess/core/shape.h>
#include <tess/storage/world.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
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

  static constexpr auto local_bounds() noexcept -> Box3 {
    return Box3{Coord3{0, 0, 0}, ShapeTraits<shape_type>::chunk};
  }

  static constexpr bool contains_local(Coord3 coord) noexcept {
    return tess::contains(local_bounds(), coord);
  }

  static constexpr auto try_local_coord(Coord3 coord) noexcept
      -> std::optional<LocalCoord3> {
    if (!contains_local(coord)) {
      return std::nullopt;
    }

    return LocalCoord3{
        static_cast<std::uint64_t>(coord.x),
        static_cast<std::uint64_t>(coord.y),
        static_cast<std::uint64_t>(coord.z),
    };
  }

  static constexpr auto local_coord(LocalTileId id) noexcept -> LocalCoord3 {
    const auto chunk = ShapeTraits<shape_type>::chunk;
    const auto local_xy = chunk.x * chunk.y;
    const auto local_z = id.value / local_xy;
    const auto remainder = id.value % local_xy;
    const auto local_y = remainder / chunk.x;
    const auto local_x = remainder % chunk.x;

    return LocalCoord3{local_x, local_y, local_z};
  }

  static constexpr auto local_tile_id(LocalCoord3 coord) noexcept
      -> LocalTileId {
    return tess::local_tile_id<shape_type>(coord);
  }

  static constexpr bool is_boundary(LocalCoord3 coord) noexcept {
    const auto chunk = ShapeTraits<shape_type>::chunk;
    return (chunk.x > 1 && (coord.x == 0 || coord.x + 1 == chunk.x)) ||
           (chunk.y > 1 && (coord.y == 0 || coord.y + 1 == chunk.y)) ||
           (chunk.z > 1 && (coord.z == 0 || coord.z + 1 == chunk.z));
  }

  static constexpr bool is_interior(LocalCoord3 coord) noexcept {
    return !is_boundary(coord);
  }

  constexpr auto world_coord(Coord3 local_candidate) const noexcept -> Coord3 {
    const auto chunk = ShapeTraits<shape_type>::chunk;
    return Coord3{
        static_cast<std::int64_t>(coord_.x * chunk.x) + local_candidate.x,
        static_cast<std::int64_t>(coord_.y * chunk.y) + local_candidate.y,
        static_cast<std::int64_t>(coord_.z * chunk.z) + local_candidate.z,
    };
  }

  constexpr auto world_coord(LocalCoord3 coord) const noexcept -> Coord3 {
    return world_coord(Coord3{
        static_cast<std::int64_t>(coord.x),
        static_cast<std::int64_t>(coord.y),
        static_cast<std::int64_t>(coord.z),
    });
  }

  constexpr auto world_coord(LocalTileId id) const noexcept -> Coord3 {
    return world_coord(local_coord(id));
  }

  template <typename Fn>
  constexpr void for_each_tile(Fn&& fn) const {
    for (std::uint64_t i = 0; i < ShapeTraits<shape_type>::local_tile_count;
         ++i) {
      const auto id = LocalTileId{i};
      std::invoke(fn, id, local_coord(id));
    }
  }

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

template <typename World, WritePolicy Policy>
class BlockCtx {
 public:
  static_assert(is_valid_write_policy(Policy));

  using world_type = std::remove_reference_t<World>;
  using view_world_type =
      std::conditional_t<Policy == WritePolicy::ReadOnly,
                         const std::remove_const_t<world_type>, world_type>;

  constexpr BlockCtx(world_type& world, ChunkDomain domain) noexcept
      : world_(&world), domain_(domain) {}

  constexpr auto world() const noexcept -> world_type& { return *world_; }

  constexpr auto domain() const noexcept -> ChunkDomain { return domain_; }

  constexpr auto policy() const noexcept -> WritePolicy { return Policy; }

  constexpr auto size() const noexcept -> std::size_t { return domain_.size(); }

  constexpr bool empty() const noexcept { return domain_.empty(); }

  constexpr auto chunk_view(ChunkKey key) const noexcept
      -> ChunkView<view_world_type> {
    return ChunkView<view_world_type>{*world_, key};
  }

  template <typename Fn>
  constexpr void for_each_chunk(Fn&& fn) const {
    for (const auto key : domain_) {
      std::invoke(std::forward<Fn>(fn), chunk_view(key));
    }
  }

 private:
  world_type* world_;
  ChunkDomain domain_;
};

template <WritePolicy Policy, typename World>
constexpr auto block_ctx(World& world, ChunkDomain domain) noexcept
    -> BlockCtx<World, Policy> {
  return BlockCtx<World, Policy>{world, domain};
}

template <WritePolicy Policy, typename World, typename Fn>
constexpr void for_each_chunk(World& world, ChunkDomain domain, Fn&& fn) {
  block_ctx<Policy>(world, domain).for_each_chunk(std::forward<Fn>(fn));
}

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
