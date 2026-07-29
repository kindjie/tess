#pragma once

// PIBT movement tier: priority inheritance with backtracking, composed with
// the joint commit's swap policy. The joint advance resolves chains and
// cycles along retained routes; on cycle-rich geometry that is not enough —
// a width-2 ring (biconnected, provably jointly solvable) strands roughly a
// fifth of its agents under `SwapPolicy::Permit` regardless of retry
// patience, because no agent ever considers an alternative tile. PIBT does:
// each agent ranks staying put and every legal neighbour, the
// highest-priority agent decides first, an agent whose chosen tile is held
// by an undecided peer lends that peer its priority so the peer decides
// immediately, and a peer that cannot place anywhere backtracks the chooser
// to its next candidate.
//
// Two contracts carry the tier's correctness:
//
// - **The ranking oracle must share the agent's movement-class
//   passability.** A terrain-only oracle under a settled-aware class rates
//   standing beside an obstruction above any detour, and the agent parks
//   there forever. This failure mode is proven in the tier's tests.
// - **Priorities must be adaptive** — incremented while an agent is
//   unarrived and reset on arrival — or agents can starve. PIBT's
//   reachability guarantee (every agent reaches its goal in finite time on
//   graphs whose adjacent vertices share a cycle of length >= 3) depends on
//   this rule.
//
// Like the distance-field product family, this tier is dense-only for now;
// the sparse slice lands with the product family's NodeIndexSpace port.

#include <tess/core/shape.h>
#include <tess/sim/joint_movement.h>
#include <tess/sim/movement.h>
#include <tess/sim/path_agent.h>
#include <tess/sim/path_agent_tick.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

namespace tess {

// Index-paired with the agent span handed to the advance, exactly like
// `PathAgentRoutes`: a caller that reorders, removes, or compacts its agents
// between ticks must reset this state or keep it in sync itself.
/// Caller-owned adaptive priorities for the PIBT movement tier.
struct PibtPriorities {
  /// Ticks each agent has spent unarrived; higher decides earlier.
  std::vector<std::uint32_t> elapsed;
  /// Scratch decision order; contents are an implementation detail.
  std::vector<std::uint32_t> order;

