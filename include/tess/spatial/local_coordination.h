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

/// One mover and its range of caller-ranked local destination options.
struct LocalMoveRequest {
  std::uint64_t agent = 0;
  Coord3 from{};
  std::uint32_t priority = 0;
  std::size_t option_offset = 0;
  std::size_t option_count = 0;
};

/// One caller-generated local destination; lower preference wins.
struct LocalMoveOption {
  Coord3 to{};
  std::int32_t preference = 0;
};

/// Whether one local mover reserved a destination or must wait.
enum class LocalMoveDecisionStatus : std::uint8_t {
  Wait,
  Reserved,
};

/// One local-resolution decision aligned with its input request.
struct LocalMoveDecision {
  std::uint64_t agent = 0;
  Coord3 from{};
  Coord3 to{};
  std::int32_t preference = 0;
  LocalMoveDecisionStatus status = LocalMoveDecisionStatus::Wait;
};

/// Feasible demand and accepted reservations for one destination.
struct LocalCongestion {
  Coord3 coord{};
  std::uint32_t demand = 0;
  std::uint32_t reserved = 0;
};

/// Aggregate outcome of deterministic local move coordination.
enum class LocalCoordinationStatus : std::uint8_t {
  Complete,
  Partial,
  InvalidInput,
};

/// Scratch-borrowing result from `resolve_local_moves`.
struct LocalCoordinationResult {
  LocalCoordinationStatus status = LocalCoordinationStatus::Complete;
  std::size_t reserved_count = 0;
  std::span<const LocalMoveDecision> decisions;
  std::span<const LocalCongestion> congestion;
};

/// Reusable storage for deterministic local move coordination.
class LocalCoordinationScratch {
 public:
  void reserve(std::size_t request_count, std::size_t option_count) {
    request_order_.reserve(request_count);
    option_owned_.reserve(option_count);
    option_feasible_.reserve(option_count);
    demand_coords_.reserve(option_count);
    congestion_.reserve(option_count);
    claimed_.reserve(request_count);
    decisions_.reserve(request_count);
  }

 private:
  template <typename CanEnterFn>
  friend auto resolve_local_moves(std::span<const LocalMoveRequest> requests,
                                  std::span<const LocalMoveOption> options,
                                  CanEnterFn&& can_enter,
                                  LocalCoordinationScratch& scratch)
      -> LocalCoordinationResult;

  std::vector<std::size_t> request_order_;
  std::vector<std::uint8_t> option_owned_;
  std::vector<std::uint8_t> option_feasible_;
  std::vector<Coord3> demand_coords_;
  std::vector<LocalCongestion> congestion_;
  std::vector<Coord3> claimed_;
  std::vector<LocalMoveDecision> decisions_;
};

namespace detail {

[[nodiscard]] inline auto local_coord_less(Coord3 lhs, Coord3 rhs) noexcept
    -> bool {
  if (lhs.x != rhs.x) {
    return lhs.x < rhs.x;
  }
  if (lhs.y != rhs.y) {
    return lhs.y < rhs.y;
  }
  return lhs.z < rhs.z;
}

}  // namespace detail

