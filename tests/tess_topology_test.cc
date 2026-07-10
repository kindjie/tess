#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <set>
#include <span>
#include <utility>
#include <vector>

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

auto region_key(tess::RegionRef region)
    -> std::pair<std::uint64_t, std::uint32_t> {
  return {region.chunk.value, region.region.value};
}

// Straightforward portal-scan BFS mirroring the documented reachability
// semantics. Used as the oracle for reachable() equivalence tests.
template <typename Shape>
auto reference_reachable(const tess::RegionGraph& graph, tess::Coord3 start,
                         tess::Coord3 goal) -> tess::ReachabilityResult {
  if (!tess::contains<Shape>(start)) {
    return {tess::ReachabilityStatus::InvalidStart, 0};
  }
  if (!tess::contains<Shape>(goal)) {
    return {tess::ReachabilityStatus::InvalidGoal, 0};
  }
  const auto start_region = graph.region_of<Shape>(start);
  if (start_region.region == tess::invalid_local_region) {
    return {tess::ReachabilityStatus::InvalidStart, 0};
  }
  const auto goal_region = graph.region_of<Shape>(goal);
  if (goal_region.region == tess::invalid_local_region) {
    return {tess::ReachabilityStatus::InvalidGoal, 0};
  }
  if (start_region == goal_region) {
    return {tess::ReachabilityStatus::Reachable, 1};
  }

  std::vector<tess::RegionRef> frontier{start_region};
  std::set<std::pair<std::uint64_t, std::uint32_t>> visited;
  visited.insert(region_key(start_region));
  while (!frontier.empty()) {
    const auto current = frontier.back();
    frontier.pop_back();
    for (const auto& portal : graph.portals()) {
      if (portal.from != current || visited.contains(region_key(portal.to))) {
        continue;
      }
      if (portal.to == goal_region) {
        return {tess::ReachabilityStatus::Reachable, visited.size() + 1};
      }
      visited.insert(region_key(portal.to));
      frontier.push_back(portal.to);
    }
  }
  return {tess::ReachabilityStatus::Unreachable, visited.size()};
}

constexpr auto next_random(std::uint64_t& state) -> std::uint64_t {
  state = state * 6364136223846793005ULL + 1442695040888963407ULL;
  return state >> 32U;
}

void expect_local_topologies_equal(const tess::LocalChunkTopology& lhs,
                                   const tess::LocalChunkTopology& rhs) {
  EXPECT_EQ(lhs.chunk(), rhs.chunk());
  EXPECT_EQ(lhs.version(), rhs.version());
  ASSERT_EQ(lhs.region_ids().size(), rhs.region_ids().size());
  EXPECT_TRUE(std::equal(lhs.region_ids().begin(), lhs.region_ids().end(),
                         rhs.region_ids().begin()));
  ASSERT_EQ(lhs.regions().size(), rhs.regions().size());
  EXPECT_TRUE(std::equal(lhs.regions().begin(), lhs.regions().end(),
                         rhs.regions().begin()));
  ASSERT_EQ(lhs.boundary_exits().size(), rhs.boundary_exits().size());
  EXPECT_TRUE(std::equal(lhs.boundary_exits().begin(),
                         lhs.boundary_exits().end(),
                         rhs.boundary_exits().begin()));
}

void expect_graphs_equal(const tess::RegionGraph& lhs,
                         const tess::RegionGraph& rhs) {
  ASSERT_EQ(lhs.local_topologies().size(), rhs.local_topologies().size());
  for (std::size_t i = 0; i < lhs.local_topologies().size(); ++i) {
    expect_local_topologies_equal(lhs.local_topologies()[i],
                                  rhs.local_topologies()[i]);
  }
  ASSERT_EQ(lhs.portals().size(), rhs.portals().size());
  EXPECT_TRUE(std::equal(lhs.portals().begin(), lhs.portals().end(),
                         rhs.portals().begin()));
  EXPECT_EQ(lhs.region_count(), rhs.region_count());
}

template <typename Shape>
void expect_reachability_equal(const tess::RegionGraph& lhs,
                               const tess::RegionGraph& rhs,
                               std::span<const tess::Coord3> probes) {
  tess::RegionGraphScratch lhs_scratch;
  tess::RegionGraphScratch rhs_scratch;
  for (std::size_t i = 0; i < probes.size(); ++i) {
    const auto start = probes[i];
    const auto goal = probes[(i * 5 + 7) % probes.size()];
    const auto expected = tess::reachable<Shape>(rhs, start, goal, rhs_scratch);
    const auto actual = tess::reachable<Shape>(lhs, start, goal, lhs_scratch);
    EXPECT_EQ(actual.status, expected.status) << "probe " << i;
    EXPECT_EQ(actual.visited_regions, expected.visited_regions)
        << "probe " << i;
  }
}

}  // namespace

