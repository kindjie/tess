#pragma once

// Joint movement commit: decide one tick's moves as a set, so that a move
// into a tile being vacated in the same tick is admissible. The per-agent
// `commit_movement_intent` validates each destination against current state,
// which makes chains ("everyone steps forward together"), rotations (a cycle
// of agents shifts one place), and swaps (the two-agent cycle) unreachable by
// construction — the front agent's tile is still occupied at the moment the
// rear agent validates. This header supplies the batch alternative while
// reusing the per-agent validation for everything that is not occupancy:
// bounds, passability, adjacency, topology, and reservations behave exactly
// as they do in `commit_movement_intent`.
//
// Whether the two-agent cycle may resolve is a semantic question, not a
// tuning knob: admitting it means both agents traverse the same edge in
// opposite directions in one tick, which standard multi-agent path finding
// forbids because embodied agents cannot pass through each other. It is
// therefore an explicit `SwapPolicy`, and the default forbids it. Cycles of
// length three or more involve no shared edge — every member vacates its
// tile simultaneously — and are always admitted.
//
// Determinism matches the per-agent advance: outcomes are deterministic
// given the caller's agent span order (ECS adapters already require a
// replay-stable order), and input-order invariance is a documented non-goal,
// exactly as for `advance_path_agents_with_movement`.

#include <tess/core/shape.h>
#include <tess/sim/movement.h>
#include <tess/sim/path_agent.h>
#include <tess/sim/path_agent_tick.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace tess {

// - Forbid: the standard MAPF constraint and the zero-surprise default; a
//   mutually blocked pair stays Blocked and retries under the usual budget.
// - Permit: a mutually blocked pair exchanges tiles this tick.
// - PermitOnDeadlock: the pair exchanges only after both members have been
//   blocked for `JointMoveOptions::deadlock_ticks` consecutive ticks, so
//   ordinary passing traffic never interpenetrates.
/// Selects whether two mutually blocked agents may exchange tiles.
enum class SwapPolicy : std::uint8_t {
  Forbid,
  Permit,
  PermitOnDeadlock,
};

/// Configures cycle admission for one joint movement pass.
struct JointMoveOptions {
  SwapPolicy swap_policy = SwapPolicy::Forbid;
  /// Consecutive blocked ticks both members of a 2-cycle must have accrued
  /// before `SwapPolicy::PermitOnDeadlock` admits the exchange. This gates on
  /// each member's own `blocked_retries` streak, not on how long the specific
  /// pair has been mutually blocked: two long-congested agents that have only
  /// just met exchange immediately. Per-pair streak tracking is deliberately
  /// deferred until a consumer needs the stricter gate.
  std::uint32_t deadlock_ticks = 4;
};

/// Reports joint-admission outcomes alongside the standard movement stats.
struct JointMoveStats {
  PathAgentFrameStats frame{};
  /// Moves admitted only because their destination was vacated this tick.
  std::size_t chained = 0;
  /// Cycles of length >= 3 rotated one place.
  std::size_t rotations = 0;
  /// Two-agent cycles exchanged under the active policy.
  std::size_t swaps = 0;
  /// Two-agent cycles refused by the active policy.
  std::size_t swaps_denied = 0;
};

struct JointMoveScratch;

namespace detail {

struct JointMoveScratchAccess;

// The round buffers themselves. They were public members of
// `JointMoveScratch` under a comment calling them an implementation detail,
// which left the layout inside the 1.0 promise anyway: a consumer could size,
// read or overwrite any of them, and nothing but that comment said what the
// pass would then do. They live here so the promise covers what the type is
// for — reserving storage — and not how a round is bookkept.
struct JointMoveScratchState {
  std::vector<std::uint64_t> occupant_key;
  std::vector<std::uint32_t> occupant_agent;
  std::vector<std::uint64_t> claimed;
  std::vector<Coord3> desired;
  std::vector<std::uint8_t> state;
  std::vector<std::uint8_t> failure;
  std::vector<std::uint32_t> cycle_walk;
  std::vector<std::uint8_t> on_walk;
  std::vector<std::uint8_t> walked;
  std::vector<std::uint32_t> committed;
  std::vector<Coord3> committed_from;

