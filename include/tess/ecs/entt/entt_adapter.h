#pragma once

#include <tess/ecs/adapter.h>

#if defined(TESS_ENABLE_ENTT)

#ifndef ENTT_VERSION
#error \
    "Include <entt/entity/registry.hpp> before <tess/ecs/entt/entt_adapter.h>"
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

// The EnTT adapter (M10): concrete PathAgentSource/PathAgentSink types
// over an entt::registry, explicit lifecycle intents (spawn / despawn /
// teleport / park / place -- raw registry.destroy on an on-board agent
// leaks a permanently blocked tile, so the intents are the only
// sanctioned mutation paths), and tick_entt_* drivers that are thin
// instantiations of the generic tick_ecs_* pipeline. Compiled only when
// the consumer defines TESS_ENABLE_ENTT and includes
// <entt/entity/registry.hpp> first; tess core never provides EnTT.
namespace tess {

/**
 * Lossless conversion between `entt::entity` and `EntityHandle`.
 *
 * The native null representation is mapped explicitly rather than through its
 * integral value.
 */
struct EnttHandleAdapter {
  using entity_type = entt::entity;

  [[nodiscard]] static auto to_handle(entt::entity entity) noexcept
      -> EntityHandle {
    if (entity == entt::null) {
      return kNullEntityHandle;
    }
    return EntityHandle{static_cast<std::uint64_t>(entt::to_integral(entity))};
  }

  [[nodiscard]] static auto to_entity(EntityHandle handle) noexcept
      -> entt::entity {
    if (handle.is_null()) {
      return entt::null;
    }
    return static_cast<entt::entity>(static_cast<entt::id_type>(handle.value));
  }
};
static_assert(EntityHandleAdapter<EnttHandleAdapter>);

/** Position adapter backed by the shared EnTT `TilePosition` component. */
class EnttTilePositionAdapter {
 public:
  explicit EnttTilePositionAdapter(entt::registry& registry) noexcept
      : registry_(&registry) {}

  [[nodiscard]] auto position(entt::entity entity) const -> Coord3 {
    return registry_->get<TilePosition>(entity).coord;
  }

  void set_position(entt::entity entity, Coord3 coord) {
    registry_->get<TilePosition>(entity).coord = coord;
  }

 private:
  entt::registry* registry_;
};
static_assert(PositionAdapter<EnttTilePositionAdapter, entt::entity>);

/** Collected EnTT entity paired with its deterministic sort key and state. */
struct EnttAgentEntry {
  std::uint64_t agent_id = 0;
  entt::entity entity = entt::null;
  // Cached during the collect view walk (EnTT component addresses are
  // stable while no PathState is added/removed, and only the entry
  // vector is sorted in between), saving a registry lookup per agent in
  // the fill loop (audit 2026-07-11 low).
  PathState* state = nullptr;
};

/**
 * Persistent tick state and reusable scratch for one EnTT agent system.
 *
 * Reserve once to avoid warm-path allocations. Persist `next_agent_id` with
 * the world so collection order remains deterministic across save and load.
 */
struct EnttPathAgentContext {
  PathAgentTickState tick_state{};
  PathAgentBatch batch{};
  std::vector<EnttAgentEntry> entries{};
  std::uint64_t next_agent_id = 0;

  void reserve(std::size_t agent_capacity) {
    batch.reserve(agent_capacity);
    entries.reserve(agent_capacity);
  }
};

namespace detail {

template <typename World>
[[nodiscard]] auto ecs_tile_resolves(const World& world, Coord3 tile) noexcept
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
void ecs_mark_tile_dirty(World& world, Coord3 tile, std::uint32_t mask) {
  if (mask != 0) {
    world.mark_dirty(world.resolve(tile).chunk_key, mask,
                     Box3{tile, Extent3{1, 1, 1}});
  }
}

}  // namespace detail

/**
 * Collects on-board EnTT agents in ascending `AgentId` order.
 *
 * Collection reconciles changed `PathGoal` components into lifecycle state,
 * reports newly armed goals as dirty, and leaves unchanged unreachable goals
 * terminal until the consumer supplies a different goal.
 */
class EnttPathAgentSource {
 public:
  EnttPathAgentSource(entt::registry& registry,
                      EnttPathAgentContext& context) noexcept
      : registry_(&registry), context_(&context) {}

