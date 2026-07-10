#pragma once

#include <tess/core/shape.h>
#include <tess/storage/world.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace tess {

enum class WritePolicy : std::uint8_t {
  ReadOnly,
  UniquePerTile,
  UniquePerChunk,
  Unsafe,
};
static_assert(sizeof(WritePolicy) == sizeof(std::uint8_t));

[[nodiscard]] constexpr bool is_valid_write_policy(
    WritePolicy policy) noexcept {
  switch (policy) {
    case WritePolicy::ReadOnly:
    case WritePolicy::UniquePerTile:
    case WritePolicy::UniquePerChunk:
    case WritePolicy::Unsafe:
      return true;
  }
  return false;
}

class BlockScratch {
 public:
  BlockScratch() = default;

  BlockScratch(BlockScratch&& other) noexcept
      : storage_(std::move(other.storage_)),
        capacity_bytes_(std::exchange(other.capacity_bytes_, 0)),
        used_bytes_(std::exchange(other.used_bytes_, 0)) {}

  auto operator=(BlockScratch&& other) noexcept -> BlockScratch& {
    storage_ = std::move(other.storage_);
    capacity_bytes_ = std::exchange(other.capacity_bytes_, 0);
    used_bytes_ = std::exchange(other.used_bytes_, 0);
    return *this;
  }

  BlockScratch(const BlockScratch&) = delete;
  auto operator=(const BlockScratch&) -> BlockScratch& = delete;

  ~BlockScratch() = default;

  // Growth allocates a fresh buffer: previously returned spans are
  // invalidated and scratch contents are not preserved. Only the byte
  // accounting (`used_bytes()`) carries over.
  void reserve_bytes(std::size_t bytes) {
    const auto word_count =
        bytes / word_size + (bytes % word_size == 0 ? 0 : 1);
    if (word_count > std::numeric_limits<std::size_t>::max() / word_size) {
      throw std::bad_alloc{};
    }
    const auto byte_capacity = word_count * word_size;
    if (byte_capacity > capacity_bytes_) {
      // The std::byte array-new implicitly creates implicit-lifetime
      // objects in its storage ([intro.object]/13), which makes the
      // typed spans returned by allocate<T> well-defined.
      storage_ = std::make_unique_for_overwrite<std::byte[]>(byte_capacity);
      capacity_bytes_ = byte_capacity;
    }
  }

  constexpr void reset() noexcept { used_bytes_ = 0; }

  [[nodiscard]] constexpr auto capacity_bytes() const noexcept -> std::size_t {
    return capacity_bytes_;
  }

  [[nodiscard]] constexpr auto used_bytes() const noexcept -> std::size_t {
    return used_bytes_;
  }

  [[nodiscard]] constexpr auto remaining_bytes() const noexcept -> std::size_t {
    return capacity_bytes() - used_bytes_;
  }

  template <typename T>
  [[nodiscard]] auto allocate(std::size_t count) noexcept -> std::span<T> {
    static_assert(!std::is_void_v<T>);
    static_assert(alignof(T) <= alignof(std::max_align_t));
    static_assert(std::is_trivially_default_constructible_v<T>);
    static_assert(std::is_trivially_destructible_v<T>);

    if (count == 0) {
      return {};
    }
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
      return {};
    }

    const auto byte_count = count * sizeof(T);
    const auto aligned_offset = align_offset(used_bytes_, alignof(T));
    if (aligned_offset > capacity_bytes() ||
        byte_count > capacity_bytes() - aligned_offset) {
      return {};
    }

    // cppcheck misparses std::byte* as void* here (suppressed in
    // TessProjectOptions.cmake); std::byte pointer arithmetic is
    // well-defined.
    auto* ptr =
        std::launder(reinterpret_cast<T*>(storage_.get() + aligned_offset));
    used_bytes_ = aligned_offset + byte_count;
    return std::span<T>{ptr, count};
  }

 private:
  static constexpr auto word_size = sizeof(std::max_align_t);

  // `new std::byte[n]` only guarantees the default new alignment; the class
  // promises alignof(std::max_align_t) for the buffer base.
  static_assert(__STDCPP_DEFAULT_NEW_ALIGNMENT__ >= alignof(std::max_align_t));

  [[nodiscard]] static constexpr auto align_offset(
      std::size_t offset, std::size_t alignment) noexcept -> std::size_t {
    const auto remainder = offset % alignment;
    if (remainder == 0) {
      return offset;
    }
    return offset + (alignment - remainder);
  }

  std::unique_ptr<std::byte[]> storage_;
  std::size_t capacity_bytes_ = 0;
  std::size_t used_bytes_ = 0;
};

