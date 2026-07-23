#pragma once

#include <tess/ops/async_work.h>
#include <tess/sim/schedule.h>

namespace tess {

/// Adapts a cooperative result queue to a scheduler Background task.
template <typename T>
class ResumableWorkTask {
 public:
  explicit ResumableWorkTask(ResumableWorkQueue<T>& queue) noexcept
      : queue_(&queue) {}

  [[nodiscard]] auto operator()(const ScheduleTaskContext& context)
      -> ScheduleTaskResult {
    const auto stats = queue_->advance(AsyncWorkBudget{context.budget_items});
    return ScheduleTaskResult{0, stats.items_done, stats.pending != 0, 0};
  }

 private:
  ResumableWorkQueue<T>* queue_ = nullptr;
};

}  // namespace tess
