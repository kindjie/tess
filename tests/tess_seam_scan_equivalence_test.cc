#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace {

// Differential oracle for detail::best_chunk_portal's page-hoisted seam
// fast path: every case compares found/portal/scan_tiles against a
// reference scan that resolves each tile through detail::is_passable —
// the generic loop's exact semantics. The shape is genuinely 3D and
// asymmetric (4x8x2-tile chunks in a 2x2x2 chunk grid) so a mixed-up
// stride or axis order cannot cancel out.

struct PassableTag {};

using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>>;
using Shape3D = tess::Shape<tess::Extent3{8, 16, 4}, tess::Extent3{4, 8, 2}>;
using Dense = tess::AlwaysResidentWorld<Shape3D, Schema>;
using Sparse = tess::SparseResidentWorld<Shape3D, Schema>;

constexpr auto kChunk = tess::ShapeTraits<Shape3D>::chunk;

struct PortalScan {
  bool found = false;
  tess::Coord3 portal{-99, -99, -99};
  std::size_t scan_tiles = 0;
};

template <typename World>
auto fast_scan(const World& world, tess::ChunkCoord3 from, tess::ChunkCoord3 to,
               tess::Coord3 current, tess::Coord3 goal) -> PortalScan {
  auto out = PortalScan{};
  out.found = tess::detail::best_chunk_portal<World, PassableTag>(
      world, from, to, current, goal, out.portal, &out.scan_tiles);
  return out;
}

template <typename World>
auto reference_scan(const World& world, tess::ChunkCoord3 from,
                    tess::ChunkCoord3 to, tess::Coord3 current,
                    tess::Coord3 goal) -> PortalScan {
  auto out = PortalScan{};
  if (!tess::detail::adjacent_chunk<Shape3D>(from, to)) {
    return out;
  }
  const auto origin = tess::detail::chunk_origin<Shape3D>(from);
  auto best_score = std::numeric_limits<std::uint32_t>::max();
  const auto consider = [&](tess::Coord3 source, tess::Coord3 target) {
    ++out.scan_tiles;
    if (!tess::detail::is_passable<World, PassableTag>(world, source)) {
      return;
    }
    if (!tess::detail::is_passable<World, PassableTag>(world, target)) {
      return;
    }
    const auto score =
        tess::detail::saturating_add(tess::detail::manhattan(current, target),
                                     tess::detail::manhattan(target, goal));
    if (!out.found || score < best_score) {
      out.found = true;
      best_score = score;
      out.portal = target;
    }
  };

  if (from.x != to.x) {
    const auto step = from.x < to.x ? std::int64_t{1} : std::int64_t{-1};
    const auto source_x =
        step > 0 ? origin.x + static_cast<std::int64_t>(kChunk.x) - 1
                 : origin.x;
    for (std::int64_t z = origin.z;
         z < origin.z + static_cast<std::int64_t>(kChunk.z); ++z) {
      for (std::int64_t y = origin.y;
           y < origin.y + static_cast<std::int64_t>(kChunk.y); ++y) {
        consider(tess::Coord3{source_x, y, z},
                 tess::Coord3{source_x + step, y, z});
      }
    }
  } else if (from.y != to.y) {
    const auto step = from.y < to.y ? std::int64_t{1} : std::int64_t{-1};
    const auto source_y =
        step > 0 ? origin.y + static_cast<std::int64_t>(kChunk.y) - 1
                 : origin.y;
    for (std::int64_t z = origin.z;
         z < origin.z + static_cast<std::int64_t>(kChunk.z); ++z) {
      for (std::int64_t x = origin.x;
           x < origin.x + static_cast<std::int64_t>(kChunk.x); ++x) {
        consider(tess::Coord3{x, source_y, z},
                 tess::Coord3{x, source_y + step, z});
      }
    }
  } else {
    const auto step = from.z < to.z ? std::int64_t{1} : std::int64_t{-1};
    const auto source_z =
        step > 0 ? origin.z + static_cast<std::int64_t>(kChunk.z) - 1
                 : origin.z;
    for (std::int64_t y = origin.y;
         y < origin.y + static_cast<std::int64_t>(kChunk.y); ++y) {
      for (std::int64_t x = origin.x;
           x < origin.x + static_cast<std::int64_t>(kChunk.x); ++x) {
        consider(tess::Coord3{x, y, source_z},
                 tess::Coord3{x, y, source_z + step});
      }
    }
  }
  return out;
}

