#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstdint>

#include "allocation_counter.h"

namespace {

struct PassableTag {};
struct ConstructionTag {};
using Schema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>,
                                 tess::Field<ConstructionTag, std::uint8_t>>;

// Builder may enter construction tiles the plain walker cannot (M6).
using Builder = tess::movement::MovementClass<
    tess::movement::AnyOf<tess::movement::Field<PassableTag>,
                          tess::movement::Field<ConstructionTag>>,
    tess::movement::UnitCost>;

template <typename Shape>
using DenseWorld = tess::AlwaysResidentWorld<Shape, Schema>;

// 16x8 split into two 8-wide chunks (0 and 1 along x).
using Split = tess::Shape<tess::Extent3{16, 8, 1}, tess::Extent3{8, 8, 1}>;

// 128x128 in 32-wide chunks: room for resident/non-resident boundaries.
using Wide = tess::Shape<tess::Extent3{128, 128, 1}, tess::Extent3{32, 32, 1}>;
using SparseWide = tess::SparseResidentWorld<Wide, Schema>;

template <typename WorldType>
void fill_passable(WorldType& world, std::uint8_t value) {
  for (auto& page : world.chunks()) {
    auto passable = page.template field_span<PassableTag>();
    for (auto& tile : passable) {
      tile = value;
    }
  }
}

template <typename WorldType>
void make_chunk_passable(WorldType& world, tess::ChunkKey key) {
  world.ensure_resident(key);
  auto& page = world.chunk(key);
  for (std::uint64_t i = 0; i < WorldType::local_tile_count; ++i) {
    page.template field<PassableTag>(tess::LocalTileId{i}) = 1;
  }
}

// Builds a dense split world whose two chunks are disconnected: chunk 0's east
// column (x=7) is walled, so no portal bridges chunk 0 and chunk 1.
auto build_disconnected_split(DenseWorld<Split>& world,
                              tess::RegionGraph& graph)
    -> tess::LocalTopologyResult {
  fill_passable(world, 1);
  for (std::int64_t y = 0; y < 8; ++y) {
    world.field<PassableTag>(tess::Coord3{7, y, 0}) = 0;
  }
  tess::LocalTopologyScratch scratch;
  return tess::build_region_graph<DenseWorld<Split>, PassableTag>(
      world, scratch, graph);
}

}  // namespace

TEST(TessPrecheck, ReachableWithinConnectedRegion) {
  DenseWorld<Split> world;
  tess::RegionGraph graph;
  ASSERT_EQ(build_disconnected_split(world, graph).status,
            tess::TopologyStatus::Built);
  tess::RegionGraphScratch scratch;
  EXPECT_EQ(tess::precheck_path<PassableTag>(graph, world, {0, 0, 0}, {6, 7, 0},
                                             scratch),
            tess::PrecheckStatus::Reachable);
}

TEST(TessPrecheck, UnreachableAcrossWalledChunkSkipsAStar) {
  DenseWorld<Split> world;
  tess::RegionGraph graph;
  ASSERT_EQ(build_disconnected_split(world, graph).status,
            tess::TopologyStatus::Built);
  tess::RegionGraphScratch scratch;
  const auto status = tess::precheck_path<PassableTag>(graph, world, {0, 0, 0},
                                                       {15, 7, 0}, scratch);
  EXPECT_EQ(status, tess::PrecheckStatus::Unreachable);
  EXPECT_TRUE(tess::precheck_rules_out_path(status));
  // Only Unreachable licenses skipping A*.
  EXPECT_FALSE(tess::precheck_rules_out_path(tess::PrecheckStatus::Reachable));
  EXPECT_FALSE(
      tess::precheck_rules_out_path(tess::PrecheckStatus::MissingChunk));
  EXPECT_FALSE(tess::precheck_rules_out_path(tess::PrecheckStatus::GraphStale));
  EXPECT_FALSE(tess::precheck_rules_out_path(tess::PrecheckStatus::NoGraph));
}

