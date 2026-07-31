#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstdint>

#include "sparse_stream_harness.h"

namespace {

namespace sparse = tess_test::sparse;

// 256x256 in 32x32 chunks: 64 chunks, so a 25% budget holds 16 and a
// 5% budget holds 3 — enough contrast for the streaming axis while
// staying cheap enough for the PR tier.
using Shape256 =
    tess::Shape<tess::Extent3{256, 256, 1}, tess::Extent3{32, 32, 1}>;
using Stream = sparse::SparseStream<Shape256>;

auto config_at(double fraction) -> sparse::StreamConfig {
  sparse::StreamConfig config;
  config.budget_fraction = fraction;
  config.requests = 12;
  return config;
}

TEST(TessSparseStream, FullyResidentSparseWorldMatchesDense) {
  auto config = config_at(1.0);
  config.stream_all_before_search = true;

  const auto run = Stream(config).run();

  // Sparse storage must not change an answer: with every chunk
  // resident the sparse world agrees with the dense reference
  // exactly, status and cost.
  ASSERT_EQ(run.outcomes.size(), run.dense_status.size());
  for (std::size_t i = 0; i < run.outcomes.size(); ++i) {
    SCOPED_TRACE(i);
    EXPECT_TRUE(run.outcomes[i].converged);
    EXPECT_EQ(run.outcomes[i].status, run.dense_status[i]);
    EXPECT_EQ(run.outcomes[i].cost, run.dense_cost[i]);
  }
}

// The streaming contract, checked at every budget: an on-demand loop
// stops at the first definitive answer, which the library may return
// while non-resident chunks were skipped. That makes a streamed cost
// an UPPER BOUND on the true cost, never an under-estimate, and never
// a path where none exists.
void expect_streaming_is_sound(const sparse::StreamRun& run) {
  ASSERT_EQ(run.outcomes.size(), run.dense_status.size());
  for (std::size_t i = 0; i < run.outcomes.size(); ++i) {
    SCOPED_TRACE(i);
    const auto& outcome = run.outcomes[i];
    if (!outcome.converged) {
      // A loop that gave up must say so rather than report a
      // definitive status.
      EXPECT_EQ(outcome.status, tess::PathStatus::Indeterminate);
      continue;
    }
    if (outcome.status == tess::PathStatus::Found) {
      EXPECT_EQ(run.dense_status[i], tess::PathStatus::Found)
          << "streamed a path the dense world says does not exist";
      EXPECT_GE(outcome.cost, run.dense_cost[i])
          << "streamed cost is below the true optimum";
    } else if (outcome.status == tess::PathStatus::NoPath) {
      EXPECT_NE(run.dense_status[i], tess::PathStatus::Found)
          << "reported NoPath where the dense world finds a route";
    }
  }
}

TEST(TessSparseStream, StreamingIsSoundAtQuarterBudget) {
  const auto run = Stream(config_at(0.25)).run();

  expect_streaming_is_sound(run);
  EXPECT_GT(run.total_stream_steps, 0u);
}

TEST(TessSparseStream, StreamingIsSoundAtFivePercentBudget) {
  const auto run = Stream(config_at(0.05)).run();

  // Section 3.1's low-budget fraction. Convergence is not assumed
  // here: with three resident chunks the eviction policy can drop a
  // chunk the running search still needs. What must hold is that the
  // loop stops cleanly and never answers wrongly.
  expect_streaming_is_sound(run);
  EXPECT_GT(run.total_stream_steps, 0u);
}

TEST(TessSparseStream, ResidencyStaysWithinBudget) {
  for (const double fraction : {1.0, 0.25, 0.05}) {
    SCOPED_TRACE(fraction);
    const auto run = Stream(config_at(fraction)).run();

    EXPECT_TRUE(run.stayed_within_budget);
    EXPECT_LE(run.peak_resident, run.capacity_chunks);
  }
}

TEST(TessSparseStream, TighterBudgetsStreamHarderAndConvergeLess) {
  const auto quarter = Stream(config_at(0.25)).run();
  const auto tight = Stream(config_at(0.05)).run();

  const auto converged = [](const sparse::StreamRun& run) {
    std::size_t count = 0;
    for (const auto& outcome : run.outcomes) {
      count += outcome.converged ? 1u : 0u;
    }
    return count;
  };

  EXPECT_LT(tight.capacity_chunks, quarter.capacity_chunks);
  // The measured cost of a tight budget: more streaming rounds for
  // fewer answers. Pinned so a change in eviction or search behaviour
  // that quietly improves or degrades streaming shows up here.
  EXPECT_GT(tight.total_stream_steps, quarter.total_stream_steps);
  EXPECT_LT(converged(tight), converged(quarter));
}

TEST(TessSparseStream, ResidencyFlowIdentitiesHold) {
  const auto run = Stream(config_at(0.25)).run();

  // Section 3.3's identities applied to residency admission and
  // eviction. The sparse world has no accounting hooks of its own, so
  // the harness attributes them around its own ensure_resident calls;
  // the identities are the same.
  EXPECT_TRUE(run.residency_flow.admission_identity_holds())
      << "offered=" << run.residency_flow.offered
      << " admitted=" << run.residency_flow.admitted
      << " coalesced=" << run.residency_flow.coalesced_into_pending;
  EXPECT_TRUE(run.residency_flow.retention_identity_holds())
      << "admitted=" << run.residency_flow.admitted
      << " terminal=" << run.residency_flow.terminal()
      << " outstanding=" << run.residency_flow.outstanding_current;
  EXPECT_GT(run.residency_flow.admitted, 0u);
  // Outstanding is exactly what is resident, and a tight budget must
  // have displaced something.
  EXPECT_LE(run.residency_flow.outstanding_current, run.capacity_chunks);
  EXPECT_GT(run.residency_flow.dropped_after_admission, 0u);
}

TEST(TessSparseStream, RunsAreDeterministic) {
  const auto first = Stream(config_at(0.25)).run();
  const auto second = Stream(config_at(0.25)).run();

  ASSERT_EQ(first.outcomes.size(), second.outcomes.size());
  EXPECT_EQ(first.total_stream_steps, second.total_stream_steps);
  for (std::size_t i = 0; i < first.outcomes.size(); ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(first.outcomes[i].status, second.outcomes[i].status);
    EXPECT_EQ(first.outcomes[i].cost, second.outcomes[i].cost);
    EXPECT_EQ(first.outcomes[i].converged, second.outcomes[i].converged);
    EXPECT_EQ(first.outcomes[i].chunks_streamed,
              second.outcomes[i].chunks_streamed);
  }
}

}  // namespace
