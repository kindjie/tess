#pragma once

#include <tess/core/assert.h>
#include <tess/ecs/entity_handle.h>
#include <tess/sim/delta_frame.h>
#include <tess/sim/path_agent.h>
#include <tess/sim/path_agent_tick.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// The ECS-agnostic integration layer (M10). Everything here is free of
// third-party dependencies: concrete ECS adapters (the EnTT and Flecs
// adapters in their respective subdirectories, or an application's own)
// implement the concepts below and reuse the shared components, batch
// scratch, occupancy index, and tick pipeline. The seam is deliberately
// "agents in deterministic order in, state write-back out" -- request
// submission, tickets, retry
// budgets, and exactly-once result application all stay inside the
// PathAgentState lifecycle (sim/path_agent.h), so adapters can never
// duplicate or violate it.
namespace tess {

// EntityHandle and kNullEntityHandle live in <tess/ecs/entity_handle.h>
// (re-exported here) so dependency-light layers can name entity identity.

/**
 * Lossless conversion contract between a native entity and `EntityHandle`.
 *
 * Implementations must map the native null entity to `kNullEntityHandle` in
 * both directions.
 */
template <typename A>
concept EntityHandleAdapter = requires(const A& adapter, EntityHandle handle,
                                       const typename A::entity_type& entity) {
  typename A::entity_type;
  { adapter.to_handle(entity) } noexcept -> std::same_as<EntityHandle>;
  {
    adapter.to_entity(handle)
  } noexcept -> std::same_as<typename A::entity_type>;
};

/** Converts an entity's game-defined position to and from tile coordinates. */
template <typename A, typename Entity>
concept PositionAdapter = requires(A& adapter, const A& const_adapter,
                                   const Entity& entity, Coord3 coord) {
  { const_adapter.position(entity) } -> std::convertible_to<Coord3>;
  adapter.set_position(entity, coord);
};

/** Summary of a source collection pass and whether path processing is dirty. */
struct PathAgentCollectInfo {
  std::size_t count = 0;
  // True iff any goal was armed or re-armed during collection; the tick
  // pipeline maps it to mark_pathing_dirty. Goal clears do not set it: a
  // cleared agent is skipped by every processing pass, so re-pathing the
  // survivors would be pure waste.
  bool pathing_dirty = false;
};

/**
 * Reusable parallel arrays of entity handles and agent lifecycle states.
 *
 * Reserve during setup; clear and refill each tick to avoid warm-path heap
 * traffic. Entries at the same index always identify the same agent.
 */
class PathAgentBatch {
 public:
  void reserve(std::size_t agent_capacity) {
    handles_.reserve(agent_capacity);
    agents_.reserve(agent_capacity);
  }

  void clear() noexcept {
    handles_.clear();
    agents_.clear();
  }

  void push(EntityHandle handle, const PathAgentState& agent) {
    handles_.push_back(handle);
    agents_.push_back(agent);
  }

  [[nodiscard]] auto size() const noexcept -> std::size_t {
    return agents_.size();
  }

  [[nodiscard]] auto agents() noexcept -> std::span<PathAgentState> {
    return agents_;
  }

  [[nodiscard]] auto agents() const noexcept
      -> std::span<const PathAgentState> {
    return agents_;
  }

  [[nodiscard]] auto handles() const noexcept -> std::span<const EntityHandle> {
    return handles_;
  }

 private:
  std::vector<EntityHandle> handles_;
  std::vector<PathAgentState> agents_;
};

/**
 * Source that fills a batch in deterministic, replay-stable agent order.
 *
 * Native entity values and pool iteration order are not valid ordering keys;
 * use a monotonic spawn identifier such as `AgentId`.
 */
template <typename S>
concept PathAgentSource = requires(S& source, PathAgentBatch& batch) {
  { source.collect(batch) } -> std::same_as<PathAgentCollectInfo>;
};

/**
 * Idempotent sink that mirrors processed batch state into an ECS.
 *
 * A sink must not reapply path results; exactly-once application belongs to
 * the tess tick pipeline.
 */
template <typename S>
concept PathAgentSink =
    requires(S& sink, const PathAgentBatch& batch) { sink.apply(batch); };

// Shared components for ECS-side agents. Plain PODs with no dependency on
// any ECS library, so every adapter (and any game-defined store) can reuse
// them.

/**
 * Monotonic, non-recycled spawn identifier used for deterministic ordering.
 *
 * Persist the producing counter with the world to preserve replay order across
 * save and load.
 */
struct AgentId {
  std::uint64_t value = 0;
};

/** ECS-facing mirror of an agent's current tile position. */
struct TilePosition {
  Coord3 coord{};
};

/** ECS input component requesting that an agent move to `coord`. */
struct PathGoal {
  Coord3 coord{};
};

/**
 * ECS component owning the complete tess-maintained agent lifecycle.
 *
 * Consumers may inspect its phase and status but must not mutate lifecycle
 * fields independently.
 */
struct PathState {
  PathAgentState agent{};
};

/**
 * Marks a parked agent with no tile or occupancy claim.
 *
 * Sources exclude marked agents until an integration explicitly places them.
 */
struct OffBoard {};

/**
 * Injective tile-to-entity index mirroring ECS agent occupancy.
 *
 * Reserve for the maximum population to keep steady-state insertions
 * allocation-free. The index enforces one mapped entity per non-negative tile;
 * mapped tiles must also be marked in the world's occupancy field.
 */
class TileOccupancyIndex {
 public:
  // Sizes the table so `entity_capacity` entries fit without rehashing.
  void reserve(std::size_t entity_capacity) {
    auto target = std::size_t{8};
    while (target < entity_capacity * 2) {
      target *= 2;
    }
    if (target > slots_.size()) {
      rehash(target);
    }
  }

