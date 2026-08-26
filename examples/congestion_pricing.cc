// Congestion pricing, end to end, against public APIs only: the
// compile-checked companion to docs/guide/congestion.md. One signal
// (the cooling-memory value champion; the signal block is the part you
// swap), the versioned publish, the scoped replan through the
// experimental route-crossing query, and the disarm rule. The numbers
// behind every choice here live in the repository's evidence records;
// this file is the recipe, not the measurement.
#include <tess/experimental/path_agent_replan_selection.h>
#include <tess/tess.h>

#include <cstdio>
#include <vector>

namespace {

int failures = 0;
#define CHECK(cond, msg)                                \
  do {                                                  \
    if (!(cond)) {                                      \
      ++failures;                                       \
      std::printf("FAIL line %d: %s\n", __LINE__, msg); \
    }                                                   \
  } while (0)

// [congestion-world]
struct PassableTag {};
struct CostTag {};
using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                 tess::Field<CostTag, std::uint8_t>>;
using Shape = tess::Shape<tess::Extent3{64, 64, 1}, tess::Extent3{16, 16, 1}>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;
// The movement class prices the cost field: planners and caches see
// congestion prices as ordinary weighted terrain.
using Traveler =
    tess::movement::MovementClass<tess::movement::Field<PassableTag>,
                                  tess::movement::FieldCost<CostTag>>;
// [congestion-world]

constexpr int kWidth = 64;
constexpr int kHeight = 64;

// [congestion-state]
// Pricing state, all caller-owned. `heat` is the cooling memory;
// `increased` marks this repricing's price rises for the scoped
// replan; `changed_chunks` collects the versioned-edit marks.
// Reorder or compact your agent storage and you must remap or clear
// any per-agent reference state alongside it, exactly as you already
// must for retained routes.
struct PricingState {
  std::vector<std::uint16_t> heat;
  std::vector<std::uint8_t> increased;
  std::vector<bool> changed_chunks;

  explicit PricingState()
      : heat(static_cast<std::size_t>(kWidth) * kHeight, std::uint16_t{0}),
        increased(heat.size(), std::uint8_t{0}),
        changed_chunks(tess::ShapeTraits<Shape>::chunk_count, false) {}
};
// [congestion-state]

// [congestion-reprice]
// Every repricing period (4 fixed ticks measured best): compute the
// signal, halve-and-add the cooling memory, write
// price = 1 + min(3, heat), publish content marks ONLY, and remember
// which tiles rose. No global pathing-dirty: that is the ~500x
// mistake.
void reprice(World& world, std::span<const tess::PathAgentState> agents,
             PricingState& state) {
  std::vector<std::uint16_t> signal(state.heat.size(), std::uint16_t{0});
  const auto bump = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) {
      return;
    }
    ++signal[static_cast<std::size_t>(y) * kWidth +
             static_cast<std::size_t>(x)];
  };
  for (const auto& agent : agents) {
    if (!agent.has_goal) {
      continue;
    }
    const auto ax = static_cast<int>(agent.position.x);
    const auto ay = static_cast<int>(agent.position.y);
    bump(ax, ay);
    bump(ax + 1, ay);
    bump(ax - 1, ay);
    bump(ax, ay + 1);
    bump(ax, ay - 1);
  }
  std::fill(state.increased.begin(), state.increased.end(), std::uint8_t{0});
  std::fill(state.changed_chunks.begin(), state.changed_chunks.end(), false);
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      const auto at =
          static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x);
      // floor halving: heat fully evaporates once crowds move on.
      state.heat[at] =
          static_cast<std::uint16_t>(state.heat[at] / 2 + signal[at]);
      const auto capped =
          state.heat[at] > 3 ? std::uint16_t{3} : state.heat[at];
      const auto price = static_cast<std::uint8_t>(1 + capped);
      const tess::Coord3 coord{x, y, 0};
      auto& cost = world.field<CostTag>(coord);
      if (cost == price) {
        continue;
      }
      if (price > cost) {
        state.increased[at] = 1;
      }
      cost = price;
      state.changed_chunks[static_cast<std::size_t>(
          tess::chunk_key<Shape>(tess::chunk_coord<Shape>(coord)).value)] =
          true;
    }
  }
  for (std::uint64_t k = 0; k < tess::ShapeTraits<Shape>::chunk_count; ++k) {
    if (state.changed_chunks[static_cast<std::size_t>(k)]) {
      world.mark_content_changed(tess::ChunkKey{k});
    }
  }
}
// [congestion-reprice]

