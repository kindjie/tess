#include <gtest/gtest.h>
#include <tess/tess.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

// S7.3/S7.4: the auto-exec schedule task. Behavior (plan -> phases ->
// execute -> per-phase dirty apply -> drain -> paired clears), the
// auto-exec == manual pipeline golden, the serial == pool golden, dirty
// propagation into OnDirty tasks, per-phase dirty merging across
// write-then-read phase splits, and the policy-mismatch guard.
namespace {

struct TerrainTag {};
struct CostTag {};

constexpr std::uint32_t DirtyTerrain = 1u << 0u;
constexpr std::uint32_t DirtyCost = 1u << 1u;

using TopDown2D =
    tess::Shape<tess::Extent3{64, 32, 1}, tess::Extent3{32, 16, 1}>;
using Schema = tess::FieldSchema<tess::Field<TerrainTag, std::uint16_t>,
                                 tess::Field<CostTag, float>>;
using World = tess::AlwaysResidentWorld<TopDown2D, Schema>;

struct Ack {
  std::size_t chunks = 0;
};

// The per-chunk kernel every variant runs: a deterministic terrain stamp.
struct StampKernel {
  template <typename View>
  void operator()(View view, Ack& ack) {
    auto terrain = view.template field_span<TerrainTag>();
    terrain[0] = static_cast<std::uint16_t>(view.key().value + 11);
    ++ack.chunks;
  }
};

using StampTask = tess::AutoExecTask<World, tess::WritePolicy::UniquePerChunk,
                                     Ack, StampKernel>;
static_assert(
    std::is_nothrow_invocable_v<StampTask::ResultHook, void*, tess::OpHandle,
                                const tess::OpCompletion&, const Ack*>);

void enqueue_stamps(tess::FrameOps& ops, std::size_t count) {
  for (std::uint64_t chunk = 0; chunk < count; ++chunk) {
    (void)ops.update_field(
        tess::DomainDesc::explicit_chunks(
            std::vector<tess::ChunkKey>{tess::ChunkKey{chunk}}),
        tess::FieldAccessDesc{0, DirtyTerrain, DirtyTerrain},
        tess::WritePolicy::UniquePerChunk);
  }
}

struct DrainLog {
  std::vector<std::uint64_t> handles;
  std::size_t chunks = 0;
  std::size_t failed = 0;
  bool storage_failed = false;