TEST(TessTopology, PublicValueTypesDefaultInitialize) {
  EXPECT_EQ(tess::LocalRegion{}.id, tess::LocalRegionId{});
  EXPECT_EQ(tess::LocalBoundaryExit{}.region, tess::LocalRegionId{});
  EXPECT_EQ(tess::LocalBoundaryExit{}.local_tile, tess::LocalTileId{});
  EXPECT_EQ(tess::LocalBoundaryExit{}.coord, tess::Coord3{});
  EXPECT_EQ(tess::LocalBoundaryExit{}.face, tess::BoundaryFace::NegativeX);
  EXPECT_EQ(tess::LocalBoundaryExit{}.target_chunk, tess::ChunkKey{});
  EXPECT_EQ(tess::RegionRef{}.chunk, tess::ChunkKey{});
  EXPECT_EQ(tess::RegionRef{}.region, tess::LocalRegionId{});
  EXPECT_EQ(tess::RegionPortal{}.from, tess::RegionRef{});
  EXPECT_EQ(tess::RegionPortal{}.to, tess::RegionRef{});
  EXPECT_EQ(tess::RegionPortal{}.from_coord, tess::Coord3{});
  EXPECT_EQ(tess::RegionPortal{}.to_coord, tess::Coord3{});
  EXPECT_EQ(tess::RegionPortal{}.face, tess::BoundaryFace::NegativeX);
}

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

TEST(TessTopology, CapturesChunkTopologyVersion) {
  using Shape = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{8, 8, 1}>;
  World<Shape> world;
  fill_passable(world, 1);
  world.mark_topology_dirty(
      tess::ChunkKey{0}, 1u,
      tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}});

  tess::LocalTopologyScratch scratch;
  tess::LocalChunkTopology topology;
  const auto result =
      tess::build_local_chunk_topology<decltype(world), PassableTag>(
          world, tess::ChunkKey{0}, scratch, topology);

  EXPECT_EQ(result.status, tess::TopologyStatus::Built);
  EXPECT_EQ(result.version, 1u);
  EXPECT_EQ(topology.version(), 1u);
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

TEST(TessTopology, RegionGraphSameRegionReachabilityReturnsImmediately) {
  using Shape = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{8, 8, 1}>;
  World<Shape> world;
  fill_passable(world, 1);

  tess::LocalTopologyScratch local_scratch;
  tess::RegionGraph graph;
  const auto result = tess::build_region_graph<decltype(world), PassableTag>(
      world, local_scratch, graph);

  EXPECT_EQ(result.status, tess::TopologyStatus::Built);
  EXPECT_EQ(result.region_count, 1u);

  tess::RegionGraphScratch graph_scratch;
  const auto reachable = tess::reachable<Shape>(
      graph, tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}, graph_scratch);

  EXPECT_EQ(reachable.status, tess::ReachabilityStatus::Reachable);
  EXPECT_EQ(reachable.visited_regions, 1u);
}

