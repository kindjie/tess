#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstdint>
#include <new>
#include <vector>

#include "path_test_util.h"

namespace {

struct DiagTerrainTag {};

constexpr std::uint32_t DiagDirtyTerrain = 1u << 0u;

using DiagTopDown2D =
    tess::Shape<tess::Extent3{64, 32, 1}, tess::Extent3{32, 16, 1}>;
using DiagSchema =
    tess::FieldSchema<tess::Field<DiagTerrainTag, std::uint16_t>>;
using DiagWorld = tess::AlwaysResidentWorld<DiagTopDown2D, DiagSchema>;

int increment(int& value) {
  ++value;
  return value;
}

TEST(TessDiagnostics, EnabledWhenCompileDefinitionIsPresent) {
  static_assert(TESS_DIAGNOSTICS_ENABLED == 1);
  EXPECT_EQ(TESS_DIAGNOSTICS_ENABLED, 1);
}

TEST(TessDiagnostics, EnabledMacrosEvaluateArgumentsOnce) {
  int value = 0;
  int counter = 0;

  TESS_DIAGNOSTIC_ONLY(++value);
  EXPECT_EQ(value, 1);

  TESS_DIAGNOSTIC_INC(value);
  EXPECT_EQ(value, 2);

  TESS_DIAGNOSTIC_ADD(counter, increment(value));
  EXPECT_EQ(value, 3);
  EXPECT_EQ(counter, 3);
}

TEST(TessDiagnostics, ScopedPathCountersRecordGenericEvents) {
  tess::diagnostics::PathCounters counters;
  {
    tess::diagnostics::ScopedPathCounters scope{counters};

    TESS_DIAG_EVENT_VALUE(path_clear, 7);
    TESS_DIAG_EVENT(path_heap_push);
    TESS_DIAG_EVENT(path_heap_pop);
    TESS_DIAG_EVENT(path_neighbor_candidate);
    TESS_DIAG_EVENT(path_passability_check);
    TESS_DIAG_EVENT(path_cost_read);
    TESS_DIAG_EVENT(path_neighbor_blocked);
    TESS_DIAG_EVENT(path_relax_attempt);
    TESS_DIAG_EVENT(path_relax_success);
    TESS_DIAG_EVENT(path_touch_node);
    TESS_DIAG_EVENT(path_heuristic);
    TESS_DIAG_EVENT(path_reconstruct_node);
    TESS_DIAG_EVENT_VALUE(path_skip_pop, true);
    TESS_DIAG_EVENT_VALUE(path_skip_pop, false);
  }

  EXPECT_EQ(counters.scratch_clear_calls, 1u);
  EXPECT_EQ(counters.scratch_clear_nodes, 7u);
  EXPECT_EQ(counters.heap_pushes, 1u);
  EXPECT_EQ(counters.heap_pops, 1u);
  EXPECT_EQ(counters.neighbor_candidates, 1u);
  EXPECT_EQ(counters.passability_checks, 1u);
  EXPECT_EQ(counters.cost_reads, 1u);
  EXPECT_EQ(counters.blocked_neighbors, 1u);
  EXPECT_EQ(counters.relax_attempts, 1u);
  EXPECT_EQ(counters.relax_successes, 1u);
  EXPECT_EQ(counters.touched_nodes, 1u);
  EXPECT_EQ(counters.heuristic_calls, 1u);
  EXPECT_EQ(counters.reconstructed_nodes, 1u);
  EXPECT_EQ(counters.closed_pops, 1u);
  EXPECT_EQ(counters.stale_pops, 1u);
}

TEST(TessDiagnostics, ScopedQueuedPhaseCountersRecordGenericEvents) {
  tess::diagnostics::QueuedPhaseCounters counters;
  {
    tess::diagnostics::ScopedQueuedPhaseCounters scope{counters};

    TESS_DIAG_EVENT_VALUE(queued_phase_execute, 4);
    TESS_DIAG_EVENT(queued_phase_invalid_range);
    TESS_DIAG_EVENT(queued_phase_failure);
    TESS_DIAG_EVENT_VALUE(queued_partitioned_phase, 3);
    TESS_DIAG_EVENT_VALUE(queued_scoped_thread_dispatch, 2);
    TESS_DIAG_EVENT_VALUE(queued_dirty_collect, 5);
    TESS_DIAG_EVENT_VALUE(queued_dirty_merge, 2);
  }

  EXPECT_EQ(counters.phase_calls, 1u);
  EXPECT_EQ(counters.phase_operations, 4u);
  EXPECT_EQ(counters.phase_invalid_ranges, 1u);
  EXPECT_EQ(counters.phase_failures, 1u);
  EXPECT_EQ(counters.partitioned_phase_calls, 1u);
  EXPECT_EQ(counters.dirty_partitions, 3u);
  EXPECT_EQ(counters.scoped_thread_calls, 1u);
  EXPECT_EQ(counters.scoped_thread_workers, 2u);
  EXPECT_EQ(counters.dirty_records_collected, 5u);
  EXPECT_EQ(counters.dirty_chunks_merged, 2u);

  counters.reset();
  EXPECT_EQ(counters.phase_calls, 0u);
  EXPECT_EQ(counters.dirty_chunks_merged, 0u);
}

TEST(TessDiagnostics, ScopedQueuedPhaseCountersObservePartitionedExecution) {
  DiagWorld world;
  tess::FrameOps ops;
  tess::PlannedPhaseExecutionScratch scratch;
  tess::ScopedThreadPhaseExecutor executor{2};
  tess::diagnostics::QueuedPhaseCounters counters;
  constexpr auto writes_terrain = tess::FieldAccessDesc{
      0,
      DiagDirtyTerrain,
      DiagDirtyTerrain,
  };
  const std::vector<tess::ChunkKey> first_keys{tess::ChunkKey{0}};
  const std::vector<tess::ChunkKey> second_keys{tess::ChunkKey{1}};

  (void)ops.update_field(tess::DomainDesc::explicit_chunks(first_keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(second_keys),
                         writes_terrain, tess::WritePolicy::UniquePerChunk);
  const auto report = tess::plan_operations(world, ops);
  ASSERT_TRUE(report.ok());
  const auto phases = tess::plan_parallel_execution_phases(report.plan());
  ASSERT_TRUE(phases.ok());
  ASSERT_EQ(phases.phases().size(), 1u);

  {
    tess::diagnostics::ScopedQueuedPhaseCounters scope{counters};
    const auto result = tess::execute_phase_partitioned_dirty_with<
        tess::WritePolicy::UniquePerChunk>(
        executor, world, report.plan(), phases.phases()[0], scratch,
        [](auto view) {
          auto terrain = view.template field_span<DiagTerrainTag>();
          terrain[0] = static_cast<std::uint16_t>(view.key().value + 3);
        });
    EXPECT_EQ(result.status, tess::PlannedExecutionStatus::Executed);
    EXPECT_EQ(result.chunk_count, 2u);
    EXPECT_EQ(tess::merge_planned_dirty(world, scratch), 2u);
  }

  EXPECT_EQ(counters.phase_calls, 1u);
  EXPECT_EQ(counters.phase_operations, 2u);
  EXPECT_EQ(counters.phase_invalid_ranges, 0u);
  EXPECT_EQ(counters.phase_failures, 0u);
  EXPECT_EQ(counters.partitioned_phase_calls, 1u);
  EXPECT_EQ(counters.dirty_partitions, 2u);
  EXPECT_EQ(counters.scoped_thread_calls, 1u);
  EXPECT_EQ(counters.scoped_thread_workers, 2u);
  EXPECT_EQ(counters.dirty_records_collected, 2u);
  EXPECT_EQ(counters.dirty_chunks_merged, 2u);
}

// Mutation guard: the serpentine mazes must be answered by the real A*
// heap loop, never by a pre-A* fast path. Fast paths never push onto the
// open list, so heap_pushes > 0 pins the heap loop as the producer of the
// Found result. If a future fast path learns to answer these fixtures,
// this test fails and the fixtures must be strengthened.
TEST(TessDiagnostics, SerpentineMazeReachesUnitHeapSearch) {
  tess::AlwaysResidentWorld<tess_test::SerpTopDown2D, tess_test::SerpSchema>
      world;
  const auto endpoints = tess_test::build_serpentine_topdown(world);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::diagnostics::PathCounters counters;
  {
    tess::diagnostics::ScopedPathCounters scope{counters};
    const auto result =
        tess::astar_path<decltype(world), tess_test::SerpPassableTag>(
            world, tess::PathRequest{endpoints.start, endpoints.goal}, scratch);
    ASSERT_EQ(result.status, tess::PathStatus::Found);
    EXPECT_GT(result.expanded_nodes, result.path.size());
  }

  EXPECT_GT(counters.heap_pushes, 0u);
  EXPECT_GT(counters.heap_pops, 0u);
}

TEST(TessDiagnostics, SerpentineMazeReachesWeightedHeapSearch) {
  tess::AlwaysResidentWorld<tess_test::SerpTopDown2D, tess_test::SerpSchema>
      world;
  const auto endpoints = tess_test::build_serpentine_topdown(world);
  tess_test::fill_cost(world, 1);

  tess::PathScratch scratch;
  scratch.reserve_nodes(64);
  tess::diagnostics::PathCounters counters;
  {
    tess::diagnostics::ScopedPathCounters scope{counters};
    const auto result =
        tess::weighted_astar_path<decltype(world), tess_test::SerpPassableTag,
                                  tess_test::SerpCostTag>(
            world, tess::PathRequest{endpoints.start, endpoints.goal}, scratch);
    ASSERT_EQ(result.status, tess::PathStatus::Found);
    EXPECT_GT(result.expanded_nodes, result.path.size());
  }

  EXPECT_GT(counters.heap_pushes, 0u);
  EXPECT_GT(counters.heap_pops, 0u);
}

TEST(TessDiagnostics, ScopedAllocationCountersRecordGlobalNewAndDelete) {
  tess::diagnostics::AllocationCounters counters;
  {
    tess::diagnostics::ScopedAllocationCounters scope{counters};

    void* storage = ::operator new(sizeof(int));
    auto* value = new (storage) int{7};
    EXPECT_EQ(*value, 7);
    ::operator delete(storage);
  }

  EXPECT_GE(counters.allocations, 1u);
  EXPECT_GE(counters.allocation_bytes, sizeof(int));
  EXPECT_GE(counters.deallocations, 1u);
}

}  // namespace
