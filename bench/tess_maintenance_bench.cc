#include <benchmark/benchmark.h>
#include <tess/experimental/maintenance.h>
#include <tess/storage/world.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <vector>

namespace {

namespace maintenance = tess::experimental::maintenance;

void maintenance_bench_check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "tess_bench correctness check failed: %s\n", message);
    std::abort();
  }
}

struct RebuildTask final : maintenance::MaintenanceTask {
  std::uint64_t executions = 0;

  void run(maintenance::MaintenanceBudget& budget) override {
    if (budget.consume()) {
      ++executions;
      benchmark::DoNotOptimize(executions);
    }
  }
};

struct MaintenanceFieldTag {};
using MaintenanceShape =
    tess::Shape<tess::Extent3{512, 512, 1}, tess::Extent3{32, 32, 1}>;
using MaintenanceSchema =
    tess::FieldSchema<tess::Field<MaintenanceFieldTag, std::uint8_t>>;
using MaintenanceWorld =
    tess::AlwaysResidentWorld<MaintenanceShape, MaintenanceSchema>;

constexpr auto kDerivedDirty = std::uint32_t{1u << 4u};

struct ChunkRebuildTask final : maintenance::MaintenanceTask {
  MaintenanceWorld* world = nullptr;
  tess::ChunkKey key{};
  std::uint32_t derived_version = 0;

  void run(maintenance::MaintenanceBudget& budget) override {
    if (!budget.consume()) {
      return;
    }
    const auto observed = world->observe_dirty(key, kDerivedDirty);
    if (observed.flags == 0) {
      return;
    }
    derived_version = observed.version;
    static_cast<void>(world->clear_dirty_observed(key, observed));
    benchmark::DoNotOptimize(derived_version);
  }
};

struct BudgetedRebuildTask final : maintenance::MaintenanceTask {
  maintenance::MaintenanceScheduler* scheduler = nullptr;
  std::uint64_t remaining = 0;
  std::uint64_t processed = 0;

  void run(maintenance::MaintenanceBudget& budget) override {
    while (remaining != 0 && budget.consume()) {
      --remaining;
      ++processed;
    }
    if (remaining != 0) {
      static_cast<void>(scheduler->schedule(*this));
    }
  }
};

template <typename Scheduler, typename Task>
void register_if_required(Scheduler& scheduler, std::vector<Task>& tasks) {
  if constexpr (std::is_same_v<Scheduler, maintenance::DirtyBitScheduler>) {
    for (auto& task : tasks) {
      maintenance_bench_check(scheduler.register_task(task),
                              "dirty-bit task registration failed");
    }
    scheduler.seal();
  }
}

template <typename Scheduler>
void BM_maintenance_sparse(benchmark::State& state) {
  const auto task_count = static_cast<std::size_t>(state.range(0));
  std::vector<RebuildTask> tasks(task_count);
  Scheduler scheduler(task_count);
  if constexpr (std::is_same_v<Scheduler, maintenance::DirtyBitScheduler>) {
    for (auto& task : tasks) {
      maintenance_bench_check(scheduler.register_task(task),
                              "dirty-bit task registration failed");
    }
    scheduler.seal();
  }
  for (auto _ : state) {
    for (auto& task : tasks) {
      auto scheduled = scheduler.schedule(task);
      benchmark::DoNotOptimize(scheduled);
    }
    auto flushed = scheduler.flush();
    benchmark::DoNotOptimize(flushed);
  }
  state.counters["schedule_calls"] =
      static_cast<double>(scheduler.metrics().schedule_calls);
  state.counters["executions"] =
      static_cast<double>(scheduler.metrics().executions);
  const auto expected = static_cast<std::uint64_t>(state.iterations()) *
                        static_cast<std::uint64_t>(task_count);
  maintenance_bench_check(scheduler.metrics().executions == expected,
                          "sparse maintenance missed an execution");
}

template <typename Scheduler>
void BM_maintenance_dense(benchmark::State& state) {
  const auto schedules = static_cast<std::size_t>(state.range(0));
  RebuildTask task;
  Scheduler scheduler(schedules);
  if constexpr (std::is_same_v<Scheduler, maintenance::DirtyBitScheduler>) {
    maintenance_bench_check(scheduler.register_task(task),
                            "dirty-bit task registration failed");
    scheduler.seal();
  }
  for (auto _ : state) {
    for (std::size_t i = 0; i < schedules; ++i) {
      auto scheduled = scheduler.schedule(task);
      benchmark::DoNotOptimize(scheduled);
    }
    auto flushed = scheduler.flush();
    benchmark::DoNotOptimize(flushed);
  }
  state.counters["schedule_calls"] =
      static_cast<double>(scheduler.metrics().schedule_calls);
  state.counters["executions"] =
      static_cast<double>(scheduler.metrics().executions);
  const auto executions_per_iteration =
      std::is_same_v<Scheduler, maintenance::CoalescingScheduler> ||
              std::is_same_v<Scheduler, maintenance::DirtyBitScheduler>
          ? std::uint64_t{1}
          : static_cast<std::uint64_t>(schedules);
  const auto expected =
      static_cast<std::uint64_t>(state.iterations()) * executions_per_iteration;
  maintenance_bench_check(scheduler.metrics().executions == expected,
                          "dense maintenance execution count is wrong");
}

