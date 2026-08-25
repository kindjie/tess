#pragma once

#include <cstdint>
#include <memory>

namespace tess::examples::web_colony {

inline constexpr int width = 128;
inline constexpr int height = 128;
inline constexpr int max_agents = 1024;

/**
 * Owns the deterministic colony simulation and its presentation snapshots.
 *
 * Logical agents remain on integer tiles. The previous/current arrays and
 * interpolation alpha are presentation data for renderers; feeding fractional
 * coordinates back into the simulation would violate movement invariants.
 */
class ColonyModel {
 public:
  explicit ColonyModel(int agent_count);
  ~ColonyModel();

  ColonyModel(const ColonyModel&) = delete;
  auto operator=(const ColonyModel&) -> ColonyModel& = delete;
  ColonyModel(ColonyModel&&) = delete;
  auto operator=(ColonyModel&&) -> ColonyModel& = delete;

  [[nodiscard]] auto set_wall(int x, int y, bool built) -> bool;
  [[nodiscard]] auto queue_wall(int x, int y) -> bool {
    return set_wall(x, y, true);
  }
  void set_replan_each_tick(bool enabled) noexcept;
  void set_spread_congested_routes(bool enabled) noexcept;
  /// Selects the congestion-pricing accounting policy (0 disables; the
  /// policy registry lives with the implementation). Prices are ordinary
  /// cost-field writes through the versioned edit channel; disabling
  /// restores every tile to unit cost.
  void set_congestion_pricing(int policy) noexcept;
  [[nodiscard]] auto tick(double dt_seconds) -> double;
  [[nodiscard]] auto relaunch() -> int;

  [[nodiscard]] auto leg() const noexcept -> int;
  [[nodiscard]] auto completed_legs() const noexcept -> int;
  [[nodiscard]] auto aborted_legs() const noexcept -> int;
  [[nodiscard]] auto agent_count() const noexcept -> int;
  [[nodiscard]] auto arrived() const -> int;
  [[nodiscard]] auto unreachable() const -> int;
  [[nodiscard]] auto crowd_blocked() const -> int;
  [[nodiscard]] auto turnaround_ready() const -> bool;
  [[nodiscard]] auto stalled_ticks() const noexcept -> int;
  [[nodiscard]] auto planning_pending() const noexcept -> int;
  [[nodiscard]] auto advanced_last_tick() const noexcept -> int;
  [[nodiscard]] auto movement_waits_last_tick() const noexcept -> int;

  [[nodiscard]] auto tiles() const noexcept -> const std::uint8_t*;
  [[nodiscard]] auto current_agents() const noexcept -> const std::int16_t*;
  [[nodiscard]] auto previous_agents() const noexcept -> const std::int16_t*;
  [[nodiscard]] auto interpolation_alpha() const noexcept -> double;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  friend struct ColonyModelNativeAccess;
};

}  // namespace tess::examples::web_colony
