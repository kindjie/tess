#include <tess/pathfinding.h>

#include <cstddef>
#include <cstdint>
#include <iostream>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define TESS_DEMO_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define TESS_DEMO_EXPORT
#endif

namespace {

constexpr int kWidth = 32;
constexpr int kHeight = 24;

struct PassableTag {};

using Shape =
    tess::Shape<tess::Extent3{kWidth, kHeight, 1}, tess::Extent3{8, 8, 1}>;
using Schema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

struct DemoState {
  World world;
  tess::PathScratch scratch;
  tess::PathView path;
};

auto state() -> DemoState& {
  static DemoState value;
  return value;
}

auto in_bounds(int x, int y) -> bool {
  return x >= 0 && x < kWidth && y >= 0 && y < kHeight;
}

auto coord(int x, int y) -> tess::Coord3 { return {x, y, 0}; }

}  // namespace

extern "C" {

TESS_DEMO_EXPORT auto tess_demo_width() -> int { return kWidth; }

TESS_DEMO_EXPORT auto tess_demo_height() -> int { return kHeight; }

TESS_DEMO_EXPORT void tess_demo_reset() {
  auto& demo = state();
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      demo.world.field<PassableTag>(coord(x, y)) = 1;
    }
  }
  demo.path = {};
}

TESS_DEMO_EXPORT auto tess_demo_set_blocked(int x, int y, int blocked) -> int {
  if (!in_bounds(x, y)) {
    return 0;
  }
  auto& demo = state();
  demo.world.field<PassableTag>(coord(x, y)) = blocked == 0 ? 1 : 0;
  demo.path = {};
  return 1;
}

TESS_DEMO_EXPORT auto tess_demo_find_path(int start_x, int start_y, int goal_x,
                                          int goal_y) -> int {
  if (!in_bounds(start_x, start_y) || !in_bounds(goal_x, goal_y)) {
    return -1;
  }
  auto& demo = state();
  const auto result = tess::astar_path<World, PassableTag>(
      demo.world,
      tess::PathRequest{coord(start_x, start_y), coord(goal_x, goal_y)},
      demo.scratch);
  demo.path =
      result.status == tess::PathStatus::Found ? result.path : tess::PathView{};
  return static_cast<int>(demo.path.size());
}

TESS_DEMO_EXPORT auto tess_demo_path_x(int index) -> int {
  const auto& path = state().path;
  if (index < 0 || static_cast<std::size_t>(index) >= path.size()) {
    return -1;
  }
  return static_cast<int>(path[static_cast<std::size_t>(index)].x);
}

TESS_DEMO_EXPORT auto tess_demo_path_y(int index) -> int {
  const auto& path = state().path;
  if (index < 0 || static_cast<std::size_t>(index) >= path.size()) {
    return -1;
  }
  return static_cast<int>(path[static_cast<std::size_t>(index)].y);
}

}  // extern "C"

int main() {
  tess_demo_reset();
#ifndef __EMSCRIPTEN__
  const auto open_length = tess_demo_find_path(0, 0, kWidth - 1, kHeight - 1);
  if (open_length != kWidth + kHeight - 1) {
    return 1;
  }
  for (int y = 0; y < kHeight; ++y) {
    tess_demo_set_blocked(kWidth / 2, y, 1);
  }
  if (tess_demo_find_path(0, 0, kWidth - 1, kHeight - 1) != 0) {
    return 1;
  }
  std::cout << "web pathfinder model: ok\n";
#endif
  return 0;
}