template <typename Scheduler>
void BM_maintenance_chunk_dirty(benchmark::State& state) {
  const auto active_tasks = static_cast<std::size_t>(state.range(0));
  const auto edits = static_cast<std::size_t>(state.range(1));
  const auto capacity = std::is_same_v<Scheduler, maintenance::FifoScheduler>
                            ? edits
                            : active_tasks;
  MaintenanceWorld world;
  std::vector<ChunkRebuildTask> tasks(active_tasks);
  Scheduler scheduler(capacity);
  for (std::size_t index = 0; index < tasks.size(); ++index) {
    tasks[index].world = &world;
    tasks[index].key = tess::ChunkKey{index};
  }
  register_if_required(scheduler, tasks);

  for (auto _ : state) {
    for (std::size_t edit = 0; edit < edits; ++edit) {
      const auto index = (edit * 17u) % tasks.size();
      const auto chunk_x = static_cast<std::int64_t>(index % 16u);
      const auto chunk_y = static_cast<std::int64_t>(index / 16u);
      const auto offset = static_cast<std::int64_t>(edit % 32u);
      const auto coord =
          tess::Coord3{chunk_x * 32 + offset, chunk_y * 32 + offset, 0};
      world.mark_dirty(tasks[index].key, kDerivedDirty,
                       tess::Box3{coord, tess::Extent3{1, 1, 1}});
      maintenance_bench_check(scheduler.schedule(tasks[index]),
                              "chunk maintenance schedule failed");
    }
    maintenance_bench_check(scheduler.flush(),
                            "chunk maintenance flush failed");
  }

  for (const auto& task : tasks) {
    maintenance_bench_check((world.dirty_flags(task.key) & kDerivedDirty) == 0,
                            "chunk maintenance left dirty state pending");
    maintenance_bench_check(
        task.derived_version == world.meta(task.key).version,
        "chunk derived version is stale");
  }
  state.counters["schedule_calls"] =
      static_cast<double>(scheduler.metrics().schedule_calls);
  state.counters["executions"] =
      static_cast<double>(scheduler.metrics().executions);
}

template <typename Scheduler>
void BM_maintenance_flush_sparse(benchmark::State& state) {
  const auto task_count = static_cast<std::size_t>(state.range(0));
  std::vector<RebuildTask> tasks(task_count);
  Scheduler scheduler(task_count);
  register_if_required(scheduler, tasks);
  for (auto _ : state) {
    state.PauseTiming();
    for (auto& task : tasks) {
      maintenance_bench_check(scheduler.schedule(task),
                              "flush benchmark schedule failed");
    }
    state.ResumeTiming();
    maintenance_bench_check(scheduler.flush(), "flush benchmark drain failed");
  }
}

template <typename Scheduler>
void BM_maintenance_budgeted(benchmark::State& state) {
  const auto task_count = static_cast<std::size_t>(state.range(0));
  const auto work_per_task = static_cast<std::uint64_t>(state.range(1));
  const auto budget_units = static_cast<std::uint64_t>(state.range(2));
  const auto total_work =
      static_cast<std::uint64_t>(task_count) * work_per_task;
  const auto drains = (total_work + budget_units - 1u) / budget_units;
  std::vector<BudgetedRebuildTask> tasks(task_count);
  Scheduler scheduler(task_count);
  for (auto& task : tasks) {
    task.scheduler = &scheduler;
  }
  register_if_required(scheduler, tasks);

  for (auto _ : state) {
    state.PauseTiming();
    for (auto& task : tasks) {
      task.remaining = work_per_task;
      task.processed = 0;
    }
    state.ResumeTiming();
    for (auto& task : tasks) {
      maintenance_bench_check(scheduler.schedule(task),
                              "budgeted maintenance schedule failed");
    }
    for (std::uint64_t drain = 0; drain < drains; ++drain) {
      maintenance_bench_check(
          scheduler.run_some(maintenance::MaintenanceBudget{budget_units}),
          "budgeted maintenance drain stalled");
    }
    maintenance_bench_check(scheduler.flush(),
                            "budgeted maintenance flush failed");
  }

  for (const auto& task : tasks) {
    maintenance_bench_check(
        task.remaining == 0 && task.processed == work_per_task,
        "budgeted maintenance lost work");
  }
}

