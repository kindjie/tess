#include <benchmark/benchmark.h>
#include <tess/tess.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

// M5 scheduler bench family: empty-tick overhead, many-task cadence
// dispatch, the dirty-trigger path, and the auto-exec pipeline per tick.
// Parallel speedups are deliberately trend-only (never gated); every gated
// case here is serial CPU time.
namespace {

void scheduler_bench_check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "tess_bench correctness check failed: %s\n", message);
    std::abort();
  }
}

struct NoOpTask {
  auto operator()(const tess::ScheduleTaskContext&)
      -> tess::ScheduleTaskResult {
    return {};
  }
};

struct DirtyConsumerTask {
  std::uint64_t fires = 0;
  auto operator()(const tess::ScheduleTaskContext&)
      -> tess::ScheduleTaskResult {
    ++fires;
    return {};
  }
};

// A sealed schedule whose tasks are never due: the pure per-tick dispatch
// floor (cadence checks and phase iteration only).
void BM_scheduler_empty_tick(benchmark::State& state) {
  std::vector<NoOpTask> tasks(8);
  tess::Schedule schedule;
  schedule.reserve_tasks(tasks.size());
  for (auto& task : tasks) {
    (void)schedule.add_task(
        {"idle", tess::SimPhase::Background, tess::Cadence::manual()}, task);
  }
  schedule.seal();

  tess::SimClock clock;
  for (auto _ : state) {
    const auto stats = schedule.run_tick(clock);
    benchmark::DoNotOptimize(stats.tasks_run);
  }
  scheduler_bench_check(
      clock.tick == static_cast<std::uint64_t>(state.iterations()),
      "empty tick did not advance the clock");
}

// 100 EveryTick no-op tasks across phases: cadence dispatch throughput.
void BM_scheduler_cadence_dispatch_100(benchmark::State& state) {
  std::vector<NoOpTask> tasks(100);
  tess::Schedule schedule;
  schedule.reserve_tasks(tasks.size());
  for (std::size_t i = 0; i < tasks.size(); ++i) {
    const auto phase = static_cast<tess::SimPhase>(
        i % static_cast<std::size_t>(tess::SimPhase::Count));
    (void)schedule.add_task({"noop", phase, tess::Cadence::every_tick()},
                            tasks[i]);
  }
  schedule.seal();

  tess::SimClock clock;
  std::uint64_t last_run = 0;
  for (auto _ : state) {
    last_run = schedule.run_tick(clock).tasks_run;
    benchmark::DoNotOptimize(last_run);
  }
  scheduler_bench_check(last_run == 100, "cadence dispatch missed tasks");
}

// notify_dirty + one OnDirty fire per tick: the dirty-trigger path.
void BM_scheduler_dirty_trigger(benchmark::State& state) {
  DirtyConsumerTask consumer;
  std::vector<NoOpTask> idle(8);
  tess::Schedule schedule;
  schedule.reserve_tasks(idle.size() + 1);
  for (auto& task : idle) {
    (void)schedule.add_task(
        {"idle", tess::SimPhase::Movement, tess::Cadence::manual()}, task);
  }
  (void)schedule.add_task(
      {"consumer", tess::SimPhase::Topology, tess::Cadence::on_dirty(1u)},
      consumer);
  schedule.seal();

  tess::SimClock clock;
  for (auto _ : state) {
    schedule.notify_dirty(1u);
    const auto stats = schedule.run_tick(clock);
    benchmark::DoNotOptimize(stats.tasks_run);
  }
  scheduler_bench_check(
      consumer.fires == static_cast<std::uint64_t>(state.iterations()),
      "dirty trigger did not fire once per tick");
}

// The auto-exec pipeline per tick over the standard resident-update
// workload (one op covering every chunk), including plan, execute, dirty
// merge, and ack drain.
struct SchedTerrainTag {};
using SchedSchema =
    tess::FieldSchema<tess::Field<SchedTerrainTag, std::uint16_t>>;
using SchedShape =
    tess::Shape<tess::Extent3{256, 256, 1}, tess::Extent3{32, 32, 1}>;
using SchedWorld = tess::AlwaysResidentWorld<SchedShape, SchedSchema>;

struct SchedAck {
  std::uint64_t chunks = 0;
};

struct SchedKernel {
  template <typename View>
  void operator()(View view, SchedAck& ack) {
    auto terrain = view.template field_span<SchedTerrainTag>();
    terrain[0] = static_cast<std::uint16_t>(view.key().value);
    ++ack.chunks;
  }
};

void BM_scheduler_auto_exec_tick(benchmark::State& state) {
  SchedWorld world;
  tess::FrameOps ops;
  tess::AutoExecTask<SchedWorld, tess::WritePolicy::UniquePerChunk, SchedAck,
                     SchedKernel>
      task{world, ops, SchedKernel{}};
  task.reserve_operations(1);
  tess::Schedule schedule;
  (void)schedule.add_task(
      {"ops", tess::SimPhase::PreUpdate, tess::Cadence::every_tick()}, task);
  schedule.seal();

  tess::SimClock clock;
  std::uint64_t executed = 0;
  for (auto _ : state) {
    (void)ops.update_field(tess::DomainDesc::resident_chunks(),
                           tess::FieldAccessDesc{0, 1u, 1u},
                           tess::WritePolicy::UniquePerChunk);
    (void)schedule.run_tick(clock);
    executed = task.last_run().executed_chunks;
    benchmark::DoNotOptimize(executed);
  }
  scheduler_bench_check(executed == SchedWorld::chunk_count,
                        "auto-exec did not visit every resident chunk");
}

BENCHMARK(BM_scheduler_empty_tick)->Name("scheduler/empty_tick");
BENCHMARK(BM_scheduler_cadence_dispatch_100)
    ->Name("scheduler/cadence_dispatch_100");
BENCHMARK(BM_scheduler_dirty_trigger)->Name("scheduler/dirty_trigger");
BENCHMARK(BM_scheduler_auto_exec_tick)->Name("scheduler/auto_exec_tick");

}  // namespace