TEST(TessPrecheck, OutOfBoundsStartIsInvalidStart) {
  DenseWorld<Split> world;
  tess::RegionGraph graph;
  ASSERT_EQ(build_disconnected_split(world, graph).status,
            tess::TopologyStatus::Built);
  tess::RegionGraphScratch scratch;
  EXPECT_EQ(tess::precheck_path<PassableTag>(graph, world, {-1, 0, 0},
                                             {6, 7, 0}, scratch),
            tess::PrecheckStatus::InvalidStart);
  // A walled (in-bounds but region-less) start is InvalidStart too: A* is
  // authoritative on start passability, so the gate must not rule it out.
  const auto status = tess::precheck_path<PassableTag>(graph, world, {7, 0, 0},
                                                       {6, 7, 0}, scratch);
  EXPECT_EQ(status, tess::PrecheckStatus::InvalidStart);
  EXPECT_FALSE(tess::precheck_rules_out_path(status));
}

TEST(TessPrecheck, OutOfBoundsGoalIsInvalidGoal) {
  DenseWorld<Split> world;
  tess::RegionGraph graph;
  ASSERT_EQ(build_disconnected_split(world, graph).status,
            tess::TopologyStatus::Built);
  tess::RegionGraphScratch scratch;
  EXPECT_EQ(tess::precheck_path<PassableTag>(graph, world, {0, 0, 0},
                                             {16, 0, 0}, scratch),
            tess::PrecheckStatus::InvalidGoal);
}

TEST(TessPrecheck, EmptyGraphIsNoGraph) {
  DenseWorld<Split> world;
  fill_passable(world, 1);
  tess::RegionGraph graph;  // never built
  tess::RegionGraphScratch scratch;
  EXPECT_EQ(tess::precheck_path<PassableTag>(graph, world, {0, 0, 0},
                                             {15, 7, 0}, scratch),
            tess::PrecheckStatus::NoGraph);
}

TEST(TessPrecheck, StaleGraphIsGraphStaleNotWrongUnreachable) {
  DenseWorld<Split> world;
  tess::RegionGraph graph;
  ASSERT_EQ(build_disconnected_split(world, graph).status,
            tess::TopologyStatus::Built);
  tess::RegionGraphScratch scratch;
  // A topology edit after the build must degrade to A*, never a stale verdict.
  world.mark_topology_rebuilt(tess::ChunkKey{0});
  EXPECT_EQ(tess::precheck_path<PassableTag>(graph, world, {0, 0, 0},
                                             {15, 7, 0}, scratch),
            tess::PrecheckStatus::GraphStale);
}

TEST(TessPrecheck, SparseNonResidentBoundaryIsMissingChunk) {
  // A two-chunk resident corridor (chunks 0,1 along x) whose east edge exits
  // into a non-resident chunk. A goal past that edge cannot be ruled out.
  SparseWide world{tess::ResidencyConfig{8 * SparseWide::page_byte_size}};
  make_chunk_passable(world, tess::ChunkKey{0});  // x in [0,32)
  make_chunk_passable(world, tess::ChunkKey{1});  // x in [32,64)
  tess::LocalTopologyScratch local_scratch;
  tess::SparseRegionGraph graph;
  const auto built = tess::build_region_graph<SparseWide, PassableTag>(
      world, local_scratch, graph);
  ASSERT_EQ(built.status, tess::TopologyStatus::Built);

  tess::RegionGraphScratch scratch;
  // Within the resident corridor: reachable.
  EXPECT_EQ(tess::precheck_path<PassableTag>(graph, world, {0, 0, 0},
                                             {40, 0, 0}, scratch),
            tess::PrecheckStatus::Reachable);
  // Toward a goal in a non-resident chunk (x=80, chunk 2): the corridor's edge
  // exits into unloaded space, so the route cannot be ruled out.
  EXPECT_EQ(tess::precheck_path<PassableTag>(graph, world, {0, 0, 0},
                                             {80, 0, 0}, scratch),
            tess::PrecheckStatus::MissingChunk);
}