BENCHMARK_TEMPLATE(BM_maintenance_sparse, maintenance::ImmediateScheduler)
    ->Name("maintenance/sparse/immediate")
    ->Arg(256);
BENCHMARK_TEMPLATE(BM_maintenance_sparse, maintenance::FifoScheduler)
    ->Name("maintenance/sparse/fifo")
    ->Arg(256);
BENCHMARK_TEMPLATE(BM_maintenance_sparse, maintenance::CoalescingScheduler)
    ->Name("maintenance/sparse/coalescing")
    ->Arg(256);
BENCHMARK_TEMPLATE(BM_maintenance_sparse, maintenance::DirtyBitScheduler)
    ->Name("maintenance/sparse/dirty_bit")
    ->Arg(256);
BENCHMARK_TEMPLATE(BM_maintenance_dense, maintenance::ImmediateScheduler)
    ->Name("maintenance/dense/immediate")
    ->Arg(512);
BENCHMARK_TEMPLATE(BM_maintenance_dense, maintenance::FifoScheduler)
    ->Name("maintenance/dense/fifo")
    ->Arg(512);
BENCHMARK_TEMPLATE(BM_maintenance_dense, maintenance::CoalescingScheduler)
    ->Name("maintenance/dense/coalescing")
    ->Arg(512);
BENCHMARK_TEMPLATE(BM_maintenance_dense, maintenance::DirtyBitScheduler)
    ->Name("maintenance/dense/dirty_bit")
    ->Arg(512);

#define TESS_CHUNK_MAINTENANCE_BENCHMARK(backend, type) \
  BENCHMARK_TEMPLATE(BM_maintenance_chunk_dirty, type)  \
      ->Name("maintenance/chunk_dirty/sparse/" backend) \
      ->Args({256, 256});                               \
  BENCHMARK_TEMPLATE(BM_maintenance_chunk_dirty, type)  \
      ->Name("maintenance/chunk_dirty/dense/" backend)  \
      ->Args({1, 512});                                 \
  BENCHMARK_TEMPLATE(BM_maintenance_chunk_dirty, type)  \
      ->Name("maintenance/chunk_dirty/mixed/" backend)  \
      ->Args({64, 4'096})

TESS_CHUNK_MAINTENANCE_BENCHMARK("immediate", maintenance::ImmediateScheduler);
TESS_CHUNK_MAINTENANCE_BENCHMARK("fifo", maintenance::FifoScheduler);
TESS_CHUNK_MAINTENANCE_BENCHMARK("coalescing",
                                 maintenance::CoalescingScheduler);
TESS_CHUNK_MAINTENANCE_BENCHMARK("dirty_bit", maintenance::DirtyBitScheduler);

#undef TESS_CHUNK_MAINTENANCE_BENCHMARK

BENCHMARK_TEMPLATE(BM_maintenance_flush_sparse, maintenance::FifoScheduler)
    ->Name("maintenance/flush_sparse/fifo")
    ->Arg(256);
BENCHMARK_TEMPLATE(BM_maintenance_flush_sparse,
                   maintenance::CoalescingScheduler)
    ->Name("maintenance/flush_sparse/coalescing")
    ->Arg(256);
BENCHMARK_TEMPLATE(BM_maintenance_flush_sparse, maintenance::DirtyBitScheduler)
    ->Name("maintenance/flush_sparse/dirty_bit")
    ->Arg(256);

BENCHMARK_TEMPLATE(BM_maintenance_budgeted, maintenance::FifoScheduler)
    ->Name("maintenance/budgeted/fifo")
    ->Args({256, 10, 64});
BENCHMARK_TEMPLATE(BM_maintenance_budgeted, maintenance::CoalescingScheduler)
    ->Name("maintenance/budgeted/coalescing")
    ->Args({256, 10, 64});
BENCHMARK_TEMPLATE(BM_maintenance_budgeted, maintenance::DirtyBitScheduler)
    ->Name("maintenance/budgeted/dirty_bit")
    ->Args({256, 10, 64});

BENCHMARK_TEMPLATE(BM_maintenance_sparse, maintenance::ImmediateScheduler)
    ->Name("maintenance/sparse_scaling/immediate")
    ->RangeMultiplier(4)
    ->Range(16, 4'096);
BENCHMARK_TEMPLATE(BM_maintenance_sparse, maintenance::CoalescingScheduler)
    ->Name("maintenance/sparse_scaling/coalescing")
    ->RangeMultiplier(4)
    ->Range(16, 4'096);
BENCHMARK_TEMPLATE(BM_maintenance_sparse, maintenance::DirtyBitScheduler)
    ->Name("maintenance/sparse_scaling/dirty_bit")
    ->RangeMultiplier(4)
    ->Range(16, 4'096);

}  // namespace
