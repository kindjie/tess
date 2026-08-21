// Focused downstream tryout of the stable maintenance contract against the
// installed package: task registration and opaque handles, sparse residency,
// budgeted draining through a consumer-defined structural backend, explicit
// flush, and checked shutdown. Only `tess::maintenance` spellings appear
// here. Self-checking: returns nonzero on any failed contract.

#include <tess/maintenance.h>
#include <tess/storage/sparse_world.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

namespace maintenance = tess::maintenance;

struct ValueTag {};
using Schema = tess::FieldSchema<tess::Field<ValueTag, std::uint16_t>>;
using Shape = tess::Shape<tess::Extent3{12, 4, 1}, tess::Extent3{4, 4, 1}>;
using World = tess::SparseResidentWorld<Shape, Schema>;

struct Summary {
  std::uint32_t sum = 0;
};

struct RebuildSummary {
  void operator()(const World& world, tess::ChunkKey key,
                  tess::DirtyObservation, Summary& summary) const {
    summary = {};
    for (const auto value : world.field_span<ValueTag>(key)) {
      summary.sum += value;
    }
  }
};

// Deferred bounded FIFO over the stable structural backend boundary. It
// exists so an explicit budgeted drain provably runs work admitted earlier;
// this consumer drives it from one thread only.
class DeferredBackend {
 public:
  explicit DeferredBackend(std::size_t capacity) : queue_(capacity) {}

  [[nodiscard]] auto schedule(maintenance::MaintenanceTask& task)
      -> maintenance::ScheduleResult {
    ++metrics_.schedule_calls;
    if (size_ == queue_.size()) {
      ++metrics_.capacity_failures;
      return maintenance::ScheduleResult::CapacityExhausted;
    }
    queue_[(head_ + size_) % queue_.size()] = &task;
    ++size_;
    return maintenance::ScheduleResult::Accepted;
  }

  [[nodiscard]] auto run_some(maintenance::MaintenanceBudget budget)
      -> maintenance::BackendDrainResult {
    while (budget.remaining() != 0 && size_ != 0) {
      auto* task = queue_[head_];
      queue_[head_] = nullptr;
      head_ = (head_ + 1) % queue_.size();
      --size_;
      ++metrics_.executions;
      task->run(budget);
    }
    return maintenance::BackendDrainResult::Completed;
  }

  [[nodiscard]] auto flush() -> maintenance::BackendDrainResult {
    return run_some(maintenance::MaintenanceBudget{});
  }

  [[nodiscard]] auto metrics() const noexcept
      -> maintenance::MaintenanceMetrics {
    return metrics_;
  }

  [[nodiscard]] auto has_pending() const noexcept -> bool { return size_ != 0; }

 private:
  std::vector<maintenance::MaintenanceTask*> queue_;
  std::size_t head_ = 0;
  std::size_t size_ = 0;
  maintenance::MaintenanceMetrics metrics_{};
};

static_assert(maintenance::MaintenanceBackend<DeferredBackend>);

struct UnitTask final : maintenance::MaintenanceTask {
  std::uint32_t runs = 0;

  void run(maintenance::MaintenanceBudget& budget) override {
    if (budget.consume()) {
      ++runs;
    }
  }
};

constexpr auto kOwned = tess::DirtyMask{1};

[[nodiscard]] auto one_tile_box(std::uint64_t chunk) -> tess::Box3 {
  return tess::Box3{tess::Coord3{static_cast<std::int64_t>(chunk * 4u), 0, 0},
                    tess::Extent3{1, 1, 1}};
}

// Registration, opaque handles, budgeted drain, flush, and checked release
// through the registered facade over the consumer backend.
[[nodiscard]] auto registered_scheduler_workflow() -> bool {
  UnitTask first;
  UnitTask second;
  maintenance::RegisteredScheduler<DeferredBackend> scheduler{2};
  const auto first_handle = scheduler.register_task(first);
  const auto second_handle = scheduler.register_task(second);
  if (!first_handle.has_value() || !second_handle.has_value()) {
    return false;
  }
  scheduler.seal();
  if (scheduler.schedule(*first_handle) !=
          maintenance::ScheduleResult::Accepted ||
      scheduler.schedule(*second_handle) !=
          maintenance::ScheduleResult::Accepted) {
    return false;
  }
  // One budget unit runs exactly the first admitted task and reports the
  // still-pending second one.
  if (scheduler.run_some(maintenance::MaintenanceBudget{1}) !=
          maintenance::DrainResult::BudgetExhausted ||
      first.runs != 1 || second.runs != 0) {
    return false;
  }
  if (scheduler.flush() != maintenance::DrainResult::Drained ||
      scheduler.flush() != maintenance::DrainResult::Idle || second.runs != 1) {
    return false;
  }
  // Shutdown retires each registration once; a retired handle is invalid.
  return scheduler.try_release(*first_handle) ==
             maintenance::ReleaseResult::Released &&
         scheduler.try_release(*second_handle) ==
             maintenance::ReleaseResult::Released &&
         scheduler.try_release(*first_handle) ==
             maintenance::ReleaseResult::InvalidHandle;
}

