#pragma once

#include <cstdint>
#include <memory>

namespace tess::examples::web_tower {

// A tower rather than a slab: agents route through genuinely three
// dimensional space, and the only way between floors is a stairwell.
// Kept small enough to run on a phone.
inline constexpr int width = 48;
inline constexpr int depth = 48;
inline constexpr int floors = 6;
inline constexpr int max_agents = 96;

/**
 * Owns the deterministic tower simulation and its presentation snapshots.
 *
 * Agents occupy integer tiles in three dimensions. The previous/current
 * arrays and the interpolation alpha are presentation data: feeding
 * interpolated coordinates back into the simulation would violate
 * movement invariants, exactly as in the two-dimensional demo.
 */
class TowerModel {
 public:
  explicit TowerModel(int agent_count);
  ~TowerModel();

  TowerModel(const TowerModel&) = delete;
  auto operator=(const TowerModel&) -> TowerModel& = delete;
  TowerModel(TowerModel&&) = delete;
  auto operator=(TowerModel&&) = delete;

  /// Opens or closes one stairwell. A closed stairwell stays walkable
  /// and becomes expensive, so routes divert to another while anyone
  /// already on the stairs can still walk out. Returns false only for
  /// an out-of-range index.
  [[nodiscard]] auto set_stairwell(int index, bool open) -> bool;
  [[nodiscard]] auto stairwell_count() const noexcept -> int;
  [[nodiscard]] auto stairwell_open(int index) const noexcept -> bool;
  /// Stairwell footprint, for drawing and for hit-testing a tap.
  [[nodiscard]] auto stairwell_x(int index) const noexcept -> int;
  [[nodiscard]] auto stairwell_y(int index) const noexcept -> int;
  [[nodiscard]] auto stairwell_floor(int index) const noexcept -> int;

  [[nodiscard]] auto tick(double dt_seconds) -> double;
  [[nodiscard]] auto relaunch() -> int;

  [[nodiscard]] auto agent_count() const noexcept -> int;
  [[nodiscard]] auto arrived() const -> int;
  [[nodiscard]] auto unreachable() const -> int;
  [[nodiscard]] auto crowd_blocked() const -> int;
  [[nodiscard]] auto turnaround_ready() const -> bool;
  [[nodiscard]] auto leg() const noexcept -> int;
  [[nodiscard]] auto completed_legs() const noexcept -> int;
  [[nodiscard]] auto planning_pending() const noexcept -> int;
  [[nodiscard]] auto advanced_last_tick() const noexcept -> int;
  [[nodiscard]] auto stalled_ticks() const noexcept -> int;
  /// Agents whose route currently crosses a floor boundary.
  [[nodiscard]] auto climbing() const noexcept -> int;

  /// Position and goal of the i-th agent, for diagnostics. Returns
  /// false when the index is out of range.
  [[nodiscard]] auto agent_debug(int i, int* px, int* py, int* pz, int* gx,
                                 int* gy, int* gz, int* phase) const -> bool;

  /// Passability per tile, indexed z*width*depth + y*width + x.
  [[nodiscard]] auto tiles() const noexcept -> const std::uint8_t*;
  /// Agent positions as (x, y, z) triples; -1 marks an inactive agent.
  [[nodiscard]] auto current_agents() const noexcept -> const std::int16_t*;
  [[nodiscard]] auto previous_agents() const noexcept -> const std::int16_t*;
  [[nodiscard]] auto interpolation_alpha() const noexcept -> double;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace tess::examples::web_tower
