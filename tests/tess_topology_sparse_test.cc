#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace {

struct PassableTag {};
using Schema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>>;

// 4 x 4 chunks along x/y (32-wide chunks): enough neighbors to exercise
// resident/non-resident boundaries. Solo is a single chunk (no neighbors).
using Small = tess::Shape<tess::Extent3{128, 128, 1}, tess::Extent3{32, 32, 1}>;
using Solo = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{32, 32, 1}>;
using SparseSmall = tess::SparseResidentWorld<Small, Schema>;
using SparseSolo = tess::SparseResidentWorld<Solo, Schema>;

template <typename World>
void make_chunk_passable(World& world, tess::ChunkKey key) {
  world.ensure_resident(key);
  auto& page = world.chunk(key);
  for (std::uint64_t i = 0; i < World::local_tile_count; ++i) {
    page.template field<PassableTag>(tess::LocalTileId{i}) = 1;
  }
}

// Mirrors the dense expect_graphs_equal in tess_topology_test.cc, templated so
// it works on RegionGraphT<SparseResident>. Compares the full observable graph
// state: per-chunk topology (region ids, regions, boundary exits), the global
// portal list, and the region count.
template <typename Graph>
void expect_graphs_equal(const Graph& lhs, const Graph& rhs) {
  ASSERT_EQ(lhs.local_topologies().size(), rhs.local_topologies().size());
  for (std::size_t i = 0; i < lhs.local_topologies().size(); ++i) {
    const auto& a = lhs.local_topologies()[i];
    const auto& b = rhs.local_topologies()[i];
    EXPECT_EQ(a.chunk(), b.chunk());
    ASSERT_EQ(a.region_ids().size(), b.region_ids().size());
    EXPECT_TRUE(std::equal(a.region_ids().begin(), a.region_ids().end(),
                           b.region_ids().begin()));
    ASSERT_EQ(a.regions().size(), b.regions().size());
    EXPECT_TRUE(std::equal(a.regions().begin(), a.regions().end(),
                           b.regions().begin()));
    ASSERT_EQ(a.boundary_exits().size(), b.boundary_exits().size());
    EXPECT_TRUE(std::equal(a.boundary_exits().begin(), a.boundary_exits().end(),
                           b.boundary_exits().begin()));
  }
  ASSERT_EQ(lhs.portals().size(), rhs.portals().size());
  EXPECT_TRUE(std::equal(lhs.portals().begin(), lhs.portals().end(),
                         rhs.portals().begin()));
  EXPECT_EQ(lhs.region_count(), rhs.region_count());
}

TEST(TessSparseTopology, BuildsGraphOverResidentSetOnly) {
  SparseSmall world{tess::ResidencyConfig{8 * SparseSmall::page_byte_size}};
  make_chunk_passable(world, tess::ChunkKey{0});  // (0,0)
  make_chunk_passable(world, tess::ChunkKey{1});  // (1,0), east of chunk 0
  tess::LocalTopologyScratch scratch;
  tess::SparseRegionGraph graph;

  const auto result =
      tess::build_region_graph<SparseSmall, PassableTag>(world, scratch, graph);
  EXPECT_EQ(result.status, tess::TopologyStatus::Built);
  // Only the two resident chunks are in the graph -- never chunk_count.
  EXPECT_EQ(graph.local_topologies().size(), 2u);
  EXPECT_EQ(result.region_count, 2u);
  // The two fully-passable adjacent chunks share a 32-tile boundary: one
  // directed portal per boundary tile per direction (32 + 32).
  EXPECT_EQ(graph.portals().size(), 64u);
}

TEST(TessSparseTopology, ReachableAcrossResidentChunks) {
  SparseSmall world{tess::ResidencyConfig{8 * SparseSmall::page_byte_size}};
  make_chunk_passable(world, tess::ChunkKey{0});
  make_chunk_passable(world, tess::ChunkKey{1});
  tess::LocalTopologyScratch scratch;
  tess::SparseRegionGraph graph;
  tess::build_region_graph<SparseSmall, PassableTag>(world, scratch, graph);

  tess::RegionGraphScratch reach;
  const auto result =
      tess::reachable<Small>(graph, {0, 0, 0}, {40, 0, 0}, reach);
  // A definite path exists via the portal, even though chunk 0 also borders
  // non-resident chunks: goal-found dominates the missing-frontier flag.
  EXPECT_EQ(result.status, tess::ReachabilityStatus::Reachable);
}

