#include <tess/core/config.h>

#include <exception>
#include <iostream>

#include "pathfinding_strategies_model.h"

namespace demo = tess::examples::pathfinding_strategies;

namespace {

[[nodiscard]] auto verify_copied_paths_after_scratch_reuse(
    const demo::StrategyModel& model) -> bool {
  return model.valid() && model.copied_paths_valid();
}

[[nodiscard]] auto verify_obstacle_map(const demo::StrategyModel& model)
    -> bool {
  return model.cell_passable(4, 4).value_or(false) &&
         !model.cell_passable(4, 5).value_or(true) &&
         model.cell_passable(8, 11).value_or(false) &&
         !model.cell_passable(8, 10).value_or(true) &&
         model.cell_passable(12, 6).value_or(false) &&
         !model.cell_passable(12, 7).value_or(true);
}

[[nodiscard]] auto verify_invalid_checked_reads(
    const demo::StrategyModel& model) -> bool {
  return !model.strategy(-1).has_value() && !model.strategy(4).has_value() &&
         !model.request(0, -1).has_value() &&
         !model.request(1, 2).has_value() &&
         !model.path_point(0, 0, -1).has_value() &&
         !model.path_point(0, 0, 256).has_value() &&
         !model.cell_passable(-1, 0).has_value() &&
         !model.cell_passable(16, 0).has_value();
}

}  // namespace

auto main() -> int {
#if TESS_HAS_EXCEPTIONS
  try {
#endif
    const auto model = demo::StrategyModel{};
    if (!verify_copied_paths_after_scratch_reuse(model) ||
        !verify_obstacle_map(model) || !verify_invalid_checked_reads(model)) {
      std::cerr << "pathfinding strategy comparison failed\n";
      return 1;
    }
    std::cout << "pathfinding strategies: ok\n";
#if TESS_HAS_EXCEPTIONS
  } catch (const std::exception& error) {
    std::cerr << "pathfinding strategy comparison failed: " << error.what()
              << '\n';
    return 1;
  }
#endif
  return 0;
}
