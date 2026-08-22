// Shared movement scenario fixtures and settle classification for the
// post-0.13 movement stream (execution plan PRs C1, C3, X1). Harness
// support only, never a public header.
//
// The plan's movement gates name five fixture families as if they
// existed; none did. The PIBT tier's in-repo evidence was one pinned
// ring seed with a test-local BFS helper, and the sweep harness that
// produced its gate evidence lived outside the tree and is gone. This
// header is that substrate, built once so three experiments compare
// against the same instances rather than three private approximations.
//
// Two properties decide whether downstream arms are comparable at all,
// and both are pinned here rather than left to each experiment:
//
//  - Every run option that can censor a result is a fixture property.
//    `SwapPolicy` changes what "no mover could succeed" means.
//    `blocked_exhaustion_policy` is the knob that can manufacture seals
//    outright, because `MarkUnreachable` settles an exhausted agent and
//    a settled agent can cut another agent's goal off; it is pinned to
//    `RemainBlocked` here so exhaustion never settles anyone. Under
//    that policy `max_blocked_retries` still matters -- exhaustion
//    stops an agent replanning -- so it is pinned large, and two
//    experiments with different budgets would still get different
//    results on identical seeds.
//  - The ranking oracle is fixed to `RouteAttachmentRanking`, because
//    C1 layers its tie-break inside that oracle. Note this makes ring
//    results NOT numerically comparable to the pinned ring regression
//    in tess_pibt_movement_test.cc, which ranks with per-agent exact
//    BFS tables instead. The terrain is shared; the numbers are not.
#pragma once

#include <tess/tess.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "grid_map_generators.h"