TEST(TessSparseTopology, IndeterminateAcrossNonResidentBoundary) {
  SparseSmall world{tess::ResidencyConfig{8 * SparseSmall::page_byte_size}};
  make_chunk_passable(world, tess::ChunkKey{0});  // (0,0)
  make_chunk_passable(world, tess::ChunkKey{2});  // (2,0): gap chunk 1 missing
  ASSERT_FALSE(world.is_resident(tess::ChunkKey{1}));
  tess::LocalTopologyScratch scratch;
  tess::SparseRegionGraph graph;
  tess::build_region_graph<SparseSmall, PassableTag>(world, scratch, graph);

  tess::RegionGraphScratch reach;
  // start in chunk 0, goal in chunk 2. No portal bridges them (chunk 1 is
  // unloaded), but chunk 0's region exits into the missing chunk 1, so the
  // answer is Indeterminate -- never a wrong Unreachable.
  const auto result =
      tess::reachable<Small>(graph, {0, 0, 0}, {70, 0, 0}, reach);
  EXPECT_EQ(result.status, tess::ReachabilityStatus::Indeterminate);
}

TEST(TessSparseTopology, UnreachableWithinFullyResidentEnclosedComponent) {
  // A single-chunk world has no neighboring chunks, so no region can reach a
  // non-resident chunk: a genuinely walled-off goal is Unreachable, not
  // Indeterminate.
  SparseSolo world{tess::ResidencyConfig{SparseSolo::page_byte_size}};
  make_chunk_passable(world, tess::ChunkKey{0});
  for (std::int32_t y = 0; y < 32; ++y) {
    world.field<PassableTag>(tess::Coord3{16, y, 0}) = 0;  // full-height wall
  }
  tess::LocalTopologyScratch scratch;
  tess::SparseRegionGraph graph;
  tess::build_region_graph<SparseSolo, PassableTag>(world, scratch, graph);

  tess::RegionGraphScratch reach;
  const auto result =
      tess::reachable<Solo>(graph, {0, 0, 0}, {31, 0, 0}, reach);
  EXPECT_EQ(result.status, tess::ReachabilityStatus::Unreachable);
}

TEST(TessSparseTopology, NonResidentEndpointIsIndeterminate) {
  SparseSmall world{tess::ResidencyConfig{4 * SparseSmall::page_byte_size}};
  make_chunk_passable(world, tess::ChunkKey{0});
  ASSERT_FALSE(world.is_resident(tess::ChunkKey{1}));
  tess::LocalTopologyScratch scratch;
  tess::SparseRegionGraph graph;
  tess::build_region_graph<SparseSmall, PassableTag>(world, scratch, graph);

  tess::RegionGraphScratch reach;
  const auto from_missing =
      tess::reachable<Small>(graph, {40, 0, 0}, {0, 0, 0}, reach);
  EXPECT_EQ(from_missing.status, tess::ReachabilityStatus::Indeterminate);
  const auto to_missing =
      tess::reachable<Small>(graph, {0, 0, 0}, {40, 0, 0}, reach);
  EXPECT_EQ(to_missing.status, tess::ReachabilityStatus::Indeterminate);
}

