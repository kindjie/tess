#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstdint>

#include "allocation_counter.h"

// Planner reuse (audit-2026-07-11 M4): the plan_operations overload that
// plans into a caller-owned ExecutionReport must recycle the report's
// rows, planned operations, and chunk lists.
namespace {

struct TerrainTag {};
struct CostTag {};

constexpr std::uint32_t DirtyTerrain = 1u << 0u;
constexpr std::uint32_t DirtyCost = 1u << 1u;

using TopDown2D =
    tess::Shape<tess::Extent3{128, 64, 1}, tess::Extent3{32, 16, 1}>;
using Schema = tess::FieldSchema<tess::Field<TerrainTag, std::uint16_t>,
                                 tess::Field<CostTag, float>>;
using World = tess::AlwaysResidentWorld<TopDown2D, Schema>;

// The reuse overload must make steady-state planning allocation-free once
// the report's rows, planned ops, and pooled chunk lists are warm (audit
// 2026-07-11 M4).
TEST(TessQueuedPlanning, WarmPlanningReuseIsAllocationFree) {
  World world;
  tess::FrameOps ops;
  (void)ops.update_field(tess::DomainDesc::resident_chunks(),
                         tess::FieldAccessDesc{0, DirtyTerrain, DirtyTerrain},
                         tess::WritePolicy::UniquePerChunk);
  (void)ops.update_field(tess::DomainDesc::dirty_chunks(DirtyCost),
                         tess::FieldAccessDesc{DirtyCost, 0, 0},
                         tess::WritePolicy::ReadOnly);
  world.mark_dirty(tess::ChunkKey{1}, DirtyCost,
                   tess::Box3{tess::Coord3{32, 0, 0}, tess::Extent3{1, 1, 1}});

  tess::ExecutionReport report;
  // Warm all capacities: the first plan sizes rows and chunk lists, the
  // second exercises reset() once so the recycle pool itself is sized.
  (void)tess::plan_operations(world, ops, report);
  (void)tess::plan_operations(world, ops, report);
  ASSERT_EQ(report.planned_count(), 2u);
  {
    tess_test::ScopedAllocationCounter counter;
    for (int frame = 0; frame < 4; ++frame) {
      const auto& reused = tess::plan_operations(world, ops, report);
      ASSERT_EQ(reused.planned_count(), 2u);
    }
    EXPECT_EQ(counter.count(), 0u);
  }
}

}  // namespace
