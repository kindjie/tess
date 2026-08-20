#pragma once

#include <tess/sim/path_agent.h>
#include <tess/sim/time.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace tess {

/// Owns the shared clock, pathing-dirty marker, and retained routes across
/// ticks.
struct PathAgentTickState {
  SimClock clock{};
  // WORLD-scoped pathing dirt: set it (via mark_pathing_dirty) after any
  // world change that can invalidate existing routes; the next tick then
  // replans EVERY agent. Agent-scoped needs (a newly armed goal, a Blocked
  // retry) do not set it -- those agents alone replan while Following
  // agents keep walking their retained routes (audit/optimization-log
  // per-agent pathing-dirty item). Starts true so the first tick plans
  // everyone.
  bool pathing_dirty = true;
  // Per-agent retained routes; see PathAgentRoutes for the index-pairing
  // contract (reorder/remove agents => mark_pathing_dirty).
  PathAgentRoutes routes{};
  /// Optional caller-owned goal-lifecycle accounting (see FlowCounters).
  /// The accountant must outlive the tick state's use of it. Attach or
  /// detach only while no accounted goal is outstanding, and never
  /// duplicate an attached tick state: two states updating one
  /// accountant double-terminalize lifecycles. Copies therefore start
  /// unattached and moves transfer the attachment.
  diagnostics::FlowAccounting* flow_accounting = nullptr;

  PathAgentTickState() = default;
  PathAgentTickState(const PathAgentTickState& other)
      : clock{other.clock},
        pathing_dirty{other.pathing_dirty},
        routes{other.routes} {}
  auto operator=(const PathAgentTickState& other) -> PathAgentTickState& {
    if (this != &other) {
      clock = other.clock;
      pathing_dirty = other.pathing_dirty;
      routes = other.routes;
      flow_accounting = nullptr;
    }
    return *this;
  }
  PathAgentTickState(PathAgentTickState&& other) noexcept
      : clock{other.clock},
        pathing_dirty{other.pathing_dirty},
        routes{std::move(other.routes)},
        flow_accounting{other.flow_accounting} {
    other.flow_accounting = nullptr;
  }
  auto operator=(PathAgentTickState&& other) noexcept -> PathAgentTickState& {
    if (this != &other) {
      clock = other.clock;
      pathing_dirty = other.pathing_dirty;
      routes = std::move(other.routes);
      flow_accounting = other.flow_accounting;
      other.flow_accounting = nullptr;
    }
    return *this;
  }
  ~PathAgentTickState() = default;
};

/// Selects what retry exhaustion means for an otherwise active blocked agent.
enum class BlockedAgentExhaustionPolicy : std::uint8_t {
  /// Preserve the goal and optional last search result while waiting for
  /// progress or a caller-owned recovery verdict. This policy never invents
  /// `NoPath` from a clock and is the default.
  RemainBlocked,
  /// Retry exhaustion marks the lifecycle terminally `Unreachable` and clears
  /// the last search result because a retry clock is not a path search.
  MarkUnreachable,
};

/// Configures per-tick movement, caching, and blocked-agent retry limits.
struct PathAgentTickOptions {
  std::size_t max_steps = 1;
  DirtyMask movement_dirty_mask{};
  PathRuntimeCachePolicy cache_policy{};
  /// Budget of consecutive ticks spent retrying a Blocked agent.
  ///
  /// Occupied and reserved destinations retry the retained step without an
  /// occupancy-blind search; route-invalidating failures re-path. The first
  /// movement failure records the block, and each following tick consumes one
  /// attempt until a successful move resets the count. At exhaustion the
  /// policy below either keeps the agent blocked without further path
  /// processing, or preserves the historical terminal transition.
  std::uint32_t max_blocked_retries = 8;
  BlockedAgentExhaustionPolicy blocked_exhaustion_policy =
      BlockedAgentExhaustionPolicy::RemainBlocked;
};