  auto collect(PathAgentBatch& batch) -> PathAgentCollectInfo {
    auto& entries = context_->entries;
    entries.clear();
    batch.clear();

    auto view = registry_->view<PathState, TilePosition, AgentId>(
        entt::exclude<OffBoard>);
    for (const auto entity : view) {
      entries.push_back(EnttAgentEntry{view.template get<AgentId>(entity).value,
                                       entity,
                                       &view.template get<PathState>(entity)});
    }
    std::sort(entries.begin(), entries.end(),
              [](const EnttAgentEntry& lhs, const EnttAgentEntry& rhs) {
                return lhs.agent_id < rhs.agent_id;
              });

    PathAgentCollectInfo info;
    info.count = entries.size();
    // Fill the batch strictly in sorted-entry order so batch index i and
    // entries[i] stay one agent for the sink's write-back.
    for (const auto& entry : entries) {
      auto& state = *entry.state;
      if (const auto* goal = registry_->try_get<PathGoal>(entry.entity)) {
        if (!state.agent.has_goal || state.agent.goal != goal->coord) {
          set_path_agent_goal(state.agent, goal->coord);
          info.pathing_dirty = true;
        }
      } else if (state.agent.has_goal) {
        clear_path_agent_goal(state.agent);
      }
      batch.push(EnttHandleAdapter::to_handle(entry.entity), state.agent);
    }
    return info;
  }

 private:
  entt::registry* registry_;
  EnttPathAgentContext* context_;
};
static_assert(PathAgentSource<EnttPathAgentSource>);

template <typename Position = EnttTilePositionAdapter>
  requires PositionAdapter<Position, entt::entity>
/**
 * Mirrors a processed batch into EnTT without reapplying path results.
 *
 * Arrived goals are consumed to prevent re-arming; unreachable goals remain
 * present and inert until the consumer changes them.
 */
class EnttPathAgentSink {
 public:
  EnttPathAgentSink(entt::registry& registry, EnttPathAgentContext& context,
                    Position position) noexcept
      : registry_(&registry),
        context_(&context),
        position_(static_cast<Position&&>(position)) {}

  EnttPathAgentSink(entt::registry& registry,
                    EnttPathAgentContext& context) noexcept
    requires std::constructible_from<Position, entt::registry&>
      : EnttPathAgentSink(registry, context, Position(registry)) {}

  void apply(const PathAgentBatch& batch) {
    const auto agents = batch.agents();
    const auto& entries = context_->entries;
    TESS_ASSERT(agents.size() == entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
      const auto entity = entries[i].entity;
      const auto& agent = agents[i];
      registry_->get<PathState>(entity).agent = agent;
      position_.set_position(entity, agent.position);
      if (const auto* goal = registry_->try_get<PathGoal>(entity);
          goal != nullptr && !agent.has_goal && agent.position == goal->coord) {
        registry_->remove<PathGoal>(entity);
      }
    }
  }

