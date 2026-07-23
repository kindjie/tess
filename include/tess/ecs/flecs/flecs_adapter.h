#pragma once

#include <tess/ecs/adapter.h>

#if defined(TESS_ENABLE_FLECS)

#ifndef FLECS_H
#error "Include <flecs.h> before <tess/ecs/flecs/flecs_adapter.h>"
#endif

#if FLECS_VERSION_MAJOR < 4 ||                               \
    (FLECS_VERSION_MAJOR == 4 && FLECS_VERSION_MINOR < 1) || \
    (FLECS_VERSION_MAJOR == 4 && FLECS_VERSION_MINOR == 1 && \
     FLECS_VERSION_PATCH < 5)
#error "The tess Flecs adapter requires Flecs 4.1.5 or newer"
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

// The Flecs adapter (M10): concrete PathAgentSource/PathAgentSink types
// over a flecs::world, explicit lifecycle intents (spawn / despawn /
// teleport / park / place -- raw entity destruction on an on-board agent
// leaks a permanently blocked tile, so the intents are the only
// sanctioned mutation paths), and tick_flecs_* drivers that are thin
// instantiations of the generic tick_ecs_* pipeline. Compiled only when
// the consumer defines TESS_ENABLE_FLECS and includes
// <flecs.h> first; tess core never provides Flecs.
namespace tess {

/**
 * Lossless conversion between Flecs' 64-bit entity ID and `EntityHandle`.
 *
 * The native null representation is mapped explicitly rather than through its
 * integral value.
 */
struct FlecsHandleAdapter {
  using entity_type = flecs::entity_t;

  [[nodiscard]] static auto to_handle(flecs::entity_t entity) noexcept
      -> EntityHandle {
    if (entity == 0u) {
      return kNullEntityHandle;
    }
    return EntityHandle{entity};
  }

  [[nodiscard]] static auto to_entity(EntityHandle handle) noexcept
      -> flecs::entity_t {
    return handle.is_null() ? 0u : handle.value;
  }
};
static_assert(EntityHandleAdapter<FlecsHandleAdapter>);

/** Position adapter backed by the shared Flecs `TilePosition` component. */
class FlecsTilePositionAdapter {
 public:
  explicit FlecsTilePositionAdapter(flecs::world& world) noexcept
      : world_(&world) {}

  [[nodiscard]] auto position(flecs::entity_t entity) const -> Coord3 {
    return world_->entity(entity).get<TilePosition>().coord;
  }

  void set_position(flecs::entity_t entity, Coord3 coord) {
    world_->entity(entity).set<TilePosition>(TilePosition{coord});
  }

 private:
  flecs::world* world_;
};
static_assert(PositionAdapter<FlecsTilePositionAdapter, flecs::entity_t>);

/** Collected Flecs entity paired with its deterministic sort key and state. */
struct FlecsAgentEntry {
  std::uint64_t agent_id = 0;
  flecs::entity_t entity = 0u;
  // Cached during query iteration. Flecs component addresses remain stable
  // because collection performs no structural mutation before fill ends.
  PathState* state = nullptr;
};

/**
 * Persistent tick state and reusable scratch for one Flecs agent system.
 *
 * Reserve once to avoid warm-path allocations. Persist `next_agent_id` with
 * the world so collection order remains deterministic across save and load.
 */
struct FlecsPathAgentContext {
  explicit FlecsPathAgentContext(flecs::world& world)
      : agent_query(
            world.query_builder<PathState, const TilePosition, const AgentId>()
                .build()) {}

  PathAgentTickState tick_state{};
  PathAgentBatch batch{};
  std::vector<FlecsAgentEntry> entries{};
  std::uint64_t next_agent_id = 0;
  flecs::query<PathState, const TilePosition, const AgentId> agent_query;

