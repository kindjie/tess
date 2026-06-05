#include <gtest/gtest.h>
#include <tess/tess.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::atomic<bool> count_allocations{false};
std::atomic<int> allocation_count{0};

}  // namespace

void* operator new(std::size_t size) {
  if (count_allocations.load(std::memory_order_relaxed)) {
    allocation_count.fetch_add(1, std::memory_order_relaxed);
  }
  if (void* ptr = std::malloc(size)) {
    return ptr;
  }
  throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
  if (count_allocations.load(std::memory_order_relaxed)) {
    allocation_count.fetch_add(1, std::memory_order_relaxed);
  }
  if (void* ptr = std::malloc(size)) {
    return ptr;
  }
  throw std::bad_alloc();
}

void operator delete(void* ptr) noexcept { std::free(ptr); }

void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }

void operator delete[](void* ptr) noexcept { std::free(ptr); }

void operator delete[](void* ptr, std::size_t) noexcept { std::free(ptr); }

namespace {

struct TerrainTag {};
struct CostTag {};

constexpr std::uint32_t DirtyTerrain = 1u << 0u;
constexpr std::uint32_t DirtyCost = 1u << 1u;
constexpr std::uint32_t ActiveFluid = 1u << 0u;
constexpr std::uint32_t ActiveFire = 1u << 1u;

using TopDown2D =
    tess::Shape<tess::Extent3{128, 64, 1}, tess::Extent3{32, 16, 1}>;
using Schema = tess::FieldSchema<tess::Field<TerrainTag, std::uint16_t>,
                                 tess::Field<CostTag, float>>;
using World = tess::AlwaysResidentWorld<TopDown2D, Schema>;

auto update_from_helper(tess::FrameOps& ops, tess::DomainDesc domain)
    -> tess::OpHandle {
  return ops.update_field(std::move(domain), tess::WritePolicy::ReadOnly);
}

auto planned_keys(const tess::ExecutionReport& report, std::size_t index)
    -> std::vector<tess::ChunkKey> {
  const auto ops = report.plan().operations();
  if (index >= ops.size()) {
    return {};
  }
  return {ops[index].chunks.begin(), ops[index].chunks.end()};
}

TEST(TessQueued, EmptyFramePlansToEmptyReport) {
  World world;
  const tess::FrameOps ops;

  const auto report = tess::plan_operations(world, ops);

  EXPECT_TRUE(ops.empty());
  EXPECT_EQ(ops.size(), 0u);
  EXPECT_TRUE(report.ok());
  EXPECT_TRUE(report.operations().empty());
  EXPECT_TRUE(report.plan().empty());
  EXPECT_EQ(report.plan().size(), 0u);
}

TEST(TessQueued, StableHandlesAndIdsFollowEnqueueOrder) {
  tess::FrameOps ops;
  const auto first = ops.update_field(
      tess::DomainDesc::resident_chunks(), tess::WritePolicy::ReadOnly,
      tess::Priority::Immediate, tess::BudgetPolicy::MustRun);
  const auto second = ops.update_field(
      tess::DomainDesc::resident_chunks(), tess::WritePolicy::UniquePerChunk,
      tess::Priority::Background, tess::BudgetPolicy::CanDefer);

  ASSERT_NE(ops.operation(first), nullptr);
  ASSERT_NE(ops.operation(second), nullptr);
  EXPECT_EQ(first, (tess::OpHandle{0}));
  EXPECT_EQ(second, (tess::OpHandle{1}));
  EXPECT_EQ(ops.operation(first)->id, (tess::OpId{0}));
  EXPECT_EQ(ops.operation(second)->id, (tess::OpId{1}));
  EXPECT_EQ(ops.operation(first)->priority, tess::Priority::Immediate);
  EXPECT_EQ(ops.operation(second)->priority, tess::Priority::Background);
  EXPECT_EQ(ops.operation(second)->budget_policy, tess::BudgetPolicy::CanDefer);
  EXPECT_EQ(ops.operation(tess::OpHandle{2}), nullptr);
}

TEST(TessQueued, ExplicitChunkDomainExpandsInChunkKeyOrder) {
  World world;
  tess::FrameOps ops;
  const std::vector<tess::ChunkKey> requested{
      tess::ChunkKey{7},
      tess::ChunkKey{2},
      tess::ChunkKey{5},
  };

  const auto handle =
      ops.update_field(tess::DomainDesc::explicit_chunks(requested),
                       tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);

  ASSERT_TRUE(report.ok());
  ASSERT_EQ(report.operations().size(), 1u);
  ASSERT_EQ(report.plan().operations().size(), 1u);
  EXPECT_EQ(report.operations()[0].handle, handle);
  EXPECT_EQ(report.operations()[0].chunk_count, 3u);
  EXPECT_EQ(report.operations()[0].access.write_policy,
            tess::WritePolicy::UniquePerChunk);
  EXPECT_EQ(report.operations()[0].access.domain_kind,
            tess::DomainKind::ExplicitChunks);
  EXPECT_EQ(report.operations()[0].access.domain_mask, 0u);
  EXPECT_EQ(report.plan().operations()[0].access.write_policy,
            tess::WritePolicy::UniquePerChunk);
  EXPECT_EQ(planned_keys(report, 0), (std::vector<tess::ChunkKey>{
                                         tess::ChunkKey{2},
                                         tess::ChunkKey{5},
                                         tess::ChunkKey{7},
                                     }));
}

TEST(TessQueued, FieldAccessMetadataPropagatesToPlanAndReport) {
  World world;
  tess::FrameOps ops;
  constexpr auto field_access = tess::FieldAccessDesc{
      DirtyCost,
      DirtyTerrain,
      DirtyTerrain,
  };

  const auto handle = ops.update_field(
      tess::DomainDesc::resident_chunks(), field_access,
      tess::WritePolicy::UniquePerTile, tess::Priority::VisibleSoon,
      tess::BudgetPolicy::BudgetedIncremental);
  const auto report = tess::plan_operations(world, ops);

  ASSERT_TRUE(report.ok());
  ASSERT_NE(ops.operation(handle), nullptr);
  ASSERT_NE(report.find(handle), nullptr);
  ASSERT_EQ(report.plan().operations().size(), 1u);

  EXPECT_EQ(ops.operation(handle)->field_access, field_access);
  EXPECT_EQ(report.find(handle)->field_access, field_access);
  EXPECT_EQ(report.plan().operations()[0].field_access, field_access);
  EXPECT_EQ(report.plan().operations()[0].priority,
            tess::Priority::VisibleSoon);
  EXPECT_EQ(report.plan().operations()[0].budget_policy,
            tess::BudgetPolicy::BudgetedIncremental);
}

TEST(TessQueued, DirtyChunkDomainExpandsThroughWorldMetadata) {
  World world;
  tess::FrameOps ops;
  const auto bounds = tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}};

  world.mark_dirty(tess::ChunkKey{7}, DirtyTerrain, bounds);
  world.mark_dirty(tess::ChunkKey{2}, DirtyCost, bounds);
  world.mark_dirty(tess::ChunkKey{5}, DirtyTerrain | DirtyCost, bounds);

  (void)ops.update_field(tess::DomainDesc::dirty_chunks(DirtyTerrain),
                         tess::WritePolicy::UniquePerTile);
  const auto report = tess::plan_operations(world, ops);

  ASSERT_TRUE(report.ok());
  EXPECT_EQ(planned_keys(report, 0), (std::vector<tess::ChunkKey>{
                                         tess::ChunkKey{5},
                                         tess::ChunkKey{7},
                                     }));
}