template <typename World>
void expect_equivalent(const World& world, tess::ChunkCoord3 from,
                       tess::ChunkCoord3 to, tess::Coord3 current,
                       tess::Coord3 goal) {
  const auto fast = fast_scan(world, from, to, current, goal);
  const auto ref = reference_scan(world, from, to, current, goal);
  const auto context = ::testing::Message()
                       << "from=(" << from.x << "," << from.y << "," << from.z
                       << ") to=(" << to.x << "," << to.y << "," << to.z
                       << ") current=(" << current.x << "," << current.y << ","
                       << current.z << ") goal=(" << goal.x << "," << goal.y
                       << "," << goal.z << ")";
  EXPECT_EQ(fast.found, ref.found) << context;
  EXPECT_EQ(fast.scan_tiles, ref.scan_tiles) << context;
  if (ref.found) {
    EXPECT_EQ(fast.portal.x, ref.portal.x) << context;
    EXPECT_EQ(fast.portal.y, ref.portal.y) << context;
    EXPECT_EQ(fast.portal.z, ref.portal.z) << context;
  }
}

// All 24 ordered adjacent chunk pairs in the 2x2x2 grid: every axis in
// both directions, from every chunk.
template <typename Visit>
void for_each_adjacent_pair(Visit&& visit) {
  constexpr std::uint64_t kGrid = 2;
  for (std::uint64_t cz = 0; cz < kGrid; ++cz) {
    for (std::uint64_t cy = 0; cy < kGrid; ++cy) {
      for (std::uint64_t cx = 0; cx < kGrid; ++cx) {
        const auto from = tess::ChunkCoord3{cx, cy, cz};
        const tess::ChunkCoord3 neighbors[] = {
            {cx + 1, cy, cz}, {cx - 1, cy, cz}, {cx, cy + 1, cz},
            {cx, cy - 1, cz}, {cx, cy, cz + 1}, {cx, cy, cz - 1},
        };
        for (const auto& to : neighbors) {
          // Unsigned decrement from zero wraps far past the grid, so one
          // bound check also rejects it.
          if (to.x >= kGrid || to.y >= kGrid || to.z >= kGrid) {
            continue;
          }
          visit(from, to);
        }
      }
    }
  }
}

template <typename World, typename Pattern>
void fill_pattern(World& world, Pattern&& pattern) {
  for (std::int64_t z = 0; z < 4; ++z) {
    for (std::int64_t y = 0; y < 16; ++y) {
      for (std::int64_t x = 0; x < 8; ++x) {
        const auto coord = tess::Coord3{x, y, z};
        world.template field<PassableTag>(coord) = pattern(coord);
      }
    }
  }
}

// Deterministic per-coordinate hash so "random" maps are reproducible.
[[nodiscard]] auto coord_hash(tess::Coord3 coord, std::uint64_t seed)
    -> std::uint64_t {
  auto h = seed * 0x9E3779B97F4A7C15ull;
  h ^= static_cast<std::uint64_t>(coord.x + 17) * 0xBF58476D1CE4E5B9ull;
  h ^= static_cast<std::uint64_t>(coord.y + 41) * 0x94D049BB133111EBull;
  h ^= static_cast<std::uint64_t>(coord.z + 89) * 0xD6E8FEB86659FD93ull;
  h ^= h >> 29;
  return h * 0x2545F4914F6CDD1Dull;
}

const tess::Coord3 kProbes[][2] = {
    {{0, 0, 0}, {7, 15, 3}},
    {{7, 15, 3}, {0, 0, 0}},
    {{3, 8, 1}, {4, 7, 2}},
    {{5, 2, 3}, {1, 14, 0}},
};

template <typename World>
void expect_all_pairs_equivalent(const World& world) {
  for_each_adjacent_pair([&](tess::ChunkCoord3 from, tess::ChunkCoord3 to) {
    for (const auto& probe : kProbes) {
      expect_equivalent(world, from, to, probe[0], probe[1]);
    }
  });
}

TEST(TessSeamScanEquivalence, AllPassTieBreaksMatchOnEveryAxis) {
  Dense world;
  fill_pattern(world, [](tess::Coord3) { return true; });
  expect_all_pairs_equivalent(world);
}

TEST(TessSeamScanEquivalence, FullyBlockedSeamsMatch) {
  Dense world;
  fill_pattern(world, [](tess::Coord3) { return false; });
  expect_all_pairs_equivalent(world);
}

TEST(TessSeamScanEquivalence, SeededRandomMapsMatch) {
  for (std::uint64_t seed = 1; seed <= 5; ++seed) {
    Dense world;
    fill_pattern(world, [seed](tess::Coord3 coord) {
      return (coord_hash(coord, seed) & 1u) != 0u;
    });
    expect_all_pairs_equivalent(world);
  }
}