  // Maps `tile` to `entity`. Returns false (and mutates nothing) if the
  // tile already maps to a DIFFERENT entity -- occupancy uniqueness is
  // structural, not advisory. Re-inserting the same mapping succeeds.
  // A refusal is not allocation-free at the growth threshold: the table
  // rehashes before discovering the duplicate tile.
  [[nodiscard]] auto insert(Coord3 tile, EntityHandle entity) -> bool {
    TESS_ASSERT_MSG(!entity.is_null(),
                    "TileOccupancyIndex cannot map a null entity");
    // probe_start's fast lane combine relies on this domain; see its
    // comment.
    TESS_ASSERT_MSG(tile.x >= 0 && tile.y >= 0 && tile.z >= 0,
                    "TileOccupancyIndex stores world tiles, which are "
                    "non-negative");
    if (slots_.empty() || (size_ + 1) * 2 > slots_.size()) {
      rehash(slots_.empty() ? 8 : slots_.size() * 2);
    }
    auto index = probe_start(tile);
    for (;;) {
      auto& slot = slots_[index];
      if (slot.entity.is_null()) {
        slot = Slot{tile, entity};
        ++size_;
        return true;
      }
      if (slot.tile == tile) {
        return slot.entity == entity;
      }
      index = (index + 1) & mask();
    }
  }

  // Unmaps `tile`, returning the entity it held (null if it held none).
  auto erase(Coord3 tile) noexcept -> EntityHandle {
    if (slots_.empty()) {
      return kNullEntityHandle;
    }
    auto index = probe_start(tile);
    for (;;) {
      auto& slot = slots_[index];
      if (slot.entity.is_null()) {
        return kNullEntityHandle;
      }
      if (slot.tile == tile) {
        break;
      }
      index = (index + 1) & mask();
    }
    const auto erased = slots_[index].entity;
    // Backward-shift deletion: pull every trailing probe-chain entry
    // whose ideal slot lies cyclically at or before the hole into the
    // hole, so no lookup's probe path is ever severed.
    auto hole = index;
    auto next = index;
    for (;;) {
      next = (next + 1) & mask();
      const auto& candidate = slots_[next];
      if (candidate.entity.is_null()) {
        break;
      }
      const auto ideal = probe_start(candidate.tile);
      const auto in_gap = (next > hole) ? (ideal > hole && ideal <= next)
                                        : (ideal > hole || ideal <= next);
      if (!in_gap) {
        slots_[hole] = candidate;
        hole = next;
      }
    }
    slots_[hole] = Slot{};
    --size_;
    return erased;
  }

  // erase(from) + insert(to, entity) as the movement-commit hot path,
  // with debug asserts that `from` held `entity` and `to` was empty.
  // Never rehashes: the net size is unchanged.
  void move(Coord3 from, Coord3 to, EntityHandle entity) noexcept {
    // Same pinned domain as insert(): move() is the second write path
    // into the table, and probe_start's fast lane combine relies on it.
    TESS_ASSERT_MSG(to.x >= 0 && to.y >= 0 && to.z >= 0,
                    "TileOccupancyIndex stores world tiles, which are "
                    "non-negative");
    const auto erased = erase(from);
    TESS_ASSERT_MSG(erased == entity,
                    "TileOccupancyIndex::move source held another entity");
    static_cast<void>(erased);
    TESS_ASSERT_MSG(entity_at(to).is_null(),
                    "TileOccupancyIndex::move destination already mapped");
    auto index = probe_start(to);
    while (!slots_[index].entity.is_null()) {
      index = (index + 1) & mask();
    }
    slots_[index] = Slot{to, entity};
    ++size_;
  }

