#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstdint>
#include <utility>

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
    EXPECT_TRUE(run.outcomes[i].optimal_certified);
    EXPECT_EQ(run.outcomes[i].status, run.dense_status[i]);
    EXPECT_EQ(run.outcomes[i].cost, run.dense_cost[i]);
  }
}

TEST(TessSparseStream, StreamAndRetryConvergesToTheDenseOptimum) {
  // Section 3.1's contract: stream and retry to convergence, and the
  // converged result equals the dense reference. Convergence needs a
  // budget that can hold what the search needs; the loop keeps
  // streaming past the first definitive answer until it can certify
  // that nothing further could change it.
  const auto run = Stream(config_at(1.0)).run();

  ASSERT_EQ(run.outcomes.size(), run.dense_status.size());
  for (std::size_t i = 0; i < run.outcomes.size(); ++i) {
    SCOPED_TRACE(i);
    ASSERT_TRUE(run.outcomes[i].optimal_certified);
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
    if (!outcome.definitive) {
      // A loop that gave up must say so rather than report a
      // definitive status.
      EXPECT_EQ(outcome.status, tess::PathStatus::Indeterminate);
      continue;
    }
    if (outcome.optimal_certified) {
      // Certified answers are the optimum, not a bound.
      EXPECT_EQ(outcome.status, run.dense_status[i]);
      EXPECT_EQ(outcome.cost, run.dense_cost[i]);
    } else if (outcome.status == tess::PathStatus::Found) {
      EXPECT_EQ(run.dense_status[i], tess::PathStatus::Found)
          << "streamed a path the dense world says does not exist";
      EXPECT_GE(outcome.cost, run.dense_cost[i])
          << "streamed cost is below the true optimum";
    } else if (outcome.status == tess::PathStatus::NoPath) {
      // Note: the room-and-corridor terrain is one connected
      // component and endpoints are chosen passable, so the dense
      // reference is always Found and this branch does not currently
      // fire. It is kept as a guard rather than as coverage; an
      // enclosed-region fixture would be needed to exercise it.
      EXPECT_NE(run.dense_status[i], tess::PathStatus::Found)
          << "reported NoPath where the dense world finds a route";
    } else {
      // Endpoints are chosen passable and in bounds, so nothing else
      // is a legitimate definitive status here.
      ADD_FAILURE() << "unexpected definitive status "
                    << static_cast<int>(outcome.status);
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

TEST(TessSparseStream, StoppingAtTheFirstAnswerYieldsAnUpperBound) {
  auto config = config_at(1.0);
  config.stop_at_first_definitive = true;

  const auto run = Stream(config).run();

  // The finding, witnessed rather than asserted in the abstract: even
  // with a budget large enough to hold the whole world, a loop that
  // stops at the first definitive answer reports strictly longer
  // routes than the optimum, because the search returns on reaching
  // the goal while non-resident chunks were still skipped. Streaming
  // on to certification (the test above) removes exactly this gap.
  std::size_t strictly_above = 0;
  for (std::size_t i = 0; i < run.outcomes.size(); ++i) {
    SCOPED_TRACE(i);
    const auto& outcome = run.outcomes[i];
    ASSERT_TRUE(outcome.definitive);
    EXPECT_FALSE(outcome.optimal_certified);
    EXPECT_GE(outcome.cost, run.dense_cost[i]);
    if (outcome.cost > run.dense_cost[i]) {
      ++strictly_above;
    }
  }
  EXPECT_GT(strictly_above, 0u)
      << "no request demonstrated the upper-bound behaviour";
}

// Behavioural golden for the streaming axis. Budget fraction changes
// outcomes in ways no invariant captures, and the relationship is not
// monotone -- a mid budget can stream more rounds than a tight one
// because more requests keep making progress -- so the numbers are
// pinned rather than compared.
TEST(TessSparseStream, StreamingGolden) {
  const auto full = Stream(config_at(1.0)).run();
  const auto quarter = Stream(config_at(0.25)).run();
  const auto tight = Stream(config_at(0.05)).run();

  const auto counts = [](const sparse::StreamRun& run) {
    std::pair<std::size_t, std::size_t> result{0, 0};
    for (const auto& outcome : run.outcomes) {
      result.first += outcome.definitive ? 1u : 0u;
      result.second += outcome.optimal_certified ? 1u : 0u;
    }
    return result;
  };

  EXPECT_EQ(full.capacity_chunks, 64u);
  EXPECT_EQ(counts(full), std::make_pair(std::size_t{12}, std::size_t{12}));
  EXPECT_EQ(full.total_stream_steps, 28u);

  EXPECT_EQ(quarter.capacity_chunks, 16u);
  EXPECT_EQ(counts(quarter), std::make_pair(std::size_t{9}, std::size_t{0}));
  EXPECT_EQ(quarter.total_stream_steps, 204u);

  // Three resident chunks cannot hold a corridor, so almost nothing
  // reaches a definitive answer: the loop stops cleanly instead.
  EXPECT_EQ(tight.capacity_chunks, 3u);
  EXPECT_EQ(counts(tight), std::make_pair(std::size_t{1}, std::size_t{0}));
  EXPECT_EQ(tight.total_stream_steps, 264u);

  // The cost of certainty: stopping at the first answer is far
  // cheaper in streaming rounds and never certifies.
  auto fast = config_at(1.0);
  fast.stop_at_first_definitive = true;
  const auto fast_run = Stream(fast).run();
  EXPECT_EQ(counts(fast_run), std::make_pair(std::size_t{12}, std::size_t{0}));
  EXPECT_EQ(fast_run.total_stream_steps, 8u);
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
  // The accounting must describe the world, not just itself: the
  // outstanding count is exactly what is resident, and the high-water
  // mark never exceeds the budget.
  EXPECT_EQ(run.residency_flow.outstanding_current, run.final_resident);
  EXPECT_LE(run.residency_flow.outstanding_high_water, run.capacity_chunks);
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
    EXPECT_EQ(first.outcomes[i].definitive, second.outcomes[i].definitive);
    EXPECT_EQ(first.outcomes[i].optimal_certified,
              second.outcomes[i].optimal_certified);
    EXPECT_EQ(first.outcomes[i].chunks_streamed,
              second.outcomes[i].chunks_streamed);
  }
}

}  // namespace
