#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

namespace tess::examples::pathfinding_strategies {

inline constexpr std::int32_t width = 16;
inline constexpr std::int32_t height = 16;
inline constexpr std::size_t request_capacity = 3;
inline constexpr std::size_t path_capacity =
    static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

enum class StrategyKind : std::int32_t {
  IndependentAstar = 0,
  ExactRouteCache = 1,
  WeightedBatch = 2,
  DistanceField = 3,
};

enum class BrowserPathStatus : std::int32_t {
  Found = 1,
  InvalidStart = 2,
  InvalidGoal = 3,
  NoPath = 4,
  CostOverflow = 5,
  Indeterminate = 6,
};

struct PathPoint {
  std::int32_t x = 0;
  std::int32_t y = 0;

  auto operator==(const PathPoint&) const -> bool = default;
};

struct RequestSnapshot {
  BrowserPathStatus status = BrowserPathStatus::NoPath;
  std::uint32_t cost = 0;
  std::uint32_t expanded_nodes = 0;
  std::uint32_t path_size = 0;
  std::array<PathPoint, path_capacity> path{};
};

struct StrategySnapshot {
  StrategyKind kind = StrategyKind::IndependentAstar;
  std::uint32_t request_count = 0;
  std::array<RequestSnapshot, request_capacity> requests{};
  std::uint32_t cache_hits = 0;
  std::uint32_t cache_misses = 0;
  std::uint32_t unique_goals = 0;
  std::uint32_t field_builds = 0;
  std::uint32_t astar_fallbacks = 0;
  std::uint32_t field_build_expansions = 0;
  std::uint32_t field_reached_nodes = 0;
};

class StrategyModel {
 public:
  StrategyModel();

  [[nodiscard]] auto valid() const noexcept -> bool { return valid_; }
  [[nodiscard]] auto copied_paths_valid() const noexcept -> bool;
  [[nodiscard]] auto strategy(std::int32_t strategy_index) const noexcept
      -> std::optional<std::reference_wrapper<const StrategySnapshot>>;
  [[nodiscard]] auto request(std::int32_t strategy_index,
                             std::int32_t request_index) const noexcept
      -> std::optional<std::reference_wrapper<const RequestSnapshot>>;
  [[nodiscard]] auto path_point(std::int32_t strategy_index,
                                std::int32_t request_index,
                                std::int32_t point_index) const noexcept
      -> std::optional<PathPoint>;
  [[nodiscard]] auto cell_passable(std::int32_t x,
                                   std::int32_t y) const noexcept
      -> std::optional<bool>;

 private:
  std::array<StrategySnapshot, 4> strategies_{};
  std::array<std::uint8_t, path_capacity> passable_{};
  bool valid_ = false;
};

}  // namespace tess::examples::pathfinding_strategies
