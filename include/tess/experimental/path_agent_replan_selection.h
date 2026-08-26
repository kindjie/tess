#pragma once

#include <tess/core/shape.h>
#include <tess/sim/path_agent.h>
#include <tess/sim/path_agent_tick.h>

#include <span>
#include <utility>

namespace tess::experimental {

/**
 * Requests replans for agents whose remaining retained route crosses a
 * tile the caller reports as newly more expensive.
 *
 * A cost change never invalidates a retained route -- validity is
 * passability, optimality is cost -- so a periodic cost-field edit
 * (dynamic congestion pricing, seasonal terrain, tolls) does not need
 * to replan every agent. This helper encodes the measured discipline:
 * ask only the agents whose own remaining route touches a nominated
 * tile to replan, and nominate cost INCREASES only. Nominating
 * cheapened tiles chases newly cheap ground and oscillates; leave
 * decreases to each agent's next natural replan.
 *
 * `cost_increased` is invoked as `bool(Coord3)`; it must be
 * deterministic and side-effect free, and must not mutate `agents`,
 * `routes`, or `queue`. It is consulted at most once per remaining
 * route step, in ascending agent index then ascending step order, and
 * consultation for an agent stops at its first crossing. Agents
 * without a goal or in `PathAgentPhase::Unreachable` are skipped (the
 * queue also refuses them); an agent whose route entry is missing,
 * empty, or fully consumed contributes nothing. Requests deduplicate
 * through `PathAgentReplanQueue`; the return value counts agents newly
 * queued by THIS call. Allocates only through the queue's own growth.
 *
 * Experimental: the spelling and one contract detail (whether the scan
 * starts at the agent's current tile, as here, or at the next step)
 * may still change before any stable promotion.
 */
template <typename CostIncreasedFn>
[[nodiscard]] auto request_replans_for_route_crossings(
    std::span<const PathAgentState> agents, const PathAgentRoutes& routes,
    CostIncreasedFn&& cost_increased, PathAgentReplanQueue& queue)
    -> std::size_t {
  std::size_t newly_queued = 0;
  for (std::size_t i = 0; i < agents.size(); ++i) {
    const auto& agent = agents[i];
    if (!agent.has_goal || agent.phase == PathAgentPhase::Unreachable ||
        i >= routes.routes.size()) {
      continue;
    }
    const auto& route = routes.routes[i];
    for (auto step = agent.path_index; step < route.size(); ++step) {
      if (cost_increased(route[step])) {
        if (queue.request(i, agent)) {
          ++newly_queued;
        }
        break;
      }
    }
  }
  return newly_queued;
}

}  // namespace tess::experimental
