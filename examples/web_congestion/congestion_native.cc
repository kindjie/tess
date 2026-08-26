// Native runner for the congestion-pricing laboratory: the same model
// the browser demo runs, driven at fixed steps. This is the screen and
// matrix entry point -- evidence runs and the WASM demo share one code
// path by construction.
#include <array>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "../web_colony/colony_endpoint_guard_fixture.h"
#include "../web_colony/colony_model.h"
#include "congestion_model.h"

namespace wc = tess::examples::web_colony;
namespace wcg = tess::examples::web_congestion;

namespace {

[[nodiscard]] auto parse_int(std::string_view text, int& value) -> bool {
  const auto* begin = text.data();
  const auto* end = begin + text.size();
  const auto result = std::from_chars(begin, end, value);
  return result.ec == std::errc{} && result.ptr == end;
}

// The seven evidence scenarios, mirroring the colony native runner's
// wall constructions over this model's passthrough.
std::pair<std::size_t, std::size_t> queue_walls(wcg::CongestionModel& model,
                                                std::string_view scenario) {
  auto accepted = std::size_t{0};
  auto attempted = std::size_t{0};
  if (scenario == "browser-guard") {
    for (const auto& [x, y] : wc::kEndpointGuardReproductionWalls) {
      ++attempted;
      accepted += model.queue_wall(x, y) ? 1U : 0U;
    }
    return {accepted, attempted};
  }
  if (scenario == "goal-wall") {
    for (auto y = 0; y < 96; ++y) {
      ++attempted;
      accepted += model.queue_wall(wc::width - 19, y) ? 1U : 0U;
    }
    return {accepted, attempted};
  }
  for (auto y = 0; y < wc::height; ++y) {
    const auto wall = scenario == "tip" ? y >= 32
                      : scenario == "two-gates"
                          ? !((y >= 24 && y < 32) || (y >= 96 && y < 104))
                      : scenario == "four-gates"
                          ? !((y >= 16 && y < 24) || (y >= 48 && y < 56) ||
                              (y >= 80 && y < 88) || (y >= 112 && y < 120))
                          : false;
    if (wall) {
      ++attempted;
      accepted += model.queue_wall(64, y) ? 1U : 0U;
    }
  }
  return {accepted, attempted};
}

}  // namespace

int main(int argc, char** argv) {
  std::string_view scenario = "open";
  int agents = 256;
  int policy = 0;
  int max_ticks = 5000;
  bool spread = false;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    const auto next = [&]() -> std::string_view {
      return i + 1 < argc ? argv[++i] : std::string_view{};
    };
    if (arg == "--scenario") {
      scenario = next();
    } else if (arg == "--agents" && !parse_int(next(), agents)) {
      return 2;
    } else if (arg == "--policy" && !parse_int(next(), policy)) {
      return 2;
    } else if (arg == "--max-ticks" && !parse_int(next(), max_ticks)) {
      return 2;
    } else if (arg == "--spread") {
      spread = true;
    } else if (arg == "--help") {
      std::printf(
          "usage: tess_web_congestion_model --scenario "
          "<open|tip|two-gates|four-gates|goal-wall|browser-guard|"
          "browser-incremental> [--agents N] [--policy 0..28] [--spread] "
          "[--max-ticks N]\n");
      return 0;
    }
  }
  wcg::CongestionModel model{agents};
  model.set_spread_congested_routes(spread);
  model.set_pricing_policy(policy);

  const bool incremental = scenario == "browser-incremental";
  std::size_t incremental_wall = 0;
  constexpr std::size_t kIncrementalTotal =
      std::size(wc::kEndpointGuardReproductionWalls);
  const auto incremental_pending = [&] {
    return incremental && incremental_wall < kIncrementalTotal;
  };
  int ticks = 0;
  bool walls_ok = true;
  for (; ticks < max_ticks &&
         (!model.turnaround_ready() || incremental_pending());
       ++ticks) {
    if (ticks == 4 && !incremental) {
      const auto [accepted, attempted] = queue_walls(model, scenario);
      walls_ok = walls_ok && accepted == attempted;
    }
    if (ticks >= 4 && incremental_pending()) {
      constexpr std::size_t kWallsPerTick = 4;
      std::size_t accepted_this_tick = 0;
      while (accepted_this_tick < kWallsPerTick && incremental_pending()) {
        const auto& [x, y] =
            wc::kEndpointGuardReproductionWalls[incremental_wall];
        if (!model.queue_wall(x, y)) {
          break;
        }
        ++accepted_this_tick;
        ++incremental_wall;
      }
    }
    (void)model.tick(0.05);
  }
  walls_ok =
      walls_ok && (!incremental || incremental_wall == kIncrementalTotal);
  std::printf(
      "congestion scenario=%.*s agents=%d policy=%d spread=%d ticks=%d "
      "arrived=%d crowd_blocked=%d unreachable=%d turnaround=%d walls=%s "
      "scoped_replans=%lld\n",
      static_cast<int>(scenario.size()), scenario.data(), agents, policy,
      spread ? 1 : 0, ticks, model.arrived(), model.crowd_blocked(),
      model.unreachable(), model.turnaround_ready() ? 1 : 0,
      walls_ok ? "ok" : "REFUSED", model.scoped_replans());
  return walls_ok ? 0 : 1;
}
