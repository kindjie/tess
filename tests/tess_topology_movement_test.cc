#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "allocation_counter.h"

// S5.3: per-class region labeling plus the graph movement-class stamp. The
// identity case pins byte-identical labels against the legacy raw-tag build;
// the Walker/Builder cases prove per-class labels diverge exactly on
// construction tiles; the stamp cases prove a graph built for one class never
// answers for another (update falls back to a full rebuild, class-aware
// freshness reports not-fresh).
namespace {

namespace mv = tess::movement;

struct PassableTag {};
struct ConstructionTag {};

using Schema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>,
                                 tess::Field<ConstructionTag, std::uint8_t>>;
using TopDown2D = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{4, 4, 1}>;
using World = tess::AlwaysResidentWorld<TopDown2D, Schema>;

using Walker = mv::MovementClass<
    mv::AllOf<mv::Field<PassableTag>, mv::Not<mv::Field<ConstructionTag>>>,
    mv::UnitCost>;
using Builder = mv::MovementClass<
    mv::AnyOf<mv::Field<PassableTag>, mv::Field<ConstructionTag>>,
    mv::UnitCost>;

void fill_passable(World& world, std::uint8_t value) {
  for (auto& page : world.chunks()) {
    auto passable = page.template field_span<PassableTag>();
    std::fill(passable.begin(), passable.end(), value);
  }
}

// Walls that cross chunk seams so portal pairing differs per class too.
void carve_walls(World& world) {
  for (std::int64_t y = 0; y < 8; ++y) {
    if (y == 5) {
      continue;
    }
    world.field<PassableTag>(tess::Coord3{2, y, 0}) = 0;
  }
  for (std::int64_t x = 4; x < 8; ++x) {
    world.field<PassableTag>(tess::Coord3{x, 3, 0}) = 0;
  }
}

// Construction sites on both sides of a chunk seam: a passable tile under
// construction and an impassable site tile.
void mark_construction(World& world) {
  world.field<ConstructionTag>(tess::Coord3{3, 4, 0}) = 1;  // passable + site
  world.field<PassableTag>(tess::Coord3{2, 4, 0}) = 0;      // wall becomes...
  world.field<ConstructionTag>(tess::Coord3{2, 4, 0}) = 1;  // ...a site
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

TEST(TessTopologyMovement, IdentityLabelsMatchTheLegacyTagBuild) {
  World world;
  fill_passable(world, 1);
  carve_walls(world);

  tess::LocalTopologyScratch scratch;
  tess::RegionGraph by_tag;
  tess::RegionGraph by_class;
  const auto tag_result =
      tess::build_region_graph<World, PassableTag>(world, scratch, by_tag);
  const auto class_result =
      tess::build_region_graph<World, mv::WalkableField<PassableTag>>(
          world, scratch, by_class);

  EXPECT_EQ(tag_result.status, tess::TopologyStatus::Built);
  EXPECT_EQ(tag_result.region_count, class_result.region_count);
  EXPECT_EQ(tag_result.passable_tile_count, class_result.passable_tile_count);
  expect_graphs_equal(by_class, by_tag);
}

TEST(TessTopologyMovement, WalkerAndBuilderDivergeExactlyOnConstruction) {
  World world;
  fill_passable(world, 1);
  carve_walls(world);
  mark_construction(world);

  tess::LocalTopologyScratch scratch;
  tess::RegionGraph walker_graph;
  tess::RegionGraph builder_graph;
  const auto walker_built =
      tess::build_region_graph<World, Walker>(world, scratch, walker_graph);
  ASSERT_EQ(walker_built.status, tess::TopologyStatus::Built);
  const auto builder_built =
      tess::build_region_graph<World, Builder>(world, scratch, builder_graph);
  ASSERT_EQ(builder_built.status, tess::TopologyStatus::Built);

  // Tile-level: labels are valid for exactly the class's passable set, so the
  // graphs differ precisely on the construction tiles.
  for (std::int64_t y = 0; y < 8; ++y) {
    for (std::int64_t x = 0; x < 8; ++x) {
      const auto coord = tess::Coord3{x, y, 0};
      const auto walker_region = walker_graph.region_of<TopDown2D>(coord);
      const auto builder_region = builder_graph.region_of<TopDown2D>(coord);
      const auto walker_valid =
          walker_region.region != tess::invalid_local_region;
      const auto builder_valid =
          builder_region.region != tess::invalid_local_region;
      const auto construction = world.field<ConstructionTag>(coord) != 0;
      if (construction) {
        EXPECT_FALSE(walker_valid) << coord.x << "," << coord.y;
        EXPECT_TRUE(builder_valid) << coord.x << "," << coord.y;
      } else {
        EXPECT_EQ(walker_valid, builder_valid) << coord.x << "," << coord.y;
      }
    }
  }

  // Region-level: the construction sites bridge the x=2 wall for the Builder
  // only ({1,4} and {3,4} connect through the two site tiles).
  tess::RegionGraphScratch reach_scratch;
  const auto start = tess::Coord3{1, 4, 0};
  const auto goal = tess::Coord3{3, 4, 0};
  EXPECT_EQ(
      tess::reachable<TopDown2D>(builder_graph, start, goal, reach_scratch)
          .status,
      tess::ReachabilityStatus::Reachable);
  const auto walker_reach =
      tess::reachable<TopDown2D>(walker_graph, start, goal, reach_scratch);
  EXPECT_NE(walker_reach.status, tess::ReachabilityStatus::Reachable);
}

TEST(TessTopologyMovement, PerClassIncrementalUpdateEqualsFullRebuild) {
  World world;
  fill_passable(world, 1);
  carve_walls(world);

  tess::LocalTopologyScratch scratch;
  tess::RegionGraph graph;
  const auto built =
      tess::build_region_graph<World, Builder>(world, scratch, graph);
  ASSERT_EQ(built.status, tess::TopologyStatus::Built);

  // Construction edits change Builder passability only; both dirty chunks
  // sit on a seam so portal re-derivation is exercised.
  mark_construction(world);
  world.field<ConstructionTag>(tess::Coord3{4, 3, 0}) = 1;
  const auto dirty = std::vector<tess::ChunkKey>{
      tess::chunk_key<TopDown2D>(
          tess::chunk_coord<TopDown2D>(tess::Coord3{2, 4, 0})),
      tess::chunk_key<TopDown2D>(
          tess::chunk_coord<TopDown2D>(tess::Coord3{4, 3, 0})),
  };

  const auto updated =
      tess::update_region_graph<World, Builder>(world, scratch, graph, dirty);
  EXPECT_EQ(updated.status, tess::TopologyStatus::Built);

  tess::RegionGraph reference;
  const auto rebuilt =
      tess::build_region_graph<World, Builder>(world, scratch, reference);
  EXPECT_EQ(updated.region_count, rebuilt.region_count);
  EXPECT_EQ(updated.passable_tile_count, rebuilt.passable_tile_count);
  EXPECT_EQ(updated.boundary_exit_count, rebuilt.boundary_exit_count);
  expect_graphs_equal(graph, reference);
}

TEST(TessTopologyMovement, ClassStampMismatchForcesFullRebuildOnUpdate) {
  World world;
  fill_passable(world, 1);
  carve_walls(world);
  mark_construction(world);

  tess::LocalTopologyScratch scratch;
  tess::RegionGraph graph;
  const auto built =
      tess::build_region_graph<World, Walker>(world, scratch, graph);
  ASSERT_EQ(built.status, tess::TopologyStatus::Built);

  // An empty dirty set is a no-op when the class matches; under a class
  // mismatch it must instead be a full rebuild with the new class's labels.
  const auto updated =
      tess::update_region_graph<World, Builder>(world, scratch, graph, {});
  EXPECT_EQ(updated.status, tess::TopologyStatus::Built);

  tess::RegionGraph reference;
  tess::build_region_graph<World, Builder>(world, scratch, reference);
  expect_graphs_equal(graph, reference);
  EXPECT_TRUE(graph.matches_class<Builder>());
  EXPECT_FALSE(graph.matches_class<Walker>());
}

TEST(TessTopologyMovement, FreshnessIsPerClass) {
  World world;
  fill_passable(world, 1);

  tess::LocalTopologyScratch scratch;
  tess::RegionGraph graph;
  tess::build_region_graph<World, PassableTag>(world, scratch, graph);

  // The class-agnostic freshness is unchanged; the class-aware form demands
  // a stamp match, and a raw tag shares its identity class's stamp.
  EXPECT_TRUE(tess::is_region_graph_fresh(world, graph));
  EXPECT_TRUE(tess::is_region_graph_fresh_for<PassableTag>(world, graph));
  EXPECT_TRUE((tess::is_region_graph_fresh_for<mv::WalkableField<PassableTag>>(
      world, graph)));
  EXPECT_FALSE(tess::is_region_graph_fresh_for<Walker>(world, graph));
  EXPECT_FALSE(tess::is_region_graph_fresh_for<Builder>(world, graph));

  // An unbuilt graph is bound to no class at all.
  tess::RegionGraph unbuilt;
  EXPECT_FALSE(unbuilt.matches_class<PassableTag>());
  EXPECT_FALSE(tess::is_region_graph_fresh_for<PassableTag>(world, unbuilt));
}

TEST(TessTopologyMovement, WarmPerClassRelabelIsAllocationFree) {
  World world;
  fill_passable(world, 1);
  carve_walls(world);
  mark_construction(world);

  tess::LocalTopologyScratch scratch;
  scratch.reserve_tiles(World::local_tile_count);
  tess::LocalChunkTopology topology;
  const auto chunk = tess::ChunkKey{0};
  // Warm the vectors, then relabel the same chunk: no allocations.
  const auto warm = tess::build_local_chunk_topology<World, Builder>(
      world, chunk, scratch, topology);
  ASSERT_EQ(warm.status, tess::TopologyStatus::Built);
  {
    tess_test::ScopedAllocationCounter counter;
    const auto relabel = tess::build_local_chunk_topology<World, Builder>(
        world, chunk, scratch, topology);
    EXPECT_EQ(relabel.status, tess::TopologyStatus::Built);
    EXPECT_EQ(counter.count(), 0u);
  }
}

// S5.7: the TransitionProvider contract. A provider contributes extra
// directed transitions as portals; the default AdjacentTransitions
// contributes nothing (byte-identical build), the provider type is stamped
// on the graph, and a sparse transition into a non-resident chunk degrades
// reachability to Indeterminate rather than a wrong Unreachable.

// Bridges the x=2 wall inside the south-west chunk: both directions between
// {1,4} and {3,4} (same chunk, so each direction enumerates from it).
struct BridgeTransitions {
  template <typename WorldT, typename Sink>
  void for_each_transition(const WorldT&, tess::ChunkKey chunk,
                           Sink&& sink) const {
    const auto home = tess::chunk_key<TopDown2D>(
        tess::chunk_coord<TopDown2D>(tess::Coord3{1, 4, 0}));
    if (chunk.value != home.value) {
      return;
    }
    sink(tess::Coord3{1, 4, 0}, tess::Coord3{3, 4, 0});
    sink(tess::Coord3{3, 4, 0}, tess::Coord3{1, 4, 0});
  }
};

static_assert(tess::TransitionProviderFor<tess::AdjacentTransitions, World>);
static_assert(tess::TransitionProviderFor<BridgeTransitions, World>);
static_assert(!tess::TransitionProviderFor<int, World>);

// Stair fixtures (S5.8): a schema carrying a stair-direction field plus
// walker/builder classes over it.
struct StairTag {};
using StairSchema =
    tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>,
                      tess::Field<ConstructionTag, std::uint8_t>,
                      tess::Field<StairTag, std::uint8_t>>;
using StairWalker = mv::MovementClass<
    mv::AllOf<mv::Field<PassableTag>, mv::Not<mv::Field<ConstructionTag>>>,
    mv::UnitCost>;
using StairBuilder = mv::MovementClass<
    mv::AnyOf<mv::Field<PassableTag>, mv::Field<ConstructionTag>>,
    mv::UnitCost>;

// A provider transition from the resident west chunk into the missing middle
// chunk of the sparse three-chunk fixture (face-adjacent, per the contract).
struct EastwardHop {
  template <typename WorldT, typename Sink>
  void for_each_transition(const WorldT&, tess::ChunkKey chunk,
                           Sink&& sink) const {
    if (chunk.value != 0) {
      return;
    }
    sink(tess::Coord3{30, 0, 0}, tess::Coord3{33, 0, 0});
  }
};

TEST(TessTopologyMovement, DefaultProviderBuildIsIdenticalToProviderless) {
  World world;
  fill_passable(world, 1);
  carve_walls(world);

  tess::LocalTopologyScratch scratch;
  tess::RegionGraph plain;
  tess::RegionGraph with_default;
  tess::build_region_graph<World, Walker>(world, scratch, plain);
  tess::build_region_graph<World, Walker>(world, scratch, with_default,
                                          tess::AdjacentTransitions{});
  expect_graphs_equal(with_default, plain);
  EXPECT_TRUE(plain.matches_provider<tess::AdjacentTransitions>());
  EXPECT_FALSE(plain.matches_provider<BridgeTransitions>());
}

TEST(TessTopologyMovement, ProviderTransitionsBridgeWalledRegions) {
  World world;
  fill_passable(world, 1);
  carve_walls(world);
  // Seal the wall's y=5 gap so the bridge is the ONLY link across x=2.
  world.field<PassableTag>(tess::Coord3{2, 5, 0}) = 0;

  tess::LocalTopologyScratch scratch;
  tess::RegionGraph plain;
  tess::build_region_graph<World, Walker>(world, scratch, plain);
  tess::RegionGraph bridged;
  tess::build_region_graph<World, Walker>(world, scratch, bridged,
                                          BridgeTransitions{});

  ASSERT_EQ(bridged.portals().size(), plain.portals().size() + 2);
  const auto start = tess::Coord3{1, 4, 0};
  const auto goal = tess::Coord3{3, 4, 0};
  tess::RegionGraphScratch reach;
  EXPECT_NE(tess::reachable<TopDown2D>(plain, start, goal, reach).status,
            tess::ReachabilityStatus::Reachable);
  EXPECT_EQ(tess::reachable<TopDown2D>(bridged, start, goal, reach).status,
            tess::ReachabilityStatus::Reachable);
  EXPECT_EQ(tess::reachable<TopDown2D>(bridged, goal, start, reach).status,
            tess::ReachabilityStatus::Reachable);
}

TEST(TessTopologyMovement, ProviderIncrementalUpdateEqualsFullRebuild) {
  World world;
  fill_passable(world, 1);
  carve_walls(world);

  tess::LocalTopologyScratch scratch;
  tess::RegionGraph graph;
  tess::build_region_graph<World, Walker>(world, scratch, graph,
                                          BridgeTransitions{});

  // Edit passability around one bridge endpoint, in the bridge's chunk.
  world.field<PassableTag>(tess::Coord3{1, 5, 0}) = 0;
  world.field<PassableTag>(tess::Coord3{2, 5, 0}) = 1;
  const auto dirty = std::vector<tess::ChunkKey>{tess::chunk_key<TopDown2D>(
      tess::chunk_coord<TopDown2D>(tess::Coord3{1, 5, 0}))};
  const auto updated = tess::update_region_graph<World, Walker>(
      world, scratch, graph, dirty, BridgeTransitions{});
  EXPECT_EQ(updated.status, tess::TopologyStatus::Built);

  tess::RegionGraph reference;
  tess::build_region_graph<World, Walker>(world, scratch, reference,
                                          BridgeTransitions{});
  expect_graphs_equal(graph, reference);
}

TEST(TessTopologyMovement, ProviderMismatchForcesFullRebuildOnUpdate) {
  World world;
  fill_passable(world, 1);
  carve_walls(world);

  tess::LocalTopologyScratch scratch;
  tess::RegionGraph graph;
  tess::build_region_graph<World, Walker>(world, scratch, graph);
  ASSERT_TRUE(graph.matches_provider<tess::AdjacentTransitions>());

  // Empty dirty set, different provider type: must be a full rebuild that
  // carries the bridge portals and restamps the provider.
  const auto updated = tess::update_region_graph<World, Walker>(
      world, scratch, graph, {}, BridgeTransitions{});
  EXPECT_EQ(updated.status, tess::TopologyStatus::Built);
  EXPECT_TRUE(graph.matches_provider<BridgeTransitions>());

  tess::RegionGraph reference;
  tess::build_region_graph<World, Walker>(world, scratch, reference,
                                          BridgeTransitions{});
  expect_graphs_equal(graph, reference);
}

TEST(TessTopologyMovement, SparseProviderIntoMissingChunkIsIndeterminate) {
  using ThreeChunk =
      tess::Shape<tess::Extent3{96, 32, 1}, tess::Extent3{32, 32, 1}>;
  using Sparse = tess::SparseResidentWorld<ThreeChunk, Schema>;

  Sparse world{tess::ResidencyConfig{3 * Sparse::page_byte_size}};
  for (const auto key : {tess::ChunkKey{0}, tess::ChunkKey{2}}) {
    world.ensure_resident(key);
    auto& page = world.chunk(key);
    auto span = page.template field_span<PassableTag>();
    std::fill(span.begin(), span.end(), std::uint8_t{1});
  }
  // Wall the west chunk's east column so it has NO boundary exits: without
  // the provider its region is definitively enclosed within known walls.
  auto& west = world.chunk(tess::ChunkKey{0});
  for (std::uint64_t y = 0; y < 32; ++y) {
    west.field<PassableTag>(
        tess::local_tile_id<ThreeChunk>(tess::LocalCoord3{31, y, 0})) = 0;
  }

  tess::LocalTopologyScratch scratch;
  const auto start = tess::Coord3{0, 0, 0};
  const auto goal = tess::Coord3{70, 0, 0};
  tess::RegionGraphScratch reach;

  tess::SparseRegionGraph plain;
  tess::build_region_graph<Sparse, Walker>(world, scratch, plain);
  EXPECT_EQ(tess::reachable<ThreeChunk>(plain, start, goal, reach).status,
            tess::ReachabilityStatus::Unreachable);

  tess::SparseRegionGraph hopped;
  tess::build_region_graph<Sparse, Walker>(world, scratch, hopped,
                                           EastwardHop{});
  EXPECT_EQ(tess::reachable<ThreeChunk>(hopped, start, goal, reach).status,
            tess::ReachabilityStatus::Indeterminate);
}

// S5.8: the StairTransitions vertical provider. An offset stair (foot ->
// one step sideways, one z up) is the vertical special transition: two
// stacked passable tiles are already six-axis adjacent, so only the offset
// form adds connectivity. Stairs are per-class through the label filter,
// incremental-safe, and diagonal chunk crossings contribute nothing.

// Two z-levels in separate chunks (chunk z extent 1): every stair crosses
// the z chunk boundary, exercising the cross-chunk down-transition path.
using TwoLevel = tess::Shape<tess::Extent3{8, 8, 2}, tess::Extent3{4, 4, 1}>;
using LevelWorld = tess::AlwaysResidentWorld<TwoLevel, StairSchema>;
using Stairs = tess::StairTransitions<StairTag>;

// Ground floor open on rows y < 2; an upper platform over impassable ground
// (rows y in [2,4)) so no vertical face adjacency connects the levels; the
// offset stair from the open ground row is the only link.
void build_two_levels(LevelWorld& world) {
  for (std::int64_t y = 0; y < 8; ++y) {
    for (std::int64_t x = 0; x < 8; ++x) {
      world.field<PassableTag>(tess::Coord3{x, y, 0}) = 1;
    }
  }
  for (std::int64_t y = 2; y < 4; ++y) {
    for (std::int64_t x = 0; x < 4; ++x) {
      world.field<PassableTag>(tess::Coord3{x, y, 1}) = 1;
      world.field<PassableTag>(tess::Coord3{x, y, 0}) = 0;
    }
  }
  // Foot on the open ground row; landing (2,2,1) on the platform.
  world.field<StairTag>(tess::Coord3{2, 1, 0}) =
      static_cast<std::uint8_t>(tess::StairDirection::PositiveY);
}

TEST(TessTopologyMovement, StairConnectsLevelsInBothDirections) {
  LevelWorld world;
  build_two_levels(world);

  tess::LocalTopologyScratch scratch;
  tess::RegionGraphScratch reach;
  const auto ground = tess::Coord3{6, 6, 0};
  const auto platform = tess::Coord3{1, 2, 1};

  tess::RegionGraph plain;
  tess::build_region_graph<LevelWorld, PassableTag>(world, scratch, plain);
  EXPECT_EQ(tess::reachable<TwoLevel>(plain, ground, platform, reach).status,
            tess::ReachabilityStatus::Unreachable);

  tess::RegionGraph stairs;
  tess::build_region_graph<LevelWorld, PassableTag>(world, scratch, stairs,
                                                    Stairs{});
  EXPECT_EQ(tess::reachable<TwoLevel>(stairs, ground, platform, reach).status,
            tess::ReachabilityStatus::Reachable);
  EXPECT_EQ(tess::reachable<TwoLevel>(stairs, platform, ground, reach).status,
            tess::ReachabilityStatus::Reachable);
}

TEST(TessTopologyMovement, StairEdgesAreFilteredPerClass) {
  LevelWorld world;
  build_two_levels(world);
  // The landing becomes a construction site: passable to the Builder's
  // AnyOf, invisible to the Walker's Not<Construction>.
  world.field<PassableTag>(tess::Coord3{2, 2, 1}) = 0;
  world.field<ConstructionTag>(tess::Coord3{2, 2, 1}) = 1;

  tess::LocalTopologyScratch scratch;
  tess::RegionGraphScratch reach;
  const auto ground = tess::Coord3{6, 6, 0};
  const auto platform = tess::Coord3{1, 2, 1};

  tess::RegionGraph walker_graph;
  tess::build_region_graph<LevelWorld, StairWalker>(world, scratch,
                                                    walker_graph, Stairs{});
  EXPECT_NE(
      tess::reachable<TwoLevel>(walker_graph, ground, platform, reach).status,
      tess::ReachabilityStatus::Reachable);

  tess::RegionGraph builder_graph;
  tess::build_region_graph<LevelWorld, StairBuilder>(world, scratch,
                                                     builder_graph, Stairs{});
  EXPECT_EQ(
      tess::reachable<TwoLevel>(builder_graph, ground, platform, reach).status,
      tess::ReachabilityStatus::Reachable);
}

TEST(TessTopologyMovement, StairIncrementalUpdateEqualsFullRebuild) {
  LevelWorld world;
  build_two_levels(world);

  tess::LocalTopologyScratch scratch;
  tess::RegionGraph graph;
  tess::build_region_graph<LevelWorld, PassableTag>(world, scratch, graph,
                                                    Stairs{});

  // Remove the stair and open a second one elsewhere; dirty the foot chunk.
  world.field<StairTag>(tess::Coord3{2, 1, 0}) =
      static_cast<std::uint8_t>(tess::StairDirection::None);
  world.field<StairTag>(tess::Coord3{0, 1, 0}) =
      static_cast<std::uint8_t>(tess::StairDirection::PositiveY);
  const auto dirty = std::vector<tess::ChunkKey>{tess::chunk_key<TwoLevel>(
      tess::chunk_coord<TwoLevel>(tess::Coord3{2, 1, 0}))};
  const auto updated = tess::update_region_graph<LevelWorld, PassableTag>(
      world, scratch, graph, dirty, Stairs{});
  EXPECT_EQ(updated.status, tess::TopologyStatus::Built);

  tess::RegionGraph reference;
  tess::build_region_graph<LevelWorld, PassableTag>(world, scratch, reference,
                                                    Stairs{});
  expect_graphs_equal(graph, reference);

  // The new stair (landing {0,2,1} on the platform) restores the link.
  tess::RegionGraphScratch reach;
  EXPECT_EQ(tess::reachable<TwoLevel>(graph, tess::Coord3{6, 6, 0},
                                      tess::Coord3{1, 2, 1}, reach)
                .status,
            tess::ReachabilityStatus::Reachable);
}

TEST(TessTopologyMovement, DiagonalChunkCrossingStairContributesNothing) {
  LevelWorld world;
  build_two_levels(world);
  // Move the stair foot to the chunk's +x edge: the landing would cross the
  // x boundary AND the z boundary at once, violating the face-neighbor
  // contract, so the stair is skipped entirely.
  world.field<StairTag>(tess::Coord3{2, 1, 0}) =
      static_cast<std::uint8_t>(tess::StairDirection::None);
  world.field<PassableTag>(tess::Coord3{3, 1, 0}) = 1;
  world.field<StairTag>(tess::Coord3{3, 1, 0}) =
      static_cast<std::uint8_t>(tess::StairDirection::PositiveX);
  // Give the would-be landing a passable tile so only the contract, not
  // labeling, is what drops it.
  world.field<PassableTag>(tess::Coord3{4, 1, 1}) = 1;

  tess::LocalTopologyScratch scratch;
  tess::RegionGraph graph;
  tess::build_region_graph<LevelWorld, PassableTag>(world, scratch, graph,
                                                    Stairs{});
  tess::RegionGraph plain;
  tess::build_region_graph<LevelWorld, PassableTag>(world, scratch, plain);
  EXPECT_EQ(graph.portals().size(), plain.portals().size());
}

TEST(TessTopologyMovement, SameChunkStairConnectsItsOwnLevels) {
  using OneChunk = tess::Shape<tess::Extent3{4, 4, 2}, tess::Extent3{4, 4, 2}>;
  using OneChunkWorld = tess::AlwaysResidentWorld<OneChunk, StairSchema>;
  OneChunkWorld world;
  for (std::int64_t y = 0; y < 4; ++y) {
    for (std::int64_t x = 0; x < 4; ++x) {
      world.field<PassableTag>(tess::Coord3{x, y, 0}) = 1;
    }
  }
  // Upper platform over impassable ground, foot column excluded as before.
  for (std::int64_t y = 2; y < 4; ++y) {
    for (std::int64_t x = 0; x < 4; ++x) {
      world.field<PassableTag>(tess::Coord3{x, y, 1}) = 1;
      world.field<PassableTag>(tess::Coord3{x, y, 0}) = 0;
    }
  }
  world.field<PassableTag>(tess::Coord3{1, 1, 0}) = 1;
  world.field<StairTag>(tess::Coord3{1, 1, 0}) =
      static_cast<std::uint8_t>(tess::StairDirection::PositiveY);

  tess::LocalTopologyScratch scratch;
  tess::RegionGraph graph;
  tess::build_region_graph<OneChunkWorld, PassableTag>(
      world, scratch, graph, tess::StairTransitions<StairTag>{});
  tess::RegionGraphScratch reach;
  EXPECT_EQ(tess::reachable<OneChunk>(graph, tess::Coord3{0, 0, 0},
                                      tess::Coord3{3, 3, 1}, reach)
                .status,
            tess::ReachabilityStatus::Reachable);
}

// Regression (pre-merge review): a stair whose landing steps sideways
// across an x/y chunk boundary at a local z BELOW the chunk top must emit
// its down transition too -- the down direction originates in the sideways
// neighbor chunk, whose enumeration scans adjacent foot chunks.
TEST(TessTopologyMovement, SidewaysCrossingStairConnectsBothDirections) {
  // Two chunks along x, both z levels INSIDE each chunk (z extent 2), so the
  // landing crosses only the x boundary.
  using TwoWide = tess::Shape<tess::Extent3{8, 4, 2}, tess::Extent3{4, 4, 2}>;
  using WideWorld = tess::AlwaysResidentWorld<TwoWide, StairSchema>;
  WideWorld world;
  // Ground floor open everywhere on the west chunk and the east chunk's
  // ground is impassable under an east platform at z=1.
  for (std::int64_t y = 0; y < 4; ++y) {
    for (std::int64_t x = 0; x < 4; ++x) {
      world.field<PassableTag>(tess::Coord3{x, y, 0}) = 1;
    }
    for (std::int64_t x = 4; x < 8; ++x) {
      world.field<PassableTag>(tess::Coord3{x, y, 1}) = 1;
    }
  }
  // Foot (3,1,0) in the west chunk, landing (4,1,1) on the east platform:
  // chunk crossing is sideways-only (z stays inside the chunk).
  world.field<StairTag>(tess::Coord3{3, 1, 0}) =
      static_cast<std::uint8_t>(tess::StairDirection::PositiveX);

  tess::LocalTopologyScratch scratch;
  tess::RegionGraph plain;
  tess::build_region_graph<WideWorld, PassableTag>(world, scratch, plain);
  tess::RegionGraph stairs;
  tess::build_region_graph<WideWorld, PassableTag>(
      world, scratch, stairs, tess::StairTransitions<StairTag>{});
  EXPECT_EQ(stairs.portals().size(), plain.portals().size() + 2);

  tess::RegionGraphScratch reach;
  const auto ground = tess::Coord3{0, 0, 0};
  const auto platform = tess::Coord3{7, 3, 1};
  EXPECT_NE(tess::reachable<TwoWide>(plain, ground, platform, reach).status,
            tess::ReachabilityStatus::Reachable);
  EXPECT_EQ(tess::reachable<TwoWide>(stairs, ground, platform, reach).status,
            tess::ReachabilityStatus::Reachable);
  EXPECT_EQ(tess::reachable<TwoWide>(stairs, platform, ground, reach).status,
            tess::ReachabilityStatus::Reachable);

  // Incremental update across the seam stays equal to a full rebuild.
  world.field<StairTag>(tess::Coord3{3, 2, 0}) =
      static_cast<std::uint8_t>(tess::StairDirection::PositiveX);
  const auto dirty = std::vector<tess::ChunkKey>{tess::ChunkKey{0}};
  const auto updated = tess::update_region_graph<WideWorld, PassableTag>(
      world, scratch, stairs, dirty, tess::StairTransitions<StairTag>{});
  EXPECT_EQ(updated.status, tess::TopologyStatus::Built);
  tess::RegionGraph reference;
  tess::build_region_graph<WideWorld, PassableTag>(
      world, scratch, reference, tess::StairTransitions<StairTag>{});
  expect_graphs_equal(stairs, reference);
}

// Regression (pre-merge review): a stair field value outside the
// StairDirection range reads as None instead of leaking an unintended
// straight-up transition.
TEST(TessTopologyMovement, OutOfRangeStairValueReadsAsNone) {
  LevelWorld world;
  build_two_levels(world);
  world.field<StairTag>(tess::Coord3{2, 1, 0}) = 250;  // garbage

  tess::LocalTopologyScratch scratch;
  tess::RegionGraph graph;
  tess::build_region_graph<LevelWorld, PassableTag>(
      world, scratch, graph, tess::StairTransitions<StairTag>{});
  tess::RegionGraph plain;
  tess::build_region_graph<LevelWorld, PassableTag>(world, scratch, plain);
  expect_graphs_equal(graph, plain);
}

}  // namespace
