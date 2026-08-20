// Deterministic procedural map generators for the S1 scenario layer
// (redesign section 3.1): recursive-division maze and room-and-corridor
// layouts emitted as strict Moving AI map text for
// grid_benchmark_harness.h's parser, plus deterministic endpoint
// sampling. Harness support only — never a public header (grid TDD
// section 1). All decisions derive from the seed through an explicit
// SplitMix64 stream; nothing reads the clock, the environment, or
// unordered iteration (TDD section 13).
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "grid_benchmark_harness.h"

namespace tess_test::grid_benchmark {

// SplitMix64 (public-domain construction): unsigned 64-bit arithmetic
// only, so every platform produces the identical stream.
class SplitMix64 {
 public:
  explicit SplitMix64(std::uint64_t seed) : state_(seed) {}

  auto next() -> std::uint64_t {
    state_ += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = state_;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }

  // Bounded selection by multiply-shift over the high 32 bits
  // (Lemire, 32-bit form): every product fits in 64 bits, so this
  // needs no 128-bit type — MSVC has none — and yields the identical
  // stream on every supported compiler. Bounds here are map extents,
  // far below 2^32. Multiply-shift alone carries a range bias below
  // 2^-32, which is irrelevant for layout choices that only have to
  // be varied and reproducible, not statistically uniform.
  auto below(std::uint64_t bound) -> std::uint64_t {
    const std::uint64_t draw = next() >> 32;
    return (draw * bound) >> 32;
  }

 private:
  std::uint64_t state_;
};

// Both generators accept 8..64 in each dimension (the oracle tests
// load into a fixed 64x64 world superset); anything else is a
// contract violation and yields nullopt.
inline constexpr std::size_t kMinGeneratedExtent = 8;
inline constexpr std::size_t kMaxGeneratedExtent = 64;

namespace detail {

class GridBuilder {
 public:
  GridBuilder(std::size_t width, std::size_t height, bool passable)
      : width_(width),
        height_(height),
        cells_(width * height, passable ? '.' : '@') {}

  auto at(std::size_t x, std::size_t y) -> char& {
    return cells_[y * width_ + x];
  }

  [[nodiscard]] auto text() const -> std::string {
    std::string out;
    out += "type octile\n";
    out += "height " + std::to_string(height_) + "\n";
    out += "width " + std::to_string(width_) + "\n";
    out += "map\n";
    for (std::size_t y = 0; y < height_; ++y) {
      out.append(cells_.data() + y * width_, width_);
      out += '\n';
    }
    return out;
  }

