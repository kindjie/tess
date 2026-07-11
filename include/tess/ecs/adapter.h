#pragma once

#include <tess/core/assert.h>
#include <tess/ecs/entity_handle.h>
#include <tess/sim/path_agent.h>
#include <tess/sim/path_agent_tick.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// The ECS-agnostic integration layer (M10). Everything here is free of
// third-party dependencies: concrete ECS adapters (the EnTT adapter in
// <tess/ecs/entt/entt_adapter.h>, or a game's own) implement the concepts
// below and reuse the shared components, batch scratch, occupancy index,
// and tick pipeline. The seam is deliberately "agents in deterministic
// order in, state write-back out" -- request submission, tickets, retry
// budgets, and exactly-once result application all stay inside the
// PathAgentState lifecycle (sim/path_agent.h), so adapters can never
// duplicate or violate it.
namespace tess {

// EntityHandle and kNullEntityHandle live in <tess/ecs/entity_handle.h>
// (re-exported here) so dependency-light layers can name entity identity.

// Converts between an ECS's native entity type and EntityHandle. The
// mapping must be lossless for live entities and must map the ECS's null
// entity to kNullEntityHandle (and back).
template <typename A>
concept EntityHandleAdapter = requires(const A& adapter, EntityHandle handle,
                                       const typename A::entity_type& entity) {
  typename A::entity_type;
  { adapter.to_handle(entity) } noexcept -> std::same_as<EntityHandle>;
  {
    adapter.to_entity(handle)
  } noexcept -> std::same_as<typename A::entity_type>;
};

// Reads and writes a game-defined position component as tile coordinates.
// Games own their position representation; the pipeline only needs Coord3
// get/set per entity.
template <typename A, typename Entity>
concept PositionAdapter = requires(A& adapter, const A& const_adapter,
                                   const Entity& entity, Coord3 coord) {
  { const_adapter.position(entity) } -> std::convertible_to<Coord3>;
  adapter.set_position(entity, coord);
};

// What a source's collect pass observed while filling the batch.
struct PathAgentCollectInfo {
  std::size_t count = 0;
  // True iff any goal was armed or re-armed during collection; the tick
  // pipeline maps it to mark_pathing_dirty. Goal clears do not set it: a
  // cleared agent is skipped by every processing pass, so re-pathing the
  // survivors would be pure waste.
  bool pathing_dirty = false;
};

// Parallel SoA scratch for one tick: entry i is one agent, handles and
// lifecycle states in lockstep. reserve() once, then clear() + push() per
// tick with no heap traffic once warm.
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

// Fills the batch with every pathing-relevant agent in a DETERMINISTIC
// order of the source's choosing. The contract: the order must be stable
// across storage-packing history and replays -- sort by a monotonic spawn
// id (AgentId below), never by native entity value or pool order, both of
// which depend on create/destroy churn.
template <typename S>
concept PathAgentSource = requires(S& source, PathAgentBatch& batch) {
  { source.collect(batch) } -> std::same_as<PathAgentCollectInfo>;
};

// Writes batch state back to the ECS after the tick. Must be an
// idempotent mirror: it copies state, it never re-applies results
// (exactly-once application lives in the tess tick pipeline, gated by the
// pathing-dirty flag, not in the sink).
template <typename S>
concept PathAgentSink =
    requires(S& sink, const PathAgentBatch& batch) { sink.apply(batch); };

// Shared components for ECS-side agents. Plain PODs with no dependency on
// any ECS library, so every adapter (and any game-defined store) can reuse
// them.

// Monotonic per-spawn id; the deterministic sort key for collection.
// Never recycled, unlike native entity ids, so collection order is
// independent of pool-packing history and identical across replays. For
// replay determinism across save/load, persist the spawn counter that
// mints these alongside the world.
struct AgentId {
  std::uint64_t value = 0;
};

// ECS-visible mirror of PathAgentState::position, written back by sinks
// so game systems (rendering, AI queries) never reach into the lifecycle
// struct for the current tile.
struct TilePosition {
  Coord3 coord{};
};

// Pathing input: presence means "this agent wants to be there". Arming,
// re-arming, and arrival consumption are the adapter's job; games write
// this component and read TilePosition/PathState, never the reverse.
struct PathGoal {
  Coord3 coord{};
};

// The full agent lifecycle as a component. Deliberately not decomposed:
// PathAgentState's fields (ticket, phase, retry budget, ...) form one
// invariant unit maintained by tess; games read phase/status for AI and
// must never mutate the struct directly.
struct PathState {
  PathAgentState agent{};
};

// Marks an agent that is not on the board: it holds no tile, claims no
// occupancy, appears in no occupancy index, and is excluded from
// collection until placed back. Off-board agents are how consumers park
// units when no free tile exists (e.g. after a world edit shrinks the
// walkable area).
struct OffBoard {};

// Injective tile -> entity occupancy map, the ECS-side mirror of a bool
// occupancy field. Open addressing over a flat power-of-two table keyed
// by Coord3; an empty slot holds kNullEntityHandle; erasure backward-
// shifts trailing probe-chain entries so lookups never need tombstones.
// reserve() sizes the table for a load factor <= 0.5; steady state
// performs no allocation, and growth beyond the reserved capacity
// rehashes (allocates) as a setup-time event only.
//
// The index maps at most one entity per tile by construction; the library
// invariant it mirrors is one-directional (a mapped tile implies the
// occupancy field is set -- games may set occupancy from non-agent
// sources the index never sees).
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
    const auto hash = mix(static_cast<std::uint64_t>(tile.x) ^
                          mix(static_cast<std::uint64_t>(tile.y) ^
                              mix(static_cast<std::uint64_t>(tile.z))));
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

// advance_path_agents_with_movement over a batch, keeping `index`
// synchronized through the commit observer: every successful commit moves
// the committing agent's mapping from -> to, and failed validations touch
// neither the world nor the index. Arrivals need no extra work -- the
// arrival step itself was a commit, and the agent legitimately occupies
// the goal tile afterwards.
template <typename World, typename ClassOrTag, typename OccupancyTag,
          typename ReservationTag>
inline auto advance_path_agents_with_index(
    World& world, PathAgentBatch& batch, const PathRequestRuntime& runtime,
    TileOccupancyIndex& index, std::size_t max_steps = 1,
    std::uint32_t movement_dirty_mask = 0) -> PathAgentFrameStats {
  const auto handles = batch.handles();
  return advance_path_agents_with_movement<World, ClassOrTag, OccupancyTag,
                                           ReservationTag>(
      world, batch.agents(), runtime, max_steps, movement_dirty_mask,
      [&index, handles](std::size_t agent_index, Coord3 from, Coord3 to) {
        index.move(from, to, handles[agent_index]);
      });
}

// The ECS-agnostic full tick: source.collect -> dirty-gated path
// processing (the exactly-once seam, identical to the tick_* drivers) ->
// index-synchronized movement -> sink.apply. The runtime passed here must
// be exclusive to this agent system: tickets persist inside the collected
// state across non-processing ticks, and any other submitter calling
// clear_requests would stale every Following agent's ticket (asserted in
// debug, silently stalled in release). One runtime per (world, movement
// class, agent system) is the contract.
template <typename World, typename ClassOrTag, typename OccupancyTag,
          typename ReservationTag, PathAgentSource Source, PathAgentSink Sink>
[[nodiscard]] auto tick_ecs_unit_path_agents(
    PathAgentTickState& state, World& world, Source& source, Sink& sink,
    PathAgentBatch& batch, PathRequestRuntime& runtime,
    TileOccupancyIndex& index, PathAgentTickOptions options = {},
    std::uint32_t movement_dirty_mask = 0,
    const RegionGraphT<typename World::residency_type>* graph = nullptr)
    -> PathAgentTickStats {
  const auto info = source.collect(batch);
  if (info.pathing_dirty) {
    mark_pathing_dirty(state);
  }

  PathAgentTickStats stats;
  stats.tick = advance_sim_tick(state.clock);

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
      world, batch, runtime, index, options.max_steps, movement_dirty_mask);
  sink.apply(batch);
  return stats;
}