  [[nodiscard]] auto entity_at(Coord3 tile) const noexcept -> EntityHandle {
    if (slots_.empty()) {
      return kNullEntityHandle;
    }
    auto index = probe_start(tile);
    for (;;) {
      const auto& slot = slots_[index];
      if (slot.entity.is_null()) {
        return kNullEntityHandle;
      }
      if (slot.tile == tile) {
        return slot.entity;
      }
      index = (index + 1) & mask();
    }
  }

  [[nodiscard]] auto size() const noexcept -> std::size_t { return size_; }

  void clear() noexcept {
    for (auto& slot : slots_) {
      slot = Slot{};
    }
    size_ = 0;
  }

 private:
  struct Slot {
    Coord3 tile{};
    EntityHandle entity = kNullEntityHandle;
  };

  [[nodiscard]] auto mask() const noexcept -> std::size_t {
    return slots_.size() - 1;
  }

  [[nodiscard]] static auto mix(std::uint64_t value) noexcept -> std::uint64_t {
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
  }

  [[nodiscard]] auto probe_start(Coord3 tile) const noexcept -> std::size_t {
    // One avalanche over per-lane multiplies instead of three chained
    // mix() rounds (6 serial multiplies): the lanes now hash in parallel
    // and erase's backward-shift, which recomputes probe_start per
    // displaced entry, pays one round (audit 2026-07-11 low).
    //
    // The XOR combine has sign/lane-swap symmetries (Codex review:
    // (-n, n, 0) collides with (n, -n, 0)), but those inputs are out of
    // domain -- insert() asserts non-negative world coordinates, and the
    // symmetry needs a negative lane. Additive and rotated combines were
    // measured 1.7-1.9x slower here (interleaved A/B), so the domain is
    // pinned instead of the hash hardened.
    const auto hash =
        mix(static_cast<std::uint64_t>(tile.x) * 0x9E3779B97F4A7C15ULL ^
            static_cast<std::uint64_t>(tile.y) * 0xC2B2AE3D27D4EB4FULL ^
            static_cast<std::uint64_t>(tile.z) * 0x165667B19E3779F9ULL);
    return static_cast<std::size_t>(hash) & mask();
  }

  void rehash(std::size_t new_capacity) {
    auto old = std::vector<Slot>(new_capacity);
    old.swap(slots_);
    size_ = 0;
    for (const auto& slot : old) {
      if (!slot.entity.is_null()) {
        auto index = probe_start(slot.tile);
        while (!slots_[index].entity.is_null()) {
          index = (index + 1) & mask();
        }
        slots_[index] = slot;
        ++size_;
      }
    }
  }

