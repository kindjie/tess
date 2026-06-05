#include <gtest/gtest.h>
#include <tess/tess.h>

#include <new>

namespace {

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
  EXPECT_EQ(counters.blocked_neighbors, 1u);
  EXPECT_EQ(counters.relax_attempts, 1u);
  EXPECT_EQ(counters.relax_successes, 1u);
  EXPECT_EQ(counters.touched_nodes, 1u);
  EXPECT_EQ(counters.heuristic_calls, 1u);
  EXPECT_EQ(counters.reconstructed_nodes, 1u);
  EXPECT_EQ(counters.closed_pops, 1u);
  EXPECT_EQ(counters.stale_pops, 1u);
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
