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

// Lossless entt::entity <-> EntityHandle conversion. The null mapping is
// an explicit special case: entt's null entity zero-extends to a value
// that is NOT the all-ones kNullEntityHandle, so both directions branch
// on null instead of trusting the integral cast.
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

// Default PositionAdapter for EnTT stores: reads and writes the shared
// TilePosition component. Games with their own position component supply
// their own adapter to EnttPathAgentSink instead.
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

// One collected agent: the deterministic sort key plus its entity,
// parallel to the batch entry at the same position.
struct EnttAgentEntry {
  std::uint64_t agent_id = 0;
  entt::entity entity = entt::null;
};

// Persistent per-agent-system state: the tick driver's clock/dirty
// state, the batch scratch, the sorted entry scratch, and the monotonic
// spawn-id counter. reserve() once; every per-tick container is
// clear-and-refill after that. For replay determinism across save/load,
// persist next_agent_id alongside the world.
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

// Collects every on-board agent (entities holding PathState, TilePosition,
// and AgentId, excluding OffBoard) in ascending AgentId order -- ids are
// unique and never recycled, so the order is total and independent of
// pool-packing history. Collection reconciles the PathGoal component into
// the lifecycle before batching:
//   - goal present and differing from the armed goal (or no goal armed):
//     set_path_agent_goal, reported as pathing_dirty. An Unreachable agent
//     whose PathGoal is UNCHANGED stays terminal -- the lifecycle retains
//     the failed goal, so the inequality is false until the game writes a
//     new goal.
//   - goal component absent while the lifecycle is armed:
//     clear_path_agent_goal (not dirty; cleared agents are skipped by
//     every processing pass).
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
      entries.push_back(
          EnttAgentEntry{view.template get<AgentId>(entity).value, entity});
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
      auto& state = registry_->get<PathState>(entry.entity);
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

// Mirrors batch state back to the registry: PathState is stored
// unconditionally (idempotent -- results were already applied inside the
// tick pipeline), positions flow through the PositionAdapter, and the
// PathGoal component is CONSUMED on arrival (present, lifecycle no longer
// armed, position equals the goal) so the arrival-cleared agent does not
// re-arm the same goal at the next collect and oscillate. A goal retained
// after an Unreachable failure is deliberately not consumed: it sits
// inert until the game changes it.
template <typename Position = EnttTilePositionAdapter>
  requires PositionAdapter<Position, entt::entity>
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

// Creates an on-board agent at `position`: AgentId from the context's
// monotonic counter, TilePosition, PathState (Idle at `position`), the
// occupancy field claimed, and the index mapping inserted. Returns
// entt::null (mutating nothing) if the tile does not resolve or is
// already occupied per field or index -- occupancy uniqueness is checked
// before any write.
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

// Creates a parked agent: AgentId + PathState (Idle) + TilePosition (its
// last-known value is meaningless while parked) + OffBoard. No tile, no
// occupancy claim, no index entry; place_entt_path_agent puts it on the
// board later. This is how consumers spawn populations larger than the
// currently free tile set.
[[nodiscard]] inline auto spawn_entt_path_agent_off_board(
    entt::registry& registry, EnttPathAgentContext& context) -> entt::entity {
  const auto entity = registry.create();
  registry.emplace<AgentId>(entity, AgentId{context.next_agent_id++});
  registry.emplace<TilePosition>(entity, TilePosition{});
  registry.emplace<PathState>(entity, PathState{});
  registry.emplace<OffBoard>(entity);
  return entity;
}

// Destroys an agent entity, releasing its tile first (occupancy field
// cleared, index mapping erased) unless it is parked. THE only sanctioned
// destroy path for agents: raw registry.destroy on an on-board agent
// leaves its tile permanently blocked because nothing else clears
// resting-tile occupancy. Returns false (mutating nothing) if `entity`
// does not hold PathState.
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

// Occupancy-checked relocation without identity churn: frees the old
// tile, claims the new one, and resets the lifecycle to Idle at `to`.
// The PathGoal component is deliberately RETAINED: the next collect
// re-arms it from the new position, so relocated agents resume their
// goals (teleporting onto the goal tile arrives at the next processed
// tick). Returns false (mutating nothing) if the agent is parked or
// `to` does not resolve or is occupied per field or index.
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

// Takes an on-board agent off the board: occupancy field cleared, index
// mapping erased, lifecycle reset to Idle, OffBoard added. A retained
// PathGoal component sits inert while parked (parked agents are excluded
// from collection) and re-arms when the agent is placed back. Returns
// false if the agent is missing PathState or already parked.
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

// Puts a parked agent on the board at `position`: the mirror of
// park_entt_path_agent, with the same availability checks as spawning.
// Returns false if the agent is not parked or the tile does not resolve
// or is occupied per field or index.
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

// Arms (or re-arms) an agent's goal by writing the PathGoal component;
// the next collect reconciles it into the lifecycle.
inline void set_entt_path_agent_goal(entt::registry& registry,
                                     entt::entity entity, Coord3 goal) {
  registry.emplace_or_replace<PathGoal>(entity, PathGoal{goal});
}

// Removes the PathGoal component; the next collect clears the lifecycle.
inline void clear_entt_path_agent_goal(entt::registry& registry,
                                       entt::entity entity) {
  registry.remove<PathGoal>(entity);
}

// The EnTT tick drivers: thin instantiations of the generic tick_ecs_*
// pipeline with the concrete source/sink above. See
// tick_ecs_unit_path_agents for the pipeline and the runtime-exclusivity
// contract; the context's tick_state owns the sim clock, so drive these
// from at most one place per frame.
template <typename World, typename ClassOrTag, typename OccupancyTag,
          typename ReservationTag, typename Position = EnttTilePositionAdapter>
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

// Weighted movement-class form.
template <typename World, typename Class, std::uint32_t MaxCost,
          typename OccupancyTag, typename ReservationTag,
          typename Position = EnttTilePositionAdapter>
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

// Legacy passable/cost tag-pair form.
template <typename World, typename PassableTag, typename CostTag,
          std::uint32_t MaxCost, typename OccupancyTag, typename ReservationTag,
          typename Position = EnttTilePositionAdapter>
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