  static void hook(void* ctx, tess::OpHandle handle,
                   const tess::OpCompletion& completion,
                   const Ack* ack) noexcept {
    auto& log = *static_cast<DrainLog*>(ctx);
    try {
      log.handles.push_back(handle.value);
    } catch (...) {
      log.storage_failed = true;
      return;
    }
    if (completion.ok() && ack != nullptr) {
      log.chunks += ack->chunks;
    } else {
      ++log.failed;
    }
  }
};

TEST(TessAutoExec, RunsThePipelineAndFeedsOnDirtyTasks) {
  World world;
  tess::FrameOps ops;
  StampTask task{world, ops, StampKernel{}};
  task.reserve_operations(4);
  DrainLog drained;
  task.set_result_hook(&drained, DrainLog::hook);

  // A later-phase OnDirty task observing the auto-exec's produced mask.
  struct Probe {
    std::size_t fires = 0;
    auto operator()(const tess::ScheduleTaskContext&)
        -> tess::ScheduleTaskResult {
      ++fires;
      return {};
    }
  } probe;

  tess::Schedule schedule;
  (void)schedule.add_task(
      {"ops", tess::SimPhase::PreUpdate, tess::Cadence::every_tick()}, task);
  (void)schedule.add_task({"topology", tess::SimPhase::Topology,
                           tess::Cadence::on_dirty(DirtyTerrain)},
                          probe);
  schedule.seal();

  tess::SimClock clock;
  enqueue_stamps(ops, 4);
  const auto stats = schedule.run_tick(clock);
  EXPECT_EQ(stats.dirty_mask_produced, DirtyTerrain);
  EXPECT_EQ(probe.fires, 1u);  // same tick, later phase

  EXPECT_EQ(task.last_run().status, tess::AutoExecStatus::Executed);
  EXPECT_EQ(task.last_run().planned_ops, 4u);
  EXPECT_EQ(task.last_run().executed_chunks, 4u);
  EXPECT_EQ(task.last_run().merged_dirty_chunks, 4u);
  ASSERT_EQ(drained.handles.size(), 4u);
  EXPECT_EQ(drained.chunks, 4u);
  EXPECT_EQ(drained.failed, 0u);
  EXPECT_FALSE(drained.storage_failed);
  EXPECT_TRUE(ops.empty());  // paired clears ended the run

  for (std::uint64_t chunk = 0; chunk < 4; ++chunk) {
    EXPECT_EQ(
        world.chunk(tess::ChunkKey{chunk}).template field_span<TerrainTag>()[0],
        static_cast<std::uint16_t>(chunk + 11));
    EXPECT_NE(world.dirty_flags(tess::ChunkKey{chunk}) & DirtyTerrain, 0u);
  }

  // An idle tick is a no-op that produces no dirty.
  const auto idle = schedule.run_tick(clock);
  EXPECT_EQ(idle.dirty_mask_produced, 0u);
  EXPECT_EQ(task.last_run().status, tess::AutoExecStatus::Idle);
  EXPECT_EQ(probe.fires, 1u);
}

// Golden: the auto-exec pipeline leaves the world (fields + chunk meta)
// byte-identical to the hand-rolled plan/execute/merge pipeline.
TEST(TessAutoExec, MatchesTheManualPipelineGolden) {
  World auto_world;
  World manual_world;

  // Auto side, through a schedule tick.
  tess::FrameOps auto_ops;
  StampTask task{auto_world, auto_ops, StampKernel{}};
  tess::Schedule schedule;
  (void)schedule.add_task(
      {"ops", tess::SimPhase::PreUpdate, tess::Cadence::every_tick()}, task);
  schedule.seal();
  tess::SimClock clock;
  enqueue_stamps(auto_ops, 4);
  (void)schedule.run_tick(clock);

  // Manual side, the pre-existing pipeline.
  tess::FrameOps manual_ops;
  enqueue_stamps(manual_ops, 4);
  const auto report = tess::plan_operations(manual_world, manual_ops);
  ASSERT_TRUE(report.ok());
  const auto phases = tess::plan_parallel_execution_phases(report.plan());
  ASSERT_TRUE(phases.ok());
  tess::PlannedPhaseExecutionScratch scratch;
  const tess::SerialPhaseExecutor serial;
  Ack sink{};
  for (const auto phase : phases.phases()) {
    (void)tess::execute_phase_partitioned_dirty_with<
        tess::WritePolicy::UniquePerChunk>(
        serial, manual_world, report.plan(), phase, scratch,
        [&](auto view) { StampKernel{}(view, sink); });
    (void)tess::merge_planned_dirty(manual_world, scratch);
  }

  for (std::uint64_t chunk = 0; chunk < World::chunk_count; ++chunk) {
    const auto key = tess::ChunkKey{chunk};
    const auto auto_span =
        auto_world.chunk(key).template field_span<TerrainTag>();
    const auto manual_span =
        manual_world.chunk(key).template field_span<TerrainTag>();
    for (std::size_t i = 0; i < auto_span.size(); ++i) {
      ASSERT_EQ(auto_span[i], manual_span[i]) << chunk << ":" << i;
    }
    EXPECT_EQ(auto_world.meta(key).version, manual_world.meta(key).version);
    EXPECT_EQ(auto_world.dirty_flags(key), manual_world.dirty_flags(key));
  }
}

// Golden: serial and pool execution deliver identical worlds and identical
// drained ack sequences (pre-validation makes aborts unreachable, so the
// executors can never diverge).
TEST(TessAutoExec, SerialAndPoolRunsAreIdentical) {
  World serial_world;
  World pool_world;

  tess::FrameOps serial_ops;
  StampTask serial_task{serial_world, serial_ops, StampKernel{}};
  DrainLog serial_drained;
  serial_task.set_result_hook(&serial_drained, DrainLog::hook);

  tess::FrameOps pool_ops;
  StampTask pool_task{pool_world, pool_ops, StampKernel{}};
  DrainLog pool_drained;
  pool_task.set_result_hook(&pool_drained, DrainLog::hook);
  tess::WorkerPoolPhaseExecutor pool{2};
  pool_task.use_pool(pool, 1);

  tess::Schedule serial_schedule;
  (void)serial_schedule.add_task(
      {"ops", tess::SimPhase::PreUpdate, tess::Cadence::every_tick()},
      serial_task);
  serial_schedule.seal();
  tess::Schedule pool_schedule;
  (void)pool_schedule.add_task(
      {"ops", tess::SimPhase::PreUpdate, tess::Cadence::every_tick()},
      pool_task);
  pool_schedule.seal();

  tess::SimClock serial_clock;
  tess::SimClock pool_clock;
  for (int tick = 0; tick < 3; ++tick) {
    enqueue_stamps(serial_ops, World::chunk_count);
    enqueue_stamps(pool_ops, World::chunk_count);
    (void)serial_schedule.run_tick(serial_clock);
    (void)pool_schedule.run_tick(pool_clock);
  }
  EXPECT_GE(pool_task.last_run().pool_phases, 1u);

  ASSERT_EQ(serial_drained.handles, pool_drained.handles);
  EXPECT_EQ(serial_drained.chunks, pool_drained.chunks);
  EXPECT_FALSE(serial_drained.storage_failed);
  EXPECT_FALSE(pool_drained.storage_failed);
  for (std::uint64_t chunk = 0; chunk < World::chunk_count; ++chunk) {
    const auto key = tess::ChunkKey{chunk};
    EXPECT_EQ(serial_world.chunk(key).template field_span<TerrainTag>()[0],
              pool_world.chunk(key).template field_span<TerrainTag>()[0]);
    EXPECT_EQ(serial_world.meta(key).version, pool_world.meta(key).version);
    EXPECT_EQ(serial_world.dirty_flags(key), pool_world.dirty_flags(key));
  }
}

// Per-phase dirty merging: a write-then-read hazard splits the plan into
// two phases; BOTH phases' dirty records must reach the chunk meta (a
// single post-loop merge would drop the first phase's).
TEST(TessAutoExec, PerPhaseMergeKeepsEveryPhasesDirty) {
  World world;
  tess::FrameOps ops;
  StampTask task{world, ops, StampKernel{}};
  tess::Schedule schedule;
  (void)schedule.add_task(
      {"ops", tess::SimPhase::PreUpdate, tess::Cadence::every_tick()}, task);
  schedule.seal();

  // Op 0 is a maskless mutator on chunk 0 and op 1 a same-chunk reader:
  // phase planning splits them. Op 2 (a masked writer on chunk 1) shares
  // phase 1 with op 0 -- if only the LAST phase's dirty were merged, chunk
  // 1's dirty flags and version bump would be silently dropped.
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(
                             std::vector<tess::ChunkKey>{tess::ChunkKey{0}}),
                         tess::WritePolicy::UniquePerChunk);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(
                             std::vector<tess::ChunkKey>{tess::ChunkKey{0}}),
                         tess::FieldAccessDesc{DirtyTerrain, 0, 0},
                         tess::WritePolicy::UniquePerChunk);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(
                             std::vector<tess::ChunkKey>{tess::ChunkKey{1}}),
                         tess::FieldAccessDesc{0, DirtyCost, DirtyCost},
                         tess::WritePolicy::UniquePerChunk);

  tess::SimClock clock;
  (void)schedule.run_tick(clock);
  ASSERT_EQ(task.last_run().status, tess::AutoExecStatus::Executed);
  ASSERT_EQ(task.last_run().phases, 2u);
  EXPECT_EQ(task.last_run().merged_dirty_chunks, 1u);
  // Phase 1 (ops 0 and 2) merged its dirty before phase 2 re-prepared the
  // scratch: chunk 1's masked write survives.
  EXPECT_NE(world.dirty_flags(tess::ChunkKey{1}) & DirtyCost, 0u);
  EXPECT_GT(world.meta(tess::ChunkKey{1}).version, 0u);
}

