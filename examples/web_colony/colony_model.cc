// Platform-neutral model for the colony tutorial. colony_wasm.cc exposes
// it to JavaScript; colony_native.cc owns native verification scenarios.

#include "colony_model_internal.h"

namespace tess::examples::web_colony {

ColonyModel::Impl::Impl(int agent_count) {
  const auto count = static_cast<std::size_t>(agent_count);
  initialize_world();
  reserve_working_memory(count);
  initialize_agents(count);
  configure_build_task();
  configure_schedule();
  initialize_render_consumer();
}

void ColonyModel::Impl::initialize_world() {
  world.fill_field<PassableTag>(true);
  world.fill_field<CostTag>(1);
  tess::build_region_graph<World, Walker>(world, topo_scratch, graph);
}

void ColonyModel::Impl::reserve_working_memory(std::size_t agent_count) {
  // These one-time reserves keep the browser's fixed-tick hot path free of
  // incidental growth. Request capacity permits two queued jobs per maximum
  // population; node capacities cover the demo's 128x128 grid searches and
  // retained routes. Delta storage covers half a grid of sparse tile edits,
  // with box records as its bounded fallback.
  joint_scratch.reserve(agent_count);
  recovery_schedule.reserve(agent_count);
  replan_queue.reserve(agent_count);
  diversity_replan_queue.reserve(agent_count);
  wide_merge_replan_queue.reserve(agent_count);
  merge_claims.resize(static_cast<std::size_t>(kWidth) * kHeight);
  runtime.reserve_requests(2048);
  runtime.reserve_search_nodes(65536);
  runtime.reserve_path_nodes(262144);
  replan_scratch.reserve_nodes(65536);
  deltas.reserve(World::chunk_count, 8192, 16);
}

void ColonyModel::Impl::initialize_agents(std::size_t agent_count) {
  agents.resize(agent_count);
  agent_xy.resize(agents.size() * 2);
  previous_agent_xy.resize(agents.size() * 2);
  crowd_blocked.assign(agents.size(), 0);
  terrain_confirmation_pending.assign(agents.size(), 0);
  for (std::size_t i = 0; i < agents.size(); ++i) {
    agents[i].position = home_tile(i);
    world.field<OccupancyTag>(agents[i].position) = true;
    tess::set_path_agent_goal(tick_state, agents[i], away_tile(i));
    agents[i].phase = tess::PathAgentPhase::Blocked;
  }
  snapshot_after_movement();
  snapshot_before_movement();
  replan_queue.request_all(agents);
  tick_state.pathing_dirty = false;
}

void ColonyModel::Impl::configure_build_task() {
  build_task = std::make_unique<BuildTask>(world, ops, BuildTaskFn{this});
  build_task->reserve_operations(64);
  build_task->set_result_hook(
      this, [](void* ctx, tess::OpHandle, const tess::OpCompletion& done,
               const BuildAck* ack) noexcept {
        auto* self = static_cast<Impl*>(ctx);
        if (done.ok() && ack != nullptr) {
          self->built_tiles += ack->tiles;
          // AutoExec executes accepted kernels before draining hooks. The
          // accepted operation for a chunk therefore applies all pending walls
          // in that chunk before this completion clears the shared input.
          self->pending_walls.clear();
        }
      });
}

void ColonyModel::Impl::configure_schedule() {
  // [colony-schedule-order]
  schedule.reserve_tasks(3);
  (void)schedule.add_task(
      {"build", tess::SimPhase::PreUpdate, tess::Cadence::every_tick()},
      *build_task);
  (void)schedule.add_task({"topology", tess::SimPhase::Pathing,
                           tess::Cadence::on_dirty(kTerrainDirty)},
                          topology_task);
  // Pathing follows PreUpdate in the same fixed tick, so walls built above
  // refresh topology before the Movement task can plan against them.
  (void)schedule.add_task(
      {"agents", tess::SimPhase::Movement, tess::Cadence::every_tick()},
      agent_task);
  schedule.seal();
  // [colony-schedule-order]
}

void ColonyModel::Impl::initialize_render_consumer() {
  shadow.assign(static_cast<std::size_t>(kWidth) * kHeight, 0);
  // A fresh consumer accepts only a baseline. Incremental frames start after
  // the shadow and its consumer version have adopted this complete snapshot.
  tess::collect_baseline(deltas, world, kTerrainDirty);
  if (!consume_frame(deltas.publish())) {
    TESS_ASSERT(false);
  }
}

// [colony-queued-edits]
auto ColonyModel::Impl::set_wall(tess::Coord3 coord, bool built) -> bool {
  // Example: queue a world edit. Admission is synchronous, but mutation is
  // deferred to the PreUpdate AutoExec task so dirty publication, topology,
  // and movement retain one deterministic schedule order.
  // JavaScript and the Wasm model run on one thread: no fixed tick can move
  // an agent between this admission check and the next PreUpdate build. Keep
  // the invariant every other colony writer already follows -- construction
  // never turns an occupied source into impassable terrain.
  const auto pending = std::find_if(
      pending_walls.begin(), pending_walls.end(),
      [coord](const WallEdit& edit) { return edit.coord == coord; });
  const auto effective = pending != pending_walls.end()
                             ? pending->built
                             : world.field<ConstructionTag>(coord) != 0;
  if (effective == built) {
    return true;
  }
  if (built && world.field<OccupancyTag>(coord)) {
    return false;
  }
  if (pending != pending_walls.end()) {
    pending->built = built;
    return true;
  }
  pending_walls.push_back(WallEdit{coord, built});
  const auto key = tess::chunk_key<Shape>(tess::chunk_coord<Shape>(coord));
  (void)ops.update_field(
      tess::DomainDesc::explicit_chunks({&key, 1}),
      tess::FieldAccessDesc{0, kTerrainDirty.value, kTerrainDirty},
      tess::WritePolicy::UniquePerChunk);
  return true;
}
// [colony-queued-edits]

// [colony-delta-recovery]
void ColonyModel::Impl::publish_render_frame() {
  tess::collect_tile_deltas(deltas, world, kTerrainDirty);
  if (consume_frame(deltas.publish())) {
    return;
  }

  // Example: recover a rejected DeltaFrame. A version gap or truncation is
  // structural, so skipping it and resuming incrementals cannot repair the
  // shadow. Publish a complete baseline and adopt its version instead.
  tess::collect_baseline(deltas, world, kTerrainDirty);
  if (!consume_frame(deltas.publish())) {
    TESS_ASSERT(false);
  }
}
// [colony-delta-recovery]

ColonyModel::ColonyModel(int agent_count)
    : impl_(std::make_unique<Impl>(std::clamp(agent_count, 1, kMaxAgents))) {}

ColonyModel::~ColonyModel() = default;

auto ColonyModel::set_wall(int x, int y, bool built) -> bool {
  if (x < kWallMinX || x > kWallMaxX || y < 0 || y >= kHeight) {
    return false;
  }
  return impl_->set_wall(tess::Coord3{x, y, 0}, built);
}

void ColonyModel::set_replan_each_tick(bool enabled) noexcept {
  impl_->replan_each_tick = enabled;
}

void ColonyModel::set_spread_congested_routes(bool enabled) noexcept {
  impl_->spread_congested_routes = enabled;
  if (!enabled &&
      (!impl_->diversity_replan_queue.empty() ||
       !impl_->wide_merge_replan_queue.empty() || impl_->routes_diversified)) {
    impl_->diversity_replan_queue.clear();
    impl_->wide_merge_replan_queue.clear();
    impl_->routes_diversified = false;
    impl_->replan_queue.request_all(impl_->agents);
  }
}

auto ColonyModel::tick(double dt_seconds) -> double {
  return impl_->tick(dt_seconds);
}

auto ColonyModel::relaunch() -> int { return impl_->relaunch(); }

auto ColonyModel::leg() const noexcept -> int { return impl_->current_leg(); }

auto ColonyModel::completed_legs() const noexcept -> int {
  return impl_->completed_leg_count();
}

auto ColonyModel::aborted_legs() const noexcept -> int {
  return impl_->aborted_leg_count();
}

auto ColonyModel::agent_count() const noexcept -> int {
  return static_cast<int>(impl_->agents.size());
}

auto ColonyModel::arrived() const -> int { return impl_->arrived(); }

auto ColonyModel::unreachable() const -> int { return impl_->unreachable(); }

auto ColonyModel::crowd_blocked() const -> int {
  return impl_->crowd_blocked_count();
}

auto ColonyModel::turnaround_ready() const -> bool {
  return impl_->turnaround_ready();
}

auto ColonyModel::stalled_ticks() const noexcept -> int {
  return static_cast<int>(impl_->stalled_ticks);
}

auto ColonyModel::planning_pending() const noexcept -> int {
  return static_cast<int>(impl_->replan_queue.pending() +
                          impl_->diversity_replan_queue.pending() +
                          impl_->wide_merge_replan_queue.pending());
}

auto ColonyModel::advanced_last_tick() const noexcept -> int {
  return static_cast<int>(impl_->last_advanced);
}

auto ColonyModel::movement_waits_last_tick() const noexcept -> int {
  return static_cast<int>(impl_->last_movement_waits);
}

auto ColonyModel::tiles() const noexcept -> const std::uint8_t* {
  return impl_->shadow.data();
}

auto ColonyModel::current_agents() const noexcept -> const std::int16_t* {
  return impl_->agent_xy.data();
}

auto ColonyModel::previous_agents() const noexcept -> const std::int16_t* {
  return impl_->previous_agent_xy.data();
}

auto ColonyModel::interpolation_alpha() const noexcept -> double {
  return impl_->interpolation_alpha;
}

}  // namespace tess::examples::web_colony