  /// Pre-sizes both containers for `agent_count` agents.
  void reserve(std::size_t agent_count) {
    elapsed.reserve(agent_count);
    order.reserve(agent_count);
  }
};

namespace detail {

template <typename Ranking>
concept PibtRanking = requires(Ranking& rank, std::size_t agent, Coord3 coord) {
  { rank(agent, coord) } -> std::convertible_to<std::uint32_t>;
};

}  // namespace detail

// One decision pass per call (PIBT is single-step by construction):
//   1. Adaptive priorities update: unarrived agents' `elapsed` increments,
//      arrived agents reset to zero; decision order is elapsed descending
//      with span index as the deterministic tie-break.
//   2. Each undecided agent, in that order, considers staying put plus every
//      legal transition of its movement class (enumerated through the
//      resolved transition model, so hex and diagonal lattices are handled),
//      skipping reserved tiles, ranked by the caller's oracle (lower is
//      better; enumeration order breaks ties).
//   3. Vertex conflicts skip the candidate. Edge conflicts (the candidate's
//      occupant has already decided to enter this agent's tile) follow
//      `SwapPolicy`, sharing `JointMoveOptions` with the joint advance.
//   4. A candidate held by an undecided peer is claimed tentatively and the
//      peer inherits the decision turn; if the peer cannot place anywhere,
//      the next candidate is tried (backtracking) while the claim stays with
//      the failed peer, protecting its tile from later deciders. An agent
//      with no placeable candidate keeps its tile.
//   5. The decided configuration applies as a set with the joint commit's
//      semantics: sources clear before destinations set, reservations clear
//      on entry, dirty marks match `commit_movement_intent`, and observer
//      callbacks fire only after the whole configuration is applied. A move
//      off the retained route drops the route (`NoPath`) so scoped
//      resubmission replans; an agent that wanted to move and could not
//      records an `Occupied` block with the usual retry semantics.
/// Advances agents one step with PIBT decisions and joint-commit application.
template <typename World, typename ClassOrTag, typename OccupancyTag,
          typename ReservationTag, typename Ranking, typename OnCommit>
  requires detail::PibtRanking<Ranking> &&
           std::invocable<OnCommit&, std::size_t, Coord3, Coord3>
auto advance_path_agents_with_pibt(
    World& world, std::span<PathAgentState> agents,
    const PathAgentRoutes& routes, PibtPriorities& priorities,
    JointMoveScratch& scratch, Ranking&& rank, JointMoveOptions options,
    std::uint32_t movement_dirty_mask, OnCommit&& on_commit) -> JointMoveStats {
  using Shape = typename World::shape_type;
  using Class = movement::movement_class_of<ClassOrTag>;
  using Model = ResolvedTransitionModel<World, Class, AdjacentTransitions>;
  static_assert(
      std::is_same_v<typename World::residency_type, AlwaysResident>,
      "advance_path_agents_with_pibt is dense-only; the sparse slice lands "
      "with the distance-field product NodeIndexSpace port.");
  TESS_ASSERT(routes.routes.size() >= agents.size());
  const auto model = Model{AdjacentTransitions{}};
  JointMoveStats stats;
  const auto n = agents.size();
  const auto none = static_cast<std::uint32_t>(-1);
  constexpr std::uint8_t undecided = 0;
  constexpr std::uint8_t deciding = 1;
  constexpr std::uint8_t decided = 2;

  // 1: adaptive priorities and decision order.
  if (priorities.elapsed.size() < n) {
    priorities.elapsed.resize(n, 0);
  }
  for (std::size_t i = 0; i < n; ++i) {
    const auto active =
        agents[i].has_goal && agents[i].phase != PathAgentPhase::Unreachable;
    auto& elapsed = priorities.elapsed[i];
    elapsed = active ? (elapsed == std::numeric_limits<std::uint32_t>::max()
                            ? elapsed
                            : elapsed + 1)
                     : 0;
  }
  priorities.order.resize(n);
  for (std::size_t i = 0; i < n; ++i) {
    priorities.order[i] = static_cast<std::uint32_t>(i);
  }
  // Insertion sort keeps the warm path allocation-free (std::stable_sort may
  // allocate a temporary buffer) and is stable, so span index breaks ties.
  for (std::size_t i = 1; i < n; ++i) {
    const auto value = priorities.order[i];
    std::size_t j = i;
    while (j > 0 && priorities.elapsed[priorities.order[j - 1]] <
                        priorities.elapsed[value]) {
      priorities.order[j] = priorities.order[j - 1];
      --j;
    }
    priorities.order[j] = value;
  }

  // Occupant index over every agent (movers or not), as in the joint pass.
  scratch.desired.assign(n, Coord3{});
  scratch.state.assign(n, undecided);
  scratch.claimed.clear();
  scratch.committed.clear();
  scratch.committed_from.clear();
  scratch.occupant_key.clear();
  scratch.occupant_agent.clear();
  for (std::size_t i = 0; i < n; ++i) {
    scratch.desired[i] = agents[i].position;
    scratch.occupant_key.push_back(tile_key<Shape>(agents[i].position).value);
    scratch.occupant_agent.push_back(static_cast<std::uint32_t>(i));
  }
  {
    auto& keys = scratch.occupant_key;
    auto& vals = scratch.occupant_agent;
    for (std::size_t i = 1; i < keys.size(); ++i) {
      auto key = keys[i];
      auto val = vals[i];
      std::size_t j = i;
      while (j > 0 && keys[j - 1] > key) {
        keys[j] = keys[j - 1];
        vals[j] = vals[j - 1];
        --j;
      }
      keys[j] = key;
      vals[j] = val;
    }
  }

  // Candidate buffers reuse scratch: `cycle_walk` holds ranked candidate
  // coordinate keys per decision frame is not possible (recursion), so the
  // recursive decision uses stack-local fixed candidate arrays bounded by the
  // lattice's maximum degree plus one.
  struct Candidate {
    Coord3 coord{};
    std::uint32_t rank_value = 0;
  };
  constexpr std::size_t kMaxCandidates = 16;

  const auto find_occupant = [&](Coord3 coord) -> std::uint32_t {
    return detail::joint_find_occupant(scratch, tile_key<Shape>(coord).value);
  };
  const auto claim = [&](Coord3 coord) -> bool {
    return detail::joint_claim(scratch, tile_key<Shape>(coord).value);
  };
  // 2-4: the recursive decision.
  const auto decide = [&](auto&& self, std::size_t i) -> bool {
    scratch.state[i] = deciding;
    Candidate candidates[kMaxCandidates];
    std::size_t count = 0;
    const auto position = agents[i].position;
    const auto index = detail::tile_index<Shape>(position);
    model.for_each_forward(world, position, index, [&](auto probe) {
      if (probe.availability != TransitionAvailability::Legal ||
          probe.cost_overflow || count >= kMaxCandidates - 1) {
        return;
      }
      const auto coord = detail::tile_coord<Shape>(probe.to_index);
      if (!detail::is_passable<World, ClassOrTag>(world, coord)) {
        return;
      }
      if (world.template field<ReservationTag>(coord)) {
        return;  // application-owned do-not-enter, as in the joint pass
      }
      candidates[count++] = Candidate{coord, rank(i, coord)};
    });
    candidates[count++] = Candidate{position, rank(i, position)};
    // Insertion sort: allocation-free and stable, so the model's enumeration
    // order breaks ranking ties deterministically.
    for (std::size_t a = 1; a < count; ++a) {
      const auto value = candidates[a];
      std::size_t b = a;
      while (b > 0 && candidates[b - 1].rank_value > value.rank_value) {
        candidates[b] = candidates[b - 1];
        --b;
      }
      candidates[b] = value;
    }

    for (std::size_t k = 0; k < count; ++k) {
      const auto v = candidates[k].coord;
      const auto occupant = find_occupant(v);
      const bool moving = !(v == position);
      // Edge conflict: the occupant of `v` is entering this agent's tile.
      // `desired` starts at each agent's own position, so equality with this
      // agent's position means the occupant actively chose it — whether it is
      // fully decided or is the inheritance parent still mid-decision (the
      // parent writes `desired` before recursing, as pypibt sets `Q_to`).
      if (moving && occupant != none && scratch.desired[occupant] == position) {
        bool allow = false;
        switch (options.swap_policy) {
          case SwapPolicy::Permit:
            allow = true;
            break;
          case SwapPolicy::PermitOnDeadlock:
            allow = agents[i].blocked_retries >= options.deadlock_ticks &&
                    agents[occupant].blocked_retries >= options.deadlock_ticks;
            break;
          case SwapPolicy::Forbid:
            allow = false;
            break;
        }
        if (!allow) {
          ++stats.swaps_denied;
          continue;
        }
        ++stats.swaps;
      }
      if (!claim(v)) {
        continue;  // vertex conflict
      }
      scratch.desired[i] = v;
      if (moving && occupant != none && scratch.state[occupant] == undecided) {
        // Priority inheritance: the occupant decides now; if it cannot place
        // anywhere, try the next candidate. The claim on `v` must NOT be
        // released — the failed peer stays on `v`, so the claim now protects
        // it from later deciders (pypibt re-marks `occupied_nxt` with the
        // failed agent for the same reason).
        if (!self(self, occupant)) {
          scratch.desired[i] = position;
          continue;
        }
      }
      scratch.state[i] = decided;
      return true;
    }
    // Nowhere to place: keep the tile so inheritance chains cannot displace
    // this agent, and report failure to the inheriting caller.
    (void)claim(position);
    scratch.desired[i] = position;
    scratch.state[i] = decided;
    return false;
  };

  for (const auto i : priorities.order) {
    const auto agent_index = static_cast<std::size_t>(i);
    if (scratch.state[agent_index] != undecided) {
      continue;
    }
    if (!agents[agent_index].has_goal ||
        agents[agent_index].phase == PathAgentPhase::Unreachable) {
      scratch.state[agent_index] = decided;
      continue;
    }
    (void)decide(decide, agent_index);
  }

  // 5: apply the configuration with the joint commit's semantics.
  for (std::size_t i = 0; i < n; ++i) {
    if (!(scratch.desired[i] == agents[i].position)) {
      world.template field<OccupancyTag>(agents[i].position) = false;
    }
  }
  for (std::size_t i = 0; i < n; ++i) {
    auto& agent = agents[i];
    const auto from = agent.position;
    const auto to = scratch.desired[i];
    if (to == from) {
      if (agent.has_goal && agent.phase != PathAgentPhase::Unreachable) {
        record_movement_failure(stats.frame.movement_failures,
                                MovementStatus::Occupied);
        detail::block_path_agent(agent, MovementStatus::Occupied);
        ++stats.frame.blocked_waits;
      }
      continue;
    }
    world.template field<OccupancyTag>(to) = true;
    world.template field<ReservationTag>(to) = false;
    if (movement_dirty_mask != 0) {
      world.mark_dirty(chunk_key<Shape>(chunk_coord<Shape>(from)),
                       movement_dirty_mask, Box3{from, Extent3{1, 1, 1}});
      world.mark_dirty(chunk_key<Shape>(chunk_coord<Shape>(to)),
                       movement_dirty_mask, Box3{to, Extent3{1, 1, 1}});
    }
    const auto& route = routes.routes[i];
    const bool on_route = agent.path_index + 1 < route.size() &&
                          route[agent.path_index + 1] == to;
    agent.position = to;
    if (on_route) {
      ++agent.path_index;
      detail::resume_path_agent(agent);
    } else {
      // Off the retained route: drop it so scoped resubmission replans from
      // the new position. Blocked-with-NoPath is the state the tick drivers
      // already treat as "needs planning".
      agent.status = PathStatus::NoPath;
      agent.phase = PathAgentPhase::Blocked;
      agent.blocked_retries = 0;
    }
    scratch.committed.push_back(static_cast<std::uint32_t>(i));
    scratch.committed_from.push_back(from);
    ++stats.frame.advanced;
    if (agent.position == agent.goal) {
      clear_path_agent_goal(agent);
      agent.status = PathStatus::Found;
      ++stats.frame.arrived;
    }
  }
  // Same observer contract as the joint advance: callbacks observe the fully
  // applied configuration; an injective tile mirror must buffer the batch.
  for (std::size_t k = 0; k < scratch.committed.size(); ++k) {
    const auto index = static_cast<std::size_t>(scratch.committed[k]);
    on_commit(index, scratch.committed_from[k], agents[index].position);
  }
  return stats;
}

/// Advances agents with PIBT decisions and no commit observer.
template <typename World, typename ClassOrTag, typename OccupancyTag,
          typename ReservationTag, typename Ranking>
  requires detail::PibtRanking<Ranking>
auto advance_path_agents_with_pibt(World& world,
                                   std::span<PathAgentState> agents,
                                   const PathAgentRoutes& routes,
                                   PibtPriorities& priorities,
                                   JointMoveScratch& scratch, Ranking&& rank,
                                   JointMoveOptions options = {},
                                   std::uint32_t movement_dirty_mask = 0)
    -> JointMoveStats {
  return advance_path_agents_with_pibt<World, ClassOrTag, OccupancyTag,
                                       ReservationTag>(
      world, agents, routes, priorities, scratch, std::forward<Ranking>(rank),
      options, movement_dirty_mask, [](std::size_t, Coord3, Coord3) {});
}

// Mirrors `tick_weighted_path_agents_with_joint_movement` with the PIBT
// advance in place of the joint one; planning semantics are identical.
/// Advances one weighted path-agent tick with PIBT movement decisions.
template <typename World, typename Class, std::uint32_t MaxCost,
          typename OccupancyTag, typename ReservationTag, typename Ranking>
  requires detail::PibtRanking<Ranking>
[[nodiscard]] auto tick_weighted_path_agents_with_pibt(
    PathAgentTickState& state, World& world, std::span<PathAgentState> agents,
    PathRequestRuntime& runtime, PibtPriorities& priorities,
    JointMoveScratch& scratch, Ranking&& rank,
    PathAgentTickOptions options = {}, JointMoveOptions pibt_options = {},
    std::uint32_t movement_dirty_mask = 0,
    const RegionGraphT<typename World::residency_type>* graph = nullptr,
    JointMoveStats* pibt_stats = nullptr) -> PathAgentTickStats {
  PathAgentTickStats stats;
  stats.tick = advance_sim_tick(state.clock);

  const bool repath_needed =
      prepare_path_agent_processing(agents, options, stats);
  state.routes.ensure_size(agents.size());
  if (state.pathing_dirty || repath_needed) {
    const auto scope =
        state.pathing_dirty ? PathSubmitScope::All : PathSubmitScope::NeedsOnly;
    stats.pathing = process_weighted_path_agents<World, Class, MaxCost>(
        world, agents, runtime, options.cache_policy, graph, scope,
        &state.routes);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  auto moved =
      advance_path_agents_with_pibt<World, Class, OccupancyTag, ReservationTag>(
          world, agents, state.routes, priorities, scratch,
          std::forward<Ranking>(rank), pibt_options, movement_dirty_mask);
  stats.movement = moved.frame;
  if (pibt_stats != nullptr) {
    *pibt_stats = moved;
  }
  return stats;
}

}  // namespace tess