  void reserve(std::size_t agent_capacity) {
    batch.reserve(agent_capacity);
    entries.reserve(agent_capacity);
  }
};

namespace detail {

template <typename World>
[[nodiscard]] auto flecs_tile_resolves(const World& world, Coord3 tile) noexcept
    -> bool {
  const auto resolved = world.try_resolve(tile);
  if (!resolved.has_value()) {
    return false;
  }
  if constexpr (std::is_same_v<typename World::residency_type,
                               SparseResident>) {
    if (!world.is_resident(resolved->chunk_key)) {
      return false;
    }
  }
  return true;
}

template <typename World>
void flecs_mark_tile_dirty(World& world, Coord3 tile, std::uint32_t mask) {
  if (mask != 0) {
    world.mark_dirty(world.resolve(tile).chunk_key, mask,
                     Box3{tile, Extent3{1, 1, 1}});
  }
}

}  // namespace detail

/**
 * Collects on-board Flecs agents in ascending `AgentId` order.
 *
 * Collection reconciles changed `PathGoal` components into lifecycle state,
 * reports newly armed goals as dirty, and leaves unchanged unreachable goals
 * terminal until the consumer supplies a different goal.
 */
class FlecsPathAgentSource {
 public:
  FlecsPathAgentSource(flecs::world& world,
                       FlecsPathAgentContext& context) noexcept
      : world_(&world), context_(&context) {}

  auto collect(PathAgentBatch& batch) -> PathAgentCollectInfo {
    auto& entries = context_->entries;
    entries.clear();
    batch.clear();

    context_->agent_query.each([&](flecs::entity entity, PathState& state,
                                   const TilePosition&,
                                   const AgentId& agent_id) {
      if (!entity.has<OffBoard>()) {
        entries.push_back(FlecsAgentEntry{agent_id.value, entity.id(), &state});
      }
    });
    std::sort(entries.begin(), entries.end(),
              [](const FlecsAgentEntry& lhs, const FlecsAgentEntry& rhs) {
                return lhs.agent_id < rhs.agent_id;
              });

    PathAgentCollectInfo info;
    info.count = entries.size();
    // Fill the batch strictly in sorted-entry order so batch index i and
    // entries[i] stay one agent for the sink's write-back.
    for (const auto& entry : entries) {
      auto& state = *entry.state;
      if (const auto* goal = world_->entity(entry.entity).try_get<PathGoal>()) {
        if (!state.agent.has_goal || state.agent.goal != goal->coord) {
          set_path_agent_goal(state.agent, goal->coord);
          info.pathing_dirty = true;
        }
      } else if (state.agent.has_goal) {
        clear_path_agent_goal(state.agent);
      }
      batch.push(FlecsHandleAdapter::to_handle(entry.entity), state.agent);
    }
    return info;
  }

 private:
  flecs::world* world_;
  FlecsPathAgentContext* context_;
};
static_assert(PathAgentSource<FlecsPathAgentSource>);

template <typename Position = FlecsTilePositionAdapter>
  requires PositionAdapter<Position, flecs::entity_t>
/**
 * Mirrors a processed batch into Flecs without reapplying path results.
 *
 * Arrived goals are consumed to prevent re-arming; unreachable goals remain
 * present and inert until the consumer changes them.
 */
class FlecsPathAgentSink {
 public:
  FlecsPathAgentSink(flecs::world& world, FlecsPathAgentContext& context,
                     Position position) noexcept
      : world_(&world),
        context_(&context),
        position_(static_cast<Position&&>(position)) {}

  FlecsPathAgentSink(flecs::world& world,
                     FlecsPathAgentContext& context) noexcept
    requires std::constructible_from<Position, flecs::world&>
      : FlecsPathAgentSink(world, context, Position(world)) {}

  void apply(const PathAgentBatch& batch) {
    const auto agents = batch.agents();
    const auto& entries = context_->entries;
    TESS_ASSERT(agents.size() == entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
      const auto entity = entries[i].entity;
      const auto& agent = agents[i];
      auto flecs_entity = world_->entity(entity);
      flecs_entity.set<PathState>(PathState{agent});
      position_.set_position(entity, agent.position);
      if (const auto* goal = flecs_entity.try_get<PathGoal>();
          goal != nullptr && !agent.has_goal && agent.position == goal->coord) {
        flecs_entity.remove<PathGoal>();
      }
    }
  }

