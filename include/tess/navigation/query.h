#pragma once

#include <tess/navigation/domain.h>
#include <tess/ops/async_work.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace tess {

/// Completed spatial outcome, independent of cooperative ticket lifecycle.
enum class NavigationOutcome : std::uint8_t {
  Found,
  NoRoute,
  Indeterminate,
  InvalidInput,
  Overflow,
  CapacityExceeded,
};

/// Exactness concerns the represented immutable graph, never continuous motion.
enum class NavigationProof : std::uint8_t { ExactSnapshot, FeasibleOnly };

/// Checked logical capacity ceilings; allocator failure follows integration
/// policy.
struct NavigationLimits {
  std::size_t max_nodes = 65536;
  std::size_t max_frontier = 131072;
  std::size_t max_route_steps = 65536;
};

/// A destination with an already-known remaining cost in the common unit.
struct NavigationGoal {
  NavigationLocation location{};
  std::uint64_t cost = 0;
};

/// Shared guidance retains the original forward transition toward a
/// destination.
struct NavigationGuidance {
  std::uint64_t cost = 0;
  NavigationTransition next{};
  bool has_next = false;
};

/**
 * Owned route or shared guidance, retaining its complete immutable input lease.
 *
 * Exact freshness requires the same snapshot, conservatively including changes
 * outside selected edges. A historic result remains readable after replacement
 * but is not permission to execute it. Route steps retain each domain crossing.
 * Guidance follows earlier settled states, including on zero-cost plateaus.
 */
struct NavigationResult {
  NavigationOutcome outcome = NavigationOutcome::InvalidInput;
  NavigationProof proof = NavigationProof::ExactSnapshot;
  /// Total route cost; guidance costs are stored individually in its labels.
  std::uint64_t cost = 0;
  std::vector<NavigationTransition> route;
  std::map<NavigationLocation, NavigationGuidance> guidance;
  std::shared_ptr<const NavigationSnapshot> snapshot;
  std::uint64_t work_items = 0;
  [[nodiscard]] auto current_for(
      const std::shared_ptr<const NavigationSnapshot>& current) const noexcept
      -> bool {
    return snapshot && snapshot == current;
  }
  [[nodiscard]] auto guidance_at(NavigationLocation at) const noexcept
      -> const NavigationGuidance* {
    const auto found = guidance.find(at);
    return found == guidance.end() ? nullptr : &found->second;
  }
  /// Logical payload only; excludes map/allocator overhead and shared input
  /// data.
  [[nodiscard]] auto payload_bytes() const noexcept -> std::size_t {
    return route.size() * sizeof(NavigationTransition) +
           guidance.size() *
               sizeof(std::pair<const NavigationLocation, NavigationGuidance>);
  }
};

/**
 * Exact cooperative Dijkstra over retained native domains and connections.
 *
 * One item performs initialization, one seed, frontier pop, candidate probe,
 * reconstruction append, reversal swap, guidance-label export or publication.
 * Ordered lookup and heap work are logarithmic in the explicit capacity limits;
 * snapshot lookup is logarithmic in its prepared domain/attachment count.
 * Candidate degree never causes an unbounded loop within an item. Reservation
 * invokes allocators but initializes no array elements. Array relocation is
 * avoided by reserving ceilings. Allocator/callback time is outside the bound.
 * Construction moves inputs; explicit snapshot preparation and destruction are
 * not query steps. Cancellation is constant work; storage is released on query
 * destruction, outside advance. A work allowance is not a wall-clock guarantee.
 *
 * UINT64_MAX is reserved; representable costs are [0, UINT64_MAX-1]. A summed
 * overflow cannot beat any representable path; it is skipped and reported if no
 * representable route exists. Guidance refuses only while an overflow endpoint
 * lacks a finite alternative. Overflow placeholders count toward max_nodes.
 * A provider's overflow probe stops conservatively.
 * General allocation/callback exceptions propagate; after an exception discard
 * this query (no retry guarantee). All access is externally synchronized.
 *
 * To use ResumableWorkQueue, submit this address-stable object with a
 * shared_ptr<const NavigationResult> result slot. Queue cancellation does not
 * release its borrowed continuation; retain it until the ticket is retired.
 * Negative spatial outcomes are completed results, not failed queue callbacks.
 */
