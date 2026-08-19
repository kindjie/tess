#include <cstdint>
#include <optional>

#include "../pathfinding_strategies_model.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define TESS_STRATEGIES_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define TESS_STRATEGIES_EXPORT
#endif

namespace demo = tess::examples::pathfinding_strategies;

namespace {

constexpr std::int32_t kInvalid = -1;

struct BrowserState {
  std::optional<demo::StrategyModel> model;
  std::int32_t readiness = 0;
};

auto state() -> BrowserState& {
  static BrowserState value;
  return value;
}

[[nodiscard]] auto model() -> const demo::StrategyModel* {
  const auto& value = state();
  if (value.readiness != 1 || !value.model.has_value()) {
    return nullptr;
  }
  return &*value.model;
}

[[nodiscard]] auto selected_strategy(std::int32_t strategy)
    -> const demo::StrategySnapshot* {
  const auto* value = model();
  if (value == nullptr) {
    return nullptr;
  }
  const auto selected = value->strategy(strategy);
  return selected.has_value() ? &selected->get() : nullptr;
}

[[nodiscard]] auto selected_request(std::int32_t strategy, std::int32_t request)
    -> const demo::RequestSnapshot* {
  const auto* value = model();
  if (value == nullptr) {
    return nullptr;
  }
  const auto selected = value->request(strategy, request);
  return selected.has_value() ? &selected->get() : nullptr;
}

}  // namespace

extern "C" {

TESS_STRATEGIES_EXPORT auto tess_strategies_readiness() -> std::int32_t {
  return state().readiness;
}

TESS_STRATEGIES_EXPORT auto tess_strategies_width() -> std::int32_t {
  return model() == nullptr ? kInvalid : demo::width;
}

TESS_STRATEGIES_EXPORT auto tess_strategies_height() -> std::int32_t {
  return model() == nullptr ? kInvalid : demo::height;
}

TESS_STRATEGIES_EXPORT auto tess_strategies_count() -> std::int32_t {
  return model() == nullptr ? kInvalid : 4;
}

TESS_STRATEGIES_EXPORT auto tess_strategies_cell_passable(std::int32_t x,
                                                          std::int32_t y)
    -> std::int32_t {
  const auto* value = model();
  if (value == nullptr) {
    return kInvalid;
  }
  const auto selected = value->cell_passable(x, y);
  return selected.has_value() ? (*selected ? 1 : 0) : kInvalid;
}

TESS_STRATEGIES_EXPORT auto tess_strategies_request_count(std::int32_t strategy)
    -> std::int32_t {
  const auto* selected = selected_strategy(strategy);
  return selected == nullptr
             ? kInvalid
             : static_cast<std::int32_t>(selected->request_count);
}

TESS_STRATEGIES_EXPORT auto tess_strategies_path_status(std::int32_t strategy,
                                                        std::int32_t request)
    -> std::int32_t {
  const auto* selected = selected_request(strategy, request);
  return selected == nullptr ? kInvalid
                             : static_cast<std::int32_t>(selected->status);
}

TESS_STRATEGIES_EXPORT auto tess_strategies_path_cost(std::int32_t strategy,
                                                      std::int32_t request)
    -> std::int32_t {
  const auto* selected = selected_request(strategy, request);
  return selected == nullptr ? kInvalid
                             : static_cast<std::int32_t>(selected->cost);
}

TESS_STRATEGIES_EXPORT auto tess_strategies_path_expansions(
    std::int32_t strategy, std::int32_t request) -> std::int32_t {
  const auto* selected = selected_request(strategy, request);
  return selected == nullptr
             ? kInvalid
             : static_cast<std::int32_t>(selected->expanded_nodes);
}

TESS_STRATEGIES_EXPORT auto tess_strategies_path_size(std::int32_t strategy,
                                                      std::int32_t request)
    -> std::int32_t {
  const auto* selected = selected_request(strategy, request);
  return selected == nullptr ? kInvalid
                             : static_cast<std::int32_t>(selected->path_size);
}

TESS_STRATEGIES_EXPORT auto tess_strategies_path_x(std::int32_t strategy,
                                                   std::int32_t request,
                                                   std::int32_t point)
    -> std::int32_t {
  const auto* value = model();
  if (value == nullptr) {
    return kInvalid;
  }
  const auto selected = value->path_point(strategy, request, point);
  return selected.has_value() ? selected->x : kInvalid;
}

TESS_STRATEGIES_EXPORT auto tess_strategies_path_y(std::int32_t strategy,
                                                   std::int32_t request,
                                                   std::int32_t point)
    -> std::int32_t {
  const auto* value = model();
  if (value == nullptr) {
    return kInvalid;
  }
  const auto selected = value->path_point(strategy, request, point);
  return selected.has_value() ? selected->y : kInvalid;
}

TESS_STRATEGIES_EXPORT auto tess_strategies_cache_hits() -> std::int32_t {
  const auto* selected = selected_strategy(1);
  return selected == nullptr ? kInvalid
                             : static_cast<std::int32_t>(selected->cache_hits);
}

TESS_STRATEGIES_EXPORT auto tess_strategies_cache_misses() -> std::int32_t {
  const auto* selected = selected_strategy(1);
  return selected == nullptr
             ? kInvalid
             : static_cast<std::int32_t>(selected->cache_misses);
}

TESS_STRATEGIES_EXPORT auto tess_strategies_batch_unique_goals()
    -> std::int32_t {
  const auto* selected = selected_strategy(2);
  return selected == nullptr
             ? kInvalid
             : static_cast<std::int32_t>(selected->unique_goals);
}

TESS_STRATEGIES_EXPORT auto tess_strategies_batch_field_builds()
    -> std::int32_t {
  const auto* selected = selected_strategy(2);
  return selected == nullptr
             ? kInvalid
             : static_cast<std::int32_t>(selected->field_builds);
}

TESS_STRATEGIES_EXPORT auto tess_strategies_batch_fallbacks() -> std::int32_t {
  const auto* selected = selected_strategy(2);
  return selected == nullptr
             ? kInvalid
             : static_cast<std::int32_t>(selected->astar_fallbacks);
}

TESS_STRATEGIES_EXPORT auto tess_strategies_field_builds() -> std::int32_t {
  const auto* selected = selected_strategy(3);
  return selected == nullptr
             ? kInvalid
             : static_cast<std::int32_t>(selected->field_builds);
}

TESS_STRATEGIES_EXPORT auto tess_strategies_field_expansions() -> std::int32_t {
  const auto* selected = selected_strategy(3);
  return selected == nullptr
             ? kInvalid
             : static_cast<std::int32_t>(selected->field_build_expansions);
}

TESS_STRATEGIES_EXPORT auto tess_strategies_field_reached_nodes()
    -> std::int32_t {
  const auto* selected = selected_strategy(3);
  return selected == nullptr
             ? kInvalid
             : static_cast<std::int32_t>(selected->field_reached_nodes);
}

}  // extern "C"

auto main() -> int {
  auto& value = state();
  value.model.emplace();
  value.readiness = value.model->valid() ? 1 : -1;
  return value.readiness == 1 ? 0 : 1;
}