/// Configures deterministic, bounded checks of persistently blocked agents.
struct BlockedAgentRecoveryOptions {
  /// Upper bound for the first jittered delay after blockage is observed.
  std::uint32_t initial_delay_ticks = 16;
  /// Upper bound for later exponentially backed-off delays.
  std::uint32_t max_delay_ticks = 256;
  /// Maximum number of indices returned from one collection pass.
  std::size_t max_probes_per_tick = 8;
  /// Caller-selected deterministic salt; no process-global RNG is consulted.
  std::uint64_t jitter_seed = 0;
};

/// Summarizes one blocked-agent recovery scheduling pass.
struct BlockedAgentRecoveryStats {
  std::size_t blocked = 0;
  std::size_t due = 0;
  std::size_t selected = 0;
  std::size_t deferred = 0;
};

/**
 * Caller-owned scheduling scratch for expensive blocked-agent checks.
 *
 * Entries are paired with agent span indices, like `PathAgentRoutes`. Reorder
 * or compact the span only when also clearing or equivalently reordering this
 * schedule. The object is externally synchronized: collect and acknowledge
 * checks on the frame-owner thread. Selected read-only work may run elsewhere
 * only when the caller provides independent search scratch and applies results
 * deterministically.
 *
 * Scheduling never decides reachability. `collect_due` returns at most the
 * configured number of due indices. After completing a selected check, call
 * `record_attempt`; an unacknowledged index remains due on the next pass.
 */
class BlockedAgentRecoverySchedule {
 public:
  void reserve(std::size_t agent_count) {
    entries_.reserve(agent_count);
    due_indices_.reserve(agent_count);
  }

  void clear() noexcept {
    entries_.clear();
    due_indices_.clear();
    scan_cursor_ = 0;
  }

  [[nodiscard]] auto collect_due(std::span<const PathAgentState> agents,
                                 std::uint64_t tick,
                                 BlockedAgentRecoveryOptions options = {})
      -> BlockedAgentRecoveryStats {
    if (entries_.size() < agents.size()) {
      entries_.resize(agents.size());
    }
    for (std::size_t i = agents.size(); i < entries_.size(); ++i) {
      entries_[i].active = false;
    }

    due_indices_.clear();
    BlockedAgentRecoveryStats stats;
    for (std::size_t i = 0; i < agents.size(); ++i) {
      const auto& agent = agents[i];
      auto& entry = entries_[i];
      if (!agent.has_goal || agent.phase != PathAgentPhase::Blocked) {
        entry.active = false;
        entry.attempt = 0;
        entry.next_tick = 0;
        continue;
      }

      ++stats.blocked;
      if (!entry.active || entry.observed_position != agent.position) {
        entry.active = true;
        entry.attempt = 0;
        ++entry.episode;
        entry.observed_position = agent.position;
        entry.next_tick =
            add_saturating(tick, jittered_delay(i, entry, options));
      }
    }

    if (!agents.empty()) {
      scan_cursor_ %= agents.size();
      for (std::size_t offset = 0; offset < agents.size(); ++offset) {
        const auto i = (scan_cursor_ + offset) % agents.size();
        const auto& entry = entries_[i];
        if (!entry.active || tick < entry.next_tick) {
          continue;
        }
        ++stats.due;
        if (due_indices_.size() < options.max_probes_per_tick) {
          due_indices_.push_back(i);
        }
      }
      if (!due_indices_.empty()) {
        scan_cursor_ = (due_indices_.back() + 1U) % agents.size();
      }
    }
    stats.selected = due_indices_.size();
    stats.deferred = stats.due - stats.selected;
    return stats;
  }

  [[nodiscard]] auto due_agent_indices() const noexcept
      -> std::span<const std::size_t> {
    return due_indices_;
  }

  void record_attempt(std::size_t agent_index, std::uint64_t tick,
                      BlockedAgentRecoveryOptions options = {}) noexcept {
    if (agent_index >= entries_.size()) {
      return;
    }
    auto& entry = entries_[agent_index];
    if (!entry.active) {
      return;
    }
    if (entry.attempt != std::numeric_limits<std::uint32_t>::max()) {
      ++entry.attempt;
    }
    entry.next_tick =
        add_saturating(tick, jittered_delay(agent_index, entry, options));
  }

