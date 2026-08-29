#pragma once

// PIBT movement tier: priority inheritance with backtracking, composed with
// the joint commit's swap policy. The joint advance only admits moves along
// retained routes, so an agent whose route is blocked never considers
// stepping aside, and wedges whose resolution requires yielding onto an
// off-route tile persist regardless of retry patience. PIBT closes that
// gap: each agent ranks staying put and every legal neighbour, the
// highest-priority agent decides first, an agent whose chosen tile is held
// by an undecided peer lends that peer its priority so the peer decides —
// and possibly yields off its route — immediately, and a peer that cannot
// place anywhere backtracks the chooser to its next candidate.
//
// The gate evidence scoping this tier (optimization log, "Phase 3 Gate
// Re-Evaluation"): on thin cycle-rich maps the dominant stranding cause is
// sealing — settled arrivals cutting a live agent's goal off — which no
// movement tier can resolve; goal placement owns that hazard. PIBT's
// measured edge is live congestion: it eliminates most
// stranded-but-reachable residuals, resolves dead-end yields under
// `Forbid`, and keeps populations moving so fewer seals form.
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
// Like the distance-field product family, this tier requires an
// AlwaysResidentWorld because its ranking product indexes the full tile space.

#include <tess/core/shape.h>
#include <tess/sim/joint_movement.h>
#include <tess/sim/movement.h>
#include <tess/sim/path_agent.h>
#include <tess/sim/path_agent_tick.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

namespace tess {

namespace detail {

inline constexpr std::size_t kPibtMaxCandidates = 16;

// One pending decision in an inheritance chain. Chains are bounded only by
// the agent count, so frames live in caller-owned heap storage rather than
// on the process stack.
struct PibtFrame {
  struct Candidate {
    Coord3 coord{};
    std::uint32_t rank_value = 0;
  };
  std::uint32_t agent = 0;
  std::uint32_t next_candidate = 0;
  std::uint32_t candidate_count = 0;
  bool waiting = false;
  Candidate candidates[kPibtMaxCandidates] = {};
};

struct PibtPrioritiesAccess;

// The round-local half of `PibtPriorities`. `elapsed` is the caller's knob
// and stays public; the decision order and the inheritance stack are rebuilt
// from scratch every pass, and `frames` was also the last public member typed
// with a `detail` struct — promoting `PibtFrame` would have frozen an
// implementation layout, so the member moves out of sight instead.
struct PibtScratchState {
  std::vector<std::uint32_t> order;
  std::vector<PibtFrame> frames;

  void reserve(std::size_t agent_count) {
    order.reserve(agent_count);
    frames.reserve(agent_count);
  }
};

}  // namespace detail

// Public because it constrains public entry points. A caller whose ranking
// callable does not satisfy this gets the constraint named in the error,
// and previously could not name the thing it had to satisfy: the concept
// deciding whether their lambda is accepted lived in `detail`, which
// docs/style.md says carries no source-compatibility guarantee.
/// Requirements on a ranking callable supplied to the PIBT movement tier.
template <typename Ranking>
concept PibtRanking = requires(Ranking& rank, std::size_t agent, Coord3 coord) {
  { rank(agent, coord) } -> std::convertible_to<std::uint32_t>;
};

// A production ranking oracle for passable worlds with walls. The oracle
// contract at the top of this header names the two hazards a ranking must
// avoid; this adds the third one measured in the mixed-colony bench:
// distance heuristics that ignore terrain (Manhattan, or any field
// truncated short of the map's doorways) rate wall-adjacent tiles best and
// park agents at local minima that yields alone cannot fix. Each agent
// already carries an exact, terrain-aware plan — its retained A* route —
// so the oracle scores a candidate by its best LOCAL attachment to that
// route: the hop onto a route point within `attach_radius`, plus that
// point's remaining route length. The radius bound is load-bearing twice
// over. First, distant attachments are wall-blind: far-side route points
// lure agents onto a wall face (measured; the regression test pins it).
// Second, the default radius of 1 is the only radius that is
// passability-safe without inspecting terrain: distance-1 tile pairs are
// edge-adjacent, so two passable tiles at distance 1 are mutually
// reachable in one step, while distance-2 pairs can sit on opposite sides
// of a one-tile wall — exactly the lure again, one tile closer. PIBT only
// ever ranks an agent's own tile and its legal neighbours, so radius 1
// covers route-following and one-tile yields; deeper displacement falls
// through to the steer-back band, which is monotone toward the route.
// A candidate with no local attachment scores far above any attached one,
// graded by its distance to the nearest route point so displaced agents
// steer back to the corridor. Agents with no usable route (fewer than two
// points, or no goal) fall back to distance toward the goal, which is
// also the natural passable-terrain behavior. All distances are the
// overflow-safe three-axis Manhattan metric clamped into the score
// domain, so stacked 3D worlds rank levels apart as apart.
//
// Complexity: O(remaining route) per candidate query, bounded by the
// route lengths the planner produces. The scan starts at the agent's
// route cursor but the cursor does not advance during off-route PIBT
// walks, so callers should treat the full-route scan as the cost model.
/// Ranks PIBT candidates by local attachment to each agent's retained
/// route.
struct RouteAttachmentRanking {
  std::span<const PathAgentState> agents;
  const PathAgentRoutes* routes = nullptr;
  /// Maximum hop from a candidate onto a route point. The default of 1
  /// is the largest passability-safe radius; see the class comment
  /// before raising it.
  std::uint32_t attach_radius = 1;