/// Resolves caller-filtered local destinations by priority and stable agent ID.
template <typename CanEnterFn>
auto resolve_local_moves(std::span<const LocalMoveRequest> requests,
                         std::span<const LocalMoveOption> options,
                         CanEnterFn&& can_enter,
                         LocalCoordinationScratch& scratch)
    -> LocalCoordinationResult {
  scratch.request_order_.clear();
  scratch.option_owned_.assign(options.size(), 0);
  scratch.option_feasible_.assign(options.size(), 0);
  scratch.demand_coords_.clear();
  scratch.congestion_.clear();
  scratch.claimed_.clear();
  scratch.decisions_.clear();

  scratch.request_order_.resize(requests.size());
  for (std::size_t i = 0; i < requests.size(); ++i) {
    const auto& request = requests[i];
    if (request.option_offset > options.size() ||
        request.option_count > options.size() - request.option_offset) {
      return {LocalCoordinationStatus::InvalidInput, 0, {}, {}};
    }
    scratch.request_order_[i] = i;
    for (std::size_t j = 0; j < request.option_count; ++j) {
      const auto option_index = request.option_offset + j;
      if (scratch.option_owned_[option_index] != 0) {
        return {LocalCoordinationStatus::InvalidInput, 0, {}, {}};
      }
      scratch.option_owned_[option_index] = 1;
    }
  }

  std::sort(scratch.request_order_.begin(), scratch.request_order_.end(),
            [&](std::size_t lhs, std::size_t rhs) {
              return requests[lhs].agent < requests[rhs].agent;
            });
  for (std::size_t i = 1; i < scratch.request_order_.size(); ++i) {
    if (requests[scratch.request_order_[i - 1]].agent ==
        requests[scratch.request_order_[i]].agent) {
      return {LocalCoordinationStatus::InvalidInput, 0, {}, {}};
    }
  }

  scratch.decisions_.resize(requests.size());
  for (std::size_t i = 0; i < requests.size(); ++i) {
    const auto& request = requests[i];
    scratch.decisions_[i] = LocalMoveDecision{
        request.agent,
        request.from,
        request.from,
        0,
        LocalMoveDecisionStatus::Wait,
    };
    for (std::size_t j = 0; j < request.option_count; ++j) {
      const auto option_index = request.option_offset + j;
      const auto& option = options[option_index];
      if (!static_cast<bool>(std::invoke(can_enter, request, option))) {
        continue;
      }
      scratch.option_feasible_[option_index] = 1;
      auto duplicate = false;
      for (std::size_t previous = 0; previous < j; ++previous) {
        const auto previous_index = request.option_offset + previous;
        if (scratch.option_feasible_[previous_index] != 0 &&
            options[previous_index].to == option.to) {
          duplicate = true;
          break;
        }
      }
      if (!duplicate) {
        scratch.demand_coords_.push_back(option.to);
      }
    }
  }

  std::sort(scratch.demand_coords_.begin(), scratch.demand_coords_.end(),
            detail::local_coord_less);
  for (const auto coord : scratch.demand_coords_) {
    if (scratch.congestion_.empty() ||
        scratch.congestion_.back().coord != coord) {
      scratch.congestion_.push_back(LocalCongestion{coord, 1, 0});
    } else if (scratch.congestion_.back().demand !=
               std::numeric_limits<std::uint32_t>::max()) {
      ++scratch.congestion_.back().demand;
    }
  }

  std::sort(scratch.request_order_.begin(), scratch.request_order_.end(),
            [&](std::size_t lhs, std::size_t rhs) {
              if (requests[lhs].priority != requests[rhs].priority) {
                return requests[lhs].priority > requests[rhs].priority;
              }
              return requests[lhs].agent < requests[rhs].agent;
            });

  auto reserved_count = std::size_t{};
  for (const auto request_index : scratch.request_order_) {
    const auto& request = requests[request_index];
    auto best = options.size();
    for (std::size_t j = 0; j < request.option_count; ++j) {
      const auto option_index = request.option_offset + j;
      const auto& option = options[option_index];
      if (scratch.option_feasible_[option_index] == 0 ||
          std::binary_search(scratch.claimed_.begin(), scratch.claimed_.end(),
                             option.to, detail::local_coord_less)) {
        continue;
      }
      if (best == options.size() ||
          option.preference < options[best].preference ||
          (option.preference == options[best].preference &&
           detail::local_coord_less(option.to, options[best].to))) {
        best = option_index;
      }
    }
    if (best == options.size()) {
      continue;
    }
    const auto& chosen = options[best];
    const auto claimed =
        std::lower_bound(scratch.claimed_.begin(), scratch.claimed_.end(),
                         chosen.to, detail::local_coord_less);
    scratch.claimed_.insert(claimed, chosen.to);
    scratch.decisions_[request_index] = LocalMoveDecision{
        request.agent,
        request.from,
        chosen.to,
        chosen.preference,
        LocalMoveDecisionStatus::Reserved,
    };
    const auto congestion =
        std::lower_bound(scratch.congestion_.begin(), scratch.congestion_.end(),
                         chosen.to, [](const LocalCongestion& lhs, Coord3 rhs) {
                           return detail::local_coord_less(lhs.coord, rhs);
                         });
    if (congestion != scratch.congestion_.end() &&
        congestion->coord == chosen.to &&
        congestion->reserved != std::numeric_limits<std::uint32_t>::max()) {
      ++congestion->reserved;
    }
    ++reserved_count;
  }

  const auto status = reserved_count == requests.size()
                          ? LocalCoordinationStatus::Complete
                          : LocalCoordinationStatus::Partial;
  return {status, reserved_count,
          std::span<const LocalMoveDecision>{scratch.decisions_},
          std::span<const LocalCongestion>{scratch.congestion_}};
}

}  // namespace tess