 private:
  struct Entry {
    std::uint64_t next_tick = 0;
    std::uint64_t episode = 0;
    std::uint32_t attempt = 0;
    Coord3 observed_position{};
    bool active = false;
  };

  [[nodiscard]] static constexpr auto mix(std::uint64_t value) noexcept
      -> std::uint64_t {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
  }

  [[nodiscard]] static constexpr auto delay_cap(
      std::uint32_t attempt, BlockedAgentRecoveryOptions options) noexcept
      -> std::uint32_t {
    auto cap = options.initial_delay_ticks < options.max_delay_ticks
                   ? options.initial_delay_ticks
                   : options.max_delay_ticks;
    if (cap == 0) {
      return 0;
    }
    for (std::uint32_t i = 0; i < attempt && cap < options.max_delay_ticks;
         ++i) {
      if (cap > options.max_delay_ticks / 2U) {
        cap = options.max_delay_ticks;
      } else {
        cap *= 2U;
      }
    }
    return cap;
  }

  [[nodiscard]] static constexpr auto jittered_delay(
      std::size_t agent_index, const Entry& entry,
      BlockedAgentRecoveryOptions options) noexcept -> std::uint32_t {
    const auto cap = delay_cap(entry.attempt, options);
    if (cap == 0) {
      return 0;
    }
    // Equal jitter: preserve half of the exponential delay and spread the
    // remainder deterministically. Unlike full jitter this cannot repeatedly
    // select a zero-delay retry.
    const auto floor = cap / 2U + cap % 2U;
    const auto width = cap - floor + 1U;
    auto key = options.jitter_seed;
    key ^= mix(static_cast<std::uint64_t>(agent_index));
    key ^= mix(entry.episode);
    key ^= mix(entry.attempt);
    return floor + static_cast<std::uint32_t>(mix(key) % width);
  }

  [[nodiscard]] static constexpr auto add_saturating(
      std::uint64_t tick, std::uint32_t delay) noexcept -> std::uint64_t {
    const auto max = std::numeric_limits<std::uint64_t>::max();
    return tick > max - delay ? max : tick + delay;
  }

  std::vector<Entry> entries_;
  std::vector<std::size_t> due_indices_;
  std::size_t scan_cursor_ = 0;
};

/// Configures one bounded drain of an exact path-agent replan queue.
struct PathAgentReplanOptions {
  /// Maximum number of exact searches performed by one processing call.
  std::size_t max_requests = 8;
  /// Sparse-world boundary behavior passed through to exact A*.
  MissingChunkPolicy missing_chunk_policy =
      MissingChunkPolicy::ReportIndeterminate;
  /**
   * Base seed for deterministic per-agent equal-cost weighted routes.
   *
   * Zero preserves canonical A* ordering. A nonzero value derives a
   * deterministic seed from each queued agent index; unit-cost replans ignore
   * this field.
   */
  std::uint64_t equal_cost_tie_seed = 0;
};

/**
 * Caller-owned FIFO of agent indices awaiting replanning.
 *
 * Pending indices are deduplicated. Like retained routes and recovery
 * schedules, the queue is paired with the caller's agent-span indices;
 * reorder or compact only when also clearing or equivalently remapping it.
 * The queue is externally synchronized. Separate owners may process separate
 * queues concurrently with independent path scratch.
 */
class PathAgentReplanQueue {
 public:
  void reserve(std::size_t agent_count) {
    const auto doubled =
        agent_count > std::numeric_limits<std::size_t>::max() / 2U
            ? std::numeric_limits<std::size_t>::max()
            : agent_count * 2U;
    indices_.reserve(doubled);
    queued_.reserve(agent_count);
  }

  void clear() noexcept {
    for (auto i = head_; i < indices_.size(); ++i) {
      queued_[indices_[i]] = 0;
    }
    indices_.clear();
    head_ = 0;
  }