// Weighted movement-class form; see tick_ecs_unit_path_agents for the
// pipeline and the runtime-exclusivity contract.
template <typename World, typename Class, std::uint32_t MaxCost,
          typename OccupancyTag, typename ReservationTag,
          PathAgentSource Source, PathAgentSink Sink>
[[nodiscard]] auto tick_ecs_path_agents(
    PathAgentTickState& state, World& world, Source& source, Sink& sink,
    PathAgentBatch& batch, PathRequestRuntime& runtime,
    TileOccupancyIndex& index, PathAgentTickOptions options = {},
    std::uint32_t movement_dirty_mask = 0,
    const RegionGraphT<typename World::residency_type>* graph = nullptr)
    -> PathAgentTickStats {
  const auto info = source.collect(batch);
  if (info.pathing_dirty) {
    mark_pathing_dirty(state);
  }

  PathAgentTickStats stats;
  stats.tick = advance_sim_tick(state.clock);

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
      world, batch, runtime, index, options.max_steps, movement_dirty_mask);
  sink.apply(batch);
  return stats;
}

// Legacy passable/cost tag-pair form; see tick_ecs_unit_path_agents for
// the pipeline and the runtime-exclusivity contract.
template <typename World, typename PassableTag, typename CostTag,
          std::uint32_t MaxCost, typename OccupancyTag, typename ReservationTag,
          PathAgentSource Source, PathAgentSink Sink>
[[nodiscard]] auto tick_ecs_path_agents(
    PathAgentTickState& state, World& world, Source& source, Sink& sink,
    PathAgentBatch& batch, PathRequestRuntime& runtime,
    TileOccupancyIndex& index, PathAgentTickOptions options = {},
    std::uint32_t movement_dirty_mask = 0,
    const RegionGraphT<typename World::residency_type>* graph = nullptr)
    -> PathAgentTickStats {
  const auto info = source.collect(batch);
  if (info.pathing_dirty) {
    mark_pathing_dirty(state);
  }

  PathAgentTickStats stats;
  stats.tick = advance_sim_tick(state.clock);

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
      world, batch, runtime, index, options.max_steps, movement_dirty_mask);
  sink.apply(batch);
  return stats;
}

}  // namespace tess
