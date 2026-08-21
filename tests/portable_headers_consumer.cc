#include <tess/maintenance/scheduler.h>
#include <tess/tess.h>

#include <cstddef>
#include <string_view>

struct MaintenanceProbe final : tess::maintenance::MaintenanceTask {
  std::size_t runs = 0;

  void run(tess::maintenance::MaintenanceBudget& budget) override {
    if (budget.consume()) {
      ++runs;
    }
  }
};

int main() {
  static_assert(tess::library_version.major == TESS_VERSION_MAJOR);
  static_assert(tess::library_version.minor == TESS_VERSION_MINOR);
  static_assert(tess::library_version.patch == TESS_VERSION_PATCH);

  MaintenanceProbe task;
  tess::maintenance::RegisteredScheduler<tess::maintenance::ImmediateScheduler>
      scheduler{1};
  const auto handle = scheduler.register_task(task);
  if (!handle.has_value()) {
    return 1;
  }
  scheduler.seal();
  return scheduler.schedule(*handle) ==
                     tess::maintenance::ScheduleResult::Accepted &&
                 scheduler.flush() == tess::maintenance::DrainResult::Idle &&
                 scheduler.try_release(*handle) ==
                     tess::maintenance::ReleaseResult::Released &&
                 task.runs == 1 &&
                 !std::string_view{TESS_VERSION_STRING}.empty()
             ? 0
             : 1;
}