TEST(TessQueued, ActiveChunkDomainExpandsThroughWorldMetadata) {
  World world;
  tess::FrameOps ops;

  world.mark_active(tess::ChunkKey{9}, ActiveFire);
  world.mark_active(tess::ChunkKey{1}, ActiveFluid);
  world.mark_active(tess::ChunkKey{4}, ActiveFluid | ActiveFire);

  (void)ops.update_field(tess::DomainDesc::active_chunks(ActiveFire),
                         tess::WritePolicy::UniquePerTile);
  const auto report = tess::plan_operations(world, ops);

  ASSERT_TRUE(report.ok());
  EXPECT_EQ(planned_keys(report, 0), (std::vector<tess::ChunkKey>{
                                         tess::ChunkKey{4},
                                         tess::ChunkKey{9},
                                     }));
}

TEST(TessQueued, ResidentChunkDomainExpandsAllAlwaysResidentChunks) {
  World world;
  tess::FrameOps ops;

  (void)ops.update_field(tess::DomainDesc::resident_chunks(),
                         tess::WritePolicy::ReadOnly);
  const auto report = tess::plan_operations(world, ops);

  ASSERT_TRUE(report.ok());
  ASSERT_EQ(report.plan().operations().size(), 1u);
  EXPECT_EQ(report.operations()[0].chunk_count, World::chunk_count);
  ASSERT_EQ(report.plan().operations()[0].chunks.size(), World::chunk_count);
  EXPECT_EQ(report.plan().operations()[0].chunks.front(), (tess::ChunkKey{0}));
  EXPECT_EQ(report.plan().operations()[0].chunks.back(), (tess::ChunkKey{15}));
}