#if TESS_ENABLE_ASSERTS
TEST(TessAutoExecDeathTest, MixedPolicyQueueAsserts) {
  World world;
  tess::FrameOps ops;
  StampTask task{world, ops, StampKernel{}};
  tess::Schedule schedule;
  (void)schedule.add_task(
      {"ops", tess::SimPhase::PreUpdate, tess::Cadence::every_tick()}, task);
  schedule.seal();

  (void)ops.update_field(tess::DomainDesc::explicit_chunks(
                             std::vector<tess::ChunkKey>{tess::ChunkKey{0}}),
                         tess::FieldAccessDesc{0, DirtyTerrain, DirtyTerrain},
                         tess::WritePolicy::UniquePerTile);
  tess::SimClock clock;
  EXPECT_DEATH((void)schedule.run_tick(clock), "mismatched write policy");
}
#endif

// Codex review: a result hook may enqueue follow-up work into the same
// queue; the end-of-run clear must not discard it (the queue clears before
// the drain, so follow-ups land in the fresh queue for the next tick).
struct FollowUpHook {
  tess::FrameOps* ops = nullptr;
  std::size_t drained = 0;
  bool enqueue_follow_up = false;
  bool enqueue_failed = false;

  static void hook(void* ctx, tess::OpHandle, const tess::OpCompletion&,
                   const Ack*) noexcept {
    auto& self = *static_cast<FollowUpHook*>(ctx);
    ++self.drained;
    if (self.enqueue_follow_up) {
      self.enqueue_follow_up = false;
      try {
        (void)self.ops->update_field(
            tess::DomainDesc::explicit_chunks(
                std::vector<tess::ChunkKey>{tess::ChunkKey{2}}),
            tess::FieldAccessDesc{0, DirtyTerrain, DirtyTerrain},
            tess::WritePolicy::UniquePerChunk);
      } catch (...) {
        self.enqueue_failed = true;
      }
    }
  }
};