  [[nodiscard]] auto request(std::size_t index, const PathAgentState& agent)
      -> bool {
    if (!agent.has_goal || agent.phase == PathAgentPhase::Unreachable) {
      return false;
    }
    if (queued_.size() <= index) {
      queued_.resize(index + 1U, 0);
    }
    if (queued_[index] != 0) {
      return false;
    }
    if (head_ != 0 && head_ >= indices_.size() / 2U) {
      indices_.erase(indices_.begin(),
                     indices_.begin() + static_cast<std::ptrdiff_t>(head_));
      head_ = 0;
    }
    indices_.push_back(index);
    queued_[index] = 1;
    return true;
  }

  void request_all(std::span<const PathAgentState> agents) {
    if (queued_.size() < agents.size()) {
      queued_.resize(agents.size(), 0);
    }
    for (std::size_t i = 0; i < agents.size(); ++i) {
      (void)request(i, agents[i]);
    }
  }

  [[nodiscard]] auto empty() const noexcept -> bool {
    return head_ == indices_.size();
  }

  [[nodiscard]] auto pending() const noexcept -> std::size_t {
    return indices_.size() - head_;
  }

  [[nodiscard]] auto front() const noexcept -> std::optional<std::size_t> {
    if (empty()) {
      return std::nullopt;
    }
    return indices_[head_];
  }

  void pop_front() noexcept {
    if (empty()) {
      return;
    }
    const auto index = indices_[head_];
    TESS_ASSERT(index < queued_.size());
    if (index < queued_.size()) {
      queued_[index] = 0;
    }
    ++head_;
    if (head_ == indices_.size()) {
      indices_.clear();
      head_ = 0;
    }
  }

 private:
  std::vector<std::size_t> indices_;
  std::vector<std::uint8_t> queued_;
  std::size_t head_ = 0;
};

/**
 * Drains a bounded FIFO of caller-planned paths into retained agent routes.
 *
 * `search` is called synchronously as `(agent_index, request)` and must return
 * a `PathResult`. Its path may borrow caller-owned storage, but must remain
 * valid until this function immediately copies it. The callback must not
 * mutate or reenter `agents`, `routes`, or `queue`, and its path must not alias
 * the destination retained route.
 *
 * This function owns only FIFO and agent-lifecycle transitions. It does not
 * validate or certify callback results as legal, exact, or optimal. If the
 * callback throws, the current queue item, agent, and retained route remain
 * unchanged; earlier items stay committed and callback-owned side effects are
 * not rolled back. The queue and associated state are externally synchronized.
 */
template <typename Search>
[[nodiscard]] auto process_path_agent_replans(
    std::span<PathAgentState> agents, PathAgentRoutes& routes,
    PathAgentReplanQueue& queue, std::size_t max_requests, Search&& search,
    diagnostics::FlowAccounting* accounting = nullptr) -> PathAgentFrameStats {
  PathAgentFrameStats stats;
  routes.ensure_size(agents.size());
  while (!queue.empty() && stats.submitted < max_requests) {
    const auto pending_index = queue.front();
    if (!pending_index.has_value()) {
      break;
    }
    const auto index = pending_index.value();
    if (index >= agents.size()) {
      queue.pop_front();
      continue;
    }
    auto& agent = agents[index];
    if (!agent.has_goal || agent.phase == PathAgentPhase::Unreachable) {
      queue.pop_front();
      continue;
    }
    if (agent.position == agent.goal) {
      arrive_path_agent(agent, accounting);
      routes.routes[index].clear();
      ++stats.arrived;
      queue.pop_front();
      continue;
    }

    const auto was_blocked = agent.phase == PathAgentPhase::Blocked;
    const auto result = search(index, PathRequest{agent.position, agent.goal});
    ++stats.submitted;
    ++stats.completed;
    record_path_agent_status(stats, result.status);
    if (result.status == PathStatus::Found) {
      // Assign first: if allocation throws, the pending queue item and agent
      // lifecycle remain unchanged and the caller can retry.
      routes.routes[index].assign(result.path.begin(), result.path.end());
      agent.path_index = 0;
      agent.last_result = PathStatus::Found;
      agent.phase = PathAgentPhase::Following;
      if (!was_blocked) {
        agent.blocked_retries = 0;
      }
    } else {
      routes.routes[index].clear();
      agent.path_index = 0;
      agent.last_result = result.status;
      agent.phase = PathAgentPhase::Blocked;
    }
    queue.pop_front();
  }
  return stats;
}