 private:
  flecs::world* world_;
  FlecsPathAgentContext* context_;
  Position position_;
};
static_assert(PathAgentSink<FlecsPathAgentSink<>>);

/**
 * Creates and indexes an idle on-board agent at `position`.
 *
 * Returns `0` without mutation when the tile cannot resolve or is
 * occupied according to either the world field or occupancy index.
 */
template <typename World, typename OccupancyTag>
[[nodiscard]] auto spawn_flecs_path_agent(
    flecs::world& ecs, FlecsPathAgentContext& context, World& world,
    TileOccupancyIndex& index, Coord3 position, std::uint32_t dirty_mask = 0,
    DeltaCollector* render_deltas = nullptr) -> flecs::entity_t {
  if (!detail::flecs_tile_resolves(world, position) ||
      static_cast<bool>(world.template field<OccupancyTag>(position)) ||
      !index.entity_at(position).is_null()) {
    return 0u;
  }
  auto state = PathAgentState{};
  state.position = position;
  const auto flecs_entity = ecs.entity()
                                .set<AgentId>(AgentId{context.next_agent_id++})
                                .set<TilePosition>(TilePosition{position})
                                .set<PathState>(PathState{state});
  const auto entity = flecs_entity.id();
  world.template field<OccupancyTag>(position) = true;
  const auto inserted =
      index.insert(position, FlecsHandleAdapter::to_handle(entity));
  TESS_ASSERT(inserted);
  static_cast<void>(inserted);
  detail::flecs_mark_tile_dirty(world, position, dirty_mask);
  if (render_deltas != nullptr) {
    render_deltas->record_spawn(FlecsHandleAdapter::to_handle(entity),
                                position);
  }
  return entity;
}

/**
 * Creates a parked agent with identity and lifecycle state but no tile claim.
 *
 * Use `place_flecs_path_agent` to put it on the board later.
 */
[[nodiscard]] inline auto spawn_flecs_path_agent_off_board(
    flecs::world& world, FlecsPathAgentContext& context) -> flecs::entity_t {
  return world.entity()
      .set<AgentId>(AgentId{context.next_agent_id++})
      .set<TilePosition>(TilePosition{})
      .set<PathState>(PathState{})
      .add<OffBoard>()
      .id();
}

/**
 * Releases any claimed tile and destroys a Flecs path-agent entity.
 *
 * Returns false without mutation when `entity` has no `PathState`. On-board
 * agents must use this operation rather than raw registry destruction.
 */
template <typename World, typename OccupancyTag>
auto despawn_flecs_path_agent(flecs::world& ecs, World& world,
                              TileOccupancyIndex& index, flecs::entity_t entity,
                              std::uint32_t dirty_mask = 0,
                              DeltaCollector* render_deltas = nullptr) -> bool {
  if (!ecs.is_alive(entity)) {
    return false;
  }
  auto flecs_entity = ecs.entity(entity);
  const auto* state = flecs_entity.try_get<PathState>();
  if (state == nullptr) {
    return false;
  }
  if (!flecs_entity.has<OffBoard>()) {
    const auto position = state->agent.position;
    world.template field<OccupancyTag>(position) = false;
    const auto erased = index.erase(position);
    TESS_ASSERT(erased == FlecsHandleAdapter::to_handle(entity));
    static_cast<void>(erased);
    detail::flecs_mark_tile_dirty(world, position, dirty_mask);
    if (render_deltas != nullptr) {
      // A parked despawn records nothing: parking already released the
      // tile and recorded it.
      render_deltas->record_despawn(FlecsHandleAdapter::to_handle(entity),
                                    position);
    }
  }
  flecs_entity.destruct();
  return true;
}

/**
 * Relocates an on-board agent and resets its lifecycle without changing ID.
 *
 * The goal component is retained for the next collection. Returns false
 * without mutation for parked agents or unavailable destinations.
 */
template <typename World, typename OccupancyTag>
auto teleport_flecs_path_agent(flecs::world& ecs, World& world,
                               TileOccupancyIndex& index,
                               flecs::entity_t entity, Coord3 to,
                               std::uint32_t dirty_mask = 0,
                               DeltaCollector* render_deltas = nullptr)
    -> bool {
  if (!ecs.is_alive(entity)) {
    return false;
  }
  auto flecs_entity = ecs.entity(entity);
  auto* state = flecs_entity.try_get_mut<PathState>();
  if (state == nullptr || flecs_entity.has<OffBoard>()) {
    return false;
  }
  if (!detail::flecs_tile_resolves(world, to) ||
      static_cast<bool>(world.template field<OccupancyTag>(to)) ||
      !index.entity_at(to).is_null()) {
    return false;
  }
  const auto from = state->agent.position;
  world.template field<OccupancyTag>(from) = false;
  world.template field<OccupancyTag>(to) = true;
  index.move(from, to, FlecsHandleAdapter::to_handle(entity));
  auto fresh = PathAgentState{};
  fresh.position = to;
  flecs_entity.set<PathState>(PathState{fresh});
  flecs_entity.set<TilePosition>(TilePosition{to});
  detail::flecs_mark_tile_dirty(world, from, dirty_mask);
  detail::flecs_mark_tile_dirty(world, to, dirty_mask);
  if (render_deltas != nullptr) {
    render_deltas->record_teleport(FlecsHandleAdapter::to_handle(entity), from,
                                   to);
  }
  return true;
}

/**
 * Parks an on-board agent after releasing its tile and resetting lifecycle.
 *
 * A retained goal remains inert until placement. Returns false for a missing
 * lifecycle component or an agent already parked.
 */
template <typename World, typename OccupancyTag>
auto park_flecs_path_agent(flecs::world& ecs, World& world,
                           TileOccupancyIndex& index, flecs::entity_t entity,
                           std::uint32_t dirty_mask = 0,
                           DeltaCollector* render_deltas = nullptr) -> bool {
  if (!ecs.is_alive(entity)) {
    return false;
  }
  auto flecs_entity = ecs.entity(entity);
  auto* state = flecs_entity.try_get_mut<PathState>();
  if (state == nullptr || flecs_entity.has<OffBoard>()) {
    return false;
  }
  const auto position = state->agent.position;
  world.template field<OccupancyTag>(position) = false;
  const auto erased = index.erase(position);
  TESS_ASSERT(erased == FlecsHandleAdapter::to_handle(entity));
  static_cast<void>(erased);
  clear_path_agent_goal(state->agent);
  flecs_entity.modified<PathState>();
  flecs_entity.add<OffBoard>();
  detail::flecs_mark_tile_dirty(world, position, dirty_mask);
  if (render_deltas != nullptr) {
    render_deltas->record_park(FlecsHandleAdapter::to_handle(entity), position);
  }
  return true;
}

/**
 * Places a parked agent at an available tile and restores its occupancy claim.
 *
 * Returns false without mutation when the agent is not parked or the tile is
 * unavailable.
 */
template <typename World, typename OccupancyTag>
auto place_flecs_path_agent(flecs::world& ecs, World& world,
                            TileOccupancyIndex& index, flecs::entity_t entity,
                            Coord3 position, std::uint32_t dirty_mask = 0,
                            DeltaCollector* render_deltas = nullptr) -> bool {
  if (!ecs.is_alive(entity)) {
    return false;
  }
  auto flecs_entity = ecs.entity(entity);
  auto* state = flecs_entity.try_get_mut<PathState>();
  if (state == nullptr || !flecs_entity.has<OffBoard>()) {
    return false;
  }
  if (!detail::flecs_tile_resolves(world, position) ||
      static_cast<bool>(world.template field<OccupancyTag>(position)) ||
      !index.entity_at(position).is_null()) {
    return false;
  }
  world.template field<OccupancyTag>(position) = true;
  const auto inserted =
      index.insert(position, FlecsHandleAdapter::to_handle(entity));
  TESS_ASSERT(inserted);
  static_cast<void>(inserted);
  auto fresh = PathAgentState{};
  fresh.position = position;
  flecs_entity.set<PathState>(PathState{fresh});
  flecs_entity.set<TilePosition>(TilePosition{position});
  flecs_entity.remove<OffBoard>();
  detail::flecs_mark_tile_dirty(world, position, dirty_mask);
  if (render_deltas != nullptr) {
    render_deltas->record_place(FlecsHandleAdapter::to_handle(entity),
                                position);
  }
  return true;
}

/** Writes a goal component for reconciliation during the next collection. */
inline void set_flecs_path_agent_goal(flecs::world& world,
                                      flecs::entity_t entity, Coord3 goal) {
  world.entity(entity).set<PathGoal>(PathGoal{goal});
}

/** Removes an agent's goal component for clearing on the next collection. */
inline void clear_flecs_path_agent_goal(flecs::world& world,
                                        flecs::entity_t entity) {
  world.entity(entity).remove<PathGoal>();
}

template <typename World, typename ClassOrTag, typename OccupancyTag,
          typename ReservationTag, typename Position = FlecsTilePositionAdapter>
/**
 * Runs the unit-cost agent pipeline using Flecs collection and write-back.
 *
 * Call from at most one driver per frame for `context`; `runtime` must be
 * exclusive to this agent system.
 */
[[nodiscard]] auto tick_flecs_unit_path_agents(
    flecs::world& ecs, FlecsPathAgentContext& context, World& world,
    PathRequestRuntime& runtime, TileOccupancyIndex& index,
    PathAgentTickOptions options = {}, std::uint32_t movement_dirty_mask = 0,
    const RegionGraphT<typename World::residency_type>* graph = nullptr,
    DeltaCollector* render_deltas = nullptr) -> PathAgentTickStats {
  FlecsPathAgentSource source(ecs, context);
  FlecsPathAgentSink<Position> sink(ecs, context);
  return tick_ecs_unit_path_agents<World, ClassOrTag, OccupancyTag,
                                   ReservationTag>(
      context.tick_state, world, source, sink, context.batch, runtime, index,
      options, movement_dirty_mask, graph, render_deltas);
}

template <typename World, typename Class, std::uint32_t MaxCost,
          typename OccupancyTag, typename ReservationTag,
          typename Position = FlecsTilePositionAdapter>
/** Runs the Flecs agent pipeline for a weighted movement class. */
[[nodiscard]] auto tick_flecs_path_agents(
    flecs::world& ecs, FlecsPathAgentContext& context, World& world,
    PathRequestRuntime& runtime, TileOccupancyIndex& index,
    PathAgentTickOptions options = {}, std::uint32_t movement_dirty_mask = 0,
    const RegionGraphT<typename World::residency_type>* graph = nullptr,
    DeltaCollector* render_deltas = nullptr) -> PathAgentTickStats {
  FlecsPathAgentSource source(ecs, context);
  FlecsPathAgentSink<Position> sink(ecs, context);
  return tick_ecs_path_agents<World, Class, MaxCost, OccupancyTag,
                              ReservationTag>(
      context.tick_state, world, source, sink, context.batch, runtime, index,
      options, movement_dirty_mask, graph, render_deltas);
}

template <typename World, typename PassableTag, typename CostTag,
          std::uint32_t MaxCost, typename OccupancyTag, typename ReservationTag,
          typename Position = FlecsTilePositionAdapter>
/** Runs the weighted Flecs pipeline using separate passability and cost tags.
 */
[[nodiscard]] auto tick_flecs_path_agents(
    flecs::world& ecs, FlecsPathAgentContext& context, World& world,
    PathRequestRuntime& runtime, TileOccupancyIndex& index,
    PathAgentTickOptions options = {}, std::uint32_t movement_dirty_mask = 0,
    const RegionGraphT<typename World::residency_type>* graph = nullptr,
    DeltaCollector* render_deltas = nullptr) -> PathAgentTickStats {
  FlecsPathAgentSource source(ecs, context);
  FlecsPathAgentSink<Position> sink(ecs, context);
  return tick_ecs_path_agents<World, PassableTag, CostTag, MaxCost,
                              OccupancyTag, ReservationTag>(
      context.tick_state, world, source, sink, context.batch, runtime, index,
      options, movement_dirty_mask, graph, render_deltas);
}

}  // namespace tess

#endif  // TESS_ENABLE_FLECS