TEST(TessAutoExec, HookEnqueuedFollowUpsSurviveIntoTheNextRun) {
  World world;
  tess::FrameOps ops;
  StampTask task{world, ops, StampKernel{}};
  FollowUpHook follow_up{&ops, 0, true, false};
  task.set_result_hook(&follow_up, FollowUpHook::hook);
  tess::Schedule schedule;
  (void)schedule.add_task(
      {"ops", tess::SimPhase::PreUpdate, tess::Cadence::every_tick()}, task);
  schedule.seal();

  tess::SimClock clock;
  enqueue_stamps(ops, 1);
  (void)schedule.run_tick(clock);
  EXPECT_EQ(follow_up.drained, 1u);
  EXPECT_FALSE(follow_up.enqueue_failed);
  EXPECT_FALSE(ops.empty());  // the follow-up survived the run's clear

  (void)schedule.run_tick(clock);
  EXPECT_EQ(follow_up.drained, 2u);  // the follow-up executed and drained
  EXPECT_EQ(task.last_run().executed_chunks, 1u);
  EXPECT_EQ(world.chunk(tess::ChunkKey{2}).template field_span<TerrainTag>()[0],
            static_cast<std::uint16_t>(2 + 11));
}

struct ThrowOnThirdKernel {
  std::size_t calls = 0;
  bool armed = true;

  template <typename View>
  void operator()(View view, Ack& ack) {
    ++calls;
    if (armed && calls == 3) {
      armed = false;
      StampKernel{}(view, ack);
      throw std::runtime_error{"test kernel failure"};
    }
    StampKernel{}(view, ack);
  }
};

struct PooledThrowState {
  std::atomic_size_t completed = 0;
  std::atomic_uint32_t completed_mask = 0;
};

struct ThrowOnChunkKernel {
  PooledThrowState* state = nullptr;