class BlockDiagnostics {
 public:
  constexpr void record_scratch_allocation_failure() noexcept {
    ++scratch_allocation_failures_;
  }

  constexpr void reset() noexcept { scratch_allocation_failures_ = 0; }

  [[nodiscard]] constexpr auto scratch_allocation_failures() const noexcept
      -> std::size_t {
    return scratch_allocation_failures_;
  }

 private:
  std::size_t scratch_allocation_failures_ = 0;
};

class ChunkDomain {
 public:
  constexpr ChunkDomain() noexcept = default;

  constexpr explicit ChunkDomain(std::span<const ChunkKey> keys) noexcept
      : keys_(keys) {}

  [[nodiscard]] constexpr auto keys() const noexcept
      -> std::span<const ChunkKey> {
    return keys_;
  }

  [[nodiscard]] constexpr auto begin() const noexcept { return keys_.begin(); }

  [[nodiscard]] constexpr auto end() const noexcept { return keys_.end(); }

  [[nodiscard]] constexpr auto size() const noexcept -> std::size_t {
    return keys_.size();
  }

  [[nodiscard]] constexpr bool empty() const noexcept { return keys_.empty(); }

 private:
  std::span<const ChunkKey> keys_;
};

class OwnedChunkDomain {
 public:
  OwnedChunkDomain() = default;

  explicit OwnedChunkDomain(std::vector<ChunkKey> keys)
      : keys_(std::move(keys)) {}

  [[nodiscard]] constexpr auto view() const noexcept -> ChunkDomain {
    return ChunkDomain{keys_};
  }

  [[nodiscard]] constexpr auto keys() const noexcept
      -> std::span<const ChunkKey> {
    return keys_;
  }

  [[nodiscard]] constexpr auto begin() const noexcept { return keys_.begin(); }

  [[nodiscard]] constexpr auto end() const noexcept { return keys_.end(); }

  [[nodiscard]] constexpr auto size() const noexcept -> std::size_t {
    return keys_.size();
  }

  [[nodiscard]] constexpr bool empty() const noexcept { return keys_.empty(); }

 private:
  std::vector<ChunkKey> keys_;
};

[[nodiscard]] constexpr auto chunk_domain(
    std::span<const ChunkKey> keys) noexcept -> ChunkDomain {
  return ChunkDomain{keys};
}

[[nodiscard]] constexpr auto chunk_domain(const OwnedChunkDomain& keys) noexcept
    -> ChunkDomain {
  return keys.view();
}

auto chunk_domain(OwnedChunkDomain&& keys) noexcept -> ChunkDomain = delete;

[[nodiscard]] inline auto explicit_chunk_domain(std::span<const ChunkKey> keys)
    -> OwnedChunkDomain {
  std::vector<ChunkKey> domain{keys.begin(), keys.end()};
  std::sort(domain.begin(), domain.end(),
            [](ChunkKey lhs, ChunkKey rhs) { return lhs.value < rhs.value; });
  return OwnedChunkDomain{std::move(domain)};
}

template <typename World>
[[nodiscard]] auto dirty_chunk_domain(const World& world, std::uint32_t flags)
    -> OwnedChunkDomain {
  return OwnedChunkDomain{world.dirty_chunks(flags)};
}

template <typename World>
[[nodiscard]] auto active_chunk_domain(const World& world, std::uint32_t flags)
    -> OwnedChunkDomain {
  return OwnedChunkDomain{world.active_chunks(flags)};
}

template <typename World>
class ChunkView {
 public:
  using world_type = std::remove_reference_t<World>;
  using mutable_world_type = std::remove_cv_t<world_type>;
  using shape_type = mutable_world_type::shape_type;
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

  [[nodiscard]] constexpr auto page() const noexcept -> page_type& {
    return *page_;
  }

  [[nodiscard]] constexpr auto meta() const noexcept -> meta_type& {
    return *meta_;
  }

  [[nodiscard]] constexpr auto key() const noexcept -> ChunkKey { return key_; }

  [[nodiscard]] constexpr auto coord() const noexcept -> ChunkCoord3 {
    return coord_;
  }

  [[nodiscard]] constexpr auto bounds() const noexcept -> Box3 {
    return bounds_;
  }

