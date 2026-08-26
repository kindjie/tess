// Native runner for the congestion-pricing laboratory: the same model
// the browser demo runs, driven at fixed steps. This is the screen and
// matrix entry point -- evidence runs and the WASM demo share one code
// path by construction.
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
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
  if (scenario == "maze") {
    // Registered forward scenario for the supported-population matrix:
    // every fourth column in the wall band, fully walled except a
    // two-tile gap at y = (x * 7) mod 120. Single-file capacity almost
    // everywhere; deterministic and connected by construction.
    for (int x = 20; x <= 108; x += 4) {
      const int gap = (x * 7) % 120;
      for (int y = 0; y < wc::height; ++y) {
        if (y == gap || y == gap + 1) {
          continue;
        }
        ++attempted;
        accepted += model.queue_wall(x, y) ? 1U : 0U;
      }
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
  int budget = 0;
  bool spread = false;
  bool measure = false;
  bool settled = false;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    const auto next = [&]() -> std::string_view {
      return i + 1 < argc ? argv[++i] : std::string_view{};
    };
    if (arg == "--scenario") {
      scenario = next();
      continue;
    }
    // One branch for every integer-valued flag: repeating the parse and
    // its failure exit per flag is what the clone check objects to.
    int* const number = arg == "--agents"      ? &agents
                        : arg == "--policy"    ? &policy
                        : arg == "--max-ticks" ? &max_ticks
                        : arg == "--budget"    ? &budget
                                               : nullptr;
    if (number != nullptr) {
      if (!parse_int(next(), *number)) {
        return 2;
      }
    } else if (arg == "--spread") {
      spread = true;
    } else if (arg == "--settled-stall") {
      // Amendment 11: one stall definition across every policy family.
      settled = true;
    } else if (arg == "--measure-productivity") {
      // Amendment-10 instrumentation. Adds per-tick route
      // fingerprinting, so runs using it are not wall-time comparable.
      measure = true;
    } else if (arg == "--help") {
      std::printf(
          "usage: tess_web_congestion_model --scenario "
          "<open|tip|two-gates|four-gates|goal-wall|browser-guard|maze|"
          "browser-incremental> [--agents N] [--policy 0..31] [--spread] "
          "[--budget N|-1|-2|-3] [--max-ticks N]\n");
      return 0;
    }
  }
  wcg::CongestionModel model{agents};
  model.set_spread_congested_routes(spread);
  model.set_pricing_policy(policy);
  model.set_planning_budget(budget);
  model.set_measure_productivity(measure);
  model.set_settled_stall(settled);

  const bool incremental = scenario == "browser-incremental";
  std::size_t incremental_wall = 0;
  constexpr std::size_t kIncrementalTotal =
      std::size(wc::kEndpointGuardReproductionWalls);
  const auto incremental_pending = [&] {
    return incremental && incremental_wall < kIncrementalTotal;
  };
  int ticks = 0;
  bool walls_ok = true;
  long long wall_us_total = 0;
  long long wall_us_max = 0;
  long long wall_us_max_late = 0;
  for (; ticks < max_ticks &&
         (!model.turnaround_ready() || incremental_pending());
       ++ticks) {
    const int batch_tick = scenario == "maze" ? 0 : 4;
    if (ticks == batch_tick && !incremental) {
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
    // Wall time is reporting only; nothing in the sim reads it. The
    // registered dynamic-budget concern is precisely work that tick
    // counts cannot see, so the runner records it directly.
    const auto begin = std::chrono::steady_clock::now();
    (void)model.tick(0.05);
    // `duration::count()` is `long` on LP64 Linux and `long long` on
    // macOS; fix the type here so std::max has one argument type.
    const auto tick_us = static_cast<long long>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - begin)
            .count());
    wall_us_total += tick_us;
    wall_us_max = std::max(wall_us_max, tick_us);
    // Tick 0 plans the whole fleet under every mode, so the plain max
    // is structurally the startup burst; the late max separates the
    // steady state the budget hypotheses are about.
    if (ticks >= 16) {
      wall_us_max_late = std::max(wall_us_max_late, tick_us);
    }
  }
  walls_ok =
      walls_ok && (!incremental || incremental_wall == kIncrementalTotal);
  std::printf(
      "congestion scenario=%.*s agents=%d policy=%d budget=%d spread=%d "
      "ticks=%d "
      "arrived=%d crowd_blocked=%d unreachable=%d turnaround=%d walls=%s "
      "scoped_replans=%lld expansions=%lld pending_integral=%lld "
      "wall_ms=%lld wall_max_us=%lld wall_max_late_us=%lld "
      "drained=%lld changed=%lld armed=%lld rescued=%lld settled=%d\n",
      static_cast<int>(scenario.size()), scenario.data(), agents, policy,
      budget, spread ? 1 : 0, ticks, model.arrived(), model.crowd_blocked(),
      model.unreachable(), model.turnaround_ready() ? 1 : 0,
      walls_ok ? "ok" : "REFUSED", model.scoped_replans(),
      model.expansions_total(), model.pending_integral(), wall_us_total / 1000,
      wall_us_max, wall_us_max_late, model.replans_drained(),
      model.replans_changed(), model.rescue_armed(), model.rescue_success(),
      settled ? 1 : 0);
  return walls_ok ? 0 : 1;
}
