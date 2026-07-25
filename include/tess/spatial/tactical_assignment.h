#pragma once

#include <tess/core/shape.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <vector>

namespace tess {

/// One caller-owned subject seeking a tactical candidate.
struct TacticalRequest {
  std::uint64_t requester = 0;
  Coord3 origin{};
  std::uint32_t priority = 0;
};

/// One caller-owned position/resource with bounded concurrent capacity.
struct TacticalCandidate {
  std::uint64_t candidate = 0;
  Coord3 position{};
  std::uint32_t capacity = 1;
};

/// Feasibility and lower-is-better caller score for one request/candidate pair.
struct TacticalScore {
  bool feasible = false;
  std::int64_t value = 0;
};

/// One result aligned with the corresponding input request.
struct TacticalAssignment {
  std::uint64_t requester = 0;
  std::uint64_t candidate = 0;
  Coord3 position{};
  std::int64_t score = 0;
  bool assigned = false;
};

/// Aggregate outcome for one greedy tactical assignment pass.
enum class TacticalAssignmentStatus : std::uint8_t {
  Complete,
  Partial,
  InvalidInput,
};

/// Scratch-borrowing result from `assign_tactical_candidates_greedy`.
struct TacticalAssignmentResult {
  TacticalAssignmentStatus status = TacticalAssignmentStatus::Complete;
  std::size_t assigned_count = 0;
  std::span<const TacticalAssignment> assignments;
};

/// Reusable storage for deterministic tactical assignment.
class TacticalAssignmentScratch {
 public:
  void reserve(std::size_t request_count, std::size_t candidate_count) {
    request_order_.reserve(request_count);
    candidate_order_.reserve(candidate_count);
    remaining_capacity_.reserve(candidate_count);
    assignments_.reserve(request_count);
  }

 private:
  template <typename ScoreFn>
  friend auto assign_tactical_candidates_greedy(
      std::span<const TacticalRequest> requests,
      std::span<const TacticalCandidate> candidates, ScoreFn&& score,
      TacticalAssignmentScratch& scratch) -> TacticalAssignmentResult;

  std::vector<std::size_t> request_order_;
  std::vector<std::size_t> candidate_order_;
  std::vector<std::uint32_t> remaining_capacity_;
  std::vector<TacticalAssignment> assignments_;
};

/// Greedily assigns scarce candidates in priority and stable-ID order.
template <typename ScoreFn>
auto assign_tactical_candidates_greedy(
    std::span<const TacticalRequest> requests,
    std::span<const TacticalCandidate> candidates, ScoreFn&& score,
    TacticalAssignmentScratch& scratch) -> TacticalAssignmentResult {
  scratch.assignments_.clear();
  scratch.request_order_.resize(requests.size());
  for (std::size_t i = 0; i < requests.size(); ++i) {
    scratch.request_order_[i] = i;
  }
  std::sort(scratch.request_order_.begin(), scratch.request_order_.end(),
            [&](std::size_t lhs, std::size_t rhs) {
              return requests[lhs].requester < requests[rhs].requester;
            });
  for (std::size_t i = 1; i < scratch.request_order_.size(); ++i) {
    if (requests[scratch.request_order_[i - 1]].requester ==
        requests[scratch.request_order_[i]].requester) {
      return {TacticalAssignmentStatus::InvalidInput, 0, {}};
    }
  }
  scratch.candidate_order_.resize(candidates.size());
  for (std::size_t i = 0; i < candidates.size(); ++i) {
    scratch.candidate_order_[i] = i;
  }
  std::sort(scratch.candidate_order_.begin(), scratch.candidate_order_.end(),
            [&](std::size_t lhs, std::size_t rhs) {
              return candidates[lhs].candidate < candidates[rhs].candidate;
            });
  for (std::size_t i = 1; i < scratch.candidate_order_.size(); ++i) {
    if (candidates[scratch.candidate_order_[i - 1]].candidate ==
        candidates[scratch.candidate_order_[i]].candidate) {
      return {TacticalAssignmentStatus::InvalidInput, 0, {}};
    }
  }

  scratch.assignments_.resize(requests.size());
  for (std::size_t i = 0; i < requests.size(); ++i) {
    scratch.assignments_[i].requester = requests[i].requester;
  }
  std::sort(scratch.request_order_.begin(), scratch.request_order_.end(),
            [&](std::size_t lhs, std::size_t rhs) {
              if (requests[lhs].priority != requests[rhs].priority) {
                return requests[lhs].priority > requests[rhs].priority;
              }
              return requests[lhs].requester < requests[rhs].requester;
            });
  scratch.remaining_capacity_.resize(candidates.size());
  for (std::size_t i = 0; i < candidates.size(); ++i) {
    scratch.remaining_capacity_[i] = candidates[i].capacity;
  }

  auto assigned_count = std::size_t{0};
  for (const auto request_index : scratch.request_order_) {
    auto best = candidates.size();
    auto best_score = std::numeric_limits<std::int64_t>::max();
    for (std::size_t candidate_index = 0; candidate_index < candidates.size();
         ++candidate_index) {
      if (scratch.remaining_capacity_[candidate_index] == 0) {
        continue;
      }
      const auto candidate_score = static_cast<TacticalScore>(std::invoke(
          score, requests[request_index], candidates[candidate_index]));
      if (!candidate_score.feasible) {
        continue;
      }
      if (best == candidates.size() || candidate_score.value < best_score ||
          (candidate_score.value == best_score &&
           candidates[candidate_index].candidate <
               candidates[best].candidate)) {
        best = candidate_index;
        best_score = candidate_score.value;
      }
    }
    if (best == candidates.size()) {
      continue;
    }
    --scratch.remaining_capacity_[best];
    scratch.assignments_[request_index] = TacticalAssignment{
        requests[request_index].requester,
        candidates[best].candidate,
        candidates[best].position,
        best_score,
        true,
    };
    ++assigned_count;
  }

  const auto status = assigned_count == requests.size()
                          ? TacticalAssignmentStatus::Complete
                          : TacticalAssignmentStatus::Partial;
  return {status, assigned_count,
          std::span<const TacticalAssignment>{scratch.assignments_}};
}

}  // namespace tess