// Sparse residency, deferred budgeted rebuilds, flush-to-idle, eviction, and
// adapter shutdown against the installed stable adapter.
[[nodiscard]] auto sparse_adapter_workflow() -> bool {
  World world{tess::ResidencyConfig{2 * World::page_byte_size}};
  maintenance::ChunkMaintenanceAdapter<World, Summary, RebuildSummary,
                                       DeferredBackend>
      adapter{world, kOwned, RebuildSummary{}};
  if (adapter.flush() != maintenance::DrainResult::Idle) {
    return false;
  }
  constexpr auto keys = std::array{tess::ChunkKey{0}, tess::ChunkKey{2}};
  for (const auto key : keys) {
    const auto resident = adapter.ensure_resident(key);
    if (resident.status != maintenance::ChunkResidencyStatus::Ready ||
        !resident.handle.generation.valid()) {
      return false;
    }
  }
  for (const auto key : keys) {
    world.field<ValueTag>(
        tess::Coord3{static_cast<std::int64_t>(key.value * 4u), 0}) =
        static_cast<std::uint16_t>(key.value + 40u);
    if (adapter.mark_dirty(key, kOwned, one_tile_box(key.value)) !=
        maintenance::ChunkMarkResult::Accepted) {
      return false;
    }
  }
  // One budget unit rebuilds only the first marked chunk; the second rebuild
  // stays pending until the explicit flush.
  if (adapter.run_some(maintenance::MaintenanceBudget{1}) !=
          maintenance::DrainResult::BudgetExhausted ||
      adapter.product(tess::ChunkKey{0}).state !=
          maintenance::ChunkProductState::Current ||
      adapter.product(tess::ChunkKey{2}).state ==
          maintenance::ChunkProductState::Current) {
    return false;
  }
  if (adapter.flush() != maintenance::DrainResult::Drained ||
      adapter.flush() != maintenance::DrainResult::Idle) {
    return false;
  }
  for (const auto key : keys) {
    const auto product = adapter.product(key);
    if (product.state != maintenance::ChunkProductState::Current ||
        product.value == nullptr || product.value->sum != key.value + 40u ||
        !adapter.current(product.token) || !world.dirty_mask(key).empty()) {
      return false;
    }
  }
  const auto metrics = adapter.metrics();
  if (metrics.schedule_calls != 2 || metrics.executions != 2 ||
      metrics.capacity_failures != 0) {
    return false;
  }
  if (adapter.evict(tess::ChunkKey{0}) !=
          maintenance::ChunkEvictionResult::Evicted ||
      world.is_resident(tess::ChunkKey{0}) ||
      adapter.product(tess::ChunkKey{0}).state !=
          maintenance::ChunkProductState::Unavailable) {
    return false;
  }
  return adapter.try_release() ==
             maintenance::ChunkAdapterReleaseResult::Released &&
         adapter.try_release() ==
             maintenance::ChunkAdapterReleaseResult::AlreadyReleased;
}

// The defaulted adapter backend parameter executes each accepted offer
// synchronously, so a mark alone completes its rebuild.
[[nodiscard]] auto default_backend_is_synchronous() -> bool {
  World world{tess::ResidencyConfig{World::page_byte_size}};
  maintenance::ChunkMaintenanceAdapter<World, Summary, RebuildSummary> adapter{
      world, kOwned, RebuildSummary{}};
  if (adapter.flush() != maintenance::DrainResult::Idle ||
      adapter.ensure_resident(tess::ChunkKey{1}).status !=
          maintenance::ChunkResidencyStatus::Ready) {
    return false;
  }
  world.field<ValueTag>(tess::Coord3{4, 0}) = 9;
  if (adapter.mark_dirty(tess::ChunkKey{1}, kOwned, one_tile_box(1)) !=
      maintenance::ChunkMarkResult::Accepted) {
    return false;
  }
  const auto product = adapter.product(tess::ChunkKey{1});
  return adapter.metrics().executions == 1 &&
         product.state == maintenance::ChunkProductState::Current &&
         product.value != nullptr && product.value->sum == 9 &&
         adapter.flush() == maintenance::DrainResult::Idle;
}

}  // namespace

int main() {
  if (!registered_scheduler_workflow()) {
    return 1;
  }
  if (!sparse_adapter_workflow()) {
    return 2;
  }
  return default_backend_is_synchronous() ? 0 : 3;
}