  [[nodiscard]] static constexpr auto local_bounds() noexcept -> Box3 {
    return Box3{Coord3{0, 0, 0}, ShapeTraits<shape_type>::chunk};
  }

  [[nodiscard]] static constexpr bool contains_local(Coord3 coord) noexcept {
    return tess::contains(local_bounds(), coord);
  }

  [[nodiscard]] static constexpr auto try_local_coord(Coord3 coord) noexcept
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

  [[nodiscard]] static constexpr auto local_coord(LocalTileId id) noexcept
      -> LocalCoord3 {
    const auto chunk = ShapeTraits<shape_type>::chunk;
    const auto local_xy = chunk.x * chunk.y;
    const auto local_z = id.value / local_xy;
    const auto remainder = id.value % local_xy;
    const auto local_y = remainder / chunk.x;
    const auto local_x = remainder % chunk.x;

    return LocalCoord3{local_x, local_y, local_z};
  }

  [[nodiscard]] static constexpr auto local_tile_id(LocalCoord3 coord) noexcept
      -> LocalTileId {
    return tess::local_tile_id<shape_type>(coord);
  }

  // True when the tile lies on a chunk face along an axis whose chunk extent
  // is greater than 1. Along a 1-tile-wide axis every tile touches both faces;
  // such an axis is deliberately NOT counted as boundary -- even when neighbor
  // chunks exist along it -- so a degenerate axis does not classify the whole
  // chunk as boundary. Callers that need "has a neighbor chunk across this
  // face" must consult the shape's chunk grid (as topology's boundary-exit
  // derivation does); is_boundary/is_interior only describe the position
  // within one chunk.
  [[nodiscard]] static constexpr bool is_boundary(LocalCoord3 coord) noexcept {
    const auto chunk = ShapeTraits<shape_type>::chunk;
    return (chunk.x > 1 && (coord.x == 0 || coord.x + 1 == chunk.x)) ||
           (chunk.y > 1 && (coord.y == 0 || coord.y + 1 == chunk.y)) ||
           (chunk.z > 1 && (coord.z == 0 || coord.z + 1 == chunk.z));
  }

  [[nodiscard]] static constexpr bool is_interior(LocalCoord3 coord) noexcept {
    return !is_boundary(coord);
  }

  [[nodiscard]] constexpr auto world_coord(
      Coord3 local_candidate) const noexcept -> Coord3 {
    const auto chunk = ShapeTraits<shape_type>::chunk;
    return Coord3{
        static_cast<std::int64_t>(coord_.x * chunk.x) + local_candidate.x,
        static_cast<std::int64_t>(coord_.y * chunk.y) + local_candidate.y,
        static_cast<std::int64_t>(coord_.z * chunk.z) + local_candidate.z,
    };
  }

  [[nodiscard]] constexpr auto world_coord(LocalCoord3 coord) const noexcept
      -> Coord3 {
    return world_coord(Coord3{
        static_cast<std::int64_t>(coord.x),
        static_cast<std::int64_t>(coord.y),
        static_cast<std::int64_t>(coord.z),
    });
  }

