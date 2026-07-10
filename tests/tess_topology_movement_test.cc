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

}  // namespace