TEST(TessQueued, InvalidWritePolicyIsRejectedWithoutPlanEntry) {
  World world;
  tess::FrameOps ops;

  (void)ops.update_field(tess::DomainDesc::resident_chunks(),
                         static_cast<tess::WritePolicy>(255));
  const auto report = tess::plan_operations(world, ops);

  ASSERT_FALSE(report.ok());
  EXPECT_TRUE(report.failed());
  EXPECT_EQ(report.planned_count(), 0u);
  EXPECT_EQ(report.failed_count(), 1u);
  ASSERT_EQ(report.operations().size(), 1u);
  EXPECT_EQ(report.operations()[0].status,
            tess::OperationStatus::InvalidWritePolicy);
  EXPECT_EQ(report.operations()[0].failure,
            tess::OperationFailure::InvalidWritePolicyValue);
  EXPECT_FALSE(report.operations()[0].has_detail_chunk);
  EXPECT_TRUE(report.plan().empty());
}

TEST(TessQueued, ReadOnlyWriteMaskIsRejectedWithoutPlanEntry) {
  World world;
  tess::FrameOps ops;
  constexpr auto field_access = tess::FieldAccessDesc{
      DirtyCost,
      DirtyTerrain,
      DirtyTerrain,
  };

  (void)ops.update_field(tess::DomainDesc::resident_chunks(), field_access,
                         tess::WritePolicy::ReadOnly);
  const auto report = tess::plan_operations(world, ops);

  ASSERT_FALSE(report.ok());
  EXPECT_TRUE(report.failed());
  EXPECT_EQ(report.planned_count(), 0u);
  EXPECT_EQ(report.failed_count(), 1u);
  ASSERT_EQ(report.operations().size(), 1u);
  EXPECT_EQ(report.operations()[0].status,
            tess::OperationStatus::InvalidFieldAccess);
  EXPECT_EQ(report.operations()[0].failure,
            tess::OperationFailure::ReadOnlyWriteMask);
  EXPECT_EQ(report.operations()[0].field_access, field_access);
  EXPECT_TRUE(report.plan().empty());
}

TEST(TessQueued, OutOfRangeExplicitChunkIsRejectedWithoutPlanEntry) {
  World world;
  tess::FrameOps ops;
  const std::vector<tess::ChunkKey> requested{
      tess::ChunkKey{3},
      tess::ChunkKey{World::chunk_count},
  };

  (void)ops.update_field(tess::DomainDesc::explicit_chunks(requested),
                         tess::WritePolicy::ReadOnly);
  const auto report = tess::plan_operations(world, ops);

  ASSERT_FALSE(report.ok());
  EXPECT_TRUE(report.failed());
  EXPECT_EQ(report.planned_count(), 0u);
  EXPECT_EQ(report.failed_count(), 1u);
  ASSERT_EQ(report.operations().size(), 1u);
  EXPECT_EQ(report.operations()[0].status,
            tess::OperationStatus::InvalidDomain);
  EXPECT_EQ(report.operations()[0].failure,
            tess::OperationFailure::ExplicitChunkOutOfRange);
  EXPECT_TRUE(report.operations()[0].has_detail_chunk);
  EXPECT_EQ(report.operations()[0].detail_chunk,
            (tess::ChunkKey{World::chunk_count}));
  EXPECT_TRUE(report.plan().empty());
  EXPECT_EQ(report.operations()[0].chunk_count, 0u);
}