TEST(TessPrecheck, WarmPrecheckIsAllocationFree) {
  DenseWorld<Split> world;
  tess::RegionGraph graph;
  ASSERT_EQ(build_disconnected_split(world, graph).status,
            tess::TopologyStatus::Built);
  tess::RegionGraphScratch scratch;
  // Warm the scratch (its visited-epoch vector) with one query first.
  (void)tess::precheck_path<PassableTag>(graph, world, {0, 0, 0}, {15, 7, 0},
                                         scratch);
  {
    tess_test::ScopedAllocationCounter counter;
    const auto status = tess::precheck_path<PassableTag>(
        graph, world, {0, 0, 0}, {15, 7, 0}, scratch);
    EXPECT_EQ(status, tess::PrecheckStatus::Unreachable);
    EXPECT_EQ(counter.count(), 0u);
    EXPECT_EQ(counter.bytes(), 0u);
  }
}

TEST(TessPrecheck, WrongClassGraphIsGraphStaleNotWrongUnreachable) {
  DenseWorld<Split> world;
  tess::RegionGraph graph;
  ASSERT_EQ(build_disconnected_split(world, graph).status,
            tess::TopologyStatus::Built);
  // A construction site bridges the wall: the Builder can cross where the
  // plain walker cannot, so the walker-labeled graph MUST NOT answer for the
  // Builder -- its definitive Unreachable would prune a route the Builder's
  // own A* can walk.
  world.field<ConstructionTag>(tess::Coord3{7, 3, 0}) = 1;

  tess::RegionGraphScratch scratch;
  EXPECT_EQ(tess::precheck_path<PassableTag>(graph, world, {0, 0, 0},
                                             {15, 7, 0}, scratch),
            tess::PrecheckStatus::Unreachable);
  EXPECT_EQ(tess::precheck_path<Builder>(graph, world, {0, 0, 0}, {15, 7, 0},
                                         scratch),
            tess::PrecheckStatus::GraphStale);
}

TEST(TessPrecheck, MatchedClassGraphRulesOutPerClass) {
  DenseWorld<Split> world;
  tess::RegionGraph graph;
  ASSERT_EQ(build_disconnected_split(world, graph).status,
            tess::TopologyStatus::Built);
  // Rebuild FOR the Builder with a bridge site: reachable for the Builder.
  world.field<ConstructionTag>(tess::Coord3{7, 3, 0}) = 1;
  tess::LocalTopologyScratch local_scratch;
  const auto rebuilt = tess::build_region_graph<DenseWorld<Split>, Builder>(
      world, local_scratch, graph);
  ASSERT_EQ(rebuilt.status, tess::TopologyStatus::Built);

  tess::RegionGraphScratch scratch;
  EXPECT_EQ(tess::precheck_path<Builder>(graph, world, {0, 0, 0}, {15, 7, 0},
                                         scratch),
            tess::PrecheckStatus::Reachable);
  // Remove the bridge and rebuild: now definitively unreachable per class.
  world.field<ConstructionTag>(tess::Coord3{7, 3, 0}) = 0;
  const auto sealed = tess::build_region_graph<DenseWorld<Split>, Builder>(
      world, local_scratch, graph);
  ASSERT_EQ(sealed.status, tess::TopologyStatus::Built);
  EXPECT_EQ(tess::precheck_path<Builder>(graph, world, {0, 0, 0}, {15, 7, 0},
                                         scratch),
            tess::PrecheckStatus::Unreachable);
  // And the raw-tag walker gets GraphStale from the Builder-stamped graph.
  EXPECT_EQ(tess::precheck_path<PassableTag>(graph, world, {0, 0, 0},
                                             {15, 7, 0}, scratch),
            tess::PrecheckStatus::GraphStale);
}