  void reserve(std::size_t agent_count) {
    occupant_key.reserve(agent_count);
    occupant_agent.reserve(agent_count);
    claimed.reserve(agent_count);
    desired.reserve(agent_count);
    state.reserve(agent_count);
    failure.reserve(agent_count);
    cycle_walk.reserve(agent_count);
    on_walk.reserve(agent_count);
    walked.reserve(agent_count);
    committed.reserve(agent_count);
    committed_from.reserve(agent_count);
  }
};

}  // namespace detail

// Callers reserve once and reuse the object across ticks so the warm path
// performs no allocation. The same index-pairing caveat as `PathAgentRoutes`
// applies: the scratch carries no per-agent state between calls, so
// reordering agents between ticks is safe with respect to this object.
/// Caller-owned workspace for the joint movement pass.
struct JointMoveScratch {
  /// Pre-sizes every internal container for `agent_count` agents.
  void reserve(std::size_t agent_count) { state_.reserve(agent_count); }

 private:
  friend struct detail::JointMoveScratchAccess;

  detail::JointMoveScratchState state_;
};

namespace detail {

// The single door onto the buffers. `RegionGraphT` befriends its algorithms
// directly, and that pattern does not reach here: the PIBT tier lives in a
// header above this one and its advance is a constrained template, so a
// matching friend declaration would have to name `PibtRanking` — declared in
// the higher header — and dropping the constraint would befriend a different
// template. So the door is a `detail` name instead. Being in `detail` is the
// boundary (`docs/style.md`: no source-compatibility guarantee), not being
// unreachable: a consumer who spells this out can still reach the buffers,
// exactly as it can reach any other internal in a header-only library.
struct JointMoveScratchAccess {
  [[nodiscard]] static auto state(JointMoveScratch& scratch) noexcept
      -> JointMoveScratchState& {
    return scratch.state_;
  }
  [[nodiscard]] static auto state(const JointMoveScratch& scratch) noexcept
      -> const JointMoveScratchState& {
    return scratch.state_;
  }
};

// Round-local agent classification. Values are ordered so that "settled this
// round" states compare greater than Pending.
enum class JointState : std::uint8_t {
  Inactive,  // no goal, no route, or already done this call
  Pending,   // wants to move; admission undecided
  Admitted,  // moves this round
  Failed,    // recorded a movement failure this round
};

[[nodiscard]] inline auto joint_find_occupant(
    const JointMoveScratchState& scratch, std::uint64_t key) noexcept
    -> std::uint32_t {
  const auto begin = scratch.occupant_key.begin();
  const auto end = scratch.occupant_key.end();
  const auto it = std::lower_bound(begin, end, key);
  if (it == end || *it != key) {
    return static_cast<std::uint32_t>(-1);
  }
  return scratch.occupant_agent[static_cast<std::size_t>(it - begin)];
}

[[nodiscard]] inline auto joint_claim(JointMoveScratchState& scratch,
                                      std::uint64_t key) -> bool {
  const auto it =
      std::lower_bound(scratch.claimed.begin(), scratch.claimed.end(), key);
  if (it != scratch.claimed.end() && *it == key) {
    return false;
  }
  scratch.claimed.insert(it, key);
  return true;
}

}  // namespace detail

// One admission round per step:
//   1. Every eligible agent's next route tile is validated exactly as the
//      per-agent commit validates it, minus nothing: an `Occupied` verdict is
//      the only outcome treated further, every other failure is recorded with
//      the per-agent semantics (transient failures block and retain or drop
//      the route, structural failures are terminal).
//   2. Free destinations are claimed in span order; a destination two agents
//      want goes to the earlier agent and the later one records `Occupied`.
//   3. Fixpoint: a move whose destination is being vacated by an admitted
//      mover is admitted. This drains queues in one tick.
//   4. The unresolved remainder is exactly the set of wants-cycles. Cycles of
//      length >= 3 rotate; 2-cycles follow `SwapPolicy`; chains ending at an
//      unadmitted or external occupant record `Occupied`. A cycle refills
//      every tile it vacates, so cycle admission never unlocks further
//      chains -- the fixpoint in step 3 is the only chain pass needed.
//   5. Admitted moves apply as a set: sources clear, destinations set, and
//      per-move dirty marking matches `commit_movement_intent`.
/// Advances retained routes with joint (batch) movement admission.
template <typename World, typename ClassOrTag, typename OccupancyTag,
          typename ReservationTag, typename OnCommit>
  requires std::invocable<OnCommit&, std::size_t, Coord3, Coord3>