TEST(TessQueued, ReportFindsOperationsByHandle) {
  World world;
  tess::FrameOps ops;

  const auto first = ops.update_field(tess::DomainDesc::resident_chunks(),
                                      tess::WritePolicy::ReadOnly);
  const auto second = ops.update_field(tess::DomainDesc::resident_chunks(),
                                       static_cast<tess::WritePolicy>(255));

  const auto report = tess::plan_operations(world, ops);

  ASSERT_NE(report.find(first), nullptr);
  ASSERT_NE(report.find(second), nullptr);
  EXPECT_EQ(report.find(first)->status, tess::OperationStatus::Planned);
  EXPECT_EQ(report.find(second)->status,
            tess::OperationStatus::InvalidWritePolicy);
  EXPECT_EQ(report.find(tess::OpHandle{2}), nullptr);
}

TEST(TessQueued, MixedReportsPreserveOrderAndCountFailures) {
  World world;
  tess::FrameOps ops;
  const std::vector<tess::ChunkKey> invalid_keys{
      tess::ChunkKey{World::chunk_count},
  };

  const auto valid = ops.update_field(tess::DomainDesc::resident_chunks(),
                                      tess::WritePolicy::ReadOnly);
  const auto bad_policy = ops.update_field(tess::DomainDesc::resident_chunks(),
                                           static_cast<tess::WritePolicy>(255));
  const auto bad_domain =
      ops.update_field(tess::DomainDesc::explicit_chunks(invalid_keys),
                       tess::WritePolicy::ReadOnly);

  const auto report = tess::plan_operations(world, ops);

  ASSERT_FALSE(report.ok());
  EXPECT_TRUE(report.failed());
  EXPECT_EQ(report.planned_count(), 1u);
  EXPECT_EQ(report.failed_count(), 2u);
  ASSERT_EQ(report.operations().size(), 3u);
  EXPECT_EQ(report.operations()[0].handle, valid);
  EXPECT_EQ(report.operations()[0].status, tess::OperationStatus::Planned);
  EXPECT_EQ(report.operations()[1].handle, bad_policy);
  EXPECT_EQ(report.operations()[1].status,
            tess::OperationStatus::InvalidWritePolicy);
  EXPECT_EQ(report.operations()[2].handle, bad_domain);
  EXPECT_EQ(report.operations()[2].status,
            tess::OperationStatus::InvalidDomain);

  ASSERT_EQ(report.plan().operations().size(), 1u);
  EXPECT_EQ(report.plan().operations()[0].handle, valid);
}