TEST(TessSparseTopology, IncrementalUpdateEqualsFreshBuild) {
  const auto build = [](SparseSmall& world, tess::SparseRegionGraph& graph) {
    tess::LocalTopologyScratch scratch;
    return tess::build_region_graph<SparseSmall, PassableTag>(world, scratch,
                                                              graph);
  };

  SparseSmall world{tess::ResidencyConfig{8 * SparseSmall::page_byte_size}};
  make_chunk_passable(world, tess::ChunkKey{0});
  make_chunk_passable(world, tess::ChunkKey{1});
  make_chunk_passable(world, tess::ChunkKey{2});
  tess::SparseRegionGraph updated;
  build(world, updated);

  // Edit chunk 1: carve a full-height wall so its topology splits, then update.
  for (std::int32_t y = 0; y < 32; ++y) {
    world.field<PassableTag>(tess::Coord3{48, y, 0}) = 0;  // x=48 is in chunk 1
  }
  tess::LocalTopologyScratch scratch;
  const std::array<tess::ChunkKey, 1> dirty{tess::ChunkKey{1}};
  tess::update_region_graph<SparseSmall, PassableTag>(world, scratch, updated,
                                                      dirty);

  tess::SparseRegionGraph fresh;
  build(world, fresh);

  expect_graphs_equal(updated, fresh);

  // And reachability agrees across the edited seam.
  tess::RegionGraphScratch a;
  tess::RegionGraphScratch b;
  const auto lhs = tess::reachable<Small>(updated, {0, 0, 0}, {40, 0, 0}, a);
  const auto rhs = tess::reachable<Small>(fresh, {0, 0, 0}, {40, 0, 0}, b);
  EXPECT_EQ(lhs.status, rhs.status);
  EXPECT_EQ(lhs.visited_regions, rhs.visited_regions);
}

TEST(TessSparseTopology, StaleGraphAfterResidencyChangeFallsBackToFullBuild) {
  SparseSmall world{tess::ResidencyConfig{8 * SparseSmall::page_byte_size}};
  make_chunk_passable(world, tess::ChunkKey{0});
  make_chunk_passable(world, tess::ChunkKey{1});
  tess::LocalTopologyScratch scratch;
  tess::SparseRegionGraph graph;
  tess::build_region_graph<SparseSmall, PassableTag>(world, scratch, graph);
  ASSERT_EQ(graph.local_topologies().size(), 2u);

  // Load a third chunk after the graph was frozen, then update with no dirty
  // chunks: the residency-generation guard must detect the change and rebuild
  // over the new resident set rather than trust the stale snapshot.
  make_chunk_passable(world, tess::ChunkKey{2});
  const std::array<tess::ChunkKey, 0> dirty{};
  tess::update_region_graph<SparseSmall, PassableTag>(world, scratch, graph,
                                                      dirty);
  EXPECT_EQ(graph.local_topologies().size(), 3u);
}

TEST(TessSparseTopology, RegionGraphFreshnessTracksResidencyAndVersion) {
  // is_region_graph_fresh over a sparse world is stale after either an in-place
  // topology-version bump on a resident chunk OR a residency change (eviction),
  // so the S3 precheck falls back to A* instead of trusting an old snapshot.
  SparseSmall world{tess::ResidencyConfig{8 * SparseSmall::page_byte_size}};
  make_chunk_passable(world, tess::ChunkKey{0});
  make_chunk_passable(world, tess::ChunkKey{1});
  tess::LocalTopologyScratch scratch;
  tess::SparseRegionGraph graph;
  const auto built =
      tess::build_region_graph<SparseSmall, PassableTag>(world, scratch, graph);
  ASSERT_EQ(built.status, tess::TopologyStatus::Built);
  EXPECT_TRUE(tess::is_region_graph_fresh(world, graph));

  // In-place topology edit on a still-resident chunk -> stale (version).
  world.mark_topology_rebuilt(tess::ChunkKey{0});
  EXPECT_FALSE(tess::is_region_graph_fresh(world, graph));

  const auto rebuilt =
      tess::build_region_graph<SparseSmall, PassableTag>(world, scratch, graph);
  ASSERT_EQ(rebuilt.status, tess::TopologyStatus::Built);
  EXPECT_TRUE(tess::is_region_graph_fresh(world, graph));

  // Residency change (evict a frozen chunk) -> stale (count/generation).
  ASSERT_TRUE(world.evict(tess::ChunkKey{1}));
  EXPECT_FALSE(tess::is_region_graph_fresh(world, graph));
}

}  // namespace
