#pragma once

#include <cstdint>
#include <memory>

namespace tess::examples::web_traffic {

inline constexpr int traffic_width = 1024;
inline constexpr int traffic_height = 512;
inline constexpr int traffic_agents = 1024;

enum class TrafficScenario : std::uint8_t {
  Aligned = 0,
  ShuffledCrossing = 1,
  Funnel = 2,
  MultiGate = 3,
};

[[nodiscard]] auto scenario_name(TrafficScenario scenario) noexcept -> const
    char*;

class TrafficModel {
 public:
  explicit TrafficModel(TrafficScenario scenario);
  ~TrafficModel();

  TrafficModel(const TrafficModel&) = delete;
  auto operator=(const TrafficModel&) -> TrafficModel& = delete;
  TrafficModel(TrafficModel&&) = delete;
  auto operator=(TrafficModel&&) -> TrafficModel& = delete;

  void reset(TrafficScenario scenario);
  [[nodiscard]] auto tick(double dt_seconds) -> double;

  [[nodiscard]] auto scenario() const noexcept -> TrafficScenario;
  [[nodiscard]] auto terrain() const noexcept -> const std::uint8_t*;
  [[nodiscard]] auto current_agents() const noexcept -> const std::int16_t*;
  [[nodiscard]] auto previous_agents() const noexcept -> const std::int16_t*;
  [[nodiscard]] auto interpolation_alpha() const noexcept -> double;
  [[nodiscard]] auto planning_us() const noexcept -> double;
  [[nodiscard]] auto planning_queries_last_tick() const noexcept -> int;
  [[nodiscard]] auto guided_queries_last_tick() const noexcept -> int;
  [[nodiscard]] auto planning_touched_nodes_last_tick() const noexcept
      -> std::uint64_t;
  [[nodiscard]] auto planning_heap_pops_last_tick() const noexcept
      -> std::uint64_t;
  [[nodiscard]] auto planning_neighbor_candidates_last_tick() const noexcept
      -> std::uint64_t;
  [[nodiscard]] auto planning_passability_checks_last_tick() const noexcept
      -> std::uint64_t;
  [[nodiscard]] auto planning_reconstructed_nodes_last_tick() const noexcept
      -> std::uint64_t;
  [[nodiscard]] auto fixed_ticks_last_call() const noexcept -> int;
  [[nodiscard]] auto planning_pending() const noexcept -> int;
  [[nodiscard]] auto advanced_last_tick() const noexcept -> int;
  [[nodiscard]] auto movement_waits_last_tick() const noexcept -> int;
  [[nodiscard]] auto blocked_agents() const noexcept -> int;
  [[nodiscard]] auto arrived_agents() const noexcept -> int;
  [[nodiscard]] auto one_progress_streak() const noexcept -> int;
  [[nodiscard]] auto longest_one_progress_streak() const noexcept -> int;
  [[nodiscard]] auto max_planning_queries() const noexcept -> int;
  [[nodiscard]] auto agent_state_hash() const noexcept -> std::uint64_t;
  [[nodiscard]] auto validate_planner() const -> bool;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace tess::examples::web_traffic
