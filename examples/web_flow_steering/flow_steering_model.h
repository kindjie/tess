#pragma once

#include <cstdint>
#include <memory>

namespace tess::examples::web_flow_steering {

inline constexpr int width = 32;
inline constexpr int height = 24;
inline constexpr int max_agents = 8;

enum class AgentState : std::uint8_t {
  Moving = 0,
  AtGoal = 1,
  Unreachable = 2,
};

/** Owns the dense distance product and independent steering agents. */
class FlowSteeringModel {
 public:
  FlowSteeringModel();
  ~FlowSteeringModel();

  FlowSteeringModel(const FlowSteeringModel&) = delete;
  auto operator=(const FlowSteeringModel&) -> FlowSteeringModel& = delete;
  FlowSteeringModel(FlowSteeringModel&&) = delete;
  auto operator=(FlowSteeringModel&&) -> FlowSteeringModel& = delete;

  /// Restores the deterministic terrain, agents, and default goal.
  void reset();

  /// Rebuilds the distance product synchronously for one passable goal.
  [[nodiscard]] auto set_goal(int x, int y) -> bool;

  /// Advances every moving agent by at most one legal distance-label step.
  [[nodiscard]] auto tick() -> int;

  [[nodiscard]] auto goal_x() const noexcept -> int;
  [[nodiscard]] auto goal_y() const noexcept -> int;
  [[nodiscard]] auto agent_count() const noexcept -> int;
  [[nodiscard]] auto agent_x(int index) const noexcept -> int;
  [[nodiscard]] auto agent_y(int index) const noexcept -> int;
  [[nodiscard]] auto agent_state(int index) const noexcept -> AgentState;
  [[nodiscard]] auto tile_passable(int x, int y) const noexcept -> bool;
  [[nodiscard]] auto tile_distance(int x, int y) const noexcept
      -> std::uint32_t;
  [[nodiscard]] auto build_generation() const noexcept -> std::uint32_t;
  [[nodiscard]] auto step_count() const noexcept -> std::uint32_t;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace tess::examples::web_flow_steering