  /// Scores below `kDetachedBase` are attached; higher steer back.
  static constexpr std::uint32_t kDetachedBase =
      std::numeric_limits<std::uint32_t>::max() / 8;

  [[nodiscard]] static auto clamped_distance(Coord3 lhs, Coord3 rhs) noexcept
      -> std::uint32_t {
    const std::uint64_t distance = manhattan_distance(lhs, rhs);
    return distance >= kDetachedBase ? kDetachedBase - 1
                                     : static_cast<std::uint32_t>(distance);
  }

  [[nodiscard]] auto operator()(std::size_t agent, Coord3 candidate) const
      -> std::uint32_t {
    const PathAgentState& state = agents[agent];
    if (routes == nullptr || agent >= routes->routes.size()) {
      return clamped_distance(candidate,
                              state.has_goal ? state.goal : state.position);
    }
    const std::vector<Coord3>& route = routes->routes[agent];
    if (!state.has_goal || route.size() < 2) {
      return clamped_distance(candidate,
                              state.has_goal ? state.goal : state.position);
    }
    std::uint32_t best = kDetachedBase;
    std::uint32_t nearest = kDetachedBase;
    for (std::size_t j = state.path_index; j < route.size(); ++j) {
      const std::uint32_t attach = clamped_distance(candidate, route[j]);
      nearest = std::min(nearest, attach);
      if (attach > attach_radius) {
        continue;
      }
      const std::uint32_t remaining =
          static_cast<std::uint32_t>(route.size() - 1 - j);
      best = std::min(best, attach + remaining);
    }
    if (best == kDetachedBase) {
      return kDetachedBase + nearest;
    }
    return best;
  }
};

// Index-paired with the agent span handed to the advance, exactly like
// `PathAgentRoutes`: a caller that reorders, removes, or compacts its agents
// between ticks must reset this state or keep it in sync itself.
/// Caller-owned adaptive priorities for the PIBT movement tier.
struct PibtPriorities {
  /// Ticks each agent has spent unarrived; higher decides earlier.
  std::vector<std::uint32_t> elapsed;

  /// Pre-sizes the containers for `agent_count` agents.
  void reserve(std::size_t agent_count) {
    elapsed.reserve(agent_count);
    scratch_.reserve(agent_count);
  }

 private:
  friend struct detail::PibtPrioritiesAccess;