/// Drains bounded exact unit-cost replans into retained route storage.
template <typename World, typename ClassOrTag>
[[nodiscard]] auto process_unit_path_agent_replans(
    const World& world, std::span<PathAgentState> agents,
    PathAgentRoutes& routes, PathAgentReplanQueue& queue, PathScratch& scratch,
    PathAgentReplanOptions options = {},
    diagnostics::FlowAccounting* accounting = nullptr) -> PathAgentFrameStats {
  return process_path_agent_replans(
      agents, routes, queue, options.max_requests,
      [&](std::size_t, PathRequest request) {
        return astar_path<World, ClassOrTag>(world, request, scratch,
                                             options.missing_chunk_policy);
      },
      accounting);
}

/// Drains bounded exact weighted replans into retained route storage.
template <typename World, typename Class>
[[nodiscard]] auto process_weighted_path_agent_replans(
    const World& world, std::span<PathAgentState> agents,
    PathAgentRoutes& routes, PathAgentReplanQueue& queue, PathScratch& scratch,
    PathAgentReplanOptions options = {},
    diagnostics::FlowAccounting* accounting = nullptr) -> PathAgentFrameStats {
  return process_path_agent_replans(
      agents, routes, queue, options.max_requests,
      [&](std::size_t index, PathRequest request) {
        if (options.equal_cost_tie_seed != 0) {
          auto seed = options.equal_cost_tie_seed +
                      static_cast<std::uint64_t>(index) + 1U;
          if (seed == 0) {
            seed = options.equal_cost_tie_seed;
          }
          return weighted_astar_path<World, Class>(
              world, request, scratch, PathTieBreak{seed},
              options.missing_chunk_policy);
        }
        return weighted_astar_path<World, Class>(world, request, scratch,
                                                 options.missing_chunk_policy);
      },
      accounting);
}

/// Summarizes path planning and movement performed during one tick.
struct PathAgentTickStats {
  std::uint64_t tick = 0;
  bool processed_paths = false;
  PathAgentFrameStats pathing{};
  PathAgentFrameStats movement{};
  // Actual route-invalidating retries that requested path processing.
  std::size_t repaths_requested = 0;
  // Historical name retained for source compatibility: counts agents whose
  // exhausted budget the selected policy terminalized.
  std::size_t repath_exhausted = 0;
};

/// Requests a full replan after a world-scoped pathing change.
inline void mark_pathing_dirty(PathAgentTickState& state) noexcept {
  state.pathing_dirty = true;
}

// Arms a goal WITHOUT touching the world-scoped pathing-dirty marker: the agent
// enters NeedsPath, which the next tick picks up as an agent-scoped
// (NeedsOnly) processing pass. Before the per-agent split this marked the
// shared flag and one new goal replanned the whole batch every tick
// (optimization-log 2026-07-11, S11.4 soak observation).
/// Arms one agent goal without forcing unrelated following agents to replan.
///
/// With flow accounting attached this is an admission; replacing a
/// still-outstanding goal terminalizes it as superseded first, while
/// re-arming after a terminal outcome (arrival or explicitly terminal
/// exhaustion) is a fresh admission with no second terminal bucket.
inline void set_path_agent_goal(PathAgentTickState& state,
                                PathAgentState& agent, Coord3 goal) noexcept {
  if (state.flow_accounting != nullptr) {
    auto& accounting = *state.flow_accounting;
    if (path_agent_goal_outstanding(agent)) {
      ++accounting.counters.superseded;
      accounting.record_left_outstanding();
      accounting.counters.residence_ticks_accumulated +=
          accounting.last_observed_tick - agent.armed_tick;
    }
    ++accounting.counters.offered;
    accounting.record_admitted();
    agent.armed_tick = accounting.last_observed_tick;
  }
  set_path_agent_goal(agent, goal);
}

