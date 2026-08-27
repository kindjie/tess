// Native driver for the tower demo: the same model the browser runs,
// stepped at a fixed rate. Keeps the demo verifiable in CI without a
// browser, exactly as the other web examples are.
#include <charconv>
#include <cstdio>
#include <string_view>

#include "tower_model.h"

namespace wt = tess::examples::web_tower;

namespace {

[[nodiscard]] auto parse_int(std::string_view text, int& value) -> bool {
  const auto* begin = text.data();
  const auto* end = begin + text.size();
  const auto result = std::from_chars(begin, end, value);
  return result.ec == std::errc{} && result.ptr == end;
}

}  // namespace

int main(int argc, char** argv) {
  int agents = 96;
  int max_ticks = 4000;
  int seal = -1;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    const auto next = [&]() -> std::string_view {
      return i + 1 < argc ? argv[++i] : std::string_view{};
    };
    // One branch for every integer flag: repeating the parse and its
    // failure exit per flag is what the clone check objects to.
    int* const number = arg == "--agents"      ? &agents
                        : arg == "--max-ticks" ? &max_ticks
                        : arg == "--seal"      ? &seal
                                               : nullptr;
    if (number != nullptr) {
      if (!parse_int(next(), *number)) {
        return 2;
      }
    } else if (arg == "--help") {
      std::printf(
          "usage: tess_web_tower_model [--agents N] [--max-ticks N]"
          " [--seal STAIRWELL]\n");
      return 0;
    }
  }

  wt::TowerModel model{agents};
  bool seal_ok = true;
  int ticks = 0;
  for (; ticks < max_ticks && !model.turnaround_ready(); ++ticks) {
    if (ticks == 40 && seal >= 0) {
      seal_ok = model.set_stairwell(seal, false);
    }
    (void)model.tick(0.05);
  }
  if (!model.turnaround_ready()) {
    for (int i = 0; i < model.agent_count(); ++i) {
      int px = 0, py = 0, pz = 0, gx = 0, gy = 0, gz = 0, phase = 0;
      if (model.agent_debug(i, &px, &py, &pz, &gx, &gy, &gz, &phase) &&
          phase != 0) {
        std::printf("  stuck agent=%d at=(%d,%d,%d) goal=(%d,%d,%d) phase=%d\n",
                    i, px, py, pz, gx, gy, gz, phase);
      }
    }
  }
  std::printf(
      "tower agents=%d floors=%d ticks=%d arrived=%d crowd_blocked=%d "
      "unreachable=%d turnaround=%d seal=%d seal_ok=%s\n",
      model.agent_count(), wt::floors, ticks, model.arrived(),
      model.crowd_blocked(), model.unreachable(),
      model.turnaround_ready() ? 1 : 0, seal, seal_ok ? "ok" : "refused");
  // turnaround_ready() counts Unreachable agents as settled, so it
  // alone would report success even if a pathing regression stranded
  // the whole fleet. Require actual completion.
  const bool completed = model.turnaround_ready() &&
                         model.arrived() == model.agent_count() &&
                         model.unreachable() == 0 && model.crowd_blocked() == 0;
  return completed ? 0 : 1;
}
