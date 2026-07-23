#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <array>
#include <cstdint>

#include "allocation_counter.h"

namespace {

using tess::LocalMoveDecisionStatus;

auto always_enter(const tess::LocalMoveRequest&, const tess::LocalMoveOption&)
    -> bool {
  return true;
}

TEST(TessLocalCoordination, PriorityClaimsContestedDestination) {
  const std::array requests{
      tess::LocalMoveRequest{.agent = 10,
                             .from = {0, 0, 0},
                             .priority = 1,
                             .option_offset = 0,
                             .option_count = 1},
      tess::LocalMoveRequest{.agent = 20,
                             .from = {2, 0, 0},
                             .priority = 2,
                             .option_offset = 1,
                             .option_count = 1},
  };
  const std::array options{
      tess::LocalMoveOption{.to = {1, 0, 0}},
      tess::LocalMoveOption{.to = {1, 0, 0}},
  };
  tess::LocalCoordinationScratch scratch;

  const auto result =
      tess::resolve_local_moves(requests, options, always_enter, scratch);

  EXPECT_EQ(result.status, tess::LocalCoordinationStatus::Partial);
  EXPECT_EQ(result.reserved_count, 1u);
  EXPECT_EQ(result.decisions[0].status, LocalMoveDecisionStatus::Wait);
  EXPECT_EQ(result.decisions[1].status, LocalMoveDecisionStatus::Reserved);
  ASSERT_EQ(result.congestion.size(), 1u);
  EXPECT_EQ(result.congestion[0].demand, 2u);
  EXPECT_EQ(result.congestion[0].reserved, 1u);
}

TEST(TessLocalCoordination, RankedAlternativeSpreadsContention) {
  const std::array requests{
      tess::LocalMoveRequest{.agent = 10,
                             .from = {0, 0, 0},
                             .priority = 1,
                             .option_offset = 0,
                             .option_count = 2},
      tess::LocalMoveRequest{.agent = 20,
                             .from = {2, 0, 0},
                             .priority = 1,
                             .option_offset = 2,
                             .option_count = 2},
  };
  const std::array options{
      tess::LocalMoveOption{.to = {1, 0, 0}, .preference = 0},
      tess::LocalMoveOption{.to = {0, 1, 0}, .preference = 5},
      tess::LocalMoveOption{.to = {1, 0, 0}, .preference = 0},
      tess::LocalMoveOption{.to = {2, 1, 0}, .preference = 5},
  };
  tess::LocalCoordinationScratch scratch;

  const auto result =
      tess::resolve_local_moves(requests, options, always_enter, scratch);

  EXPECT_EQ(result.status, tess::LocalCoordinationStatus::Complete);
  EXPECT_EQ(result.decisions[0].to, (tess::Coord3{1, 0, 0}));
  EXPECT_EQ(result.decisions[1].to, (tess::Coord3{2, 1, 0}));
}

TEST(TessLocalCoordination, CallerRejectsOccupiedOrIllegalOptions) {
  const std::array requests{
      tess::LocalMoveRequest{
          .agent = 7, .from = {0, 0, 0}, .option_offset = 0, .option_count = 2},
  };
  const std::array options{
      tess::LocalMoveOption{.to = {1, 0, 0}, .preference = 0},
      tess::LocalMoveOption{.to = {0, 1, 0}, .preference = 1},
  };
  tess::LocalCoordinationScratch scratch;

  const auto result = tess::resolve_local_moves(
      requests, options,
      [](const auto&, const auto& option) {
        return option.to != tess::Coord3{1, 0, 0};
      },
      scratch);

  EXPECT_EQ(result.status, tess::LocalCoordinationStatus::Complete);
  EXPECT_EQ(result.decisions[0].to, (tess::Coord3{0, 1, 0}));
  ASSERT_EQ(result.congestion.size(), 1u);
  EXPECT_EQ(result.congestion[0].coord, (tess::Coord3{0, 1, 0}));
}

TEST(TessLocalCoordination, StableIdsMakeChoicesInputOrderInvariant) {
  const std::array ordered_requests{
      tess::LocalMoveRequest{
          .agent = 10, .option_offset = 0, .option_count = 1},
      tess::LocalMoveRequest{
          .agent = 20, .option_offset = 1, .option_count = 1},
  };
  const std::array reversed_requests{
      tess::LocalMoveRequest{
          .agent = 20, .option_offset = 1, .option_count = 1},
      tess::LocalMoveRequest{
          .agent = 10, .option_offset = 0, .option_count = 1},
  };
  const std::array options{
      tess::LocalMoveOption{.to = {1, 0, 0}},
      tess::LocalMoveOption{.to = {1, 0, 0}},
  };
  tess::LocalCoordinationScratch first_scratch;
  tess::LocalCoordinationScratch second_scratch;

  const auto first = tess::resolve_local_moves(ordered_requests, options,
                                               always_enter, first_scratch);
  const auto second = tess::resolve_local_moves(reversed_requests, options,
                                                always_enter, second_scratch);

  const auto first_winner =
      std::ranges::find_if(first.decisions, [](const auto& decision) {
        return decision.status == LocalMoveDecisionStatus::Reserved;
      });
  const auto second_winner =
      std::ranges::find_if(second.decisions, [](const auto& decision) {
        return decision.status == LocalMoveDecisionStatus::Reserved;
      });
  ASSERT_NE(first_winner, first.decisions.end());
  ASSERT_NE(second_winner, second.decisions.end());
  EXPECT_EQ(first_winner->agent, 10u);
  EXPECT_EQ(second_winner->agent, 10u);
}

TEST(TessLocalCoordination, RejectsInvalidRangesAndDuplicateIds) {
  const std::array options{tess::LocalMoveOption{.to = {1, 0, 0}}};
  const std::array invalid_range{
      tess::LocalMoveRequest{.agent = 1, .option_offset = 1, .option_count = 1},
  };
  const std::array duplicate_ids{
      tess::LocalMoveRequest{.agent = 1, .option_offset = 0, .option_count = 1},
      tess::LocalMoveRequest{.agent = 1, .option_offset = 0, .option_count = 1},
  };
  const std::array overlapping_ranges{
      tess::LocalMoveRequest{.agent = 1, .option_offset = 0, .option_count = 1},
      tess::LocalMoveRequest{.agent = 2, .option_offset = 0, .option_count = 1},
  };
  tess::LocalCoordinationScratch scratch;

  EXPECT_EQ(
      tess::resolve_local_moves(invalid_range, options, always_enter, scratch)
          .status,
      tess::LocalCoordinationStatus::InvalidInput);
  EXPECT_EQ(
      tess::resolve_local_moves(duplicate_ids, options, always_enter, scratch)
          .status,
      tess::LocalCoordinationStatus::InvalidInput);
  EXPECT_EQ(tess::resolve_local_moves(overlapping_ranges, options, always_enter,
                                      scratch)
                .status,
            tess::LocalCoordinationStatus::InvalidInput);
}

TEST(TessLocalCoordination, WarmResolutionDoesNotAllocate) {
  const std::array requests{
      tess::LocalMoveRequest{.agent = 1, .option_offset = 0, .option_count = 2},
      tess::LocalMoveRequest{.agent = 2, .option_offset = 2, .option_count = 2},
      tess::LocalMoveRequest{.agent = 3, .option_offset = 4, .option_count = 2},
  };
  const std::array options{
      tess::LocalMoveOption{.to = {1, 0, 0}},
      tess::LocalMoveOption{.to = {0, 1, 0}, .preference = 1},
      tess::LocalMoveOption{.to = {2, 0, 0}},
      tess::LocalMoveOption{.to = {2, 1, 0}, .preference = 1},
      tess::LocalMoveOption{.to = {3, 0, 0}},
      tess::LocalMoveOption{.to = {3, 1, 0}, .preference = 1},
  };
  tess::LocalCoordinationScratch scratch;
  scratch.reserve(requests.size(), options.size());
  (void)tess::resolve_local_moves(requests, options, always_enter, scratch);

  tess_test::ScopedAllocationCounter counter;
  for (int i = 0; i < 100; ++i) {
    const auto result =
        tess::resolve_local_moves(requests, options, always_enter, scratch);
    EXPECT_EQ(result.status, tess::LocalCoordinationStatus::Complete);
  }
  EXPECT_EQ(counter.count(), 0u);
}

}  // namespace
