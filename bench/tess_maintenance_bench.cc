#include <benchmark/benchmark.h>
#include <tess/tess.h>

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

template <typename Scheduler>
void BM_maintenance_sparse(benchmark::State& state) {
  const auto task_count = static_cast<std::size_t>(state.range(0));
  std::vector<RebuildTask> tasks(task_count);
  Scheduler scheduler(task_count);
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
      std::is_same_v<Scheduler, maintenance::CoalescingScheduler>
          ? std::uint64_t{1}
          : static_cast<std::uint64_t>(schedules);
  const auto expected =
      static_cast<std::uint64_t>(state.iterations()) * executions_per_iteration;
  maintenance_bench_check(scheduler.metrics().executions == expected,
                          "dense maintenance execution count is wrong");
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
BENCHMARK_TEMPLATE(BM_maintenance_dense, maintenance::ImmediateScheduler)
    ->Name("maintenance/dense/immediate")
    ->Arg(512);
BENCHMARK_TEMPLATE(BM_maintenance_dense, maintenance::FifoScheduler)
    ->Name("maintenance/dense/fifo")
    ->Arg(512);
BENCHMARK_TEMPLATE(BM_maintenance_dense, maintenance::CoalescingScheduler)
    ->Name("maintenance/dense/coalescing")
    ->Arg(512);

}  // namespace