  detail::PibtScratchState scratch_;
};

namespace detail {

// Matches the joint scratch's door, deliberately: this type could befriend
// the advance below directly, since both live in this header, but then two
// adjacent scratch types would hide their state by two different mechanisms
// and a caller reading one would learn nothing about the other. The same
// caveat applies — `detail` is the boundary, not unreachability.
struct PibtPrioritiesAccess {
  [[nodiscard]] static auto scratch(PibtPriorities& priorities) noexcept
      -> PibtScratchState& {
    return priorities.scratch_;
  }
};

}  // namespace detail

// One decision pass per step (`max_steps` passes, zero meaning paused as in
// the joint advance; a pass that moves nobody ends the call early since it
// would repeat identically):
//   1. Adaptive priorities update: unarrived agents' `elapsed` increments,
//      arrived agents reset to zero; decision order is elapsed descending
//      with span index as the deterministic tie-break. An agent standing on
//      a tile its class cannot pass fails `ImpassableFrom` without deciding,
//      exactly as `commit_movement_intent` does.
//   2. Each undecided agent, in that order, considers staying put plus every
//      legal transition of its movement class (enumerated through the
//      resolved transition model, so hex and diagonal lattices are handled),
//      skipping reserved tiles and tiles occupied by anything outside the
//      agent span, ranked by the caller's oracle (lower is better;
//      enumeration order breaks ties).
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
//      records an `Occupied` block (or `ImpassableFrom` for an impassable
//      source) with the usual retry semantics.
/// Advances agents one step with PIBT decisions and joint-commit application.
template <typename World, typename ClassOrTag, typename OccupancyTag,
          typename ReservationTag, typename Ranking, typename OnCommit>
  requires PibtRanking<Ranking> &&
           std::invocable<OnCommit&, std::size_t, Coord3, Coord3>
auto advance_path_agents_with_pibt(
    World& world, std::span<PathAgentState> agents,
    const PathAgentRoutes& routes, PibtPriorities& priorities,
    JointMoveScratch& scratch_storage, Ranking&& rank, JointMoveOptions options,
    PathAgentAdvanceOptions advance_options, OnCommit&& on_commit,
    diagnostics::FlowAccounting* accounting = nullptr) -> JointMoveStats {
  using Shape = typename World::shape_type;
  using Class = movement::movement_class_of<ClassOrTag>;
  using Model = ResolvedTransitionModel<World, Class, AdjacentTransitions>;
  static_assert(
      std::is_same_v<typename World::residency_type, AlwaysResident>,
      "advance_path_agents_with_pibt requires an AlwaysResidentWorld; use "
      "another movement tier for sparse worlds.");
  TESS_ASSERT(routes.routes.size() >= agents.size());
  auto& scratch = detail::JointMoveScratchAccess::state(scratch_storage);
  auto& decision = detail::PibtPrioritiesAccess::scratch(priorities);
  const auto model = Model{AdjacentTransitions{}};
  JointMoveStats stats;
  if (advance_options.max_steps == 0) {
    return stats;  // paused movement, as in the joint advance
  }
  const auto n = agents.size();
  const auto none = static_cast<std::uint32_t>(-1);
  constexpr std::uint8_t undecided = 0;
  constexpr std::uint8_t deciding = 1;
  constexpr std::uint8_t decided = 2;

  for (std::size_t pass = 0; pass < advance_options.max_steps; ++pass) {
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
    decision.order.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
      decision.order[i] = static_cast<std::uint32_t>(i);
    }
    // Insertion sort keeps the warm path allocation-free (std::stable_sort may
    // allocate a temporary buffer) and is stable, so span index breaks ties.
    for (std::size_t i = 1; i < n; ++i) {
      const auto value = decision.order[i];
      std::size_t j = i;
      while (j > 0 && priorities.elapsed[decision.order[j - 1]] <
                          priorities.elapsed[value]) {
        decision.order[j] = decision.order[j - 1];
        --j;
      }
      decision.order[j] = value;
    }

    // Occupant index over every agent (movers or not), as in the joint pass.
    scratch.desired.assign(n, Coord3{});
    scratch.state.assign(n, undecided);
    scratch.failure.assign(n, static_cast<std::uint8_t>(MovementStatus::Moved));
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

    const auto find_occupant = [&](Coord3 coord) -> std::uint32_t {
      return detail::joint_find_occupant(scratch, tile_key<Shape>(coord).value);
    };
    const auto claim = [&](Coord3 coord) -> bool {
      return detail::joint_claim(scratch, tile_key<Shape>(coord).value);
    };

    // 2-4: the decision machine. Decisions cannot share one scratch candidate
    // buffer (an inheritance chain holds every participant's ranked
    // candidates at once), and chain length is bounded only by the agent
    // count, so each participant gets a fixed-size frame on the caller-owned
    // `PibtPriorities` stack — never the process stack.
    auto& frames = decision.frames;
    frames.clear();

    // Starts agent `i` deciding: either pushes its frame or fails
    // immediately. An impassable source cannot be vacated, exactly as
    // `commit_movement_intent` fails `ImpassableFrom`; the agent keeps its tile
    // and an inheriting caller must backtrack.
    //
    // An agent with no goal, or one whose lifecycle already ended at
    // `Unreachable`, cannot be vacated either. Only inheritance reaches such
    // an agent: the priority loop skips them, and the apply pass tests the
    // same condition before touching a stay-put agent. Reached through
    // inheritance they would be shoved off their tile by passing traffic and
    // rewritten to `Blocked`, restarting a terminal lifecycle. Treat them the
    // way an impassable source is treated -- claim the tile so later deciders
    // are vertex-rejected rather than stacking on it, and make the inheriting
    // parent backtrack.
    const auto start_deciding = [&](std::size_t i) -> bool {
      scratch.state[i] = deciding;
      const auto position = agents[i].position;
      if (!agents[i].has_goal ||
          agents[i].phase == PathAgentPhase::Unreachable) {
        (void)claim(position);
        scratch.failure[i] =
            static_cast<std::uint8_t>(MovementStatus::Occupied);
        scratch.state[i] = decided;
        return false;
      }
      if (!detail::is_passable<World, ClassOrTag>(world, position)) {
        (void)claim(position);
        scratch.failure[i] =
            static_cast<std::uint8_t>(MovementStatus::ImpassableFrom);
        scratch.state[i] = decided;
        return false;
      }
      frames.emplace_back();
      auto& frame = frames.back();
      frame.agent = static_cast<std::uint32_t>(i);
      const auto index = detail::tile_index<Shape>(position);
      model.for_each_forward(world, position, index, [&](auto probe) {
        if (probe.availability != TransitionAvailability::Legal ||
            probe.cost_overflow ||
            frame.candidate_count >= detail::kPibtMaxCandidates - 1) {
          return;
        }
        const auto coord = detail::tile_coord<Shape>(probe.to_index);
        if (!detail::is_passable<World, ClassOrTag>(world, coord)) {
          return;
        }
        if (world.template field<ReservationTag>(coord)) {
          return;  // application-owned do-not-enter, as in the joint pass
        }
        if (world.template field<OccupancyTag>(coord) &&
            find_occupant(coord) == none) {
          return;  // occupied by something outside this span, as in the joint
                   // pass's external-occupant rejection
        }
        frame.candidates[frame.candidate_count++] = {coord, rank(i, coord)};
      });
      frame.candidates[frame.candidate_count++] = {position, rank(i, position)};
      // Insertion sort: allocation-free and stable, so the model's
      // enumeration order breaks ranking ties deterministically.
      for (std::uint32_t a = 1; a < frame.candidate_count; ++a) {
        const auto value = frame.candidates[a];
        std::uint32_t b = a;
        while (b > 0 && frame.candidates[b - 1].rank_value > value.rank_value) {
          frame.candidates[b] = frame.candidates[b - 1];
          --b;
        }
        frame.candidates[b] = value;
      }
      return true;
    };

    const auto run_decision = [&](std::size_t root) {
      if (!start_deciding(root)) {
        return;
      }
      bool child_succeeded = false;
      while (!frames.empty()) {
        auto& frame = frames.back();
        const auto i = static_cast<std::size_t>(frame.agent);
        const auto position = agents[i].position;
        if (frame.waiting) {
          frame.waiting = false;
          if (child_succeeded) {
            scratch.state[i] = decided;
            frames.pop_back();
            child_succeeded = true;
            continue;
          }
          // The inherited peer failed and stays on its tile; its claim must
          // survive to protect it from later deciders (pypibt re-marks
          // `occupied_nxt` with the failed agent), so only this agent's
          // tentative desire is undone before trying the next candidate.
          scratch.desired[i] = position;
          ++frame.next_candidate;
        }
        if (frame.next_candidate >= frame.candidate_count) {
          // Nowhere to place: keep the tile so inheritance chains cannot
          // displace this agent, and report failure to the inheriting caller.
          (void)claim(position);
          scratch.desired[i] = position;
          scratch.state[i] = decided;
          frames.pop_back();
          child_succeeded = false;
          continue;
        }
        const auto v = frame.candidates[frame.next_candidate].coord;
        const auto occupant = find_occupant(v);
        const bool moving = !(v == position);
        // Edge conflict: the occupant of `v` is entering this agent's tile.
        // `desired` starts at each agent's own position, so equality with
        // this agent's position means the occupant actively chose it —
        // whether it is fully decided or is the inheritance parent further
        // down the stack (the parent writes `desired` before its peer
        // decides, as pypibt sets `Q_to`).
        bool is_swap = false;
        if (moving && occupant != none &&
            scratch.desired[occupant] == position) {
          bool allow = false;
          switch (options.swap_policy) {
            case SwapPolicy::Permit:
              allow = true;
              break;
            case SwapPolicy::PermitOnDeadlock:
              allow =
                  agents[i].blocked_retries >= options.deadlock_ticks &&
                  agents[occupant].blocked_retries >= options.deadlock_ticks;
              break;
            case SwapPolicy::Forbid:
              allow = false;
              break;
          }
          if (!allow) {
            ++stats.swaps_denied;
            ++frame.next_candidate;
            continue;
          }
          is_swap = true;
        }
        if (!claim(v)) {
          ++frame.next_candidate;
          continue;  // vertex conflict
        }
        if (is_swap) {
          // Counted only once the exchange is actually secured: a
          // policy-allowed swap can still lose its destination to an earlier
          // claim.
          ++stats.swaps;
        }
        scratch.desired[i] = v;
        if (moving && occupant != none &&
            scratch.state[occupant] == undecided) {
          // Priority inheritance: the occupant decides next with this
          // agent's turn. `frame` may be invalidated by the push, so the
          // waiting flag is set first.
          frame.waiting = true;
          if (!start_deciding(static_cast<std::size_t>(occupant))) {
            child_succeeded = false;  // resolved inline; waiting handles it
          }
          continue;
        }
        scratch.state[i] = decided;
        frames.pop_back();
        child_succeeded = true;
      }
    };

    for (const auto i : decision.order) {
      const auto agent_index = static_cast<std::size_t>(i);
      if (scratch.state[agent_index] != undecided) {
        continue;
      }
      if (!agents[agent_index].has_goal ||
          agents[agent_index].phase == PathAgentPhase::Unreachable) {
        scratch.state[agent_index] = decided;
        continue;
      }
      run_decision(agent_index);
    }

    // 5: apply the configuration with the joint commit's semantics.
    const auto advanced_before = stats.frame.advanced;
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
          const auto status =
              scratch.failure[i] ==
                      static_cast<std::uint8_t>(MovementStatus::Moved)
                  ? MovementStatus::Occupied
                  : static_cast<MovementStatus>(scratch.failure[i]);
          record_movement_failure(stats.frame.movement_failures, status);
          detail::block_path_agent(agent, status);
          ++stats.frame.blocked_waits;
        }
        continue;
      }
      world.template field<OccupancyTag>(to) = true;
      world.template field<ReservationTag>(to) = false;
      if (advance_options.movement_dirty_mask) {
        world.mark_dirty(chunk_key<Shape>(chunk_coord<Shape>(from)),
                         advance_options.movement_dirty_mask,
                         Box3{from, Extent3{1, 1, 1}});
        world.mark_dirty(chunk_key<Shape>(chunk_coord<Shape>(to)),
                         advance_options.movement_dirty_mask,
                         Box3{to, Extent3{1, 1, 1}});
      }
      const auto& route = routes.routes[i];
      const bool on_route =
          detail::has_next_step(agent.path_index, route.size()) &&
          route[agent.path_index + 1] == to;
      agent.position = to;
      if (on_route) {
        ++agent.path_index;
        detail::resume_path_agent(agent);
      } else {
        // Off the retained route: drop it so scoped resubmission replans from
        // the new position. A Blocked agent with no last result is the state
        // the tick drivers already treat as "needs planning".
        agent.last_result.reset();
        agent.phase = PathAgentPhase::Blocked;
        agent.blocked_retries = 0;
      }
      scratch.committed.push_back(static_cast<std::uint32_t>(i));
      scratch.committed_from.push_back(from);
      ++stats.frame.advanced;
      // `has_goal` gates the comparison because `clear_path_agent_goal`
      // zeroes `goal`, so a goalless agent standing on the origin tile would
      // otherwise register an arrival for a journey that was never admitted
      // -- inflating `completed` and breaking the retention identity. Every
      // other arrival site reaches this check behind the same gate.
      if (agent.has_goal && agent.position == agent.goal) {
        arrive_path_agent(agent, accounting);
        agent.last_result = PathStatus::Found;
        // Reset priority at the commit itself: a caller may assign a new
        // goal before the next pass, and the journey it just finished must
        // not carry its accumulated priority into the new one.
        priorities.elapsed[i] = 0;
        ++stats.frame.arrived;
      }
    }
    // Same observer contract as the joint advance: callbacks observe the fully
    // applied configuration; an injective tile mirror must buffer the batch.
    for (std::size_t k = 0; k < scratch.committed.size(); ++k) {
      const auto index = static_cast<std::size_t>(scratch.committed[k]);
      on_commit(index, scratch.committed_from[k], agents[index].position);
    }
    // A pass that moved nobody would repeat identically (a uniform elapsed
    // increment cannot reorder decisions), so further passes are pure waste.
    if (stats.frame.advanced == advanced_before) {
      break;
    }
  }
  return stats;
}