TEST(TessQueued, DisjointChunksWithSameWriteMaskDoNotConflict) {
  World world;
  tess::FrameOps ops;
  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      DirtyTerrain,
      DirtyTerrain,
  };
  const std::vector<tess::ChunkKey> first_keys{tess::ChunkKey{1}};
  const std::vector<tess::ChunkKey> second_keys{tess::ChunkKey{2}};

  (void)ops.update_field(tess::DomainDesc::explicit_chunks(first_keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(second_keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);

  const auto report = tess::plan_operations(world, ops);

  EXPECT_TRUE(report.ok());
  EXPECT_EQ(report.planned_count(), 2u);
  ASSERT_EQ(report.plan().operations().size(), 2u);
  EXPECT_EQ(report.plan().operations()[0].chunks[0], (tess::ChunkKey{1}));
  EXPECT_EQ(report.plan().operations()[1].chunks[0], (tess::ChunkKey{2}));
}

TEST(TessQueued, OverlappingWriteMasksConflictOnSameChunk) {
  World world;
  tess::FrameOps ops;
  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      DirtyTerrain,
      DirtyTerrain,
  };
  const std::vector<tess::ChunkKey> first_keys{
      tess::ChunkKey{1},
      tess::ChunkKey{3},
  };
  const std::vector<tess::ChunkKey> second_keys{tess::ChunkKey{3}};

  const auto first =
      ops.update_field(tess::DomainDesc::explicit_chunks(first_keys),
                       writes_terrain, tess::WritePolicy::UniquePerChunk);
  const auto second =
      ops.update_field(tess::DomainDesc::explicit_chunks(second_keys),
                       writes_terrain, tess::WritePolicy::UniquePerChunk);

  const auto report = tess::plan_operations(world, ops);

  ASSERT_FALSE(report.ok());
  EXPECT_EQ(report.planned_count(), 1u);
  EXPECT_EQ(report.failed_count(), 1u);
  ASSERT_NE(report.find(second), nullptr);
  EXPECT_EQ(report.find(second)->status, tess::OperationStatus::HazardConflict);
  EXPECT_EQ(report.find(second)->failure,
            tess::OperationFailure::FieldHazardConflict);
  EXPECT_TRUE(report.find(second)->has_conflict);
  EXPECT_EQ(report.find(second)->conflict_handle, first);
  EXPECT_EQ(report.find(second)->conflict_id, (tess::OpId{0}));
  EXPECT_EQ(report.find(second)->conflict_mask, DirtyTerrain);
  ASSERT_EQ(report.plan().operations().size(), 1u);
  EXPECT_EQ(report.plan().operations()[0].handle, first);
}

TEST(TessQueued, DisjointFieldMasksOnSameChunksDoNotConflict) {
  World world;
  tess::FrameOps ops;
  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      DirtyTerrain,
      DirtyTerrain,
  };
  constexpr auto writes_cost = tess::FieldAccessDesc{
      0,
      DirtyCost,
      DirtyCost,
  };

  (void)ops.update_field(tess::DomainDesc::resident_chunks(), writes_terrain,
                         tess::WritePolicy::UniquePerChunk);
  (void)ops.update_field(tess::DomainDesc::resident_chunks(), writes_cost,
                         tess::WritePolicy::UniquePerChunk);

  const auto report = tess::plan_operations(world, ops);

  EXPECT_TRUE(report.ok());
  EXPECT_EQ(report.planned_count(), 2u);
}

TEST(TessQueued, ReadWriteOverlapConflictsWithoutBarrier) {
  World world;
  tess::FrameOps ops;
  constexpr auto reads_terrain = tess::FieldAccessDesc{
      DirtyTerrain,
      0,
      0,
  };
  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      DirtyTerrain,
      DirtyTerrain,
  };

  const auto reader =
      ops.update_field(tess::DomainDesc::resident_chunks(), reads_terrain,
                       tess::WritePolicy::ReadOnly);
  const auto writer =
      ops.update_field(tess::DomainDesc::resident_chunks(), writes_terrain,
                       tess::WritePolicy::UniquePerTile);

  const auto report = tess::plan_operations(world, ops);

  ASSERT_FALSE(report.ok());
  EXPECT_EQ(report.planned_count(), 1u);
  EXPECT_EQ(report.failed_count(), 1u);
  ASSERT_NE(report.find(writer), nullptr);
  EXPECT_EQ(report.find(writer)->status, tess::OperationStatus::HazardConflict);
  EXPECT_EQ(report.find(writer)->conflict_handle, reader);
  EXPECT_EQ(report.find(writer)->conflict_mask, DirtyTerrain);
}

