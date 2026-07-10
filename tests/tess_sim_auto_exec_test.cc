#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstddef>
#include <cstdint>
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

  static void hook(void* ctx, tess::OpHandle handle,
                   const tess::OpCompletion& completion, const Ack* ack) {
    auto& log = *static_cast<DrainLog*>(ctx);
    log.handles.push_back(handle.value);
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
  EXPECT_TRUE(ops.empty());  // paired clears ended the run

  for (std::uint64_t chunk = 0; chunk < 4; ++chunk) {
    EXPECT_EQ(
        world.chunk(tess::ChunkKey{chunk}).template field_span<TerrainTag>()[0],
        static_cast<std::uint16_t>(chunk + 11));
    EXPECT_NE(
        world.meta(tess::ChunkKey{chunk}).field_dirty_flags & DirtyTerrain, 0u);
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
    EXPECT_EQ(auto_world.meta(key).field_dirty_flags,
              manual_world.meta(key).field_dirty_flags);
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
  for (std::uint64_t chunk = 0; chunk < World::chunk_count; ++chunk) {
    const auto key = tess::ChunkKey{chunk};
    EXPECT_EQ(serial_world.chunk(key).template field_span<TerrainTag>()[0],
              pool_world.chunk(key).template field_span<TerrainTag>()[0]);
    EXPECT_EQ(serial_world.meta(key).version, pool_world.meta(key).version);
    EXPECT_EQ(serial_world.meta(key).field_dirty_flags,
              pool_world.meta(key).field_dirty_flags);
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
  // Phase 1 (ops 0 and 2) merged its dirty before phase 2 re-prepared the
  // scratch: chunk 1's masked write survives.
  EXPECT_NE(world.meta(tess::ChunkKey{1}).field_dirty_flags & DirtyCost, 0u);
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

}  // namespace
