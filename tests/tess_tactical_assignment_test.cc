#include <gtest/gtest.h>
#include <tess/tess.h>

#include <array>
#include <cstdint>

#include "allocation_counter.h"

namespace {

auto distance_score(const tess::TacticalRequest& request,
                    const tess::TacticalCandidate& candidate)
    -> tess::TacticalScore {
  const auto dx = request.origin.x - candidate.position.x;
  const auto dy = request.origin.y - candidate.position.y;
  const auto dz = request.origin.z - candidate.position.z;
  return {true,
          (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) + (dz < 0 ? -dz : dz)};
}

TEST(TessTacticalAssignment, PriorityOrderClaimsScarceBestCandidates) {
  const std::array requests{
      tess::TacticalRequest{
          .requester = 10, .origin = {0, 0, 0}, .priority = 1},
      tess::TacticalRequest{
          .requester = 20, .origin = {8, 0, 0}, .priority = 2},
  };
  const std::array candidates{
      tess::TacticalCandidate{.candidate = 100, .position = {3, 0, 0}},
      tess::TacticalCandidate{.candidate = 200, .position = {9, 0, 0}},
  };
  tess::TacticalAssignmentScratch scratch;

  const auto result = tess::assign_tactical_candidates_greedy(
      requests, candidates, distance_score, scratch);

  EXPECT_EQ(result.status, tess::TacticalAssignmentStatus::Complete);
  ASSERT_EQ(result.assignments.size(), 2u);
  EXPECT_EQ(result.assignments[0].requester, 10u);
  EXPECT_EQ(result.assignments[0].candidate, 100u);
  EXPECT_EQ(result.assignments[1].requester, 20u);
  EXPECT_EQ(result.assignments[1].candidate, 200u);
}

TEST(TessTacticalAssignment, CapacityAllowsSeveralRequestsPerCandidate) {
  const std::array requests{
      tess::TacticalRequest{.requester = 1},
      tess::TacticalRequest{.requester = 2},
      tess::TacticalRequest{.requester = 3},
  };
  const std::array candidates{
      tess::TacticalCandidate{.candidate = 7, .capacity = 2},
  };
  tess::TacticalAssignmentScratch scratch;

  const auto result = tess::assign_tactical_candidates_greedy(
      requests, candidates,
      [](const auto&, const auto&) { return tess::TacticalScore{true, 5}; },
      scratch);

  EXPECT_EQ(result.status, tess::TacticalAssignmentStatus::Partial);
  EXPECT_EQ(result.assigned_count, 2u);
  EXPECT_TRUE(result.assignments[0].assigned);
  EXPECT_TRUE(result.assignments[1].assigned);
  EXPECT_FALSE(result.assignments[2].assigned);
}

TEST(TessTacticalAssignment, InfeasiblePairsRemainUnassigned) {
  const std::array requests{
      tess::TacticalRequest{.requester = 1, .origin = {0, 0, 0}},
      tess::TacticalRequest{.requester = 2, .origin = {10, 0, 0}},
  };
  const std::array candidates{
      tess::TacticalCandidate{
          .candidate = 9, .position = {1, 0, 0}, .capacity = 2},
  };
  tess::TacticalAssignmentScratch scratch;

  const auto result = tess::assign_tactical_candidates_greedy(
      requests, candidates,
      [](const auto& request, const auto&) {
        return tess::TacticalScore{request.origin.x < 5, 1};
      },
      scratch);

  EXPECT_EQ(result.status, tess::TacticalAssignmentStatus::Partial);
  EXPECT_TRUE(result.assignments[0].assigned);
  EXPECT_FALSE(result.assignments[1].assigned);
}

TEST(TessTacticalAssignment, TieBreaksByStableIdsNotInputOrder) {
  const std::array requests{
      tess::TacticalRequest{.requester = 20, .priority = 1},
      tess::TacticalRequest{.requester = 10, .priority = 1},
  };
  const std::array candidates{
      tess::TacticalCandidate{.candidate = 200},
      tess::TacticalCandidate{.candidate = 100},
  };
  tess::TacticalAssignmentScratch scratch;

  const auto result = tess::assign_tactical_candidates_greedy(
      requests, candidates,
      [](const auto&, const auto&) { return tess::TacticalScore{true, 0}; },
      scratch);

  ASSERT_EQ(result.assignments.size(), 2u);
  EXPECT_EQ(result.assignments[1].candidate, 100u);
  EXPECT_EQ(result.assignments[0].candidate, 200u);
}

TEST(TessTacticalAssignment, RejectsDuplicateStableIds) {
  const std::array requests{
      tess::TacticalRequest{.requester = 1},
      tess::TacticalRequest{.requester = 1},
  };
  const std::array candidates{
      tess::TacticalCandidate{.candidate = 9},
  };
  tess::TacticalAssignmentScratch scratch;

  const auto result = tess::assign_tactical_candidates_greedy(
      requests, candidates, distance_score, scratch);

  EXPECT_EQ(result.status, tess::TacticalAssignmentStatus::InvalidInput);
  EXPECT_TRUE(result.assignments.empty());

  const std::array distinct_requests{
      tess::TacticalRequest{.requester = 1},
  };
  const std::array duplicate_candidates{
      tess::TacticalCandidate{.candidate = 9},
      tess::TacticalCandidate{.candidate = 9},
  };
  const auto duplicate_candidate_result =
      tess::assign_tactical_candidates_greedy(
          distinct_requests, duplicate_candidates, distance_score, scratch);

  EXPECT_EQ(duplicate_candidate_result.status,
            tess::TacticalAssignmentStatus::InvalidInput);
  EXPECT_TRUE(duplicate_candidate_result.assignments.empty());
}

TEST(TessTacticalAssignment, ReservedWarmExecutionDoesNotAllocate) {
  const std::array requests{
      tess::TacticalRequest{.requester = 1, .origin = {0, 0, 0}},
      tess::TacticalRequest{.requester = 2, .origin = {4, 0, 0}},
      tess::TacticalRequest{.requester = 3, .origin = {8, 0, 0}},
  };
  const std::array candidates{
      tess::TacticalCandidate{.candidate = 10, .position = {0, 1, 0}},
      tess::TacticalCandidate{.candidate = 20, .position = {4, 1, 0}},
      tess::TacticalCandidate{.candidate = 30, .position = {8, 1, 0}},
  };
  tess::TacticalAssignmentScratch scratch;
  scratch.reserve(requests.size(), candidates.size());
  (void)tess::assign_tactical_candidates_greedy(requests, candidates,
                                                distance_score, scratch);

  {
    tess_test::ScopedAllocationCounter counter;
    for (int i = 0; i < 100; ++i) {
      const auto result = tess::assign_tactical_candidates_greedy(
          requests, candidates, distance_score, scratch);
      EXPECT_EQ(result.status, tess::TacticalAssignmentStatus::Complete);
    }
    EXPECT_EQ(counter.count(), 0u);
  }
}

}  // namespace