auto advance_path_agents_with_joint_movement(
    World& world, std::span<PathAgentState> agents,
    const PathAgentRoutes& routes, JointMoveScratch& scratch_storage,
    JointMoveOptions options, PathAgentAdvanceOptions advance_options,
    OnCommit&& on_commit, diagnostics::FlowAccounting* accounting = nullptr)
    -> JointMoveStats {
  using Shape = typename World::shape_type;
  TESS_ASSERT(routes.routes.size() >= agents.size());
  auto& scratch = detail::JointMoveScratchAccess::state(scratch_storage);
  JointMoveStats stats;
  if (advance_options.max_steps == 0) {
    return stats;
  }
  const auto n = agents.size();
  const auto none = static_cast<std::uint32_t>(-1);

  for (std::size_t step = 0; step < advance_options.max_steps; ++step) {
    scratch.desired.assign(n, Coord3{});
    scratch.state.assign(
        n, static_cast<std::uint8_t>(detail::JointState::Inactive));
    scratch.failure.assign(n, static_cast<std::uint8_t>(MovementStatus::Moved));
    scratch.claimed.clear();
    scratch.committed.clear();
    scratch.committed_from.clear();

    // Position index over every agent, movers or not: an occupied destination
    // must distinguish "held by an agent in this batch" from "held by
    // something else", because only the former can be vacated this tick.
    scratch.occupant_key.clear();
    scratch.occupant_agent.clear();
    for (std::size_t i = 0; i < n; ++i) {
      scratch.occupant_key.push_back(tile_key<Shape>(agents[i].position).value);
      scratch.occupant_agent.push_back(static_cast<std::uint32_t>(i));
    }
    // Sort both by key, keeping the pairing (indices sorted by key).
    {
      auto& keys = scratch.occupant_key;
      auto& vals = scratch.occupant_agent;
      // Insertion sort keeps this allocation-free; agent positions are
      // pairwise distinct so keys are unique.
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

    // 1-2: validate and claim in span order.
    bool any_pending = false;
    for (std::size_t i = 0; i < n; ++i) {
      auto& agent = agents[i];
      if (!agent.has_goal || agent.last_result != PathStatus::Found) {
        continue;
      }
      const auto& route = routes.routes[i];
      if (!detail::has_next_step(agent.path_index, route.size())) {
        continue;
      }
      const auto to = route[agent.path_index + 1];
      const auto verdict =
          validate_movement_intent<World, ClassOrTag, OccupancyTag,
                                   ReservationTag>(
              world, MovementIntent{agent.position, to, {}});
      scratch.desired[i] = to;
      if (verdict.status == MovementStatus::Moved) {
        if (detail::joint_claim(scratch, tile_key<Shape>(to).value)) {
          scratch.state[i] =
              static_cast<std::uint8_t>(detail::JointState::Admitted);
        } else {
          scratch.state[i] =
              static_cast<std::uint8_t>(detail::JointState::Failed);
          scratch.failure[i] =
              static_cast<std::uint8_t>(MovementStatus::Occupied);
        }
      } else if (verdict.status == MovementStatus::Occupied) {
        // The per-agent validation reports Occupied before Reserved, but a
        // reservation must not vanish behind a vacating occupant: joint
        // admission could otherwise walk an agent onto a tile the
        // application has flagged do-not-enter. Reserved wins here.
        if (world.template field<ReservationTag>(to)) {
          scratch.state[i] =
              static_cast<std::uint8_t>(detail::JointState::Failed);
          scratch.failure[i] =
              static_cast<std::uint8_t>(MovementStatus::Reserved);
        } else {
          scratch.state[i] =
              static_cast<std::uint8_t>(detail::JointState::Pending);
          any_pending = true;
        }
      } else {
        scratch.state[i] =
            static_cast<std::uint8_t>(detail::JointState::Failed);
        scratch.failure[i] = static_cast<std::uint8_t>(verdict.status);
      }
    }

    // 3: chains — admit moves into tiles vacated by admitted movers.
    bool changed = any_pending;
    while (changed) {
      changed = false;
      for (std::size_t i = 0; i < n; ++i) {
        if (scratch.state[i] !=
            static_cast<std::uint8_t>(detail::JointState::Pending)) {
          continue;
        }
        const auto key = tile_key<Shape>(scratch.desired[i]).value;
        const auto occupant = detail::joint_find_occupant(scratch, key);
        if (occupant == none ||
            scratch.state[occupant] !=
                static_cast<std::uint8_t>(detail::JointState::Admitted)) {
          continue;
        }
        if (detail::joint_claim(scratch, key)) {
          scratch.state[i] =
              static_cast<std::uint8_t>(detail::JointState::Admitted);
          ++stats.chained;
        } else {
          scratch.state[i] =
              static_cast<std::uint8_t>(detail::JointState::Failed);
          scratch.failure[i] =
              static_cast<std::uint8_t>(MovementStatus::Occupied);
        }
        changed = true;
      }
    }

    // 4: the unresolved remainder is the wants-cycle set. A cycle is
    // volume-preserving -- it refills every tile it vacates -- so admitting
    // one can never free a tile for a trailing chain; every genuinely freed
    // tile traces back to a move into a free tile, which the fixpoint above
    // already drained. Chain members walked here therefore wait this tick.
    scratch.walked.assign(n, 0);
    for (std::size_t start = 0; start < n; ++start) {
      if (scratch.state[start] !=
              static_cast<std::uint8_t>(detail::JointState::Pending) ||
          scratch.walked[start] != 0) {
        continue;
      }
      scratch.cycle_walk.clear();
      scratch.on_walk.assign(n, 0);
      auto at = static_cast<std::uint32_t>(start);
      while (at != none &&
             scratch.state[at] ==
                 static_cast<std::uint8_t>(detail::JointState::Pending) &&
             scratch.on_walk[at] == 0) {
        scratch.on_walk[at] = 1;
        scratch.walked[at] = 1;
        scratch.cycle_walk.push_back(at);
        at = detail::joint_find_occupant(
            scratch, tile_key<Shape>(scratch.desired[at]).value);
      }

      const bool closed = at != none && scratch.on_walk[at] != 0;
      if (!closed) {
        continue;  // an open chain; settled by the sweep below
      }
      std::size_t cycle_begin = scratch.cycle_walk.size();
      for (std::size_t k = 0; k < scratch.cycle_walk.size(); ++k) {
        if (scratch.cycle_walk[k] == at) {
          cycle_begin = k;
          break;
        }
      }
      const auto cycle_len = scratch.cycle_walk.size() - cycle_begin;

      bool admit = false;
      if (cycle_len >= 3) {
        admit = true;
        ++stats.rotations;
      } else if (cycle_len == 2) {
        const auto a = scratch.cycle_walk[cycle_begin];
        const auto b = scratch.cycle_walk[cycle_begin + 1];
        switch (options.swap_policy) {
          case SwapPolicy::Permit:
            admit = true;
            break;
          case SwapPolicy::PermitOnDeadlock:
            admit = agents[a].blocked_retries >= options.deadlock_ticks &&
                    agents[b].blocked_retries >= options.deadlock_ticks;
            break;
          case SwapPolicy::Forbid:
            admit = false;
            break;
        }
        if (admit) {
          ++stats.swaps;
        } else {
          ++stats.swaps_denied;
        }
      }

      for (std::size_t k = cycle_begin; k < scratch.cycle_walk.size(); ++k) {
        const auto member = scratch.cycle_walk[k];
        if (admit) {
          // Cycle destinations are cycle members' current positions, which no
          // admitted mover can have claimed; the claim still guards the
          // invariant.
          (void)detail::joint_claim(
              scratch, tile_key<Shape>(scratch.desired[member]).value);
          scratch.state[member] =
              static_cast<std::uint8_t>(detail::JointState::Admitted);
        } else {
          scratch.state[member] =
              static_cast<std::uint8_t>(detail::JointState::Failed);
          scratch.failure[member] =
              static_cast<std::uint8_t>(MovementStatus::Occupied);
        }
      }
    }

    // Whatever is still pending waits on an occupied tile.
    for (std::size_t i = 0; i < n; ++i) {
      if (scratch.state[i] ==
          static_cast<std::uint8_t>(detail::JointState::Pending)) {
        scratch.state[i] =
            static_cast<std::uint8_t>(detail::JointState::Failed);
        scratch.failure[i] =
            static_cast<std::uint8_t>(MovementStatus::Occupied);
      }
    }

    // 5: apply as a set — every source clears before any destination sets, so
    // a rotated cycle never observes a half-applied state.
    std::size_t admitted_count = 0;
    for (std::size_t i = 0; i < n; ++i) {
      if (scratch.state[i] ==
          static_cast<std::uint8_t>(detail::JointState::Admitted)) {
        world.template field<OccupancyTag>(agents[i].position) = false;
        ++admitted_count;
      }
    }
    for (std::size_t i = 0; i < n; ++i) {
      switch (static_cast<detail::JointState>(scratch.state[i])) {
        case detail::JointState::Admitted: {
          auto& agent = agents[i];
          const auto from = agent.position;
          const auto to = scratch.desired[i];
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
          ++agent.path_index;
          agent.position = to;
          detail::resume_path_agent(agent);
          scratch.committed.push_back(static_cast<std::uint32_t>(i));
          scratch.committed_from.push_back(from);
          ++stats.frame.advanced;
          if (agent.position == agent.goal) {
            arrive_path_agent(agent, accounting);
            agent.last_result = PathStatus::Found;
            ++stats.frame.arrived;
          }
          break;
        }
        case detail::JointState::Failed: {
          auto& agent = agents[i];
          const auto status = static_cast<MovementStatus>(scratch.failure[i]);
          record_movement_failure(stats.frame.movement_failures, status);
          if (is_transient_movement_failure(status)) {
            detail::block_path_agent(agent, status);
            ++stats.frame.blocked_waits;
          } else {
            agent.last_result.reset();
            agent.phase = PathAgentPhase::Unreachable;
            fail_path_agent_flow(agent, accounting);
          }
          break;
        }
        case detail::JointState::Inactive:
        case detail::JointState::Pending:
          break;
      }
    }

    // Observer callbacks fire only after the whole round's world and agent
    // state has been applied, so every callback observes the final
    // configuration — including both halves of a swap. An observer that
    // maintains an injective tile-to-entity mirror must still buffer the
    // round: applying removals for every reported move before any insertion
    // is the only order that survives swaps and rotations, since a per-move
    // upsert collides with a not-yet-processed peer's stale entry. An
    // observer exception propagates with world and agents fully consistent;
    // callbacks for the round's later moves are skipped.
    for (std::size_t k = 0; k < scratch.committed.size(); ++k) {
      const auto index = static_cast<std::size_t>(scratch.committed[k]);
      on_commit(index, scratch.committed_from[k], agents[index].position);
    }

    if (admitted_count == 0) {
      break;  // no motion this round; further rounds cannot differ
    }
  }
  return stats;
}

/// Advances retained routes with joint movement and no commit observer.
template <typename World, typename ClassOrTag, typename OccupancyTag,
          typename ReservationTag>
auto advance_path_agents_with_joint_movement(
    World& world, std::span<PathAgentState> agents,
    const PathAgentRoutes& routes, JointMoveScratch& scratch,
    JointMoveOptions options = {}, PathAgentAdvanceOptions advance_options = {},
    diagnostics::FlowAccounting* accounting = nullptr) -> JointMoveStats {
  return advance_path_agents_with_joint_movement<World, ClassOrTag,
                                                 OccupancyTag, ReservationTag>(
      world, agents, routes, scratch, options, advance_options,
      [](std::size_t, Coord3, Coord3) {}, accounting);
}

// Mirrors `tick_weighted_path_agents_with_movement` with the joint advance in
// place of the per-agent one; see that driver for the planning semantics.
/// Advances one weighted path-agent tick with joint movement admission.
template <typename World, typename Class, std::uint32_t MaxCost,
          typename OccupancyTag, typename ReservationTag>
[[nodiscard]] auto tick_weighted_path_agents_with_joint_movement(
    PathAgentTickState& state, World& world, std::span<PathAgentState> agents,
    PathRequestRuntime& runtime, JointMoveScratch& scratch,
    PathAgentTickOptions options = {}, JointMoveOptions joint_options = {},
    const RegionGraphT<typename World::residency_type>* graph = nullptr,
    JointMoveStats* joint_stats = nullptr) -> PathAgentTickStats {
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

  auto joint = advance_path_agents_with_joint_movement<
      World, Class, OccupancyTag, ReservationTag>(
      world, agents, state.routes, scratch, joint_options,
      PathAgentAdvanceOptions{options.max_steps, options.movement_dirty_mask},
      state.flow_accounting);
  stats.movement = joint.frame;
  if (joint_stats != nullptr) {
    *joint_stats = joint;
  }
  return stats;
}

}  // namespace tess