  template <typename View>
  void operator()(View view, Ack& ack) {
    if (view.key() == tess::ChunkKey{2}) {
      const auto deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds{5};
      while (state->completed.load(std::memory_order_acquire) == 0) {
        if (std::chrono::steady_clock::now() >= deadline) {
          throw std::runtime_error{"pooled sibling rendezvous timed out"};
        }
        std::this_thread::yield();
      }
      throw std::runtime_error{"pooled test kernel failure"};
    }
    StampKernel{}(view, ack);
    state->completed_mask.fetch_or(
        std::uint32_t{1} << static_cast<std::uint32_t>(view.key().value),
        std::memory_order_relaxed);
    state->completed.fetch_add(1, std::memory_order_release);
  }
};

struct ThrowingReadOnlyState {
  std::atomic_size_t entered = 0;
};

struct ThrowingOverlappingReadOnlyKernel {
  ThrowingReadOnlyState* state = nullptr;

  template <typename View>
  void operator()(View /*view*/, Ack& /*ack*/) {
    const auto ordinal = state->entered.fetch_add(1, std::memory_order_acq_rel);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (state->entered.load(std::memory_order_acquire) < 2) {
      if (std::chrono::steady_clock::now() >= deadline) {
        throw std::runtime_error{"read-only rendezvous timed out"};
      }
      std::this_thread::yield();
    }
    if (ordinal == 0) {
      throw std::runtime_error{"read-only kernel failure"};
    }
  }
};

TEST(TessAutoExec, ThrowingKernelCannotLeakOldCompletionsIntoNextRun) {
  using ThrowTask = tess::AutoExecTask<World, tess::WritePolicy::UniquePerChunk,
                                       Ack, ThrowOnThirdKernel>;
  World world;
  tess::FrameOps ops;
  ThrowTask task{world, ops, ThrowOnThirdKernel{}};
  DrainLog drained;
  task.set_result_hook(&drained, DrainLog::hook);
  tess::Schedule schedule;
  (void)schedule.add_task(
      {"ops", tess::SimPhase::PreUpdate, tess::Cadence::every_tick()}, task);
  schedule.seal();

  // A same-chunk second mutation starts phase 2. Its following disjoint write
  // shares that phase and throws, leaving the first two channel slots ready.
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(
                             std::vector<tess::ChunkKey>{tess::ChunkKey{0}}),
                         tess::WritePolicy::UniquePerChunk);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(
                             std::vector<tess::ChunkKey>{tess::ChunkKey{0}}),
                         tess::FieldAccessDesc{DirtyTerrain, 0, 0},
                         tess::WritePolicy::UniquePerChunk);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(
                             std::vector<tess::ChunkKey>{tess::ChunkKey{1}}),
                         tess::FieldAccessDesc{0, DirtyTerrain, DirtyTerrain},
                         tess::WritePolicy::UniquePerChunk);
  tess::SimClock clock;
  EXPECT_THROW((void)schedule.run_tick(clock), std::runtime_error);
  EXPECT_TRUE(drained.handles.empty());
  EXPECT_EQ(world.dirty_flags(tess::ChunkKey{1}), DirtyTerrain);
  EXPECT_EQ(world.meta(tess::ChunkKey{1}).version, 1u);
  EXPECT_EQ(world.chunk(tess::ChunkKey{1}).template field_span<TerrainTag>()[0],
            12u);
  EXPECT_EQ(world.dirty_bounds(tess::ChunkKey{1}),
            (tess::Box3{tess::Coord3{32, 0, 0}, tess::Extent3{32, 16, 1}}));

  // Replace the failed frame with one operation. Without the exception guard,
  // the second ready slot from the old frame would drain as a ghost result.
  ops.clear();
  enqueue_stamps(ops, 1);
  EXPECT_NO_THROW((void)schedule.run_tick(clock));
  ASSERT_EQ(drained.handles.size(), 1u);
  EXPECT_EQ(drained.handles.front(), 0u);
  EXPECT_EQ(drained.chunks, 1u);
  EXPECT_FALSE(drained.storage_failed);
}