/// Advances agents with PIBT decisions and no commit observer.
template <typename World, typename ClassOrTag, typename OccupancyTag,
          typename ReservationTag, typename Ranking>
  requires PibtRanking<Ranking>
auto advance_path_agents_with_pibt(
    World& world, std::span<PathAgentState> agents,
    const PathAgentRoutes& routes, PibtPriorities& priorities,
    JointMoveScratch& scratch, Ranking&& rank, JointMoveOptions options = {},
    PathAgentAdvanceOptions advance_options = {},
    diagnostics::FlowAccounting* accounting = nullptr) -> JointMoveStats {
  return advance_path_agents_with_pibt<World, ClassOrTag, OccupancyTag,
                                       ReservationTag>(
      world, agents, routes, priorities, scratch, std::forward<Ranking>(rank),
      options, advance_options, [](std::size_t, Coord3, Coord3) {}, accounting);
}

// Mirrors `tick_weighted_path_agents_with_joint_movement` with the PIBT
// advance in place of the joint one; planning semantics are identical.
/// Advances one weighted path-agent tick with PIBT movement decisions.
template <typename World, typename Class, std::uint32_t MaxCost,
          typename OccupancyTag, typename ReservationTag, typename Ranking>
  requires PibtRanking<Ranking>
[[nodiscard]] auto tick_weighted_path_agents_with_pibt(
    PathAgentTickState& state, World& world, std::span<PathAgentState> agents,
    PathRequestRuntime& runtime, PibtPriorities& priorities,
    JointMoveScratch& scratch, Ranking&& rank,
    PathAgentTickOptions options = {}, JointMoveOptions pibt_options = {},
    const RegionGraphT<typename World::residency_type>* graph = nullptr,
    JointMoveStats* pibt_stats = nullptr) -> PathAgentTickStats {
  PathAgentTickStats stats;
  stats.tick = advance_sim_tick(state.clock);

  const bool repath_needed = prepare_path_agent_processing(
      agents, options, stats, state.flow_accounting);
  state.routes.ensure_size(agents.size());
  if (state.pathing_dirty || repath_needed) {
    const auto scope =
        state.pathing_dirty ? PathSubmitScope::All : PathSubmitScope::NeedsOnly;
    stats.pathing = process_weighted_path_agents<World, Class, MaxCost>(
        world, agents, runtime, options.cache_policy, graph, scope,
        &state.routes, state.flow_accounting);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  auto moved =
      advance_path_agents_with_pibt<World, Class, OccupancyTag, ReservationTag>(
          world, agents, state.routes, priorities, scratch,
          std::forward<Ranking>(rank), pibt_options,
          PathAgentAdvanceOptions{options.max_steps,
                                  options.movement_dirty_mask},
          state.flow_accounting);
  stats.movement = moved.frame;
  if (pibt_stats != nullptr) {
    *pibt_stats = moved;
  }
  return stats;
}

}  // namespace tess