/// Clears one agent goal, cancelling a still-outstanding lifecycle.
inline void clear_path_agent_goal(PathAgentTickState& state,
                                  PathAgentState& agent) noexcept {
  if (state.flow_accounting != nullptr && path_agent_goal_outstanding(agent)) {
    auto& accounting = *state.flow_accounting;
    ++accounting.counters.cancelled;
    accounting.record_left_outstanding();
    accounting.counters.residence_ticks_accumulated +=
        accounting.last_observed_tick - agent.armed_tick;
  }
  clear_path_agent_goal(agent);
}

/**
 * Observes one monotonic tick for goal-lifecycle time accounting.
 *
 * Call once per simulation tick before arming, clearing, or ticking:
 * weights outstanding inventory and refreshes the oldest outstanding
 * goal age from per-agent admission stamps.
 */
inline void observe_path_agent_flow_tick(PathAgentTickState& state,
                                         std::span<const PathAgentState> agents,
                                         std::uint64_t tick) noexcept {
  if (state.flow_accounting == nullptr) {
    return;
  }
  state.flow_accounting->observe_tick(tick);
  const auto now = state.flow_accounting->last_observed_tick;
  auto oldest = now;
  auto any = false;
  for (const auto& agent : agents) {
    if (path_agent_goal_outstanding(agent)) {
      any = true;
      oldest = agent.armed_tick < oldest ? agent.armed_tick : oldest;
    }
  }
  state.flow_accounting->counters.oldest_outstanding_age_ticks =
      any ? now - oldest : 0;
}

// Scans agents ahead of a tick's path processing. NeedsPath agents request
// processing with no manual dirty mark. Blocked agents consume one retry per
// following tick. A retained Found route waits without path processing for
// occupancy/reservations; invalid routes request a re-path. Exhausted agents
// stop path processing at exhaustion; the configured policy decides whether
// they remain honestly Blocked or become terminally Unreachable.
/// Advances retry accounting and reports whether any agent needs planning.
inline auto prepare_path_agent_processing(
    std::span<PathAgentState> agents, PathAgentTickOptions options,
    PathAgentTickStats& stats,
    diagnostics::FlowAccounting* accounting = nullptr) noexcept -> bool {
  bool needs_processing = false;
  for (auto& agent : agents) {
    if (!agent.has_goal) {
      continue;
    }
    if (agent.phase == PathAgentPhase::NeedsPath) {
      needs_processing = true;
      continue;
    }
    if (agent.phase != PathAgentPhase::Blocked) {
      continue;
    }
    if (options.max_steps == 0) {
      // A paused movement tick cannot prove whether the obstruction cleared.
      // Do not spend the consecutive-block budget without attempting a step.
      continue;
    }
    if (agent.blocked_retries < options.max_blocked_retries) {
      ++agent.blocked_retries;
      if (agent.last_result != PathStatus::Found) {
        ++stats.repaths_requested;
        needs_processing = true;
      }
    } else if (options.blocked_exhaustion_policy ==
               BlockedAgentExhaustionPolicy::MarkUnreachable) {
      agent.phase = PathAgentPhase::Unreachable;
      agent.last_result.reset();
      ++stats.repath_exhausted;
      if (accounting != nullptr) {
        ++accounting->counters.failed;
        accounting->record_left_outstanding();
        accounting->counters.residence_ticks_accumulated +=
            accounting->last_observed_tick - agent.armed_tick;
      }
    }
  }
  return needs_processing;
}