TEST(TessAutoExec, PooledKernelExceptionIsRethrownAndClearsCompletions) {
  using ThrowTask = tess::AutoExecTask<World, tess::WritePolicy::UniquePerChunk,
                                       Ack, ThrowOnChunkKernel>;
  World world;
  tess::FrameOps ops;
  PooledThrowState throw_state;
  ThrowTask task{world, ops, ThrowOnChunkKernel{&throw_state}};
  DrainLog drained;
  task.set_result_hook(&drained, DrainLog::hook);
  tess::WorkerPoolPhaseExecutor pool{2};
  task.use_pool(pool, 1);
  tess::Schedule schedule;
  (void)schedule.add_task(
      {"ops", tess::SimPhase::PreUpdate, tess::Cadence::every_tick()}, task);
  schedule.seal();

  tess::SimClock clock;
  enqueue_stamps(ops, 4);
  EXPECT_THROW((void)schedule.run_tick(clock), std::runtime_error);
  EXPECT_TRUE(drained.handles.empty());
  EXPECT_FALSE(ops.empty());
  EXPECT_NE(throw_state.completed_mask.load(std::memory_order_relaxed), 0u);
  EXPECT_EQ(world.dirty_flags(tess::ChunkKey{2}), DirtyTerrain);
  EXPECT_EQ(world.meta(tess::ChunkKey{2}).version, 1u);
  EXPECT_EQ(world.dirty_bounds(tess::ChunkKey{2}),
            (tess::Box3{tess::Coord3{0, 16, 0}, tess::Extent3{32, 16, 1}}));
  for (std::uint64_t key = 0; key < 4; ++key) {
    const auto mask = std::uint32_t{1} << static_cast<std::uint32_t>(key);
    if ((throw_state.completed_mask.load(std::memory_order_relaxed) & mask) !=
        0) {
      EXPECT_EQ(world.dirty_flags(tess::ChunkKey{key}), DirtyTerrain);
      EXPECT_EQ(world.meta(tess::ChunkKey{key}).version, 1u);
    }
  }

  ops.clear();
  enqueue_stamps(ops, 1);
  EXPECT_NO_THROW((void)schedule.run_tick(clock));
  ASSERT_EQ(drained.handles.size(), 1u);
  EXPECT_EQ(drained.handles.front(), 0u);
  EXPECT_EQ(drained.chunks, 1u);
  EXPECT_FALSE(drained.storage_failed);
}

TEST(TessAutoExec, ExceptionMergeCoalescesOverlappingReadOnlyDirtyRecords) {
  using ThrowTask = tess::AutoExecTask<World, tess::WritePolicy::ReadOnly, Ack,
                                       ThrowingOverlappingReadOnlyKernel>;
  World world;
  tess::FrameOps ops;
  ThrowingReadOnlyState throw_state;
  ThrowTask task{world, ops, ThrowingOverlappingReadOnlyKernel{&throw_state}};
  tess::WorkerPoolPhaseExecutor pool{2};
  task.use_pool(pool, 1);
  tess::Schedule schedule;
  (void)schedule.add_task(
      {"ops", tess::SimPhase::PreUpdate, tess::Cadence::every_tick()}, task);
  schedule.seal();

  const auto key = tess::ChunkKey{0};
  const auto keys = std::vector{key};
  constexpr auto marks_terrain = tess::FieldAccessDesc{0, 0, DirtyTerrain};
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(keys), marks_terrain,
                         tess::WritePolicy::ReadOnly);
  (void)ops.update_field(tess::DomainDesc::explicit_chunks(keys), marks_terrain,
                         tess::WritePolicy::ReadOnly);
  tess::SimClock clock;

  EXPECT_THROW((void)schedule.run_tick(clock), std::runtime_error);
  EXPECT_EQ(throw_state.entered.load(std::memory_order_relaxed), 2u);
  EXPECT_EQ(world.dirty_flags(key), DirtyTerrain);
  EXPECT_EQ(world.meta(key).version, 1u);
  EXPECT_EQ(world.dirty_bounds(key),
            (tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{32, 16, 1}}));
}

}  // namespace