  [[nodiscard]] constexpr auto world_coord(LocalTileId id) const noexcept
      -> Coord3 {
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
  [[nodiscard]] constexpr auto field_span() const noexcept {
    return page_->template field_span<Tag>();
  }

 private:
  [[nodiscard]] static constexpr auto chunk_bounds(ChunkCoord3 coord) noexcept
      -> Box3 {
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

  constexpr BlockCtx(world_type& world, ChunkDomain domain,
                     BlockScratch* scratch = nullptr,
                     BlockDiagnostics* diagnostics = nullptr) noexcept
      : world_(&world),
        domain_(domain),
        scratch_(scratch),
        diagnostics_(diagnostics) {}

  [[nodiscard]] constexpr auto world() const noexcept -> view_world_type& {
    return *world_;
  }

  [[nodiscard]] constexpr auto domain() const noexcept -> ChunkDomain {
    return domain_;
  }

  [[nodiscard]] constexpr auto policy() const noexcept -> WritePolicy {
    return Policy;
  }

  [[nodiscard]] constexpr auto size() const noexcept -> std::size_t {
    return domain_.size();
  }

  [[nodiscard]] constexpr bool empty() const noexcept {
    return domain_.empty();
  }

  [[nodiscard]] constexpr auto scratch() noexcept -> BlockScratch* {
    return scratch_;
  }

  [[nodiscard]] constexpr auto scratch() const noexcept -> const BlockScratch* {
    return scratch_;
  }

  constexpr void reset_scratch() const noexcept {
    if (scratch_ != nullptr) {
      scratch_->reset();
    }
  }

  [[nodiscard]] constexpr auto diagnostics() noexcept -> BlockDiagnostics* {
    return diagnostics_;
  }

  [[nodiscard]] constexpr auto diagnostics() const noexcept
      -> const BlockDiagnostics* {
    return diagnostics_;
  }

  constexpr void reset_diagnostics() const noexcept {
    if (diagnostics_ != nullptr) {
      diagnostics_->reset();
    }
  }

  [[nodiscard]] constexpr auto chunk_view(ChunkKey key) const noexcept
      -> ChunkView<view_world_type> {
    return ChunkView<view_world_type>{*world_, key};
  }

  template <typename Fn>
  constexpr void for_each_chunk(Fn&& fn) const {
    for (const auto key : domain_) {
      std::invoke(fn, chunk_view(key));
    }
  }

 private:
  world_type* world_;
  ChunkDomain domain_;
  BlockScratch* scratch_;
  BlockDiagnostics* diagnostics_;
};

template <WritePolicy Policy, typename World>
[[nodiscard]] constexpr auto block_ctx(World& world,
                                       ChunkDomain domain) noexcept
    -> BlockCtx<World, Policy> {
  return BlockCtx<World, Policy>{world, domain};
}

template <WritePolicy Policy, typename World>
[[nodiscard]] constexpr auto block_ctx(World& world, ChunkDomain domain,
                                       BlockScratch& scratch) noexcept
    -> BlockCtx<World, Policy> {
  return BlockCtx<World, Policy>{world, domain, &scratch};
}

template <WritePolicy Policy, typename World>
[[nodiscard]] constexpr auto block_ctx(World& world, ChunkDomain domain,
                                       BlockDiagnostics& diagnostics) noexcept
    -> BlockCtx<World, Policy> {
  return BlockCtx<World, Policy>{world, domain, nullptr, &diagnostics};
}

template <WritePolicy Policy, typename World>
[[nodiscard]] constexpr auto block_ctx(World& world, ChunkDomain domain,
                                       BlockScratch& scratch,
                                       BlockDiagnostics& diagnostics) noexcept
    -> BlockCtx<World, Policy> {
  return BlockCtx<World, Policy>{world, domain, &scratch, &diagnostics};
}

template <WritePolicy Policy, typename World, typename Fn>
constexpr void for_each_chunk(World& world, ChunkDomain domain, Fn&& fn) {
  block_ctx<Policy>(world, domain).for_each_chunk(std::forward<Fn>(fn));
}

namespace detail {

template <WritePolicy Policy, typename World, typename Fn>
constexpr void for_each_chunk_policy_view(World& world, ChunkDomain domain,
                                          Fn&& fn) {
  using world_type = std::remove_reference_t<World>;
  using view_world_type =
      std::conditional_t<Policy == WritePolicy::ReadOnly,
                         const std::remove_const_t<world_type>, world_type>;

  if constexpr (std::is_invocable_v<Fn&, ChunkView<view_world_type>>) {
    for (const auto key : domain) {
      std::invoke(fn, ChunkView<view_world_type>{world, key});
    }
  } else {
    assert(false && "callback cannot accept the selected block policy view");
    std::abort();
  }
}

}  // namespace detail

template <typename World, typename Fn>
constexpr void for_each_chunk(World& world, ChunkDomain domain,
                              WritePolicy policy, Fn&& fn) {
  assert(is_valid_write_policy(policy));
  switch (policy) {
    case WritePolicy::ReadOnly:
      detail::for_each_chunk_policy_view<WritePolicy::ReadOnly>(
          world, domain, std::forward<Fn>(fn));
      return;
    case WritePolicy::UniquePerTile:
      detail::for_each_chunk_policy_view<WritePolicy::UniquePerTile>(
          world, domain, std::forward<Fn>(fn));
      return;
    case WritePolicy::UniquePerChunk:
      detail::for_each_chunk_policy_view<WritePolicy::UniquePerChunk>(
          world, domain, std::forward<Fn>(fn));
      return;
    case WritePolicy::Unsafe:
      detail::for_each_chunk_policy_view<WritePolicy::Unsafe>(
          world, domain, std::forward<Fn>(fn));
      return;
  }
  std::abort();
}

}  // namespace tess