class NavigationQuery {
 public:
  NavigationQuery(std::shared_ptr<const NavigationSnapshot> snapshot,
                  NavigationLocation start, NavigationLocation goal,
                  NavigationLimits limits = {})
      : snapshot_(std::move(snapshot)),
        start_(start),
        goal_(goal),
        limits_(limits),
        goals_{{start, 0}} {}
  /// Shared guidance toward one final destination.
  NavigationQuery(std::shared_ptr<const NavigationSnapshot> snapshot,
                  NavigationLocation goal, NavigationLimits limits = {})
      : snapshot_(std::move(snapshot)),
        goal_(goal),
        limits_(limits),
        goals_{{goal, 0}},
        reverse_(true) {}
  /// Shared guidance with multiple destinations and explicit boundary costs.
  NavigationQuery(std::shared_ptr<const NavigationSnapshot> snapshot,
                  std::vector<NavigationGoal> goals,
                  NavigationLimits limits = {})
      : snapshot_(std::move(snapshot)),
        limits_(limits),
        goals_(std::move(goals)),
        reverse_(true) {}
  NavigationQuery(const NavigationQuery&) = delete;
  NavigationQuery& operator=(const NavigationQuery&) = delete;
  NavigationQuery(NavigationQuery&&) = delete;
  NavigationQuery& operator=(NavigationQuery&&) = delete;

  [[nodiscard]] auto state() const noexcept -> AsyncResultState {
    return state_;
  }
  [[nodiscard]] auto result() const noexcept -> const NavigationResult* {
    return state_ == AsyncResultState::Ready ? result_.get() : nullptr;
  }
  [[nodiscard]] auto retained_result() const noexcept
      -> std::shared_ptr<const NavigationResult> {
    return state_ == AsyncResultState::Ready ? result_ : nullptr;
  }
  void cancel() noexcept {
    if (state_ == AsyncResultState::Pending)
      state_ = AsyncResultState::Cancelled;
  }
  void supersede() noexcept {
    if (state_ == AsyncResultState::Pending)
      state_ = AsyncResultState::Superseded;
  }
  /// Call before advancing/publication when the host replaces any snapshot
  /// input.
  void require_snapshot(
      const std::shared_ptr<const NavigationSnapshot>& current) noexcept {
    if (snapshot_ != current && (state_ == AsyncResultState::Pending ||
                                 state_ == AsyncResultState::Ready))
      state_ = AsyncResultState::Stale;
  }
  auto advance(AsyncWorkBudget budget) -> AsyncWorkStep {
    std::uint32_t done = 0;
    while (done < budget.max_items && state_ == AsyncResultState::Pending) {
      ++done;
      ++work_;
      step();
    }
    auto phase = AsyncStepState::Pending;
    if (state_ == AsyncResultState::Ready)
      phase = AsyncStepState::Ready;
    else if (state_ == AsyncResultState::Stale)
      phase = AsyncStepState::Stale;
    else if (state_ != AsyncResultState::Pending)
      phase = AsyncStepState::Failed;
    return {phase, done, AsyncVersion{snapshot_ ? snapshot_->profile() : 0}};
  }
  /// Constant-work publication into an empty or already-identical ownership
  /// slot. Other slots return Failed with zero work, leaving query/output
  /// untouched. Release previous output explicitly outside the budgeted
  /// callback.
  auto operator()(AsyncWorkBudget budget,
                  std::shared_ptr<const NavigationResult>& output)
      -> AsyncWorkStep {
    const bool empty = !output && output.use_count() == 0;
    if (!empty &&
        (output.get() != result_.get() || output.owner_before(result_) ||
         result_.owner_before(output)))
      return {AsyncStepState::Failed, 0,
              AsyncVersion{snapshot_ ? snapshot_->profile() : 0}};
    const auto progress = advance(budget);
    if (state_ == AsyncResultState::Ready && empty) output = result_;
    return progress;
  }

