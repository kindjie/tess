#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <cstdint>

namespace {

struct PassableTag {};

using Schema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>>;

template <typename Shape>
using World = tess::AlwaysResidentWorld<Shape, Schema>;

template <typename WorldType>
void fill_passable(WorldType& world, std::uint8_t value) {
  for (auto& page : world.chunks()) {
    auto passable = page.template field_span<PassableTag>();
    for (auto& tile : passable) {
      tile = value;
    }
  }
}

template <typename Shape>
auto local_id(tess::LocalCoord3 coord) -> tess::LocalTileId {
  return tess::local_tile_id<Shape>(coord);
}

}  // namespace

TEST(TessTopology, BuildsTopDown2DLocalRegions) {
  using Shape = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{8, 8, 1}>;
  World<Shape> world;
  fill_passable(world, 1);
  for (std::int64_t y = 0; y < 8; ++y) {
    world.field<PassableTag>(tess::Coord3{3, y, 0}) = 0;
  }

  tess::LocalTopologyScratch scratch;
  tess::LocalChunkTopology topology;
  const auto result =
      tess::build_local_chunk_topology<decltype(world), PassableTag>(
          world, tess::ChunkKey{0}, scratch, topology);

  EXPECT_EQ(result.status, tess::TopologyStatus::Built);
  EXPECT_EQ(result.region_count, 2u);
  EXPECT_EQ(result.passable_tile_count, 56u);
  EXPECT_EQ(result.boundary_exit_count, 0u);
  EXPECT_EQ(topology.regions().size(), 2u);
  EXPECT_NE(topology.region_at(local_id<Shape>(tess::LocalCoord3{0, 0, 0})),
            topology.region_at(local_id<Shape>(tess::LocalCoord3{7, 0, 0})));
  EXPECT_EQ(topology.region_at(local_id<Shape>(tess::LocalCoord3{3, 0, 0})),
            tess::invalid_local_region);
}

TEST(TessTopology, ReportsChunkBoundaryExits) {
  using Shape = tess::Shape<tess::Extent3{16, 8, 1}, tess::Extent3{8, 8, 1}>;
  World<Shape> world;
  fill_passable(world, 1);

  tess::LocalTopologyScratch scratch;
  tess::LocalChunkTopology topology;
  const auto result =
      tess::build_local_chunk_topology<decltype(world), PassableTag>(
          world, tess::ChunkKey{0}, scratch, topology);

  EXPECT_EQ(result.status, tess::TopologyStatus::Built);
  EXPECT_EQ(result.region_count, 1u);
  EXPECT_EQ(result.boundary_exit_count, 8u);
  EXPECT_EQ(topology.regions().front().boundary_exit_count, 8u);
  EXPECT_TRUE(std::all_of(topology.boundary_exits().begin(),
                          topology.boundary_exits().end(), [](auto exit) {
                            return exit.face == tess::BoundaryFace::PositiveX &&
                                   exit.target_chunk == tess::ChunkKey{1};
                          }));
}

TEST(TessTopology, BuildsVertical2DRegionsWithoutDegenerateAxisExits) {
  using Shape = tess::Shape<tess::Extent3{1, 8, 8}, tess::Extent3{1, 8, 8}>;
  World<Shape> world;
  fill_passable(world, 1);
  for (std::int64_t y = 0; y < 8; ++y) {
    world.field<PassableTag>(tess::Coord3{0, y, 3}) = 0;
  }

  tess::LocalTopologyScratch scratch;
  tess::LocalChunkTopology topology;
  const auto result =
      tess::build_local_chunk_topology<decltype(world), PassableTag>(
          world, tess::ChunkKey{0}, scratch, topology);

  EXPECT_EQ(result.status, tess::TopologyStatus::Built);
  EXPECT_EQ(result.region_count, 2u);
  EXPECT_EQ(result.boundary_exit_count, 0u);
  EXPECT_NE(topology.region_at(local_id<Shape>(tess::LocalCoord3{0, 0, 0})),
            topology.region_at(local_id<Shape>(tess::LocalCoord3{0, 0, 7})));
}

TEST(TessTopology, Builds3DRegionThroughGap) {
  using Shape = tess::Shape<tess::Extent3{4, 4, 4}, tess::Extent3{4, 4, 4}>;
  World<Shape> world;
  fill_passable(world, 1);
  for (std::int64_t x = 0; x < 4; ++x) {
    for (std::int64_t y = 0; y < 4; ++y) {
      world.field<PassableTag>(tess::Coord3{x, y, 1}) = 0;
    }
  }
  world.field<PassableTag>(tess::Coord3{2, 2, 1}) = 1;

  tess::LocalTopologyScratch scratch;
  tess::LocalChunkTopology topology;
  const auto result =
      tess::build_local_chunk_topology<decltype(world), PassableTag>(
          world, tess::ChunkKey{0}, scratch, topology);

  EXPECT_EQ(result.status, tess::TopologyStatus::Built);
  EXPECT_EQ(result.region_count, 1u);
  EXPECT_EQ(result.passable_tile_count, 49u);
  EXPECT_EQ(topology.region_at(local_id<Shape>(tess::LocalCoord3{0, 0, 0})),
            topology.region_at(local_id<Shape>(tess::LocalCoord3{3, 3, 3})));
}

