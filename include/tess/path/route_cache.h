#pragma once

#include <tess/path/path.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace tess {

struct RouteCacheStats {
  std::size_t entries = 0;
  std::size_t hits = 0;
  std::size_t suffix_hits = 0;
  std::size_t misses = 0;
  std::size_t path_nodes = 0;
};

class RouteCacheScratch {
 public:
  void reserve_routes(std::size_t route_count) {
    entries_.reserve(route_count);
  }

  void reserve_path_nodes(std::size_t node_count) {
    paths_.reserve(node_count);
    temp_path_.reserve(node_count);
  }

  void clear() noexcept {
    invalidate();
    hits_ = 0;
    suffix_hits_ = 0;
    misses_ = 0;
  }

  void invalidate() noexcept {
    entries_.clear();
    paths_.clear();
    temp_path_.clear();
  }

  void reset_stats() noexcept {
    hits_ = 0;
    suffix_hits_ = 0;
    misses_ = 0;
  }

  template <typename World>
  void capture_world_versions(const World& world) noexcept {
    world_fingerprint_ = world_version_fingerprint(world);
    has_world_fingerprint_ = true;
  }

  template <typename World>
  [[nodiscard]] auto invalidate_if_world_changed(const World& world) noexcept
      -> bool {
    if (!has_world_fingerprint_) {
      capture_world_versions(world);
      return false;
    }
    const auto current = world_version_fingerprint(world);
    if (current == world_fingerprint_) {
      return false;
    }
    invalidate();
    world_fingerprint_ = current;
    has_world_fingerprint_ = true;
    return true;
  }

  [[nodiscard]] auto stats() const noexcept -> RouteCacheStats {
    return RouteCacheStats{
        entries_.size(), hits_, suffix_hits_, misses_, paths_.size(),
    };
  }

 private:
  struct Entry {
    Coord3 start{};
    Coord3 goal{};
    PathStatus status = PathStatus::NoPath;
    std::uint32_t cost = 0;
    std::size_t expanded_nodes = 0;
    std::size_t reached_nodes = 0;
    std::size_t path_offset = 0;
    std::size_t path_size = 0;
  };

  template <typename World, typename Tag>
  friend auto cached_astar_path(const World& world, PathRequest request,
                                PathScratch& scratch, RouteCacheScratch& cache)
      -> PathResult;

  [[nodiscard]] auto find(PathRequest request) const noexcept -> const Entry* {
    for (const auto& entry : entries_) {
      if (entry.start == request.start && entry.goal == request.goal) {
        return &entry;
      }
    }
    return nullptr;
  }

  [[nodiscard]] auto find_suffix(PathRequest request,
                                 std::size_t& suffix_offset) const noexcept
      -> const Entry* {
    for (const auto& entry : entries_) {
      if (entry.goal != request.goal || entry.status != PathStatus::Found) {
        continue;
      }
      for (std::size_t i = 0; i < entry.path_size; ++i) {
        if (paths_[entry.path_offset + i] == request.start) {
          suffix_offset = i;
          return &entry;
        }
      }
    }
    return nullptr;
  }

  [[nodiscard]] auto path_span(const Entry& entry,
                               std::size_t offset = 0) const noexcept
      -> std::span<const Coord3> {
    if (entry.path_size <= offset) {
      return {};
    }
    return std::span<const Coord3>{paths_.data() + entry.path_offset + offset,
                                   entry.path_size - offset};
  }

  std::vector<Entry> entries_;
  std::vector<Coord3> paths_;
  std::vector<Coord3> temp_path_;
  std::size_t hits_ = 0;
  std::size_t suffix_hits_ = 0;
  std::size_t misses_ = 0;
  std::uint64_t world_fingerprint_ = 0;
  bool has_world_fingerprint_ = false;

  template <typename World>
  [[nodiscard]] static auto world_version_fingerprint(
      const World& world) noexcept -> std::uint64_t {
    auto fingerprint = std::uint64_t{0xcbf29ce484222325ull};
    for (std::uint64_t i = 0; i < World::chunk_count; ++i) {
      const auto version = world.meta(ChunkKey{i}).version;
      fingerprint ^=
          i + 0x9e3779b97f4a7c15ull + (fingerprint << 6u) + (fingerprint >> 2u);
      fingerprint ^= version;
      fingerprint *= 0x100000001b3ull;
    }
    return fingerprint;
  }
};

// Cache hits copy the cached route into `scratch.path_` and return a span
// into that scratch, never into cache-owned storage. Hit and miss results
// therefore share one lifetime contract: the span is valid until the next
// path call that uses the same `PathScratch`. Cache-internal storage may
// reallocate on any later miss without invalidating previously returned
// spans backed by other scratches.
template <typename World, typename Tag>
auto cached_astar_path(const World& world, PathRequest request,
                       PathScratch& scratch, RouteCacheScratch& cache)
    -> PathResult {
  if (const auto* entry = cache.find(request); entry != nullptr) {
    ++cache.hits_;
    const auto cached = cache.path_span(*entry);
    scratch.path_.assign(cached.begin(), cached.end());
    return PathResult{
        entry->status,
        entry->cost,
        0,
        0,
        std::span<const Coord3>{scratch.path_},
    };
  }
  auto suffix_offset = std::size_t{0};
  if (const auto* entry = cache.find_suffix(request, suffix_offset);
      entry != nullptr) {
    ++cache.suffix_hits_;
    const auto suffix = cache.path_span(*entry, suffix_offset);
    scratch.path_.assign(suffix.begin(), suffix.end());
    return PathResult{
        PathStatus::Found,
        static_cast<std::uint32_t>(scratch.path_.size() - 1u),
        0,
        0,
        std::span<const Coord3>{scratch.path_},
    };
  }

  ++cache.misses_;
  const auto result = astar_path<World, Tag>(world, request, scratch);
  cache.temp_path_.assign(result.path.begin(), result.path.end());
  const auto path_offset = cache.paths_.size();
  cache.paths_.insert(cache.paths_.end(), cache.temp_path_.begin(),
                      cache.temp_path_.end());
  cache.entries_.push_back(RouteCacheScratch::Entry{
      request.start,
      request.goal,
      result.status,
      result.cost,
      result.expanded_nodes,
      result.reached_nodes,
      path_offset,
      cache.temp_path_.size(),
  });
  return result;
}

}  // namespace tess