 private:
  struct Label {
    std::uint64_t cost = 0;
    NavigationTransition parent{};
    bool has_parent = false;
    bool settled = false;
  };
  struct Frontier {
    std::uint64_t cost;
    NavigationLocation at;
    auto operator>(const Frontier& other) const noexcept -> bool {
      return cost != other.cost ? cost > other.cost : at > other.at;
    }
  };
  struct Greater {
    auto operator()(const Frontier& a, const Frontier& b) const noexcept
        -> bool {
      return a > b;
    }
  };
  enum class Phase {
    Init,
    Seed,
    Pop,
    Edges,
    Reconstruct,
    Reverse,
    Export,
    Publish
  };
  void finish(NavigationOutcome outcome) noexcept {
    outcome_ = outcome;
    phase_ = Phase::Publish;
  }
  auto push(NavigationLocation at, std::uint64_t cost) -> bool {
    if (frontier_.size() == limits_.max_frontier) {
      finish(NavigationOutcome::CapacityExceeded);
      return false;
    }
    frontier_.push_back({cost, at});
    std::push_heap(frontier_.begin(), frontier_.end(), Greater{});
    return true;
  }
  auto legal(NavigationLocation at) -> bool {
    const auto availability = snapshot_->availability(at);
    switch (availability) {
      case NavigationAvailability::Legal:
        return true;
      case NavigationAvailability::Unavailable:
        unknown_ = true;
        return false;
      case NavigationAvailability::Blocked:
        return false;
      case NavigationAvailability::Overflow:
        finish(NavigationOutcome::Overflow);
        return false;
      case NavigationAvailability::Invalid:
        finish(NavigationOutcome::InvalidInput);
        return false;
    }
    return false;
  }
  void step() {
    switch (phase_) {
      case Phase::Init: {
        result_ = std::make_shared<NavigationResult>();
        if (!snapshot_ || !snapshot_->valid() || goals_.empty() ||
            (!reverse_ &&
             (!snapshot_->contains(start_) || !snapshot_->contains(goal_)))) {
          finish(NavigationOutcome::InvalidInput);
          return;
        }
        unknown_ = !snapshot_->complete();
        if (limits_.max_nodes == 0 || limits_.max_frontier == 0 ||
            limits_.max_frontier > frontier_.max_size() ||
            limits_.max_route_steps > route_.max_size() ||
            limits_.max_nodes > labels_.max_size()) {
          finish(NavigationOutcome::CapacityExceeded);
          return;
        }
        frontier_.reserve(limits_.max_frontier);
        if (!reverse_) route_.reserve(limits_.max_route_steps);
        phase_ = Phase::Seed;
        return;
      }
      case Phase::Seed: {
        if (seed_ == goals_.size()) {
          phase_ = Phase::Pop;
          return;
        }
        const auto seed = goals_[seed_++];
        if (!snapshot_->contains(seed.location) || seed.cost == infinity_) {
          finish(NavigationOutcome::InvalidInput);
          return;
        }
        if (!legal(seed.location)) {
          if (phase_ != Phase::Publish && !reverse_)
            finish(unknown_ ? NavigationOutcome::Indeterminate
                            : NavigationOutcome::NoRoute);
          return;
        }
        auto found = labels_.find(seed.location);
        if (found != labels_.end() && found->second.cost <= seed.cost) return;
        if (found == labels_.end() && labels_.size() == limits_.max_nodes) {
          finish(NavigationOutcome::CapacityExceeded);
          return;
        }
        if (!push(seed.location, seed.cost)) return;
        labels_[seed.location] = Label{seed.cost, {}, false, false};
        return;
      }
      case Phase::Pop: {
        if (frontier_.empty()) {
          if (unresolved_overflow_ != 0) {
            finish(NavigationOutcome::Overflow);
            return;
          }
          if (reverse_ && labels_.empty()) {
            finish(unknown_ ? NavigationOutcome::Indeterminate
                            : NavigationOutcome::NoRoute);
            return;
          }
          if (reverse_) {
            export_ = labels_.begin();
            phase_ = Phase::Export;
          } else
            finish(unknown_ ? NavigationOutcome::Indeterminate
                            : NavigationOutcome::NoRoute);
          return;
        }
        std::pop_heap(frontier_.begin(), frontier_.end(), Greater{});
        const auto current = frontier_.back();
        frontier_.pop_back();
        auto& label = labels_.at(current.at);
        if (label.settled || label.cost != current.cost) return;
        label.settled = true;
        active_ = current.at;
        active_cost_ = label.cost;
        if (!reverse_ && active_ == goal_) {
          route_cost_ = label.cost;
          reconstruction_ = goal_;
          phase_ = Phase::Reconstruct;
          return;
        }
        cursor_ = 0;
        degree_ = snapshot_->degree(active_, reverse_);
        phase_ = Phase::Edges;
        return;
      }
      case Phase::Edges: {
        if (cursor_ == degree_) {
          phase_ = Phase::Pop;
          return;
        }
        const auto edge = snapshot_->edge(active_, cursor_++, reverse_);
        if ((!reverse_ && edge.from != active_) ||
            (reverse_ && edge.to != active_) ||
            !snapshot_->contains(edge.from) || !snapshot_->contains(edge.to) ||
            edge.cost == infinity_) {
          finish(NavigationOutcome::InvalidInput);
          return;
        }
        switch (edge.availability) {
          case NavigationAvailability::Blocked:
            return;
          case NavigationAvailability::Unavailable:
            unknown_ = true;
            return;
          case NavigationAvailability::Invalid:
            finish(NavigationOutcome::InvalidInput);
            return;
          case NavigationAvailability::Overflow:
            finish(NavigationOutcome::Overflow);
            return;
          case NavigationAvailability::Legal:
            break;
        }
        const auto next = reverse_ ? edge.from : edge.to;
        if (!legal(next)) return;
        auto found = labels_.find(next);
        if (edge.cost >= infinity_ - active_cost_) {
          // Any finite label dominates this candidate. Remember only unseen
          // endpoints, so a later finite alternative can discharge overflow.
          if (found != labels_.end()) return;
          if (labels_.size() == limits_.max_nodes) {
            finish(NavigationOutcome::CapacityExceeded);
            return;
          }
          labels_.emplace(next, Label{infinity_, {}, false, false});
          ++unresolved_overflow_;
          return;
        }
        const auto cost = active_cost_ + edge.cost;
        if (found != labels_.end() &&
            (found->second.settled || found->second.cost <= cost))
          return;
        if (found == labels_.end() && labels_.size() == limits_.max_nodes) {
          finish(NavigationOutcome::CapacityExceeded);
          return;
        }
        if (!push(next, cost)) return;
        if (found != labels_.end() && found->second.cost == infinity_)
          --unresolved_overflow_;
        labels_[next] = Label{cost, edge, true, false};
        return;
      }
      case Phase::Reconstruct: {
        if (reconstruction_ == start_) {
          reverse_cursor_ = 0;
          phase_ = Phase::Reverse;
          return;
        }
        const auto& label = labels_.at(reconstruction_);
        if (!label.has_parent || route_.size() == limits_.max_route_steps) {
          finish(NavigationOutcome::CapacityExceeded);
          return;
        }
        route_.push_back(label.parent);
        reconstruction_ = label.parent.from;
        return;
      }
      case Phase::Reverse: {
        if (reverse_cursor_ < route_.size() / 2) {
          std::swap(route_[reverse_cursor_],
                    route_[route_.size() - 1 - reverse_cursor_]);
          ++reverse_cursor_;
        } else
          finish(NavigationOutcome::Found);
        return;
      }
      case Phase::Export: {
        if (export_ == labels_.end()) {
          finish(NavigationOutcome::Found);
          return;
        }
        const auto& [at, label] = *export_++;
        guidance_.emplace(
            at, NavigationGuidance{label.cost, label.parent, label.has_parent});
        return;
      }
      case Phase::Publish: {
        result_->outcome = outcome_;
        result_->proof = unknown_ ? NavigationProof::FeasibleOnly
                                  : NavigationProof::ExactSnapshot;
        result_->snapshot = snapshot_;
        result_->work_items = work_;
        if (outcome_ == NavigationOutcome::Found) {
          result_->cost = route_cost_;
          result_->route = std::move(route_);
          result_->guidance = std::move(guidance_);
        }
        state_ = AsyncResultState::Ready;
        return;
      }
    }
  }
  static constexpr auto infinity_ = std::numeric_limits<std::uint64_t>::max();
  std::shared_ptr<const NavigationSnapshot> snapshot_;
  NavigationLocation start_{}, goal_{};
  NavigationLimits limits_;
  std::vector<NavigationGoal> goals_;
  bool reverse_ = false;
  AsyncResultState state_ = AsyncResultState::Pending;
  Phase phase_ = Phase::Init;
  NavigationOutcome outcome_ = NavigationOutcome::InvalidInput;
  std::shared_ptr<NavigationResult> result_;
  std::map<NavigationLocation, Label> labels_;
  std::map<NavigationLocation, Label>::const_iterator export_{};
  std::vector<Frontier> frontier_;
  std::vector<NavigationTransition> route_;
  std::map<NavigationLocation, NavigationGuidance> guidance_;
  NavigationLocation active_{}, reconstruction_{};
  std::uint64_t active_cost_ = 0, route_cost_ = 0, work_ = 0;
  std::size_t seed_ = 0, cursor_ = 0, degree_ = 0, reverse_cursor_ = 0;
  std::size_t unresolved_overflow_ = 0;
  bool unknown_ = false;
};

/// Synchronous driver over exactly the same continuation and capacity contract.
inline auto navigation_route(std::shared_ptr<const NavigationSnapshot> snapshot,
                             NavigationLocation start, NavigationLocation goal,
                             NavigationLimits limits = {})
    -> std::shared_ptr<const NavigationResult> {
  NavigationQuery query(std::move(snapshot), start, goal, limits);
  while (query.state() == AsyncResultState::Pending) query.advance({1024});
  return query.retained_result();
}
}  // namespace tess