namespace tess_test::movement {

struct PassableTag {};
struct CostTag {};
struct OccupancyTag {};
struct ReservationTag {};
struct SettledTag {};

using Schema = tess::FieldSchema<
    tess::Field<PassableTag, bool>, tess::Field<CostTag, std::uint8_t>,
    tess::Field<OccupancyTag, bool>, tess::Field<ReservationTag, bool>,
    tess::Field<SettledTag, bool>>;

// One shape for every family. World types and extents are compile-time
// and PIBT statically requires an always-resident world, so a single
// shape keeps binary count and compile cost bounded. The cost is real
// and recorded: these fixtures cannot reach the colony macro-harness
// populations, and any family needing a larger extent is a later
// decision rather than a silent widening here.
constexpr int kExtent = 64;
using Shape2D = tess::Shape<tess::Extent3{64, 64, 1}, tess::Extent3{16, 16, 1}>;
using World = tess::AlwaysResidentWorld<Shape2D, Schema>;

// Agents move through the settled-aware class, which is what makes a
// seal representable: a terminal agent's tile stops being passable.
using Traveler = tess::movement::MovementClass<
    tess::movement::AllOf<
        tess::movement::Field<PassableTag>,
        tess::movement::Not<tess::movement::Field<SettledTag>>>,
    tess::movement::FieldCost<CostTag>>;

enum class Family : std::uint8_t {
  Warehouse,
  Ring,
  Colony,
  RandomSparse,
  RandomMedium,
  RandomDense,
  Adversarial,
};

[[nodiscard]] inline auto family_name(Family family) -> std::string_view {
  switch (family) {
    case Family::Warehouse:
      return "warehouse";
    case Family::Ring:
      return "ring";
    case Family::Colony:
      return "colony";
    case Family::RandomSparse:
      return "random_sparse";
    case Family::RandomMedium:
      return "random_medium";
    case Family::RandomDense:
      return "random_dense";
    case Family::Adversarial:
      return "adversarial";
  }
  return "unknown";
}

// Seeds are a closed formula over a trial index, never a curated list.
// A hand-typed list would prove commit order, not knowledge order: the
// author controls when this file lands and could have run any arm
// privately first. With a formula and a pre-registered trial count
// there is no per-seed freedom to exercise. Any later exclusion must be
// recorded with its cause rather than shortening the range.
//
// The construction matches the existing pinned ring seed, which is
// itself `0x9E3779B97F4A7C15 * (trial + 1)`.
[[nodiscard]] constexpr auto family_constant(Family family) -> std::uint64_t {
  return 0x9E3779B97F4A7C15ULL * (static_cast<std::uint64_t>(family) + 1ULL) *
         1000003ULL;
}

[[nodiscard]] constexpr auto scenario_seed(Family family, unsigned trial)
    -> std::uint64_t {
  std::uint64_t z =
      family_constant(family) +
      0x9E3779B97F4A7C15ULL * (static_cast<std::uint64_t>(trial) + 1ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

// Pre-registered trial counts. Declared here, before any arm exists, so
// a later thin denominator cannot be blamed on the fixture.
[[nodiscard]] constexpr auto trial_count(Family family) -> unsigned {
  return family == Family::Adversarial ? 12u : 20u;
}

// Every option below can change a sealed count on an identical seed, so
// each is fixture-owned rather than experiment-owned.
struct FixtureOptions {
  tess::SwapPolicy swap = tess::SwapPolicy::Permit;
  // Patience is deliberately not a variable. Under the pinned
  // `RemainBlocked` policy, exhausting retries does not settle an agent
  // but does stop it replanning, which is enough to freeze a run.
  std::uint32_t max_blocked_retries = 1u << 20u;
  // Pinned rather than inherited. `MarkUnreachable` would settle an
  // exhausted agent, and a settled agent can cut another agent's goal
  // off, so the policy decides how many seals a run can manufacture.
  tess::BlockedAgentExhaustionPolicy exhaustion_policy =
      tess::BlockedAgentExhaustionPolicy::RemainBlocked;
  // Safety bound only. Termination is by no-progress fixpoint; reaching
  // this cap means the run is censored, never that the movers failed.
  int tick_cap = 3000;
  // Consecutive no-progress ticks that establish a wedge. Under a very
  // large retry budget, uniform `elapsed` growth preserves decision
  // order, so a configuration that has not moved for this long cannot
  // move later.
  int wedge_ticks = 16;
  int agent_count = 48;
  int extent = kExtent;
};

[[nodiscard]] inline auto fixture_options(Family family) -> FixtureOptions {
  FixtureOptions options;
  switch (family) {
    case Family::Adversarial:
      // Capped small so C3's tiny optimal solver can bound an instance.
      options.extent = 16;
      options.agent_count = 8;
      break;
    case Family::Ring:
      options.agent_count = 48;
      break;
    default:
      break;
  }
  return options;
}

class SplitMix64 {
 public:
  explicit SplitMix64(std::uint64_t seed) : state_(seed) {}

  auto next() -> std::uint64_t {
    state_ += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = state_;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }

  auto below(std::uint64_t bound) -> std::uint64_t {
    return ((next() >> 32) * bound) >> 32;
  }

 private:
  std::uint64_t state_;
};

// ---------------------------------------------------------------------
// Terrain
// ---------------------------------------------------------------------

using Grid = std::vector<std::uint8_t>;

[[nodiscard]] inline auto grid_at(const Grid& grid, int extent, int x, int y)
    -> bool {
  if (x < 0 || y < 0 || x >= extent || y >= extent) {
    return false;
  }
  return grid[static_cast<std::size_t>(y) * static_cast<std::size_t>(extent) +
              static_cast<std::size_t>(x)] != 0;
}

inline void grid_set(Grid& grid, int extent, int x, int y, bool value) {
  grid[static_cast<std::size_t>(y) * static_cast<std::size_t>(extent) +
       static_cast<std::size_t>(x)] = value ? 1u : 0u;
}

// Racks separated by single-tile aisles, with cross aisles at a fixed
// pitch. The seed varies rack length and the cross-aisle phase, not the
// aisle width: a warehouse whose aisles vary is a different family, and
// C1's hypothesis is about tie-breaking in narrow shared corridors.
[[nodiscard]] inline auto warehouse_terrain(int extent, std::uint64_t seed)
    -> Grid {
  Grid grid(static_cast<std::size_t>(extent) * static_cast<std::size_t>(extent),
            1u);
  SplitMix64 rng(seed);
  const auto cross_pitch = 6 + static_cast<int>(rng.below(4));
  const auto phase = static_cast<int>(rng.below(3));
  for (int y = 1; y < extent - 1; ++y) {
    if ((y + phase) % cross_pitch == 0) {
      continue;  // cross aisle
    }
    for (int x = 2; x < extent - 2; x += 3) {
      grid_set(grid, extent, x, y, false);
      grid_set(grid, extent, x + 1, y, false);
    }
  }
  for (int i = 0; i < extent; ++i) {
    grid_set(grid, extent, i, 0, false);
    grid_set(grid, extent, i, extent - 1, false);
    grid_set(grid, extent, 0, i, false);
    grid_set(grid, extent, extent - 1, i, false);
  }
  return grid;
}

// The pinned ring lattice, extracted verbatim from the ring regression
// so the terrain is shared. Seed-independent by construction; the seed
// varies only agent and goal placement.
[[nodiscard]] inline auto ring_terrain(int extent) -> Grid {
  Grid grid(static_cast<std::size_t>(extent) * static_cast<std::size_t>(extent),
            0u);
  for (int y = 1; y < extent - 1; ++y) {
    for (int x = 1; x < extent - 1; ++x) {
      if (x < 4 || x >= extent - 4 || y < 4 || y >= extent - 4) {
        grid_set(grid, extent, x, y, true);
      }
    }
  }
  return grid;
}

[[nodiscard]] inline auto colony_terrain(int extent, std::uint64_t seed)
    -> Grid {
  Grid grid(static_cast<std::size_t>(extent) * static_cast<std::size_t>(extent),
            0u);
  const auto map = tess_test::grid_benchmark::room_and_corridor(
      static_cast<std::size_t>(extent), static_cast<std::size_t>(extent), seed);
  if (!map.has_value()) {
    return grid;
  }
  // The generator emits Moving AI octile text; the four header lines
  // precede the rows. Parsed locally rather than through the benchmark
  // harness loader, whose schema is not this one.
  std::string_view text = map->text;
  std::size_t row = 0;
  std::size_t line_start = 0;
  int header = 0;
  while (line_start < text.size() && row < static_cast<std::size_t>(extent)) {
    const auto line_end = text.find('\n', line_start);
    const auto line = text.substr(line_start, line_end == std::string_view::npos
                                                  ? std::string_view::npos
                                                  : line_end - line_start);
    if (header < 4) {
      ++header;
    } else {
      for (std::size_t x = 0;
           x < line.size() && x < static_cast<std::size_t>(extent); ++x) {
        grid_set(grid, extent, static_cast<int>(x), static_cast<int>(row),
                 line[x] == '.');
      }
      ++row;
    }
    if (line_end == std::string_view::npos) {
      break;
    }
    line_start = line_end + 1;
  }
  return grid;
}

// Uniform obstacle fill. The three fractions are declared here rather
// than chosen while looking at results.
[[nodiscard]] inline auto random_fill_fraction(Family family) -> unsigned {
  switch (family) {
    case Family::RandomSparse:
      return 5u;
    case Family::RandomMedium:
      return 15u;
    case Family::RandomDense:
      return 25u;
    default:
      return 0u;
  }
}

[[nodiscard]] inline auto random_terrain(int extent, unsigned fill_percent,
                                         std::uint64_t seed) -> Grid {
  Grid grid(static_cast<std::size_t>(extent) * static_cast<std::size_t>(extent),
            1u);
  SplitMix64 rng(seed);
  for (int y = 1; y < extent - 1; ++y) {
    for (int x = 1; x < extent - 1; ++x) {
      if (rng.below(100) < fill_percent) {
        grid_set(grid, extent, x, y, false);
      }
    }
  }
  for (int i = 0; i < extent; ++i) {
    grid_set(grid, extent, i, 0, false);
    grid_set(grid, extent, i, extent - 1, false);
    grid_set(grid, extent, 0, i, false);
    grid_set(grid, extent, extent - 1, i, false);
  }
  return grid;
}

// A corridor narrower than the traffic it carries, with periodic
// side pockets: the terrain on which greedy local resolution fails.
// Kept small so a tiny optimal solver can bound an instance.
//
// The terrain affords opposing flows; it does not construct them.
// Starts and goals are drawn uniformly over free tiles like every other
// family, so a head-on encounter is emergent rather than guaranteed. If
// C3 needs guaranteed opposing demand, that is directed placement this
// family does not yet do.
[[nodiscard]] inline auto adversarial_terrain(int extent, std::uint64_t seed)
    -> Grid {
  Grid grid(static_cast<std::size_t>(extent) * static_cast<std::size_t>(extent),
            0u);
  SplitMix64 rng(seed);
  const auto corridor_y = extent / 2;
  const auto pocket_phase = 3 + static_cast<int>(rng.below(3));
  for (int x = 1; x < extent - 1; ++x) {
    grid_set(grid, extent, x, corridor_y, true);
    if (x % pocket_phase == 0) {
      grid_set(grid, extent, x, corridor_y - 1, true);
    }
  }
  for (int y = 1; y < extent - 1; ++y) {
    grid_set(grid, extent, 1, y, true);
    grid_set(grid, extent, extent - 2, y, true);
  }
  return grid;
}

[[nodiscard]] inline auto family_terrain(Family family, int extent,
                                         std::uint64_t seed) -> Grid {
  switch (family) {
    case Family::Warehouse:
      return warehouse_terrain(extent, seed);
    case Family::Ring:
      return ring_terrain(extent);
    case Family::Colony:
      return colony_terrain(extent, seed);
    case Family::Adversarial:
      return adversarial_terrain(extent, seed);
    default:
      return random_terrain(extent, random_fill_fraction(family), seed);
  }
}

// ---------------------------------------------------------------------
// Scenario construction
// ---------------------------------------------------------------------

struct Scenario {
  World world{};
  std::vector<tess::PathAgentState> agents{};
  tess::PathAgentTickState state{};
  FixtureOptions options{};
  Family family = Family::Ring;
  unsigned trial = 0;
  Grid terrain{};
};

inline void fill_world(World& world) {
  for (auto& page : world.chunks()) {
    auto open = page.template field_span<PassableTag>();
    auto cost = page.template field_span<CostTag>();
    auto occupancy = page.template field_span<OccupancyTag>();
    auto reservation = page.template field_span<ReservationTag>();
    auto settled = page.template field_span<SettledTag>();
    for (std::size_t i = 0; i < open.size(); ++i) {
      open[i] = false;
      cost[i] = 1u;
      occupancy[i] = false;
      reservation[i] = false;
      settled[i] = false;
    }
  }
}

[[nodiscard]] inline auto build_scenario(Family family, unsigned trial)
    -> std::unique_ptr<Scenario> {
  auto scenario = std::make_unique<Scenario>();
  scenario->family = family;
  scenario->trial = trial;
  scenario->options = fixture_options(family);
  const auto extent = scenario->options.extent;
  const auto seed = scenario_seed(family, trial);
  scenario->terrain = family_terrain(family, extent, seed);

  fill_world(scenario->world);
  for (int y = 0; y < extent; ++y) {
    for (int x = 0; x < extent; ++x) {
      if (grid_at(scenario->terrain, extent, x, y)) {
        scenario->world.template field<PassableTag>(tess::Coord3{x, y, 0}) =
            true;
      }
    }
  }

  // Separate streams for placement and goals, so changing the agent
  // count cannot silently shift goal selection.
  std::vector<tess::Coord3> free;
  for (int y = 0; y < extent; ++y) {
    for (int x = 0; x < extent; ++x) {
      if (grid_at(scenario->terrain, extent, x, y)) {
        free.push_back(tess::Coord3{x, y, 0});
      }
    }
  }
  if (free.empty()) {
    return scenario;
  }
  auto starts = free;
  auto goals = free;
  SplitMix64 start_rng(seed ^ 0xA5A5A5A5A5A5A5A5ULL);
  SplitMix64 goal_rng(seed ^ 0x5A5A5A5A5A5A5A5AULL);
  for (std::size_t i = starts.size(); i > 1;) {
    --i;
    std::swap(starts[i], starts[static_cast<std::size_t>(start_rng.below(
                             static_cast<std::uint64_t>(i) + 1U))]);
  }
  for (std::size_t i = goals.size(); i > 1;) {
    --i;
    std::swap(goals[i], goals[static_cast<std::size_t>(goal_rng.below(
                            static_cast<std::uint64_t>(i) + 1U))]);
  }

  const auto n = std::min(
      static_cast<std::size_t>(scenario->options.agent_count), starts.size());
  for (std::size_t i = 0; i < n; ++i) {
    tess::PathAgentState agent;
    agent.position = starts[i];
    scenario->world.template field<OccupancyTag>(agent.position) = true;
    tess::set_path_agent_goal(scenario->state, agent, goals[i]);
    scenario->agents.push_back(agent);
  }
  return scenario;
}

// ---------------------------------------------------------------------
// Classification
// ---------------------------------------------------------------------

// Five categories, not three. A live mutual wedge is invisible to a BFS
// under the terminal set: both wedged agents are unarrived and neither
// is terminal, so both would read as "the mover failed" when the real
// answer is that they are blocking each other. That distinction is X1's
// deadlock category, and it is why `Wedged` exists separately.
enum class Category : std::uint8_t {
  Arrived,
  // Goal unreachable under the terminal set. No mover could succeed.
  Sealed,
  // The goal tile itself is held by a terminal agent. An assignment
  // failure that C2's fungibility work would fix by reassignment, kept
  // separate from a corridor seal, which is unfixable.
  GoalOccupied,
  // Reachable goal, but the run reached a no-progress fixpoint.
  Wedged,
  // The safety cap was reached without a fixpoint. Nothing is known
  // about this agent; it is excluded from every metric.
  Censored,
};

[[nodiscard]] inline auto category_name(Category category) -> std::string_view {
  switch (category) {
    case Category::Arrived:
      return "arrived";
    case Category::Sealed:
      return "sealed";
    case Category::GoalOccupied:
      return "goal_occupied";
    case Category::Wedged:
      return "wedged";
    case Category::Censored:
      return "censored";
  }
  return "unknown";
}

struct Outcome {
  std::vector<Category> categories;
  int ticks = 0;
  bool fixpoint = false;
  bool censored = false;
  std::uint64_t swaps = 0;
  std::uint64_t swaps_denied = 0;
  // Agents whose goal was already unreachable on bare terrain, before
  // any agent moved. These read as sealed but no seal formed, so an
  // experiment comparing seal counts must subtract them rather than
  // credit a mover for them. Same instances across arms, so this cannot
  // decide a comparison; it can still make one look larger than it is.
  std::size_t structural_seals = 0;

  [[nodiscard]] auto count(Category category) const -> std::size_t {
    return static_cast<std::size_t>(
        std::count(categories.begin(), categories.end(), category));
  }

  [[nodiscard]] auto all_arrived() const -> bool {
    return count(Category::Arrived) == categories.size();
  }
};

// An agent is terminal when it will never move again: it has no goal,
// or the tier gave up on it. Read from the agent array rather than from
// a field, because not every consumer schema carries a settled field
// and a field-based reading would classify every permanent occupancy
// seal as a live blockage.
[[nodiscard]] inline auto is_terminal(const tess::PathAgentState& agent)
    -> bool {
  return !agent.has_goal || agent.phase == tess::PathAgentPhase::Unreachable;
}

// Reachability under the terminal set: class-passable tiles that are not
// held by a terminal agent. Neighbours come from the same four-way
// lattice the fixtures are built on; a family on a hex or diagonal
// lattice would need the transition model instead, and none exists here.
[[nodiscard]] inline auto reachable_under_terminal_set(const Scenario& scenario,
                                                       tess::Coord3 from,
                                                       tess::Coord3 goal)
    -> bool {
  const auto extent = scenario.options.extent;
  const auto span = static_cast<std::size_t>(extent);
  std::vector<std::uint8_t> blocked(span * span, 0u);
  for (const auto& agent : scenario.agents) {
    if (is_terminal(agent)) {
      blocked[static_cast<std::size_t>(agent.position.y) * span +
              static_cast<std::size_t>(agent.position.x)] = 1u;
    }
  }
  const auto index = [span](tess::Coord3 c) {
    return static_cast<std::size_t>(c.y) * span + static_cast<std::size_t>(c.x);
  };
  const auto open = [&](tess::Coord3 c) {
    return grid_at(scenario.terrain, extent, static_cast<int>(c.x),
                   static_cast<int>(c.y)) &&
           blocked[index(c)] == 0u;
  };
  if (!open(goal)) {
    return false;
  }
  std::vector<std::uint8_t> seen(span * span, 0u);
  std::vector<tess::Coord3> frontier;
  frontier.push_back(goal);
  seen[index(goal)] = 1u;
  for (std::size_t head = 0; head < frontier.size(); ++head) {
    const auto current = frontier[head];
    if (current.x == from.x && current.y == from.y) {
      return true;
    }
    for (const auto neighbour : {tess::Coord3{current.x + 1, current.y, 0},
                                 tess::Coord3{current.x - 1, current.y, 0},
                                 tess::Coord3{current.x, current.y + 1, 0},
                                 tess::Coord3{current.x, current.y - 1, 0}}) {
      if (neighbour.x < 0 || neighbour.y < 0 || neighbour.x >= extent ||
          neighbour.y >= extent) {
        continue;
      }
      if (seen[index(neighbour)] != 0u || !open(neighbour)) {
        continue;
      }
      seen[index(neighbour)] = 1u;
      frontier.push_back(neighbour);
    }
  }
  return false;
}

[[nodiscard]] inline auto classify(const Scenario& scenario, bool censored)
    -> std::vector<Category> {
  std::vector<Category> categories;
  categories.reserve(scenario.agents.size());
  for (const auto& agent : scenario.agents) {
    if (!agent.has_goal) {
      categories.push_back(Category::Arrived);
      continue;
    }
    if (censored) {
      categories.push_back(Category::Censored);
      continue;
    }
    const auto goal_held = std::any_of(
        scenario.agents.begin(), scenario.agents.end(),
        [&](const tess::PathAgentState& other) {
          return is_terminal(other) && other.position.x == agent.goal.x &&
                 other.position.y == agent.goal.y &&
                 other.position.z == agent.goal.z;
        });
    if (goal_held) {
      categories.push_back(Category::GoalOccupied);
      continue;
    }
    categories.push_back(
        reachable_under_terminal_set(scenario, agent.position, agent.goal)
            ? Category::Wedged
            : Category::Sealed);
  }
  return categories;
}

// Counts agents whose goal is unreachable on bare terrain, ignoring
// every other agent. Computed against the scenario's original goals, so
// it is a property of the instance rather than of a run.
[[nodiscard]] inline auto structural_seal_count(const Scenario& scenario)
    -> std::size_t {
  const auto extent = scenario.options.extent;
  const auto span = static_cast<std::size_t>(extent);
  std::size_t count = 0;
  for (const auto& agent : scenario.agents) {
    const auto goal = agent.has_goal ? agent.goal : agent.position;
    const auto index = [span](tess::Coord3 c) {
      return static_cast<std::size_t>(c.y) * span +
             static_cast<std::size_t>(c.x);
    };
    const auto open = [&](tess::Coord3 c) {
      return grid_at(scenario.terrain, extent, static_cast<int>(c.x),
                     static_cast<int>(c.y));
    };
    if (!open(goal)) {
      ++count;
      continue;
    }
    std::vector<std::uint8_t> seen(span * span, 0u);
    std::vector<tess::Coord3> frontier{goal};
    seen[index(goal)] = 1u;
    bool found = false;
    for (std::size_t head = 0; head < frontier.size() && !found; ++head) {
      const auto current = frontier[head];
      if (current.x == agent.position.x && current.y == agent.position.y) {
        found = true;
        break;
      }
      for (const auto n : {tess::Coord3{current.x + 1, current.y, 0},
                           tess::Coord3{current.x - 1, current.y, 0},
                           tess::Coord3{current.x, current.y + 1, 0},
                           tess::Coord3{current.x, current.y - 1, 0}}) {
        if (n.x < 0 || n.y < 0 || n.x >= extent || n.y >= extent) {
          continue;
        }
        if (seen[index(n)] != 0u || !open(n)) {
          continue;
        }
        seen[index(n)] = 1u;
        frontier.push_back(n);
      }
    }
    if (!found) {
      ++count;
    }
  }
  return count;
}

// ---------------------------------------------------------------------
// Settle loop
// ---------------------------------------------------------------------

// The settled consumer recipe: mirror terminal agents into the settled
// field so the movement class stops treating their tiles as passable,
// and mark/clear dirty around the write so cached products invalidate.
inline auto refresh_settled(Scenario& scenario) -> bool {
  bool changed = false;
  for (const auto& agent : scenario.agents) {
    const bool settled = is_terminal(agent);
    auto& field = scenario.world.template field<SettledTag>(agent.position);
    if ((field != 0) == settled) {
      continue;
    }
    field = settled;
    const auto key =
        tess::chunk_key<Shape2D>(tess::chunk_coord<Shape2D>(agent.position));
    scenario.world.mark_dirty(
        key, tess::DirtyMask{1u << 1u},
        tess::Box3{agent.position, tess::Extent3{1, 1, 1}});
    scenario.world.clear_dirty(key, tess::DirtyMask{1u << 1u});
    changed = true;
  }
  return changed;
}

// Runs the PIBT tier to a no-progress fixpoint, with the tick cap as a
// safety bound rather than the terminating condition.
//
// Terminating on a cap and then classifying would be invalid: the
// terminal set grows monotonically, so it is only final at quiescence.
// At a cap with agents still moving, more seals could still form, and
// an agent still making progress would be recorded as a mover failure
// when in fact the experiment stopped. Under a shared cap that biases
// residual counts against slower arms, which is exactly the comparison
// the movement experiments exist to make.
template <typename Ranking>
[[nodiscard]] auto settle_with_pibt(Scenario& scenario, Ranking&& rank)
    -> Outcome {
  Outcome outcome;
  tess::PathRequestRuntime runtime;
  runtime.reserve_requests(128);
  runtime.reserve_search_nodes(16384);
  runtime.reserve_path_nodes(65536);
  tess::JointMoveScratch scratch;
  scratch.reserve(scenario.agents.size());
  // Constructed fresh per run. This state is index-paired with the agent
  // span and only grows; reusing one across seeds or arms carries stale
  // `elapsed` into the next run and silently changes decision order.
  tess::PibtPriorities priorities;
  priorities.reserve(scenario.agents.size());

  auto options = tess::PathAgentTickOptions{};
  options.max_blocked_retries = scenario.options.max_blocked_retries;
  options.blocked_exhaustion_policy = scenario.options.exhaustion_policy;
  const auto move_options = tess::JointMoveOptions{scenario.options.swap};

  std::vector<tess::Coord3> previous;
  int stalled = 0;
  int tick = 0;
  for (; tick < scenario.options.tick_cap; ++tick) {
    if (std::all_of(
            scenario.agents.begin(), scenario.agents.end(),
            [](const tess::PathAgentState& a) { return !a.has_goal; })) {
      outcome.fixpoint = true;
      break;
    }
    // Settling changes passability for the movement class, so retained
    // routes crossing a newly settled tile are stale. Without this the
    // harness manufactures its own wedges: a stale-routed agent goes
    // Blocked with last_result == Found, which the scoped-submission
    // filter excludes from replanning permanently, and staying put
    // outranks any off-route candidate because an agent's own tile
    // attaches at distance zero. It then parks until the wedge rule
    // fires. Measured on the ring family, that artifact classified 354
    // of 960 agents as wedged on a lattice where this tier's own pinned
    // regression solves the whole population; with this line the same
    // family reports zero wedges. Replanning on settle is a
    // fixture-owned policy, not an experiment's choice, for exactly the
    // reason every other censoring knob is.
    if (refresh_settled(scenario)) {
      tess::mark_pathing_dirty(scenario.state);
    }
    tess::JointMoveStats move_stats;
    (void)tess::tick_weighted_path_agents_with_pibt<
        World, Traveler, 4u, OccupancyTag, ReservationTag>(
        scenario.state, scenario.world, scenario.agents, runtime, priorities,
        scratch, rank, options, move_options, nullptr, &move_stats);
    outcome.swaps += move_stats.swaps;
    outcome.swaps_denied += move_stats.swaps_denied;

    std::vector<tess::Coord3> current;
    current.reserve(scenario.agents.size());
    for (const auto& agent : scenario.agents) {
      current.push_back(agent.position);
    }
    stalled = (current == previous) ? stalled + 1 : 0;
    previous = std::move(current);
    if (stalled >= scenario.options.wedge_ticks) {
      outcome.fixpoint = true;
      ++tick;
      break;
    }
  }
  outcome.ticks = tick;
  outcome.censored = !outcome.fixpoint;
  outcome.categories = classify(scenario, outcome.censored);
  outcome.structural_seals = structural_seal_count(scenario);
  return outcome;
}

// The pinned oracle. C1 layers its tie-break inside this ranking, so
// every family uses it and no experiment substitutes its own.
[[nodiscard]] inline auto route_attachment_ranking(const Scenario& scenario)
    -> tess::RouteAttachmentRanking {
  return tess::RouteAttachmentRanking{
      std::span<const tess::PathAgentState>(scenario.agents),
      &scenario.state.routes, 1u};
}

// ---------------------------------------------------------------------
// Digest
// ---------------------------------------------------------------------

// A single test binary cannot compare itself across build
// configurations, so the instance is pinned by a committed digest that
// every configuration checks.
//
// The digests were generated from this code, so they cannot witness a
// divergence introduced while extracting the ring construction; they
// pin the construction going forward only. Ring instances here already
// differ from the pinned regression's regardless, because placement was
// deliberately moved from that test's single xorshift stream to
// separate SplitMix64 streams for starts and goals.
[[nodiscard]] inline auto scenario_digest(const Scenario& scenario)
    -> std::uint64_t {
  std::uint64_t hash = 0xCBF29CE484222325ULL;
  const auto mix = [&hash](std::uint64_t value) {
    hash ^= value + 0x9E3779B97F4A7C15ULL + (hash << 6U) + (hash >> 2U);
  };
  mix(static_cast<std::uint64_t>(scenario.options.extent));
  for (const auto cell : scenario.terrain) {
    mix(static_cast<std::uint64_t>(cell));
  }
  for (const auto& agent : scenario.agents) {
    mix(static_cast<std::uint64_t>(agent.position.x));
    mix(static_cast<std::uint64_t>(agent.position.y));
    mix(static_cast<std::uint64_t>(agent.goal.x));
    mix(static_cast<std::uint64_t>(agent.goal.y));
  }
  return hash;
}

}  // namespace tess_test::movement