TEST(TessSeamScanEquivalence, FirstAndLastTileOnlyPortalsMatch) {
  // Passable only at each chunk's first and last local tile, so a seam
  // either has exactly one crossing at an iteration-order extreme or
  // none — pinning both iteration order and the source/target pairing.
  Dense world;
  fill_pattern(world, [](tess::Coord3 coord) {
    const auto lx = coord.x % static_cast<std::int64_t>(kChunk.x);
    const auto ly = coord.y % static_cast<std::int64_t>(kChunk.y);
    const auto lz = coord.z % static_cast<std::int64_t>(kChunk.z);
    const auto first = lx == 0 && ly == 0 && lz == 0;
    const auto last = lx == static_cast<std::int64_t>(kChunk.x) - 1 &&
                      ly == static_cast<std::int64_t>(kChunk.y) - 1 &&
                      lz == static_cast<std::int64_t>(kChunk.z) - 1;
    return first || last;
  });
  expect_all_pairs_equivalent(world);
}

TEST(TessSeamScanEquivalence, SparseMissingChunksMatch) {
  const auto in_missing_chunk = [](tess::Coord3 coord) {
    const auto chunk = tess::chunk_coord<Shape3D>(coord);
    return chunk.x == 1 && chunk.y == 0 && chunk.z == 1;
  };
  for (std::uint64_t seed = 1; seed <= 3; ++seed) {
    Sparse world{tess::ResidencyConfig{8 * Sparse::page_byte_size}};
    // Leave chunk (1,0,1) non-resident: every pair touching it exercises
    // the missing-source and missing-target fallbacks.
    for_each_adjacent_pair([&](tess::ChunkCoord3 from, tess::ChunkCoord3) {
      if (from.x == 1 && from.y == 0 && from.z == 1) {
        return;
      }
      world.ensure_resident(tess::chunk_key<Shape3D>(from));
    });
    for (std::int64_t z = 0; z < 4; ++z) {
      for (std::int64_t y = 0; y < 16; ++y) {
        for (std::int64_t x = 0; x < 8; ++x) {
          const auto coord = tess::Coord3{x, y, z};
          if (in_missing_chunk(coord)) {
            continue;
          }
          world.template field<PassableTag>(coord) =
              (coord_hash(coord, seed) & 1u) != 0u;
        }
      }
    }
    expect_all_pairs_equivalent(world);
  }
}

[[nodiscard]] auto seam_tile_count(tess::ChunkCoord3 from, tess::ChunkCoord3 to)
    -> std::size_t {
  if (from.x != to.x) {
    return static_cast<std::size_t>(kChunk.y) * kChunk.z;
  }
  if (from.y != to.y) {
    return static_cast<std::size_t>(kChunk.x) * kChunk.z;
  }
  return static_cast<std::size_t>(kChunk.x) * kChunk.y;
}

TEST(TessSeamScanEquivalence, OutOfShapeNeighborsMatch) {
  Dense world;
  fill_pattern(world, [](tess::Coord3) { return true; });
  // Adjacent but past the top of the chunk grid on every axis (the only
  // out-of-shape adjacency: chunk coordinates are unsigned, so stepping
  // below zero wraps and fails the adjacency test instead — see
  // NonAdjacentChunksScanNothing). The fast path must defer to the
  // generic loop, which scans the full seam and finds nothing.
  const tess::ChunkCoord3 pairs[][2] = {
      {{1, 0, 0}, {2, 0, 0}},
      {{0, 1, 0}, {0, 2, 0}},
      {{0, 0, 1}, {0, 0, 2}},
  };
  for (const auto& pair : pairs) {
    for (const auto& probe : kProbes) {
      expect_equivalent(world, pair[0], pair[1], probe[0], probe[1]);
      const auto fast = fast_scan(world, pair[0], pair[1], probe[0], probe[1]);
      EXPECT_FALSE(fast.found);
      EXPECT_EQ(fast.scan_tiles, static_cast<std::size_t>(0) +
                                     seam_tile_count(pair[0], pair[1]));
    }
  }
}

TEST(TessSeamScanEquivalence, NonAdjacentChunksScanNothing) {
  Dense world;
  fill_pattern(world, [](tess::Coord3) { return true; });
  const auto fast =
      fast_scan(world, tess::ChunkCoord3{0, 0, 0}, tess::ChunkCoord3{1, 1, 0},
                tess::Coord3{0, 0, 0}, tess::Coord3{7, 15, 3});
  EXPECT_FALSE(fast.found);
  EXPECT_EQ(fast.scan_tiles, 0u);
  // A would-be neighbor below zero wraps to a huge unsigned coordinate
  // and must read as non-adjacent, not as a seam.
  const auto wrapped = fast_scan(world, tess::ChunkCoord3{0, 0, 0},
                                 tess::ChunkCoord3{std::uint64_t{0} - 1, 0, 0},
                                 tess::Coord3{0, 0, 0}, tess::Coord3{7, 15, 3});
  EXPECT_FALSE(wrapped.found);
  EXPECT_EQ(wrapped.scan_tiles, 0u);
}

}  // namespace