TEST(TessTopology, RegionGraphFindsMultiHopReachability) {
  using Shape = tess::Shape<tess::Extent3{24, 8, 1}, tess::Extent3{8, 8, 1}>;
  World<Shape> world;
  fill_passable(world, 1);

  tess::LocalTopologyScratch local_scratch;
  tess::RegionGraph graph;
  const auto result = tess::build_region_graph<decltype(world), PassableTag>(
      world, local_scratch, graph);

  EXPECT_EQ(result.status, tess::TopologyStatus::Built);
  EXPECT_EQ(result.region_count, 3u);
  EXPECT_EQ(graph.local_topologies().size(), 3u);
  EXPECT_EQ(graph.portals().size(), 32u);

  tess::RegionGraphScratch graph_scratch;
  const auto reachable = tess::reachable<Shape>(
      graph, tess::Coord3{0, 0, 0}, tess::Coord3{23, 7, 0}, graph_scratch);

  EXPECT_EQ(reachable.status, tess::ReachabilityStatus::Reachable);
  EXPECT_EQ(reachable.visited_regions, 3u);
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

TEST(TessTopology, RegionGraphRejectsDisconnectedSameChunkRegions) {
  using Shape = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{8, 8, 1}>;
  World<Shape> world;
  fill_passable(world, 1);
  for (std::int64_t y = 0; y < 8; ++y) {
    world.field<PassableTag>(tess::Coord3{3, y, 0}) = 0;
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
      graph, tess::Coord3{0, 0, 0}, tess::Coord3{7, 7, 0}, graph_scratch);

  EXPECT_EQ(reachable.status, tess::ReachabilityStatus::Unreachable);
  EXPECT_EQ(reachable.visited_regions, 1u);
}

TEST(TessTopology, RegionGraphRejectsEnclosedRegionReachability) {
  using Shape = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{8, 8, 1}>;
  World<Shape> world;
  fill_passable(world, 1);
  for (std::int64_t offset = -1; offset <= 1; ++offset) {
    world.field<PassableTag>(tess::Coord3{4 + offset, 3, 0}) = 0;
    world.field<PassableTag>(tess::Coord3{4 + offset, 5, 0}) = 0;
    world.field<PassableTag>(tess::Coord3{3, 4 + offset, 0}) = 0;
    world.field<PassableTag>(tess::Coord3{5, 4 + offset, 0}) = 0;
  }
  world.field<PassableTag>(tess::Coord3{4, 4, 0}) = 1;

  tess::LocalTopologyScratch local_scratch;
  tess::RegionGraph graph;
  const auto result = tess::build_region_graph<decltype(world), PassableTag>(
      world, local_scratch, graph);

  EXPECT_EQ(result.status, tess::TopologyStatus::Built);
  EXPECT_EQ(result.region_count, 2u);
  EXPECT_EQ(graph.portals().size(), 0u);

  tess::RegionGraphScratch graph_scratch;
  const auto reachable = tess::reachable<Shape>(
      graph, tess::Coord3{4, 4, 0}, tess::Coord3{0, 0, 0}, graph_scratch);

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

TEST(TessTopology, LocalRegionBoundsMatchKnown2DLayout) {
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

  ASSERT_EQ(result.status, tess::TopologyStatus::Built);
  ASSERT_EQ(topology.regions().size(), 2u);

  const auto& left = topology.regions()[0];
  EXPECT_EQ(left.tile_count, 24u);
  EXPECT_EQ(left.bounds.origin, (tess::Coord3{0, 0, 0}));
  EXPECT_EQ(left.bounds.extent, (tess::Extent3{3, 8, 1}));

  const auto& right = topology.regions()[1];
  EXPECT_EQ(right.tile_count, 32u);
  EXPECT_EQ(right.bounds.origin, (tess::Coord3{4, 0, 0}));
  EXPECT_EQ(right.bounds.extent, (tess::Extent3{4, 8, 1}));
}

TEST(TessTopology, LocalRegionBoundsMatchKnown3DLayout) {
  using Shape = tess::Shape<tess::Extent3{4, 4, 4}, tess::Extent3{4, 4, 4}>;
  World<Shape> world;
  fill_passable(world, 0);
  world.field<PassableTag>(tess::Coord3{0, 0, 0}) = 1;
  world.field<PassableTag>(tess::Coord3{1, 0, 0}) = 1;
  world.field<PassableTag>(tess::Coord3{1, 1, 0}) = 1;
  world.field<PassableTag>(tess::Coord3{1, 1, 1}) = 1;
  world.field<PassableTag>(tess::Coord3{3, 3, 3}) = 1;

  tess::LocalTopologyScratch scratch;
  tess::LocalChunkTopology topology;
  const auto result =
      tess::build_local_chunk_topology<decltype(world), PassableTag>(
          world, tess::ChunkKey{0}, scratch, topology);

  ASSERT_EQ(result.status, tess::TopologyStatus::Built);
  ASSERT_EQ(topology.regions().size(), 2u);

  const auto& ell = topology.regions()[0];
  EXPECT_EQ(ell.tile_count, 4u);
  EXPECT_EQ(ell.bounds.origin, (tess::Coord3{0, 0, 0}));
  EXPECT_EQ(ell.bounds.extent, (tess::Extent3{2, 2, 2}));

  const auto& corner = topology.regions()[1];
  EXPECT_EQ(corner.tile_count, 1u);
  EXPECT_EQ(corner.bounds.origin, (tess::Coord3{3, 3, 3}));
  EXPECT_EQ(corner.bounds.extent, (tess::Extent3{1, 1, 1}));
}

TEST(TessTopology, RegionGraphPairsZFacePortalsAcrossStackedChunks) {
  using Shape = tess::Shape<tess::Extent3{4, 4, 8}, tess::Extent3{4, 4, 4}>;
  World<Shape> world;
  fill_passable(world, 1);

  tess::LocalTopologyScratch local_scratch;
  tess::RegionGraph graph;
  const auto result = tess::build_region_graph<decltype(world), PassableTag>(
      world, local_scratch, graph);

  ASSERT_EQ(result.status, tess::TopologyStatus::Built);
  EXPECT_EQ(result.region_count, 2u);
  ASSERT_EQ(graph.portals().size(), 32u);

  std::size_t positive_z = 0;
  std::size_t negative_z = 0;
  for (const auto& portal : graph.portals()) {
    if (portal.face == tess::BoundaryFace::PositiveZ) {
      ++positive_z;
    }
    if (portal.face == tess::BoundaryFace::NegativeZ) {
      ++negative_z;
    }
    EXPECT_EQ(graph.region_of<Shape>(portal.from_coord), portal.from);
    EXPECT_EQ(graph.region_of<Shape>(portal.to_coord), portal.to);
  }
  EXPECT_EQ(positive_z, 16u);
  EXPECT_EQ(negative_z, 16u);

  tess::RegionGraphScratch graph_scratch;
  const auto reachable = tess::reachable<Shape>(
      graph, tess::Coord3{0, 0, 0}, tess::Coord3{3, 3, 7}, graph_scratch);
  EXPECT_EQ(reachable.status, tess::ReachabilityStatus::Reachable);
  EXPECT_EQ(reachable.visited_regions, 2u);
}

TEST(TessTopology, RegionGraphPairsMultipleRegionsAcrossOneSeam) {
  using Shape = tess::Shape<tess::Extent3{16, 8, 1}, tess::Extent3{8, 8, 1}>;
  World<Shape> world;
  fill_passable(world, 1);
  for (std::int64_t x = 0; x < 16; ++x) {
    world.field<PassableTag>(tess::Coord3{x, 3, 0}) = 0;
  }

  tess::LocalTopologyScratch local_scratch;
  tess::RegionGraph graph;
  const auto result = tess::build_region_graph<decltype(world), PassableTag>(
      world, local_scratch, graph);

  ASSERT_EQ(result.status, tess::TopologyStatus::Built);
  EXPECT_EQ(result.region_count, 4u);
  ASSERT_EQ(graph.portals().size(), 14u);

  for (const auto& portal : graph.portals()) {
    EXPECT_EQ(graph.region_of<Shape>(portal.from_coord), portal.from);
    EXPECT_EQ(graph.region_of<Shape>(portal.to_coord), portal.to);
    // Portals must never cross the blocked row: both endpoints stay inside
    // the same y band.
    EXPECT_EQ(portal.from_coord.y < 3, portal.to_coord.y < 3);
  }

  tess::RegionGraphScratch graph_scratch;
  const auto same_band = tess::reachable<Shape>(
      graph, tess::Coord3{0, 0, 0}, tess::Coord3{15, 0, 0}, graph_scratch);
  EXPECT_EQ(same_band.status, tess::ReachabilityStatus::Reachable);
  EXPECT_EQ(same_band.visited_regions, 2u);

  const auto cross_band = tess::reachable<Shape>(
      graph, tess::Coord3{0, 0, 0}, tess::Coord3{15, 7, 0}, graph_scratch);
  EXPECT_EQ(cross_band.status, tess::ReachabilityStatus::Unreachable);
  EXPECT_EQ(cross_band.visited_regions, 2u);
}

TEST(TessTopology, ReachableMatchesReferenceBfsOnMultiChunkMaze) {
  using Shape = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{4, 4, 1}>;
  World<Shape> world;
  fill_passable(world, 1);

  std::uint64_t rng = 0x9E3779B97F4A7C15ULL;
  for (std::int64_t y = 0; y < 32; ++y) {
    for (std::int64_t x = 0; x < 32; ++x) {
      if (next_random(rng) % 100 < 35) {
        world.field<PassableTag>(tess::Coord3{x, y, 0}) = 0;
      }
    }
  }

  tess::LocalTopologyScratch local_scratch;
  tess::RegionGraph graph;
  ASSERT_EQ((tess::build_region_graph<decltype(world), PassableTag>(
                 world, local_scratch, graph))
                .status,
            tess::TopologyStatus::Built);

  std::vector<tess::Coord3> probes;
  for (std::int64_t y = 1; y < 32 && probes.size() < 24; y += 3) {
    for (std::int64_t x = 1; x < 32 && probes.size() < 24; x += 3) {
      const auto coord = tess::Coord3{x, y, 0};
      if (world.field<PassableTag>(coord) != 0) {
        probes.push_back(coord);
      }
    }
  }
  ASSERT_GE(probes.size(), 8u);

  tess::RegionGraphScratch graph_scratch;
  for (std::size_t i = 0; i < probes.size(); ++i) {
    const auto start = probes[i];
    const auto goal = probes[(i * 5 + 7) % probes.size()];
    const auto expected = reference_reachable<Shape>(graph, start, goal);
    const auto actual =
        tess::reachable<Shape>(graph, start, goal, graph_scratch);
    EXPECT_EQ(actual.status, expected.status)
        << "probe " << i << " start(" << start.x << "," << start.y << ")";
    EXPECT_EQ(actual.visited_regions, expected.visited_regions)
        << "probe " << i << " start(" << start.x << "," << start.y << ")";
  }
}

TEST(TessTopology, LocalChunkTopologyRegionAccessorMapsOneBasedIds) {
  using Shape = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{8, 8, 1}>;
  World<Shape> world;
  fill_passable(world, 1);
  for (std::int64_t y = 0; y < 8; ++y) {
    world.field<PassableTag>(tess::Coord3{3, y, 0}) = 0;
  }

  tess::LocalTopologyScratch scratch;
  tess::LocalChunkTopology topology;
  ASSERT_EQ((tess::build_local_chunk_topology<decltype(world), PassableTag>(
                 world, tess::ChunkKey{0}, scratch, topology))
                .status,
            tess::TopologyStatus::Built);
  ASSERT_EQ(topology.regions().size(), 2u);

  EXPECT_EQ(topology.region(tess::invalid_local_region), nullptr);
  EXPECT_EQ(topology.region(tess::LocalRegionId{3}), nullptr);

  const auto left_id =
      topology.region_at(local_id<Shape>(tess::LocalCoord3{0, 0, 0}));
  const auto* left = topology.region(left_id);
  ASSERT_NE(left, nullptr);
  EXPECT_EQ(left->id, left_id);
  EXPECT_EQ(left->tile_count, 24u);

  const auto right_id =
      topology.region_at(local_id<Shape>(tess::LocalCoord3{7, 0, 0}));
  const auto* right = topology.region(right_id);
  ASSERT_NE(right, nullptr);
  EXPECT_EQ(right->id, right_id);
  EXPECT_EQ(right->tile_count, 32u);
  // The last region must be addressable: 1-based ids map to regions()[id-1].
  EXPECT_EQ(right, &topology.regions().back());

  for (const auto& region : topology.regions()) {
    EXPECT_EQ(topology.region(region.id), &region);
  }
}

TEST(TessTopology, RegionGraphExposesDenseRegionIndex) {
  using Shape = tess::Shape<tess::Extent3{16, 8, 1}, tess::Extent3{8, 8, 1}>;
  World<Shape> world;
  fill_passable(world, 1);
  for (std::int64_t x = 0; x < 16; ++x) {
    world.field<PassableTag>(tess::Coord3{x, 3, 0}) = 0;
  }

  tess::LocalTopologyScratch local_scratch;
  tess::RegionGraph graph;
  ASSERT_EQ((tess::build_region_graph<decltype(world), PassableTag>(
                 world, local_scratch, graph))
                .status,
            tess::TopologyStatus::Built);

  EXPECT_EQ(graph.region_count(), 4u);
  std::set<std::uint32_t> seen;
  for (const auto& topology : graph.local_topologies()) {
    for (const auto& region : topology.regions()) {
      const auto index =
          graph.region_index(tess::RegionRef{topology.chunk(), region.id});
      ASSERT_LT(index, graph.region_count());
      EXPECT_TRUE(seen.insert(index).second);
    }
  }
  EXPECT_EQ(seen.size(), 4u);
  EXPECT_EQ(graph.region_index(
                tess::RegionRef{tess::ChunkKey{0}, tess::invalid_local_region}),
            tess::invalid_region_index);
  EXPECT_EQ(graph.region_index(
                tess::RegionRef{tess::ChunkKey{9}, tess::LocalRegionId{1}}),
            tess::invalid_region_index);
  EXPECT_EQ(graph.region_index(
                tess::RegionRef{tess::ChunkKey{0}, tess::LocalRegionId{3}}),
            tess::invalid_region_index);
}

TEST(TessTopology, RegionIndexRejectsWraparoundReferences) {
  using Shape = tess::Shape<tess::Extent3{16, 8, 1}, tess::Extent3{8, 8, 1}>;
  World<Shape> world;
  fill_passable(world, 1);
  for (std::int64_t x = 0; x < 16; ++x) {
    world.field<PassableTag>(tess::Coord3{x, 3, 0}) = 0;
  }

  tess::LocalTopologyScratch scratch;
  tess::RegionGraph graph;
  ASSERT_EQ((tess::build_region_graph<decltype(world), PassableTag>(
                 world, scratch, graph))
                .status,
            tess::TopologyStatus::Built);
  ASSERT_EQ(graph.region_count(), 4u);

  // region_of returns this sentinel chunk for out-of-world coordinates; a
  // nonzero region id alongside it must not wrap the chunk guard.
  const auto sentinel =
      tess::ChunkKey{std::numeric_limits<std::uint64_t>::max()};
  EXPECT_EQ(
      graph.region_index(tess::RegionRef{sentinel, tess::LocalRegionId{1}}),
      tess::invalid_region_index);
  // A region id near 2^32 must not wrap the offset arithmetic back into a
  // valid dense index (chunk 1's offset is 2, so offset + id - 1 wraps to 0).
  EXPECT_EQ(graph.region_index(tess::RegionRef{
                tess::ChunkKey{1},
                tess::LocalRegionId{std::numeric_limits<std::uint32_t>::max()},
            }),
            tess::invalid_region_index);
}

TEST(TessTopology, UpdateRegionGraphEmptyDirtySetIsNoOp) {
  using Shape = tess::Shape<tess::Extent3{16, 8, 1}, tess::Extent3{8, 8, 1}>;
  World<Shape> world;
  fill_passable(world, 1);

  tess::LocalTopologyScratch scratch;
  tess::RegionGraph graph;
  const auto built = tess::build_region_graph<decltype(world), PassableTag>(
      world, scratch, graph);
  ASSERT_EQ(built.status, tess::TopologyStatus::Built);

  tess::RegionGraph reference;
  ASSERT_EQ((tess::build_region_graph<decltype(world), PassableTag>(
                 world, scratch, reference))
                .status,
            tess::TopologyStatus::Built);

  const auto updated = tess::update_region_graph<decltype(world), PassableTag>(
      world, scratch, graph, {});
  EXPECT_EQ(updated.status, tess::TopologyStatus::Built);
  EXPECT_EQ(updated.region_count, built.region_count);
  EXPECT_EQ(updated.passable_tile_count, built.passable_tile_count);
  EXPECT_EQ(updated.boundary_exit_count, built.boundary_exit_count);
  EXPECT_EQ(updated.version, built.version);
  expect_graphs_equal(graph, reference);
}

TEST(TessTopology, UpdateRegionGraphRejectsInvalidDirtyChunk) {
  using Shape = tess::Shape<tess::Extent3{16, 8, 1}, tess::Extent3{8, 8, 1}>;
  World<Shape> world;
  fill_passable(world, 1);

  tess::LocalTopologyScratch scratch;
  tess::RegionGraph graph;
  ASSERT_EQ((tess::build_region_graph<decltype(world), PassableTag>(
                 world, scratch, graph))
                .status,
            tess::TopologyStatus::Built);

  tess::RegionGraph reference;
  ASSERT_EQ((tess::build_region_graph<decltype(world), PassableTag>(
                 world, scratch, reference))
                .status,
            tess::TopologyStatus::Built);

  const auto dirty = std::array{tess::ChunkKey{2}};
  const auto updated = tess::update_region_graph<decltype(world), PassableTag>(
      world, scratch, graph, dirty);
  EXPECT_EQ(updated.status, tess::TopologyStatus::InvalidChunk);
  // The graph must stay untouched when the dirty set is rejected.
  expect_graphs_equal(graph, reference);
}

TEST(TessTopology, UpdateRegionGraphSingleChunkEditMatchesFullRebuild) {
  using Shape = tess::Shape<tess::Extent3{16, 8, 1}, tess::Extent3{8, 8, 1}>;
  World<Shape> world;
  fill_passable(world, 1);

  tess::LocalTopologyScratch scratch;
  tess::RegionGraph graph;
  ASSERT_EQ((tess::build_region_graph<decltype(world), PassableTag>(
                 world, scratch, graph))
                .status,
            tess::TopologyStatus::Built);

  // Split the second chunk in half with an interior wall.
  for (std::int64_t y = 0; y < 8; ++y) {
    world.field<PassableTag>(tess::Coord3{12, y, 0}) = 0;
  }

  const auto dirty = std::array{tess::ChunkKey{1}};
  const auto updated = tess::update_region_graph<decltype(world), PassableTag>(
      world, scratch, graph, dirty);
  ASSERT_EQ(updated.status, tess::TopologyStatus::Built);

  tess::RegionGraph reference;
  const auto rebuilt = tess::build_region_graph<decltype(world), PassableTag>(
      world, scratch, reference);
  ASSERT_EQ(rebuilt.status, tess::TopologyStatus::Built);
  EXPECT_EQ(updated.region_count, rebuilt.region_count);
  EXPECT_EQ(updated.passable_tile_count, rebuilt.passable_tile_count);
  EXPECT_EQ(updated.boundary_exit_count, rebuilt.boundary_exit_count);
  EXPECT_EQ(updated.version, rebuilt.version);
  expect_graphs_equal(graph, reference);

  const auto probes = std::array{
      tess::Coord3{0, 0, 0},  tess::Coord3{7, 7, 0},  tess::Coord3{9, 1, 0},
      tess::Coord3{11, 6, 0}, tess::Coord3{13, 0, 0}, tess::Coord3{15, 7, 0},
  };
  expect_reachability_equal<Shape>(graph, reference, probes);
}

TEST(TessTopology, UpdateRegionGraphSeamEditWithTwoDirtyChunks) {
  using Shape = tess::Shape<tess::Extent3{16, 8, 1}, tess::Extent3{8, 8, 1}>;
  World<Shape> world;
  fill_passable(world, 1);
  // Start with a blocked seam, then open one crossing tile on each side.
  for (std::int64_t y = 0; y < 8; ++y) {
    world.field<PassableTag>(tess::Coord3{7, y, 0}) = 0;
    world.field<PassableTag>(tess::Coord3{8, y, 0}) = 0;
  }

  tess::LocalTopologyScratch scratch;
  tess::RegionGraph graph;
  ASSERT_EQ((tess::build_region_graph<decltype(world), PassableTag>(
                 world, scratch, graph))
                .status,
            tess::TopologyStatus::Built);

  world.field<PassableTag>(tess::Coord3{7, 4, 0}) = 1;
  world.field<PassableTag>(tess::Coord3{8, 4, 0}) = 1;

  const auto dirty = std::array{tess::ChunkKey{0}, tess::ChunkKey{1}};
  const auto updated = tess::update_region_graph<decltype(world), PassableTag>(
      world, scratch, graph, dirty);
  ASSERT_EQ(updated.status, tess::TopologyStatus::Built);

  tess::RegionGraph reference;
  ASSERT_EQ((tess::build_region_graph<decltype(world), PassableTag>(
                 world, scratch, reference))
                .status,
            tess::TopologyStatus::Built);
  expect_graphs_equal(graph, reference);

  tess::RegionGraphScratch graph_scratch;
  const auto crossing = tess::reachable<Shape>(
      graph, tess::Coord3{0, 0, 0}, tess::Coord3{15, 7, 0}, graph_scratch);
  EXPECT_EQ(crossing.status, tess::ReachabilityStatus::Reachable);
}

TEST(TessTopology, UpdateRegionGraphAllChunksDirtyMatchesFullBuild) {
  using Shape = tess::Shape<tess::Extent3{16, 16, 1}, tess::Extent3{8, 8, 1}>;
  World<Shape> world;
  fill_passable(world, 1);

  tess::LocalTopologyScratch scratch;
  tess::RegionGraph graph;
  ASSERT_EQ((tess::build_region_graph<decltype(world), PassableTag>(
                 world, scratch, graph))
                .status,
            tess::TopologyStatus::Built);

  std::uint64_t rng = 0x51ED2701FULL;
  for (std::int64_t y = 0; y < 16; ++y) {
    for (std::int64_t x = 0; x < 16; ++x) {
      if (next_random(rng) % 100 < 40) {
        world.field<PassableTag>(tess::Coord3{x, y, 0}) = 0;
      }
    }
  }

  const auto dirty = std::array{tess::ChunkKey{0}, tess::ChunkKey{1},
                                tess::ChunkKey{2}, tess::ChunkKey{3}};
  const auto updated = tess::update_region_graph<decltype(world), PassableTag>(
      world, scratch, graph, dirty);
  ASSERT_EQ(updated.status, tess::TopologyStatus::Built);

  tess::RegionGraph reference;
  const auto rebuilt = tess::build_region_graph<decltype(world), PassableTag>(
      world, scratch, reference);
  ASSERT_EQ(rebuilt.status, tess::TopologyStatus::Built);
  EXPECT_EQ(updated.region_count, rebuilt.region_count);
  EXPECT_EQ(updated.passable_tile_count, rebuilt.passable_tile_count);
  EXPECT_EQ(updated.boundary_exit_count, rebuilt.boundary_exit_count);
  EXPECT_EQ(updated.version, rebuilt.version);
  expect_graphs_equal(graph, reference);
}

TEST(TessTopology, UpdateRegionGraphScriptedEditsMatchFullRebuild) {
  using Shape = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{4, 4, 1}>;
  World<Shape> world;
  fill_passable(world, 1);

  std::uint64_t rng = 0xC0FFEE123456789ULL;
  for (std::int64_t y = 0; y < 32; ++y) {
    for (std::int64_t x = 0; x < 32; ++x) {
      if (next_random(rng) % 100 < 30) {
        world.field<PassableTag>(tess::Coord3{x, y, 0}) = 0;
      }
    }
  }

  tess::LocalTopologyScratch scratch;
  tess::RegionGraph graph;
  ASSERT_EQ((tess::build_region_graph<decltype(world), PassableTag>(
                 world, scratch, graph))
                .status,
            tess::TopologyStatus::Built);

  std::vector<tess::Coord3> probes;
  for (std::int64_t y = 2; y < 32; y += 9) {
    for (std::int64_t x = 2; x < 32; x += 9) {
      probes.push_back(tess::Coord3{x, y, 0});
    }
  }

  for (int edit = 0; edit < 40; ++edit) {
    const auto x = static_cast<std::int64_t>(next_random(rng) % 32);
    const auto y = static_cast<std::int64_t>(next_random(rng) % 32);
    const auto coord = tess::Coord3{x, y, 0};
    auto& tile = world.field<PassableTag>(coord);
    tile = tile == 0 ? 1 : 0;

    const auto dirty =
        std::array{tess::chunk_key<Shape>(tess::chunk_coord<Shape>(coord))};
    const auto updated =
        tess::update_region_graph<decltype(world), PassableTag>(world, scratch,
                                                                graph, dirty);
    ASSERT_EQ(updated.status, tess::TopologyStatus::Built) << "edit " << edit;

    tess::RegionGraph reference;
    const auto rebuilt = tess::build_region_graph<decltype(world), PassableTag>(
        world, scratch, reference);
    ASSERT_EQ(rebuilt.status, tess::TopologyStatus::Built) << "edit " << edit;
    ASSERT_EQ(updated.region_count, rebuilt.region_count) << "edit " << edit;
    ASSERT_EQ(updated.passable_tile_count, rebuilt.passable_tile_count)
        << "edit " << edit;
    ASSERT_EQ(updated.boundary_exit_count, rebuilt.boundary_exit_count)
        << "edit " << edit;
    ASSERT_EQ(graph.portals().size(), reference.portals().size())
        << "edit " << edit;
    expect_graphs_equal(graph, reference);
    for (const auto& topology : reference.local_topologies()) {
      const auto* local = graph.local_topology(topology.chunk());
      ASSERT_NE(local, nullptr) << "edit " << edit;
      for (const auto& region : topology.regions()) {
        const auto* mirrored = local->region(region.id);
        ASSERT_NE(mirrored, nullptr) << "edit " << edit;
        EXPECT_EQ(*mirrored, region) << "edit " << edit;
      }
    }
    expect_reachability_equal<Shape>(
        graph, reference,
        std::span<const tess::Coord3>{probes.data(), probes.size()});
  }
}

TEST(TessTopology, RegionGraphFreshnessTracksTopologyVersion) {
  // is_region_graph_fresh reports whether a built graph still matches the
  // world. The S3 precheck relies on it to fall back to A* on a stale graph (a
  // stale graph could otherwise return a definitive but wrong Unreachable).
  using Shape = tess::Shape<tess::Extent3{16, 8, 1}, tess::Extent3{8, 8, 1}>;
  World<Shape> world;
  fill_passable(world, 1);

  tess::LocalTopologyScratch local_scratch;
  tess::RegionGraph graph;
  const auto built = tess::build_region_graph<decltype(world), PassableTag>(
      world, local_scratch, graph);
  ASSERT_EQ(built.status, tess::TopologyStatus::Built);
  EXPECT_TRUE(tess::is_region_graph_fresh(world, graph));

  // A topology-version bump on any chunk makes the graph stale.
  world.mark_topology_rebuilt(tess::ChunkKey{0});
  EXPECT_FALSE(tess::is_region_graph_fresh(world, graph));

  // Rebuilding against the current world restores freshness.
  const auto rebuilt = tess::build_region_graph<decltype(world), PassableTag>(
      world, local_scratch, graph);
  ASSERT_EQ(rebuilt.status, tess::TopologyStatus::Built);
  EXPECT_TRUE(tess::is_region_graph_fresh(world, graph));

  // A never-built graph is never fresh.
  tess::RegionGraph empty;
  EXPECT_FALSE(tess::is_region_graph_fresh(world, empty));
}