TEST(TessTopology, RejectsInvalidChunk) {
  using Shape = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{8, 8, 1}>;
  World<Shape> world;
  fill_passable(world, 1);

  tess::LocalTopologyScratch scratch;
  tess::LocalChunkTopology topology;
  const auto result =
      tess::build_local_chunk_topology<decltype(world), PassableTag>(
          world, tess::ChunkKey{1}, scratch, topology);

  EXPECT_EQ(result.status, tess::TopologyStatus::InvalidChunk);
  EXPECT_TRUE(topology.regions().empty());
}

TEST(TessTopology, RegionGraphPairsBoundaryExitsAndFindsReachability) {
  using Shape = tess::Shape<tess::Extent3{16, 8, 1}, tess::Extent3{8, 8, 1}>;
  World<Shape> world;
  fill_passable(world, 1);

  tess::LocalTopologyScratch local_scratch;
  tess::RegionGraph graph;
  const auto result = tess::build_region_graph<decltype(world), PassableTag>(
      world, local_scratch, graph);

  EXPECT_EQ(result.status, tess::TopologyStatus::Built);
  EXPECT_EQ(result.region_count, 2u);
  EXPECT_EQ(graph.local_topologies().size(), 2u);
  EXPECT_EQ(graph.portals().size(), 16u);

  tess::RegionGraphScratch graph_scratch;
  const auto reachable = tess::reachable<Shape>(
      graph, tess::Coord3{0, 0, 0}, tess::Coord3{15, 7, 0}, graph_scratch);

  EXPECT_EQ(reachable.status, tess::ReachabilityStatus::Reachable);
  EXPECT_EQ(reachable.visited_regions, 2u);
}

TEST(TessTopology, RegionGraphRejectsBlockedSeamReachability) {
  using Shape = tess::Shape<tess::Extent3{16, 8, 1}, tess::Extent3{8, 8, 1}>;
  World<Shape> world;
  fill_passable(world, 1);
  for (std::int64_t y = 0; y < 8; ++y) {
    world.field<PassableTag>(tess::Coord3{8, y, 0}) = 0;
  }

  tess::LocalTopologyScratch local_scratch;
  tess::RegionGraph graph;
  const auto result = tess::build_region_graph<decltype(world), PassableTag>(
      world, local_scratch, graph);

  EXPECT_EQ(result.status, tess::TopologyStatus::Built);
  EXPECT_EQ(result.region_count, 2u);
  EXPECT_EQ(graph.portals().size(), 0u);

  tess::RegionGraphScratch graph_scratch;
  const auto reachable = tess::reachable<Shape>(
      graph, tess::Coord3{0, 0, 0}, tess::Coord3{15, 7, 0}, graph_scratch);

  EXPECT_EQ(reachable.status, tess::ReachabilityStatus::Unreachable);
  EXPECT_EQ(reachable.visited_regions, 1u);
}

TEST(TessTopology, RegionGraphSupportsVertical2DChunkReachability) {
  using Shape = tess::Shape<tess::Extent3{1, 8, 16}, tess::Extent3{1, 8, 8}>;
  World<Shape> world;
  fill_passable(world, 1);

  tess::LocalTopologyScratch local_scratch;
  tess::RegionGraph graph;
  const auto result = tess::build_region_graph<decltype(world), PassableTag>(
      world, local_scratch, graph);

  EXPECT_EQ(result.status, tess::TopologyStatus::Built);
  EXPECT_EQ(result.region_count, 2u);
  EXPECT_EQ(graph.portals().size(), 16u);

  tess::RegionGraphScratch graph_scratch;
  const auto reachable = tess::reachable<Shape>(
      graph, tess::Coord3{0, 0, 0}, tess::Coord3{0, 7, 15}, graph_scratch);

  EXPECT_EQ(reachable.status, tess::ReachabilityStatus::Reachable);
  EXPECT_EQ(reachable.visited_regions, 2u);
}

TEST(TessTopology, ReachabilityReportsInvalidEndpoints) {
  using Shape = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{8, 8, 1}>;
  World<Shape> world;
  fill_passable(world, 1);
  world.field<PassableTag>(tess::Coord3{7, 7, 0}) = 0;

  tess::LocalTopologyScratch local_scratch;
  tess::RegionGraph graph;
  ASSERT_EQ((tess::build_region_graph<decltype(world), PassableTag>(
                 world, local_scratch, graph))
                .status,
            tess::TopologyStatus::Built);

  tess::RegionGraphScratch graph_scratch;
  EXPECT_EQ(tess::reachable<Shape>(graph, tess::Coord3{-1, 0, 0},
                                   tess::Coord3{0, 0, 0}, graph_scratch)
                .status,
            tess::ReachabilityStatus::InvalidStart);
  EXPECT_EQ(tess::reachable<Shape>(graph, tess::Coord3{0, 0, 0},
                                   tess::Coord3{9, 0, 0}, graph_scratch)
                .status,
            tess::ReachabilityStatus::InvalidGoal);
  EXPECT_EQ(tess::reachable<Shape>(graph, tess::Coord3{7, 7, 0},
                                   tess::Coord3{0, 0, 0}, graph_scratch)
                .status,
            tess::ReachabilityStatus::InvalidStart);
  EXPECT_EQ(tess::reachable<Shape>(graph, tess::Coord3{0, 0, 0},
                                   tess::Coord3{7, 7, 0}, graph_scratch)
                .status,
            tess::ReachabilityStatus::InvalidGoal);
}