TEST(TessQueued, PlanOperationsPreserveEnqueueOrderAcrossDomains) {
  World world;
  tess::FrameOps ops;
  const std::vector<tess::ChunkKey> first_keys{tess::ChunkKey{6}};
  const std::vector<tess::ChunkKey> second_keys{tess::ChunkKey{1}};

  const auto first =
      ops.update_field(tess::DomainDesc::explicit_chunks(first_keys),
                       tess::WritePolicy::UniquePerChunk);
  const auto second =
      ops.update_field(tess::DomainDesc::explicit_chunks(second_keys),
                       tess::WritePolicy::ReadOnly);

  const auto report = tess::plan_operations(world, ops);

  ASSERT_TRUE(report.ok());
  ASSERT_EQ(report.plan().operations().size(), 2u);
  EXPECT_EQ(report.plan().operations()[0].handle, first);
  EXPECT_EQ(report.plan().operations()[0].id, (tess::OpId{0}));
  EXPECT_EQ(report.plan().operations()[0].chunks[0], (tess::ChunkKey{6}));
  EXPECT_EQ(report.plan().operations()[1].handle, second);
  EXPECT_EQ(report.plan().operations()[1].id, (tess::OpId{1}));
  EXPECT_EQ(report.plan().operations()[1].chunks[0], (tess::ChunkKey{1}));
}

TEST(TessQueued, PlannedChunkDomainAdaptsExpandedChunks) {
  World world;
  tess::FrameOps ops;
  const auto bounds = tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}};
  const std::vector<tess::ChunkKey> requested{
      tess::ChunkKey{4},
      tess::ChunkKey{1},
  };

  world.mark_dirty(tess::ChunkKey{7}, DirtyTerrain, bounds);

  (void)ops.update_field(tess::DomainDesc::explicit_chunks(requested),
                         tess::WritePolicy::ReadOnly);
  (void)ops.update_field(tess::DomainDesc::dirty_chunks(DirtyTerrain),
                         tess::WritePolicy::ReadOnly);
  (void)ops.update_field(tess::DomainDesc::resident_chunks(),
                         tess::WritePolicy::ReadOnly);

  const auto report = tess::plan_operations(world, ops);

  ASSERT_TRUE(report.ok());
  ASSERT_EQ(report.plan().operations().size(), 3u);

  const auto explicit_domain =
      tess::planned_chunk_domain(report.plan().operations()[0]);
  const auto dirty_domain =
      tess::planned_chunk_domain(report.plan().operations()[1]);
  const auto resident_domain =
      tess::planned_chunk_domain(report.plan().operations()[2]);

  EXPECT_EQ(explicit_domain.size(), 2u);
  EXPECT_EQ(explicit_domain.keys()[0], (tess::ChunkKey{1}));
  EXPECT_EQ(explicit_domain.keys()[1], (tess::ChunkKey{4}));
  EXPECT_EQ(dirty_domain.size(), 1u);
  EXPECT_EQ(dirty_domain.keys()[0], (tess::ChunkKey{7}));
  EXPECT_EQ(resident_domain.size(), World::chunk_count);
  EXPECT_EQ(resident_domain.keys().front(), (tess::ChunkKey{0}));
  EXPECT_EQ(resident_domain.keys().back(), (tess::ChunkKey{15}));
}

TEST(TessQueued, PlannedBlockCtxRejectsPolicyMismatch) {
  World world;
  tess::FrameOps ops;

  (void)ops.update_field(tess::DomainDesc::resident_chunks(),
                         tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);

  ASSERT_TRUE(report.ok());
  ASSERT_EQ(report.plan().operations().size(), 1u);
  const auto& planned = report.plan().operations()[0];

  EXPECT_TRUE(
      tess::planned_policy_matches<tess::WritePolicy::UniquePerChunk>(planned));
  EXPECT_FALSE(
      tess::planned_policy_matches<tess::WritePolicy::ReadOnly>(planned));
  EXPECT_FALSE(
      tess::try_planned_block_ctx<tess::WritePolicy::ReadOnly>(world, planned));
}

