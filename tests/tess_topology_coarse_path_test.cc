#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <array>
#include <cstdint>

#include "allocation_counter.h"

namespace {

struct PassableTag {};

using Schema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>>;

template <typename Shape>
using World = tess::AlwaysResidentWorld<Shape, Schema>;

template <typename WorldType>
void fill_passable(WorldType& world) {
  for (auto& chunk : world.chunks()) {
    for (auto& value : chunk.template field_span<PassableTag>()) {
      value = 1;
    }
  }
}

template <typename Shape>
auto build_graph(World<Shape>& world) -> tess::RegionGraph {
  tess::LocalTopologyScratch scratch;
  tess::RegionGraph graph;
  const auto result = tess::build_region_graph<World<Shape>, PassableTag>(
      world, scratch, graph);
  EXPECT_EQ(result.status, tess::TopologyStatus::Built);
  return graph;
}

TEST(TessTopologyCoarsePath, ReconstructsShortestRegionAndPortalPath) {
  using Shape = tess::Shape<tess::Extent3{12, 4, 1}, tess::Extent3{4, 4, 1}>;
  World<Shape> world;
  fill_passable(world);
  const auto graph = build_graph<Shape>(world);
  tess::RegionGraphScratch scratch;

  const auto result = tess::coarse_path<Shape>(graph, tess::Coord3{0, 0, 0},
                                               tess::Coord3{11, 3, 0}, scratch);

  ASSERT_EQ(result.status, tess::ReachabilityStatus::Reachable);
  ASSERT_EQ(result.regions.size(), 3u);
  ASSERT_EQ(result.portals.size(), 2u);
  EXPECT_EQ(result.regions.front(), graph.region_of<Shape>({0, 0, 0}));
  EXPECT_EQ(result.regions.back(), graph.region_of<Shape>({11, 3, 0}));
  EXPECT_EQ(result.portals[0].from, result.regions[0]);
  EXPECT_EQ(result.portals[0].to, result.regions[1]);
  EXPECT_EQ(result.portals[1].from, result.regions[1]);
  EXPECT_EQ(result.portals[1].to, result.regions[2]);
  const std::array expected_chunks{tess::ChunkKey{0}, tess::ChunkKey{1},
                                   tess::ChunkKey{2}};
  EXPECT_TRUE(std::ranges::equal(result.chunks, expected_chunks));
  EXPECT_EQ(result.bounds, (tess::Box3{{0, 0, 0}, tess::Extent3{12, 4, 1}}));
}

TEST(TessTopologyCoarsePath, SameRegionProducesOneChunkAndNoPortal) {
  using Shape = tess::Shape<tess::Extent3{4, 4, 1}, tess::Extent3{4, 4, 1}>;
  World<Shape> world;
  fill_passable(world);
  const auto graph = build_graph<Shape>(world);
  tess::RegionGraphScratch scratch;

  const auto result = tess::coarse_path<Shape>(graph, tess::Coord3{0, 0, 0},
                                               tess::Coord3{3, 3, 0}, scratch);

  EXPECT_EQ(result.status, tess::ReachabilityStatus::Reachable);
  EXPECT_EQ(result.visited_regions, 1u);
  EXPECT_EQ(result.regions.size(), 1u);
  EXPECT_TRUE(result.portals.empty());
  EXPECT_EQ(result.chunks.size(), 1u);
  EXPECT_EQ(result.bounds, (tess::Box3{{0, 0, 0}, tess::Extent3{4, 4, 1}}));
}

TEST(TessTopologyCoarsePath, FindsNonMonotoneChunkDetour) {
  using Shape = tess::Shape<tess::Extent3{12, 8, 1}, tess::Extent3{4, 4, 1}>;
  World<Shape> world;
  fill_passable(world);
  for (std::int64_t y = 0; y < 4; ++y) {
    for (std::int64_t x = 4; x < 8; ++x) {
      world.template field<PassableTag>({x, y, 0}) = 0;
    }
  }
  const auto graph = build_graph<Shape>(world);
  tess::RegionGraphScratch scratch;

  const auto result = tess::coarse_path<Shape>(graph, tess::Coord3{0, 0, 0},
                                               tess::Coord3{11, 0, 0}, scratch);

  ASSERT_EQ(result.status, tess::ReachabilityStatus::Reachable);
  const std::array expected{tess::ChunkKey{0}, tess::ChunkKey{3},
                            tess::ChunkKey{4}, tess::ChunkKey{5},
                            tess::ChunkKey{2}};
  EXPECT_TRUE(std::ranges::equal(result.chunks, expected));
}

TEST(TessTopologyCoarsePath, DisconnectedResultOwnsNoStalePath) {
  using Shape = tess::Shape<tess::Extent3{8, 4, 1}, tess::Extent3{4, 4, 1}>;
  World<Shape> world;
  fill_passable(world);
  for (std::int64_t y = 0; y < 4; ++y) {
    world.template field<PassableTag>({4, y, 0}) = 0;
  }
  const auto graph = build_graph<Shape>(world);
  tess::RegionGraphScratch scratch;

  const auto found = tess::coarse_path<Shape>(graph, tess::Coord3{0, 0, 0},
                                              tess::Coord3{3, 3, 0}, scratch);
  ASSERT_EQ(found.status, tess::ReachabilityStatus::Reachable);
  const auto missing = tess::coarse_path<Shape>(graph, tess::Coord3{0, 0, 0},
                                                tess::Coord3{7, 3, 0}, scratch);

  EXPECT_EQ(missing.status, tess::ReachabilityStatus::Unreachable);
  EXPECT_TRUE(missing.regions.empty());
  EXPECT_TRUE(missing.portals.empty());
  EXPECT_TRUE(missing.chunks.empty());
  EXPECT_EQ(missing.bounds, tess::Box3{});
}

TEST(TessTopologyCoarsePath, WarmReconstructionDoesNotAllocate) {
  using Shape = tess::Shape<tess::Extent3{12, 4, 1}, tess::Extent3{4, 4, 1}>;
  World<Shape> world;
  fill_passable(world);
  const auto graph = build_graph<Shape>(world);
  tess::RegionGraphScratch scratch;
  scratch.reserve_regions(graph.region_count());
  (void)tess::coarse_path<Shape>(graph, {0, 0, 0}, {11, 3, 0}, scratch);

  {
    tess_test::ScopedAllocationCounter counter;
    for (int i = 0; i < 100; ++i) {
      const auto result =
          tess::coarse_path<Shape>(graph, {0, 0, 0}, {11, 3, 0}, scratch);
      EXPECT_EQ(result.status, tess::ReachabilityStatus::Reachable);
    }
    EXPECT_EQ(counter.count(), 0u);
  }
}

TEST(TessTopologyCoarsePath, SparseResidentCorridorIsReconstructed) {
  using Shape = tess::Shape<tess::Extent3{12, 4, 1}, tess::Extent3{4, 4, 1}>;
  using Sparse = tess::SparseResidentWorld<Shape, Schema>;
  Sparse world{tess::ResidencyConfig{3 * Sparse::page_byte_size}};
  for (const auto key :
       {tess::ChunkKey{0}, tess::ChunkKey{1}, tess::ChunkKey{2}}) {
    world.ensure_resident(key);
    auto values = world.chunk(key).template field_span<PassableTag>();
    std::fill(values.begin(), values.end(), 1);
  }
  tess::LocalTopologyScratch local;
  tess::SparseRegionGraph graph;
  ASSERT_EQ((tess::build_region_graph<Sparse, PassableTag>(world, local, graph))
                .status,
            tess::TopologyStatus::Built);
  tess::RegionGraphScratch scratch;

  const auto result =
      tess::coarse_path<Shape>(graph, {0, 0, 0}, {11, 3, 0}, scratch);

  EXPECT_EQ(result.status, tess::ReachabilityStatus::Reachable);
  ASSERT_EQ(result.chunks.size(), 3u);
  EXPECT_EQ(result.chunks.front(), tess::ChunkKey{0});
  EXPECT_EQ(result.chunks.back(), tess::ChunkKey{2});
}

TEST(TessTopologyCoarsePath, SparseMissingCorridorIsIndeterminate) {
  using Shape = tess::Shape<tess::Extent3{12, 4, 1}, tess::Extent3{4, 4, 1}>;
  using Sparse = tess::SparseResidentWorld<Shape, Schema>;
  Sparse world{tess::ResidencyConfig{2 * Sparse::page_byte_size}};
  for (const auto key : {tess::ChunkKey{0}, tess::ChunkKey{2}}) {
    world.ensure_resident(key);
    auto values = world.chunk(key).template field_span<PassableTag>();
    std::fill(values.begin(), values.end(), 1);
  }
  tess::LocalTopologyScratch local;
  tess::SparseRegionGraph graph;
  ASSERT_EQ((tess::build_region_graph<Sparse, PassableTag>(world, local, graph))
                .status,
            tess::TopologyStatus::Built);
  tess::RegionGraphScratch scratch;

  const auto result =
      tess::coarse_path<Shape>(graph, {0, 0, 0}, {11, 3, 0}, scratch);

  EXPECT_EQ(result.status, tess::ReachabilityStatus::Indeterminate);
  EXPECT_TRUE(result.regions.empty());
  EXPECT_TRUE(result.portals.empty());
  EXPECT_TRUE(result.chunks.empty());
}

}  // namespace