 private:
  entt::registry* registry_;
  EnttPathAgentContext* context_;
  Position position_;
};
static_assert(PathAgentSink<EnttPathAgentSink<>>);

/**
 * Creates and indexes an idle on-board agent at `position`.
 *
 * Returns `entt::null` without mutation when the tile cannot resolve or is
 * occupied according to either the world field or occupancy index.
 */
template <typename World, typename OccupancyTag>
[[nodiscard]] auto spawn_entt_path_agent(
    entt::registry& registry, EnttPathAgentContext& context, World& world,
    TileOccupancyIndex& index, Coord3 position, std::uint32_t dirty_mask = 0,
    DeltaCollector* render_deltas = nullptr) -> entt::entity {
  if (!detail::ecs_tile_resolves(world, position) ||
      static_cast<bool>(world.template field<OccupancyTag>(position)) ||
      !index.entity_at(position).is_null()) {
    return entt::null;
  }
  const auto entity = registry.create();
  registry.emplace<AgentId>(entity, AgentId{context.next_agent_id++});
  registry.emplace<TilePosition>(entity, TilePosition{position});
  auto state = PathAgentState{};
  state.position = position;
  registry.emplace<PathState>(entity, PathState{state});
  world.template field<OccupancyTag>(position) = true;
  const auto inserted =
      index.insert(position, EnttHandleAdapter::to_handle(entity));
  TESS_ASSERT(inserted);
  static_cast<void>(inserted);
  detail::ecs_mark_tile_dirty(world, position, dirty_mask);
  if (render_deltas != nullptr) {
    render_deltas->record_spawn(EnttHandleAdapter::to_handle(entity), position);
  }
  return entity;
}

/**
 * Creates a parked agent with identity and lifecycle state but no tile claim.
 *
 * Use `place_entt_path_agent` to put it on the board later.
 */
[[nodiscard]] inline auto spawn_entt_path_agent_off_board(
    entt::registry& registry, EnttPathAgentContext& context) -> entt::entity {
  const auto entity = registry.create();
  registry.emplace<AgentId>(entity, AgentId{context.next_agent_id++});
  registry.emplace<TilePosition>(entity, TilePosition{});
  registry.emplace<PathState>(entity, PathState{});
  registry.emplace<OffBoard>(entity);
  return entity;
}

/**
 * Releases any claimed tile and destroys an EnTT path-agent entity.
 *
 * Returns false without mutation when `entity` has no `PathState`. On-board
 * agents must use this operation rather than raw registry destruction.
 */
template <typename World, typename OccupancyTag>
auto despawn_entt_path_agent(entt::registry& registry, World& world,
                             TileOccupancyIndex& index, entt::entity entity,
                             std::uint32_t dirty_mask = 0,
                             DeltaCollector* render_deltas = nullptr) -> bool {
  const auto* state = registry.try_get<PathState>(entity);
  if (state == nullptr) {
    return false;
  }
  if (!registry.all_of<OffBoard>(entity)) {
    const auto position = state->agent.position;
    world.template field<OccupancyTag>(position) = false;
    const auto erased = index.erase(position);
    TESS_ASSERT(erased == EnttHandleAdapter::to_handle(entity));
    static_cast<void>(erased);
    detail::ecs_mark_tile_dirty(world, position, dirty_mask);
    if (render_deltas != nullptr) {
      // A parked despawn records nothing: parking already released the
      // tile and recorded it.
      render_deltas->record_despawn(EnttHandleAdapter::to_handle(entity),
                                    position);
    }
  }
  registry.destroy(entity);
  return true;
}

/**
 * Relocates an on-board agent and resets its lifecycle without changing ID.
 *
 * The goal component is retained for the next collection. Returns false
 * without mutation for parked agents or unavailable destinations.
 */
template <typename World, typename OccupancyTag>
auto teleport_entt_path_agent(entt::registry& registry, World& world,
                              TileOccupancyIndex& index, entt::entity entity,
                              Coord3 to, std::uint32_t dirty_mask = 0,
                              DeltaCollector* render_deltas = nullptr) -> bool {
  auto* state = registry.try_get<PathState>(entity);
  if (state == nullptr || registry.all_of<OffBoard>(entity)) {
    return false;
  }
  if (!detail::ecs_tile_resolves(world, to) ||
      static_cast<bool>(world.template field<OccupancyTag>(to)) ||
      !index.entity_at(to).is_null()) {
    return false;
  }
  const auto from = state->agent.position;
  world.template field<OccupancyTag>(from) = false;
  world.template field<OccupancyTag>(to) = true;
  index.move(from, to, EnttHandleAdapter::to_handle(entity));
  auto fresh = PathAgentState{};
  fresh.position = to;
  state->agent = fresh;
  registry.get<TilePosition>(entity).coord = to;
  detail::ecs_mark_tile_dirty(world, from, dirty_mask);
  detail::ecs_mark_tile_dirty(world, to, dirty_mask);
  if (render_deltas != nullptr) {
    render_deltas->record_teleport(EnttHandleAdapter::to_handle(entity), from,
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
auto park_entt_path_agent(entt::registry& registry, World& world,
                          TileOccupancyIndex& index, entt::entity entity,
                          std::uint32_t dirty_mask = 0,
                          DeltaCollector* render_deltas = nullptr) -> bool {
  auto* state = registry.try_get<PathState>(entity);
  if (state == nullptr || registry.all_of<OffBoard>(entity)) {
    return false;
  }
  const auto position = state->agent.position;
  world.template field<OccupancyTag>(position) = false;
  const auto erased = index.erase(position);
  TESS_ASSERT(erased == EnttHandleAdapter::to_handle(entity));
  static_cast<void>(erased);
  clear_path_agent_goal(state->agent);
  registry.emplace<OffBoard>(entity);
  detail::ecs_mark_tile_dirty(world, position, dirty_mask);
  if (render_deltas != nullptr) {
    render_deltas->record_park(EnttHandleAdapter::to_handle(entity), position);
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
auto place_entt_path_agent(entt::registry& registry, World& world,
                           TileOccupancyIndex& index, entt::entity entity,
                           Coord3 position, std::uint32_t dirty_mask = 0,
                           DeltaCollector* render_deltas = nullptr) -> bool {
  auto* state = registry.try_get<PathState>(entity);
  if (state == nullptr || !registry.all_of<OffBoard>(entity)) {
    return false;
  }
  if (!detail::ecs_tile_resolves(world, position) ||
      static_cast<bool>(world.template field<OccupancyTag>(position)) ||
      !index.entity_at(position).is_null()) {
    return false;
  }
  world.template field<OccupancyTag>(position) = true;
  const auto inserted =
      index.insert(position, EnttHandleAdapter::to_handle(entity));
  TESS_ASSERT(inserted);
  static_cast<void>(inserted);
  auto fresh = PathAgentState{};
  fresh.position = position;
  state->agent = fresh;
  registry.get<TilePosition>(entity).coord = position;
  registry.remove<OffBoard>(entity);
  detail::ecs_mark_tile_dirty(world, position, dirty_mask);
  if (render_deltas != nullptr) {
    render_deltas->record_place(EnttHandleAdapter::to_handle(entity), position);
  }
  return true;
}

/** Writes a goal component for reconciliation during the next collection. */
inline void set_entt_path_agent_goal(entt::registry& registry,
                                     entt::entity entity, Coord3 goal) {
  registry.emplace_or_replace<PathGoal>(entity, PathGoal{goal});
}

/** Removes an agent's goal component for clearing on the next collection. */
inline void clear_entt_path_agent_goal(entt::registry& registry,
                                       entt::entity entity) {
  registry.remove<PathGoal>(entity);
}

template <typename World, typename ClassOrTag, typename OccupancyTag,
          typename ReservationTag, typename Position = EnttTilePositionAdapter>
/**
 * Runs the unit-cost agent pipeline using EnTT collection and write-back.
 *
 * Call from at most one driver per frame for `context`; `runtime` must be
 * exclusive to this agent system.
 */
[[nodiscard]] auto tick_entt_unit_path_agents(
    entt::registry& registry, EnttPathAgentContext& context, World& world,
    PathRequestRuntime& runtime, TileOccupancyIndex& index,
    PathAgentTickOptions options = {}, std::uint32_t movement_dirty_mask = 0,
    const RegionGraphT<typename World::residency_type>* graph = nullptr,
    DeltaCollector* render_deltas = nullptr) -> PathAgentTickStats {
  EnttPathAgentSource source(registry, context);
  EnttPathAgentSink<Position> sink(registry, context);
  return tick_ecs_unit_path_agents<World, ClassOrTag, OccupancyTag,
                                   ReservationTag>(
      context.tick_state, world, source, sink, context.batch, runtime, index,
      options, movement_dirty_mask, graph, render_deltas);
}

template <typename World, typename Class, std::uint32_t MaxCost,
          typename OccupancyTag, typename ReservationTag,
          typename Position = EnttTilePositionAdapter>
/** Runs the EnTT agent pipeline for a weighted movement class. */
[[nodiscard]] auto tick_entt_path_agents(
    entt::registry& registry, EnttPathAgentContext& context, World& world,
    PathRequestRuntime& runtime, TileOccupancyIndex& index,
    PathAgentTickOptions options = {}, std::uint32_t movement_dirty_mask = 0,
    const RegionGraphT<typename World::residency_type>* graph = nullptr,
    DeltaCollector* render_deltas = nullptr) -> PathAgentTickStats {
  EnttPathAgentSource source(registry, context);
  EnttPathAgentSink<Position> sink(registry, context);
  return tick_ecs_path_agents<World, Class, MaxCost, OccupancyTag,
                              ReservationTag>(
      context.tick_state, world, source, sink, context.batch, runtime, index,
      options, movement_dirty_mask, graph, render_deltas);
}

template <typename World, typename PassableTag, typename CostTag,
          std::uint32_t MaxCost, typename OccupancyTag, typename ReservationTag,
          typename Position = EnttTilePositionAdapter>
/** Runs the weighted EnTT pipeline using separate passability and cost tags. */
[[nodiscard]] auto tick_entt_path_agents(
    entt::registry& registry, EnttPathAgentContext& context, World& world,
    PathRequestRuntime& runtime, TileOccupancyIndex& index,
    PathAgentTickOptions options = {}, std::uint32_t movement_dirty_mask = 0,
    const RegionGraphT<typename World::residency_type>* graph = nullptr,
    DeltaCollector* render_deltas = nullptr) -> PathAgentTickStats {
  EnttPathAgentSource source(registry, context);
  EnttPathAgentSink<Position> sink(registry, context);
  return tick_ecs_path_agents<World, PassableTag, CostTag, MaxCost,
                              OccupancyTag, ReservationTag>(
      context.tick_state, world, source, sink, context.batch, runtime, index,
      options, movement_dirty_mask, graph, render_deltas);
}

}  // namespace tess

#endif  // TESS_ENABLE_ENTT