TEST(TessQueued, PlannedBlockCtxIteratesWithExistingBlockApi) {
  World world;
  tess::FrameOps ops;
  const std::vector<tess::ChunkKey> requested{
      tess::ChunkKey{3},
      tess::ChunkKey{1},
  };

  (void)ops.update_field(tess::DomainDesc::explicit_chunks(requested),
                         tess::WritePolicy::ReadOnly);
  const auto report = tess::plan_operations(world, ops);

  ASSERT_TRUE(report.ok());
  ASSERT_EQ(report.plan().operations().size(), 1u);

  auto ctx = tess::try_planned_block_ctx<tess::WritePolicy::ReadOnly>(
      world, report.plan().operations()[0]);

  ASSERT_TRUE(ctx);
  EXPECT_EQ(ctx->policy(), tess::WritePolicy::ReadOnly);
  EXPECT_EQ(ctx->size(), 2u);

  std::vector<tess::ChunkKey> visited;
  ctx->for_each_chunk([&](auto view) { visited.push_back(view.key()); });

  EXPECT_EQ(visited, (std::vector<tess::ChunkKey>{
                         tess::ChunkKey{1},
                         tess::ChunkKey{3},
                     }));
}

TEST(TessQueued, PrebuiltPlannedBlockCtxIterationDoesNotAllocate) {
  World world;
  tess::FrameOps ops;

  (void)ops.update_field(tess::DomainDesc::resident_chunks(),
                         tess::WritePolicy::ReadOnly);
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());
  ASSERT_EQ(report.plan().operations().size(), 1u);

  allocation_count.store(0, std::memory_order_relaxed);
  count_allocations.store(true, std::memory_order_relaxed);

  auto ctx = tess::try_planned_block_ctx<tess::WritePolicy::ReadOnly>(
      world, report.plan().operations()[0]);
  std::uint64_t key_sum = 0;
  std::size_t visited = 0;
  if (ctx) {
    ctx->for_each_chunk([&](auto view) {
      key_sum += view.key().value;
      ++visited;
    });
  }

  count_allocations.store(false, std::memory_order_relaxed);

  ASSERT_TRUE(ctx);
  EXPECT_EQ(visited, World::chunk_count);
  EXPECT_EQ(key_sum, 120u);
  EXPECT_EQ(allocation_count.load(std::memory_order_relaxed), 0);
}

TEST(TessQueued, SourceLocationIsCapturedForDiagnostics) {
  World world;
  tess::FrameOps ops;

  const auto handle =
      update_from_helper(ops, tess::DomainDesc::resident_chunks());
  const auto report = tess::plan_operations(world, ops);

  ASSERT_NE(ops.operation(handle), nullptr);
  ASSERT_EQ(report.operations().size(), 1u);
  const auto queued_source = ops.operation(handle)->source;
  const auto reported_source = report.operations()[0].source;

  EXPECT_GT(queued_source.line(), 0u);
  EXPECT_GT(reported_source.line(), 0u);
  EXPECT_EQ(queued_source.line(), reported_source.line());
  EXPECT_NE(
      std::string_view{queued_source.file_name()}.find("tess_queued_test.cc"),
      std::string_view::npos);
  EXPECT_NE(std::string_view{queued_source.function_name()}.find(
                "update_from_helper"),
            std::string_view::npos);
}

TEST(TessQueued, InspectingQueuedAndPlannedOperationsDoesNotAllocate) {
  World world;
  tess::FrameOps ops;

  (void)ops.update_field(tess::DomainDesc::resident_chunks(),
                         tess::WritePolicy::ReadOnly);
  const auto report = tess::plan_operations(world, ops);

  allocation_count.store(0, std::memory_order_relaxed);
  count_allocations.store(true, std::memory_order_relaxed);

  const auto queued = ops.operations();
  const auto* operation = ops.operation(tess::OpHandle{0});
  const auto reports = report.operations();
  const auto planned = report.plan().operations();

  count_allocations.store(false, std::memory_order_relaxed);

  ASSERT_NE(operation, nullptr);
  EXPECT_EQ(queued.size(), 1u);
  EXPECT_EQ(operation->id, (tess::OpId{0}));
  EXPECT_EQ(reports.size(), 1u);
  EXPECT_EQ(planned.size(), 1u);
  EXPECT_EQ(allocation_count.load(std::memory_order_relaxed), 0);
}

}  // namespace
