#include <tess/maintenance/scheduler.h>
#include <tess/pathfinding.h>
#include <tess/simulation.h>
#include <tess/tess.h>

#include <cstddef>

using Shape = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{4, 4, 1}>;

struct MaintenanceProbe final : tess::maintenance::MaintenanceTask {
  std::size_t runs = 0;

  void run(tess::maintenance::MaintenanceBudget& budget) override {
    if (budget.consume()) {
      ++runs;
    }
  }
};

int main() {
#ifdef TESS_EXPECT_NO_EXCEPTIONS
  static_assert(TESS_HAS_EXCEPTIONS == 0);
  static_assert(!tess::has_exceptions);
#endif
  static_assert(Shape::size == tess::Extent3{8, 8, 1});
  MaintenanceProbe task;
  tess::maintenance::RegisteredScheduler<tess::maintenance::ImmediateScheduler>
      scheduler{1};
  const auto handle = scheduler.register_task(task);
  if (!handle.has_value()) {
    return 1;
  }
  scheduler.seal();
  if (scheduler.schedule(*handle) !=
          tess::maintenance::ScheduleResult::Accepted ||
      scheduler.flush() != tess::maintenance::DrainResult::Idle ||
      scheduler.try_release(*handle) !=
          tess::maintenance::ReleaseResult::Released) {
    return 1;
  }
  return task.runs == 1 ? 0 : 1;
}