 private:
  std::size_t width_;
  std::size_t height_;
  std::string cells_;
};

inline auto valid_extents(std::size_t width, std::size_t height) -> bool {
  return width >= kMinGeneratedExtent && width <= kMaxGeneratedExtent &&
         height >= kMinGeneratedExtent && height <= kMaxGeneratedExtent;
}

// Recursive division over an inclusive region [x0,x1]x[y0,y1] with
// even (tile-aligned) bounds: walls land on odd coordinates, gaps on
// even tile coordinates. Every wall fully separates its chamber and
// carries exactly one gap, so the passable tiles stay one connected
// component by construction.
inline void divide(GridBuilder& grid, SplitMix64& rng, std::size_t x0,
                   std::size_t y0, std::size_t x1, std::size_t y1) {
  const std::size_t inner_w = x1 - x0 + 1;
  const std::size_t inner_h = y1 - y0 + 1;
  if (inner_w < 3 || inner_h < 3) {
    return;
  }
  const bool horizontal =
      inner_h > inner_w || (inner_h == inner_w && rng.below(2) == 0);
  if (horizontal) {
    // Wall on an odd y strictly inside; gap on an even tile x.
    const std::size_t choices = (inner_h - 1) / 2;
    const std::size_t wall_y = y0 + 1 + 2 * rng.below(choices);
    const std::size_t gaps = (inner_w + 1) / 2;
    const std::size_t gap_x = x0 + 2 * rng.below(gaps);
    for (std::size_t x = x0; x <= x1; ++x) {
      grid.at(x, wall_y) = (x == gap_x) ? '.' : '@';
    }
    divide(grid, rng, x0, y0, x1, wall_y - 1);
    divide(grid, rng, x0, wall_y + 1, x1, y1);
  } else {
    const std::size_t choices = (inner_w - 1) / 2;
    const std::size_t wall_x = x0 + 1 + 2 * rng.below(choices);
    const std::size_t gaps = (inner_h + 1) / 2;
    const std::size_t gap_y = y0 + 2 * rng.below(gaps);
    for (std::size_t y = y0; y <= y1; ++y) {
      grid.at(wall_x, y) = (y == gap_y) ? '.' : '@';
    }
    divide(grid, rng, x0, y0, wall_x - 1, y1);
    divide(grid, rng, wall_x + 1, y0, x1, y1);
  }
}

}  // namespace detail

// Classic recursive-division maze. The carved lattice occupies the odd
// interior ((width-1)|1 x (height-1)|1 shrunk to odd); any leftover
// even fringe row/column stays blocked, so the emitted dimensions are
// exactly the requested ones.
inline auto recursive_division_maze(std::size_t width, std::size_t height,
                                    std::uint64_t seed)
    -> std::optional<std::string> {
  if (!detail::valid_extents(width, height)) {
    return std::nullopt;
  }
  detail::GridBuilder grid(width, height, false);
  const std::size_t lattice_w = (width % 2 == 0) ? width - 1 : width;
  const std::size_t lattice_h = (height % 2 == 0) ? height - 1 : height;
  for (std::size_t y = 0; y < lattice_h; ++y) {
    for (std::size_t x = 0; x < lattice_w; ++x) {
      // Wall posts sit at odd-odd; tiles and corridors start open and
      // the division walls below carve the rest.
      grid.at(x, y) = (x % 2 == 1 && y % 2 == 1) ? '@' : '.';
    }
  }
  SplitMix64 rng(seed);
  detail::divide(grid, rng, 0, 0, lattice_w - 1, lattice_h - 1);
  return grid.text();
}

struct RoomMapResult {
  std::string text;
  std::uint32_t rooms = 0;
};

struct RoomParams {
  std::uint32_t room_attempts = 12;
  std::size_t min_extent = 4;
  std::size_t max_extent = 10;
};

// Non-overlapping rooms connected by L-shaped corridors in placement
// order: room N corridors to room N-1, so accepted rooms form one
// component by construction. The first room places deterministically
// without rejection, guaranteeing rooms >= 1.
inline auto room_and_corridor(std::size_t width, std::size_t height,
                              std::uint64_t seed, RoomParams params = {})
    -> std::optional<RoomMapResult> {
  // The margin comparison subtracts rather than adds: max_extent is
  // caller-supplied, so `max_extent + 2` could wrap and let an
  // oversized room through into an out-of-bounds write. valid_extents
  // runs first, so the smaller dimension is at least 8 here.
  if (!detail::valid_extents(width, height) || params.min_extent < 2 ||
      params.max_extent < params.min_extent ||
      params.max_extent > std::min(width, height) - 2 ||
      params.room_attempts == 0) {
    return std::nullopt;
  }
  detail::GridBuilder grid(width, height, false);
  SplitMix64 rng(seed);
  struct Room {
    std::size_t x = 0;
    std::size_t y = 0;
    std::size_t w = 0;
    std::size_t h = 0;
    [[nodiscard]] auto cx() const -> std::size_t { return x + w / 2; }
    [[nodiscard]] auto cy() const -> std::size_t { return y + h / 2; }
  };
  std::vector<Room> rooms;
  const auto span = params.max_extent - params.min_extent + 1;
  for (std::uint32_t attempt = 0; attempt < params.room_attempts; ++attempt) {
    const std::size_t w = params.min_extent + rng.below(span);
    const std::size_t h = params.min_extent + rng.below(span);
    std::size_t x = 0;
    std::size_t y = 0;
    if (rooms.empty()) {
      // Deterministic first placement: centered, no rejection.
      x = (width - w) / 2;
      y = (height - h) / 2;
    } else {
      x = 1 + rng.below(width - w - 1);
      y = 1 + rng.below(height - h - 1);
      bool overlaps = false;
      for (const Room& other : rooms) {
        // One-tile separation so rooms stay distinct areas.
        if (x < other.x + other.w + 1 && other.x < x + w + 1 &&
            y < other.y + other.h + 1 && other.y < y + h + 1) {
          overlaps = true;
          break;
        }
      }
      if (overlaps) {
        continue;
      }
    }
    for (std::size_t yy = y; yy < y + h; ++yy) {
      for (std::size_t xx = x; xx < x + w; ++xx) {
        grid.at(xx, yy) = '.';
      }
    }
    if (!rooms.empty()) {
      // L-shaped corridor to the previous room's center.
      const Room& prev = rooms.back();
      const Room current{x, y, w, h};
      std::size_t cx = current.cx();
      const std::size_t cy = current.cy();
      const std::size_t px = prev.cx();
      const std::size_t py = prev.cy();
      // Horizontal run toward the previous center, then a vertical
      // run at that column: both endpoints are room centers inside
      // the map, so every write stays in bounds and the run
      // terminates after |cx - px| steps.
      const std::size_t step = (px > cx) ? 1 : std::size_t(-1);
      for (; cx != px; cx += step) {
        grid.at(cx, cy) = '.';
      }
      for (std::size_t yy = std::min(cy, py); yy <= std::max(cy, py); ++yy) {
        grid.at(px, yy) = '.';
      }
    }
    rooms.push_back({x, y, w, h});
  }
  RoomMapResult result;
  result.text = grid.text();
  result.rooms = static_cast<std::uint32_t>(rooms.size());
  return result;
}

struct FloodResult {
  std::size_t passable = 0;
  std::size_t reached = 0;
  // Row-major index of a farthest reached tile (BFS distance).
  std::size_t farthest_index = 0;
  std::size_t first_passable_index = 0;
};

// Single orthogonal BFS flood from the first passable tile (row-major
// order). Full connectivity <=> reached == passable.
inline auto flood_fill(const BenchmarkMap& map) -> FloodResult {
  FloodResult result;
  const std::size_t total = map.width * map.height;
  std::size_t first = total;
  for (std::size_t i = 0; i < total; ++i) {
    if (map.passability[i] != 0) {
      ++result.passable;
      if (first == total) {
        first = i;
      }
    }
  }
  if (result.passable == 0) {
    return result;
  }
  result.first_passable_index = first;
  std::vector<std::uint8_t> seen(total, 0);
  std::vector<std::size_t> frontier{first};
  seen[first] = 1;
  result.reached = 1;
  result.farthest_index = first;
  while (!frontier.empty()) {
    std::vector<std::size_t> next;
    for (const std::size_t index : frontier) {
      const std::size_t x = index % map.width;
      const std::size_t y = index / map.width;
      const std::size_t neighbors[4][2] = {
          {x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}};
      for (const auto& [nx, ny] : neighbors) {
        if (nx >= map.width || ny >= map.height) {
          continue;  // size_t wrap covers x==0 / y==0
        }
        const std::size_t ni = ny * map.width + nx;
        if (map.passability[ni] == 0 || seen[ni] != 0) {
          continue;
        }
        seen[ni] = 1;
        ++result.reached;
        next.push_back(ni);
      }
    }
    if (!next.empty()) {
      result.farthest_index = next.back();
    }
    frontier = std::move(next);
  }
  return result;
}

// Deterministic endpoint pairs for the oracle leg: seeded selection
// over the passable tiles in row-major order, clamped to what the map
// offers, always including the flood fill's deliberate long pair
// (first passable -> farthest tile). Distinct endpoints whenever more
// than one passable tile exists.
inline auto deterministic_endpoints(const BenchmarkMap& map, std::uint64_t seed,
                                    std::size_t count)
    -> std::vector<std::pair<tess::Coord3, tess::Coord3>> {
  std::vector<std::size_t> passable;
  const std::size_t total = map.width * map.height;
  for (std::size_t i = 0; i < total; ++i) {
    if (map.passability[i] != 0) {
      passable.push_back(i);
    }
  }
  std::vector<std::pair<tess::Coord3, tess::Coord3>> pairs;
  if (passable.empty() || count == 0) {
    return pairs;
  }
  const auto coord = [&](std::size_t index) {
    return tess::Coord3{static_cast<std::int32_t>(index % map.width),
                        static_cast<std::int32_t>(index / map.width), 0};
  };
  const FloodResult flood = flood_fill(map);
  pairs.emplace_back(coord(flood.first_passable_index),
                     coord(flood.farthest_index));
  SplitMix64 rng(seed);
  while (pairs.size() < count) {
    std::size_t a = passable[rng.below(passable.size())];
    std::size_t b = passable[rng.below(passable.size())];
    // Distinct endpoints where the map allows it. The redraw is
    // bounded so the loop provably terminates whatever the stream
    // does; a single-tile map keeps its degenerate pair.
    for (int retry = 0; a == b && passable.size() > 1 && retry < 8; ++retry) {
      b = passable[rng.below(passable.size())];
    }
    pairs.emplace_back(coord(a), coord(b));
  }
  return pairs;
}

}  // namespace tess_test::grid_benchmark