/// Advances one unit-cost path-agent tick without world movement validation.
template <typename World, typename ClassOrTag>
[[nodiscard]] auto tick_unit_path_agents(
    PathAgentTickState& state, const World& world,
    std::span<PathAgentState> agents, PathRequestRuntime& runtime,
    PathAgentTickOptions options = {},
    const RegionGraphT<typename World::residency_type>* graph = nullptr)
    -> PathAgentTickStats {
  PathAgentTickStats stats;
  stats.tick = advance_sim_tick(state.clock);

  const bool repath_needed = prepare_path_agent_processing(
      agents, options, stats, state.flow_accounting);
  state.routes.ensure_size(agents.size());
  if (state.pathing_dirty || repath_needed) {
    const auto scope =
        state.pathing_dirty ? PathSubmitScope::All : PathSubmitScope::NeedsOnly;
    stats.pathing = process_unit_path_agents<World, ClassOrTag>(
        world, agents, runtime, options.cache_policy, graph, scope,
        &state.routes, state.flow_accounting);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  stats.movement = advance_path_agents(agents, state.routes, options.max_steps,
                                       state.flow_accounting);
  return stats;
}

/// Advances a provider-composed unit-cost tick without movement validation.
template <typename World, typename ClassOrTag, typename Provider>
[[nodiscard]] auto tick_unit_path_agents(
    PathAgentTickState& state, const World& world,
    std::span<PathAgentState> agents, PathRequestRuntime& runtime,
    PathAgentTickOptions options,
    const RegionGraphT<typename World::residency_type>* graph,
    const Provider& provider) -> PathAgentTickStats {
  PathAgentTickStats stats;
  stats.tick = advance_sim_tick(state.clock);

  const bool repath_needed = prepare_path_agent_processing(
      agents, options, stats, state.flow_accounting);
  state.routes.ensure_size(agents.size());
  if (state.pathing_dirty || repath_needed) {
    const auto scope =
        state.pathing_dirty ? PathSubmitScope::All : PathSubmitScope::NeedsOnly;
    stats.pathing = process_unit_path_agents<World, ClassOrTag>(
        world, agents, runtime, options.cache_policy, graph, scope,
        &state.routes, provider, state.flow_accounting);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  stats.movement = advance_path_agents(agents, state.routes, options.max_steps,
                                       state.flow_accounting);
  return stats;
}

template <typename World, typename ClassOrTag, typename OccupancyTag,
          typename ReservationTag>
/// Advances one unit-cost tick using validated occupancy movement commits.
[[nodiscard]] auto tick_unit_path_agents_with_movement(
    PathAgentTickState& state, World& world, std::span<PathAgentState> agents,
    PathRequestRuntime& runtime, PathAgentTickOptions options = {},
    const RegionGraphT<typename World::residency_type>* graph = nullptr)
    -> PathAgentTickStats {
  PathAgentTickStats stats;
  stats.tick = advance_sim_tick(state.clock);

  const bool repath_needed = prepare_path_agent_processing(
      agents, options, stats, state.flow_accounting);
  state.routes.ensure_size(agents.size());
  if (state.pathing_dirty || repath_needed) {
    const auto scope =
        state.pathing_dirty ? PathSubmitScope::All : PathSubmitScope::NeedsOnly;
    stats.pathing = process_unit_path_agents<World, ClassOrTag>(
        world, agents, runtime, options.cache_policy, graph, scope,
        &state.routes, state.flow_accounting);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  stats.movement = advance_path_agents_with_movement<
      World, ClassOrTag, OccupancyTag, ReservationTag>(
      world, agents, state.routes,
      PathAgentAdvanceOptions{options.max_steps, options.movement_dirty_mask},
      state.flow_accounting);
  return stats;
}

/// Advances a provider-composed unit tick through validated movement commits.
template <typename World, typename ClassOrTag, typename OccupancyTag,
          typename ReservationTag, typename Provider>
[[nodiscard]] auto tick_unit_path_agents_with_movement(
    PathAgentTickState& state, World& world, std::span<PathAgentState> agents,
    PathRequestRuntime& runtime, PathAgentTickOptions options,
    const RegionGraphT<typename World::residency_type>* graph,
    const Provider& provider) -> PathAgentTickStats {
  PathAgentTickStats stats;
  stats.tick = advance_sim_tick(state.clock);

  const bool repath_needed = prepare_path_agent_processing(
      agents, options, stats, state.flow_accounting);
  state.routes.ensure_size(agents.size());
  if (state.pathing_dirty || repath_needed) {
    const auto scope =
        state.pathing_dirty ? PathSubmitScope::All : PathSubmitScope::NeedsOnly;
    stats.pathing = process_unit_path_agents<World, ClassOrTag>(
        world, agents, runtime, options.cache_policy, graph, scope,
        &state.routes, provider, state.flow_accounting);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  stats.movement = advance_path_agents_with_movement<
      World, ClassOrTag, OccupancyTag, ReservationTag>(
      world, agents, state.routes,
      PathAgentAdvanceOptions{options.max_steps, options.movement_dirty_mask},
      provider, state.flow_accounting);
  return stats;
}

// Class forms: one movement class drives pathing, precheck, and (for the
// movement variant) commit validation, so plan and commit provably agree.
/// Advances one bounded weighted path-agent tick without movement commits.
template <typename World, typename Class, std::uint32_t MaxCost>
[[nodiscard]] auto tick_weighted_path_agents(
    PathAgentTickState& state, const World& world,
    std::span<PathAgentState> agents, PathRequestRuntime& runtime,
    PathAgentTickOptions options = {},
    const RegionGraphT<typename World::residency_type>* graph = nullptr)
    -> PathAgentTickStats {
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

  stats.movement = advance_path_agents(agents, state.routes, options.max_steps,
                                       state.flow_accounting);
  return stats;
}

/// Advances a provider-composed bounded weighted tick without commits.
template <typename World, typename Class, std::uint32_t MaxCost,
          typename Provider>
[[nodiscard]] auto tick_weighted_path_agents(
    PathAgentTickState& state, const World& world,
    std::span<PathAgentState> agents, PathRequestRuntime& runtime,
    PathAgentTickOptions options,
    const RegionGraphT<typename World::residency_type>* graph,
    const Provider& provider) -> PathAgentTickStats {
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
        &state.routes, provider, state.flow_accounting);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  stats.movement = advance_path_agents(agents, state.routes, options.max_steps,
                                       state.flow_accounting);
  return stats;
}

template <typename World, typename Class, std::uint32_t MaxCost,
          typename OccupancyTag, typename ReservationTag>
/// Advances one bounded weighted tick using validated movement commits.
[[nodiscard]] auto tick_weighted_path_agents_with_movement(
    PathAgentTickState& state, World& world, std::span<PathAgentState> agents,
    PathRequestRuntime& runtime, PathAgentTickOptions options = {},
    const RegionGraphT<typename World::residency_type>* graph = nullptr)
    -> PathAgentTickStats {
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

  stats.movement = advance_path_agents_with_movement<World, Class, OccupancyTag,
                                                     ReservationTag>(
      world, agents, state.routes,
      PathAgentAdvanceOptions{options.max_steps, options.movement_dirty_mask},
      state.flow_accounting);
  return stats;
}

/// Advances a provider-composed weighted tick through movement commits.
template <typename World, typename Class, std::uint32_t MaxCost,
          typename OccupancyTag, typename ReservationTag, typename Provider>
[[nodiscard]] auto tick_weighted_path_agents_with_movement(
    PathAgentTickState& state, World& world, std::span<PathAgentState> agents,
    PathRequestRuntime& runtime, PathAgentTickOptions options,
    const RegionGraphT<typename World::residency_type>* graph,
    const Provider& provider) -> PathAgentTickStats {
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
        &state.routes, provider, state.flow_accounting);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  stats.movement = advance_path_agents_with_movement<World, Class, OccupancyTag,
                                                     ReservationTag>(
      world, agents, state.routes,
      PathAgentAdvanceOptions{options.max_steps, options.movement_dirty_mask},
      provider, state.flow_accounting);
  return stats;
}

}  // namespace tess
