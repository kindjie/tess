#pragma once

#include <cstdint>
#include <memory>

namespace tess::examples::web_congestion {

/**
 * The congestion-pricing laboratory: wraps the colony tutorial's
 * simulation unchanged and layers the pre-registered pricing policies
 * on top through the model's native seam, scoping replans with the
 * experimental route-crossing query. The colony demo stays a tutorial;
 * this model owns every pricing experiment, and the evidence screens
 * drive exactly this code path.
 *
 * Policy ids are the evidence vocabulary (issue #269): 0 off; 1 prox1,
 * 2 prox2, 3 self, 4 decay, 5 stalled, 6 demand, 7 queue; 8 peak1,
 * 9 cool, 10 queue2, 11 stallpeak, 12 stallcool, 13 peakcool; 14-22
 * the amendment-5 factorial; 23-28 the amendment-6 period/cap
 * configurations.
 */
class CongestionModel {
 public:
  explicit CongestionModel(int agent_count);
  ~CongestionModel();

  CongestionModel(const CongestionModel&) = delete;
  auto operator=(const CongestionModel&) -> CongestionModel& = delete;
  CongestionModel(CongestionModel&&) = delete;
  auto operator=(CongestionModel&&) = delete;

  [[nodiscard]] auto queue_wall(int x, int y) -> bool;
  [[nodiscard]] auto set_wall(int x, int y, bool built) -> bool;
  void set_spread_congested_routes(bool enabled) noexcept;
  void set_replan_each_tick(bool enabled) noexcept;
  /// Selects the pricing policy (0 disables and restores unit costs).
  void set_pricing_policy(int policy);
  /// Planning budget per fixed tick: n > 0 sets a static budget, 0
  /// restores the demo default (8), -1 selects the registered dynamic
  /// rule budget = min(32, max(8, pending / 16)), -2 drains the whole
  /// backlog every tick (unbounded), -3 selects the registered work
  /// budget: fit the request count to a 16,384 expanded-node target
  /// using last tick's measured nodes per search, clamped to [4, 64].
  void set_planning_budget(int mode);
  [[nodiscard]] auto pricing_policy() const noexcept -> int;

  [[nodiscard]] auto tick(double dt_seconds) -> double;
  [[nodiscard]] auto relaunch() -> int;

  [[nodiscard]] auto leg() const noexcept -> int;
  [[nodiscard]] auto completed_legs() const noexcept -> int;
  [[nodiscard]] auto aborted_legs() const noexcept -> int;
  [[nodiscard]] auto stalled_ticks() const noexcept -> int;
  [[nodiscard]] auto agent_count() const noexcept -> int;
  [[nodiscard]] auto arrived() const -> int;
  [[nodiscard]] auto unreachable() const -> int;
  [[nodiscard]] auto crowd_blocked() const -> int;
  [[nodiscard]] auto turnaround_ready() const noexcept -> bool;
  [[nodiscard]] auto planning_pending() const noexcept -> int;
  [[nodiscard]] auto advanced_last_tick() const noexcept -> int;
  [[nodiscard]] auto movement_waits_last_tick() const noexcept -> int;
  /// Agents asked to replan by the scoped selection since construction.
  [[nodiscard]] auto scoped_replans() const noexcept -> long long;
  /// Search nodes expanded by exact replans since construction.
  [[nodiscard]] auto expansions_total() const noexcept -> long long;
  /// Sum over ticks of the post-drain planning backlog (queue latency).
  [[nodiscard]] auto pending_integral() const noexcept -> long long;
  /// Enables amendment-10 replan-productivity measurement. Off by
  /// default: it fingerprints every retained route each tick, which is
  /// affordable for evidence runs but not free, so runs with it enabled
  /// must not be used for wall-time comparisons.
  void set_measure_productivity(bool enabled) noexcept;
  /// Searches the drain ran (denominator) and how many replaced an
  /// agent's route with a different one (numerator).
  [[nodiscard]] auto replans_drained() const noexcept -> long long;
  [[nodiscard]] auto replans_changed() const noexcept -> long long;
  /// Amendment-10b rescue counters: stalled agents that received a
  /// replaced route, and how many moved within the deadline.
  [[nodiscard]] auto rescue_armed() const noexcept -> long long;
  [[nodiscard]] auto rescue_success() const noexcept -> long long;

  [[nodiscard]] auto tiles() const noexcept -> const std::uint8_t*;
  [[nodiscard]] auto current_agents() const noexcept -> const std::int16_t*;
  [[nodiscard]] auto previous_agents() const noexcept -> const std::int16_t*;
  [[nodiscard]] auto interpolation_alpha() const noexcept -> double;
  /// Per-tile price snapshot (1..8) for the price-overlay rendering.
  [[nodiscard]] auto prices() const noexcept -> const std::uint8_t*;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace tess::examples::web_congestion