  std::vector<Slot> slots_;
  std::size_t size_ = 0;
};

template <typename World, typename ClassOrTag, typename OccupancyTag,
          typename ReservationTag>
/**
 * Advances a batch while atomically mirroring committed moves into `index`.
 *
 * Failed movement validation changes neither world nor index. When supplied,
 * `render_deltas` receives every committed step through the same observer.
 */
inline auto advance_path_agents_with_index(
    World& world, PathAgentBatch& batch, const PathRequestRuntime& runtime,
    TileOccupancyIndex& index, std::size_t max_steps = 1,
    std::uint32_t movement_dirty_mask = 0,
    DeltaCollector* render_deltas = nullptr) -> PathAgentFrameStats {
  const auto handles = batch.handles();
  return advance_path_agents_with_movement<World, ClassOrTag, OccupancyTag,
                                           ReservationTag>(
      world, batch.agents(), runtime, max_steps, movement_dirty_mask,
      [&index, handles, render_deltas](std::size_t agent_index, Coord3 from,
                                       Coord3 to) {
        index.move(from, to, handles[agent_index]);
        if (render_deltas != nullptr) {
          render_deltas->record_move(handles[agent_index], from, to);
        }
      });
}

template <typename World, typename ClassOrTag, typename OccupancyTag,
          typename ReservationTag, PathAgentSource Source, PathAgentSink Sink>
/**
 * Runs a complete unit-cost ECS path-agent tick.
 *
 * The pipeline collects, processes dirty paths exactly once, advances indexed
 * movement, then applies the batch. `runtime` must be exclusive to this agent
 * system so another submitter cannot invalidate its persistent tickets.
 */
[[nodiscard]] auto tick_ecs_unit_path_agents(
    PathAgentTickState& state, World& world, Source& source, Sink& sink,
    PathAgentBatch& batch, PathRequestRuntime& runtime,
    TileOccupancyIndex& index, PathAgentTickOptions options = {},
    std::uint32_t movement_dirty_mask = 0,
    const RegionGraphT<typename World::residency_type>* graph = nullptr,
    DeltaCollector* render_deltas = nullptr) -> PathAgentTickStats {
  const auto info = source.collect(batch);
  if (info.pathing_dirty) {
    mark_pathing_dirty(state);
  }

  PathAgentTickStats stats;
  stats.tick = advance_sim_tick(state.clock);
  if (render_deltas != nullptr) {
    // Stamp every commit this tick before movement runs.
    render_deltas->begin_tick(stats.tick);
  }

  const bool repath_needed =
      prepare_path_agent_processing(batch.agents(), options, stats);
  if (state.pathing_dirty || repath_needed) {
    stats.pathing = process_unit_path_agents<World, ClassOrTag>(
        world, batch.agents(), runtime, options.cache_policy, graph);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  stats.movement = advance_path_agents_with_index<World, ClassOrTag,
                                                  OccupancyTag, ReservationTag>(
      world, batch, runtime, index, options.max_steps, movement_dirty_mask,
      render_deltas);
  sink.apply(batch);
  return stats;
}

template <typename World, typename Class, std::uint32_t MaxCost,
          typename OccupancyTag, typename ReservationTag,
          PathAgentSource Source, PathAgentSink Sink>
/**
 * Runs the ECS tick pipeline for a weighted movement class.
 *
 * `runtime` has the same exclusive-ownership requirement as the unit-cost
 * overload.
 */
[[nodiscard]] auto tick_ecs_path_agents(
    PathAgentTickState& state, World& world, Source& source, Sink& sink,
    PathAgentBatch& batch, PathRequestRuntime& runtime,
    TileOccupancyIndex& index, PathAgentTickOptions options = {},
    std::uint32_t movement_dirty_mask = 0,
    const RegionGraphT<typename World::residency_type>* graph = nullptr,
    DeltaCollector* render_deltas = nullptr) -> PathAgentTickStats {
  const auto info = source.collect(batch);
  if (info.pathing_dirty) {
    mark_pathing_dirty(state);
  }

  PathAgentTickStats stats;
  stats.tick = advance_sim_tick(state.clock);
  if (render_deltas != nullptr) {
    // Stamp every commit this tick before movement runs.
    render_deltas->begin_tick(stats.tick);
  }

  const bool repath_needed =
      prepare_path_agent_processing(batch.agents(), options, stats);
  if (state.pathing_dirty || repath_needed) {
    stats.pathing = process_weighted_path_agents<World, Class, MaxCost>(
        world, batch.agents(), runtime, options.cache_policy, graph);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  stats.movement = advance_path_agents_with_index<World, Class, OccupancyTag,
                                                  ReservationTag>(
      world, batch, runtime, index, options.max_steps, movement_dirty_mask,
      render_deltas);
  sink.apply(batch);
  return stats;
}

template <typename World, typename PassableTag, typename CostTag,
          std::uint32_t MaxCost, typename OccupancyTag, typename ReservationTag,
          PathAgentSource Source, PathAgentSink Sink>
/**
 * Runs the weighted ECS tick using separate passability and cost field tags.
 *
 * `runtime` has the same exclusive-ownership requirement as the unit-cost
 * overload.
 */
[[nodiscard]] auto tick_ecs_path_agents(
    PathAgentTickState& state, World& world, Source& source, Sink& sink,
    PathAgentBatch& batch, PathRequestRuntime& runtime,
    TileOccupancyIndex& index, PathAgentTickOptions options = {},
    std::uint32_t movement_dirty_mask = 0,
    const RegionGraphT<typename World::residency_type>* graph = nullptr,
    DeltaCollector* render_deltas = nullptr) -> PathAgentTickStats {
  const auto info = source.collect(batch);
  if (info.pathing_dirty) {
    mark_pathing_dirty(state);
  }

  PathAgentTickStats stats;
  stats.tick = advance_sim_tick(state.clock);
  if (render_deltas != nullptr) {
    // Stamp every commit this tick before movement runs.
    render_deltas->begin_tick(stats.tick);
  }

  const bool repath_needed =
      prepare_path_agent_processing(batch.agents(), options, stats);
  if (state.pathing_dirty || repath_needed) {
    stats.pathing =
        process_weighted_path_agents<World, PassableTag, CostTag, MaxCost>(
            world, batch.agents(), runtime, options.cache_policy, graph);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  stats.movement = advance_path_agents_with_index<World, PassableTag,
                                                  OccupancyTag, ReservationTag>(
      world, batch, runtime, index, options.max_steps, movement_dirty_mask,
      render_deltas);
  sink.apply(batch);
  return stats;
}

}  // namespace tess
