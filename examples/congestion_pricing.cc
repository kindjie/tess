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
// Terrain cost belongs to the caller and pricing never writes it.
struct TerrainTag {};
// The congestion surcharge, owned entirely by the pricing code. Zero
// means "no surcharge" here, not impassable.
struct SurchargeTag {};
using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                 tess::Field<TerrainTag, std::uint8_t>,
                                 tess::Field<SurchargeTag, std::uint8_t>>;
using Shape = tess::Shape<tess::Extent3{64, 64, 1}, tess::Extent3{16, 16, 1}>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;
// Terrain and surcharge live in separate fields and the movement class
// reads their sum, so pricing never overwrites terrain and disarming is
// just clearing the surcharge. Writing the price into the terrain field
// instead would destroy the caller's terrain on the first repricing and
// erase it again on disarm.
//
// `OverlayCost` is zero exactly when terrain is zero, so a surcharge
// cannot make impassable ground enterable in the weighted search. That
// absorption is a backstop, not the whole guard: the region graph and
// the minimum-step APIs consult the passability predicate alone, so a
// caller who encodes impassable terrain as cost zero must say so there
// too. `NotZero<TerrainTag>` is what keeps those three in agreement.
using Traveler = tess::movement::MovementClass<
    tess::movement::AllOf<tess::movement::Field<PassableTag>,
                          tess::movement::NotZero<TerrainTag>>,
    tess::movement::OverlayCost<tess::movement::FieldCost<TerrainTag>,
                                tess::movement::FieldCost<SurchargeTag>>>;
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
// surcharge = min(3, heat), publish content marks ONLY, and remember
// which tiles rose. No global pathing-dirty: in the recorded case that
// is the difference between about 84 ms and 1.6 ms per tick, roughly
// 53x.
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
      // The surcharge is the capped signal itself. Terrain supplies the
      // base, so this is not biased by one the way a single-field
      // recipe has to be.
      const auto surcharge = static_cast<std::uint8_t>(capped);
      const tess::Coord3 coord{x, y, 0};
      auto& priced = world.field<SurchargeTag>(coord);
      if (priced == surcharge) {
        continue;
      }
      if (surcharge > priced) {
        state.increased[at] = 1;
      }
      priced = surcharge;
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
// clear the surcharge, publish the marks, then raise the pathing-dirty
// flag once -- every retained route was planned against prices.
//
// Clearing one field is the whole of it. A recipe that wrote prices
// into the terrain field would have to restore terrain here, and could
// only restore what it happened to know: a uniform value, not whatever
// the caller's map actually held.
void disarm(World& world, tess::PathAgentTickState& tick_state,
            PricingState& state) {
  std::fill(state.heat.begin(), state.heat.end(), std::uint16_t{0});
  std::fill(state.changed_chunks.begin(), state.changed_chunks.end(), false);
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      const tess::Coord3 coord{x, y, 0};
      auto& priced = world.field<SurchargeTag>(coord);
      if (priced != 0) {
        priced = 0;
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
  // The composed expression must still let the library prove the cost
  // range is safe. Omitting a maximum on a hand-rolled cost expression
  // degrades this to Unknown silently; this turns that into a compile
  // error instead.
  static_assert(tess::path_cost_range_assessment<World, Traveler> ==
                tess::CostRangeAssessment::ProvenSafe);

  World world;
  for (auto& page : world.chunks()) {
    auto open = page.template field_span<PassableTag>();
    auto terrain = page.template field_span<TerrainTag>();
    auto surcharge = page.template field_span<SurchargeTag>();
    for (std::size_t i = 0; i < open.size(); ++i) {
      open[i] = true;
      terrain[i] = 1;
      surcharge[i] = 0;
    }
  }
  // One tile of costlier ground, so the checks below prove pricing
  // leaves terrain alone rather than passing on a uniformly flat map
  // where every value happens to be the value pricing would write.
  constexpr tess::Coord3 rough{3, 3, 0};
  world.field<TerrainTag>(rough) = 4;
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
    // Terrain maximum (4 here) plus the surcharge cap (3). A single-cost
    // recipe only had to cover the cap; separating the fields means the
    // planner's ceiling must cover their sum.
    const auto stats = tess::tick_weighted_path_agents<World, Traveler, 7>(
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
  // Terrain survived pricing: the surcharge never touched it.
  CHECK(world.field<TerrainTag>(rough) == 4,
        "pricing leaves the caller's terrain alone");
  // Zero terrain is impassable even where the boolean says otherwise
  // and a surcharge sits on the tile -- the predicate and the cost
  // expression agree, which is what keeps the region graph and the
  // minimum-step APIs consistent with the weighted search.
  {
    constexpr tess::Coord3 sealed{5, 5, 0};
    world.field<TerrainTag>(sealed) = 0;
    world.field<SurchargeTag>(sealed) = 2;
    const auto resolved = world.resolve(sealed);
    const auto* page = world.try_chunk(resolved.chunk_key);
    CHECK(page != nullptr, "sealed tile resolves");
    if (page != nullptr) {
      CHECK(!Traveler::passable(*page, resolved.local_tile_id),
            "zero terrain stays impassable under a surcharge");
      CHECK(Traveler::entry_cost(*page, resolved.local_tile_id) == 0,
            "zero terrain absorbs the surcharge");
    }
    world.field<TerrainTag>(sealed) = 1;
    world.field<SurchargeTag>(sealed) = 0;
  }
  disarm(world, tick_state, pricing);
  bool cleared = true;
  for (int y = 0; y < kHeight && cleared; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      if (world.field<SurchargeTag>(tess::Coord3{x, y, 0}) != 0) {
        cleared = false;
        break;
      }
    }
  }
  CHECK(cleared, "disarm clears the surcharge everywhere");
  CHECK(world.field<TerrainTag>(rough) == 4,
        "disarm leaves the caller's terrain alone");
  std::printf("congestion pricing example: %s (%d) scoped=%zu arrived=%d\n",
              failures == 0 ? "OK" : "FAILURES", failures, scoped_total,
              arrived);
  return failures == 0 ? 0 : 1;
}