// [congestion-scope]
// A price change never invalidates a retained route, so only agents
// whose remaining route crosses a price increase are asked to replan.
// Price decreases request nothing; agents consider them during their
// next ordinary replan.
std::size_t scope_replans(std::span<const tess::PathAgentState> agents,
                          const tess::PathAgentRoutes& routes,
                          const PricingState& state,
                          tess::PathAgentReplanQueue& queue) {
  return tess::experimental::request_replans_for_route_crossings(
      agents, routes,
      [&](tess::Coord3 coord) {
        return state.increased[static_cast<std::size_t>(coord.y) * kWidth +
                               static_cast<std::size_t>(coord.x)] != 0;
      },
      queue);
}
// [congestion-scope]

// [congestion-disarm]
// Turning pricing OFF is the one place a global replan is correct:
// restore unit costs, publish the marks, then raise the pathing-dirty
// flag once -- every retained route was planned against prices.
void disarm(World& world, tess::PathAgentTickState& tick_state,
            PricingState& state) {
  std::fill(state.heat.begin(), state.heat.end(), std::uint16_t{0});
  std::fill(state.changed_chunks.begin(), state.changed_chunks.end(), false);
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      const tess::Coord3 coord{x, y, 0};
      auto& cost = world.field<CostTag>(coord);
      if (cost != 1) {
        cost = 1;
        state.changed_chunks[static_cast<std::size_t>(
            tess::chunk_key<Shape>(tess::chunk_coord<Shape>(coord)).value)] =
            true;
      }
    }
  }
  for (std::uint64_t k = 0; k < tess::ShapeTraits<Shape>::chunk_count; ++k) {
    if (state.changed_chunks[static_cast<std::size_t>(k)]) {
      world.mark_content_changed(tess::ChunkKey{k});
    }
  }
  tess::mark_pathing_dirty(tick_state);
}
// [congestion-disarm]

}  // namespace

int main() {
  World world;
  for (auto& page : world.chunks()) {
    auto open = page.template field_span<PassableTag>();
    auto cost = page.template field_span<CostTag>();
    for (std::size_t i = 0; i < open.size(); ++i) {
      open[i] = true;
      cost[i] = 1;
    }
  }
  // A wall with one gate, and a small crowd routed through it.
  for (int y = 0; y < kHeight; ++y) {
    if (y < 30 || y > 33) {
      world.field<PassableTag>(tess::Coord3{32, y, 0}) = false;
    }
  }
  std::vector<tess::PathAgentState> agents(24);
  tess::PathAgentTickState tick_state;
  tess::PathAgentReplanQueue replan_queue;
  replan_queue.reserve(agents.size());
  for (std::size_t i = 0; i < agents.size(); ++i) {
    agents[i].position = tess::Coord3{2, static_cast<int>(4 + i * 2), 0};
    agents[i].goal = tess::Coord3{60, static_cast<int>(4 + i * 2), 0};
    agents[i].has_goal = true;
  }
  tick_state.routes.ensure_size(agents.size());

  PricingState pricing;
  tess::PathRequestRuntime runtime;
  // Seed initial routes, then run the pricing loop for a stretch.
  tess::mark_pathing_dirty(tick_state);
  std::size_t scoped_total = 0;
  for (int tick = 0; tick < 128; ++tick) {
    if (tick % 4 == 0) {
      reprice(world, agents, pricing);
      scoped_total +=
          scope_replans(agents, tick_state.routes, pricing, replan_queue);
    }
    const auto stats = tess::tick_weighted_path_agents<World, Traveler, 4>(
        tick_state, world, agents, runtime,
        tess::PathAgentTickOptions{.max_steps = 1});
    (void)stats;
  }
  int arrived = 0;
  for (const auto& agent : agents) {
    if (!agent.has_goal) {
      ++arrived;
    }
  }
  CHECK(arrived > 0, "some agents complete under pricing");
  CHECK(scoped_total > 0, "scoped replans actually fired");
  disarm(world, tick_state, pricing);
  bool unit = true;
  for (int y = 0; y < kHeight && unit; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      if (world.field<CostTag>(tess::Coord3{x, y, 0}) != 1) {
        unit = false;
        break;
      }
    }
  }
  CHECK(unit, "disarm restores unit costs everywhere");
  std::printf("congestion pricing example: %s (%d) scoped=%zu arrived=%d\n",
              failures == 0 ? "OK" : "FAILURES", failures, scoped_total,
              arrived);
  return failures == 0 ? 0 : 1;
}
