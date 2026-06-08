#pragma once

#include <tess/block/block.h>
#include <tess/core/shape.h>
#include <tess/diagnostics/diagnostics.h>
#include <tess/storage/world.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <source_location>
#include <span>
#include <thread>
#include <utility>
#include <vector>

namespace tess {

struct OpId {
  std::uint64_t value;

  friend constexpr bool operator==(OpId lhs, OpId rhs) noexcept = default;
};

struct OpHandle {
  std::uint64_t value;

  friend constexpr bool operator==(OpHandle lhs,
                                   OpHandle rhs) noexcept = default;
};

enum class OperationKind : std::uint8_t {
  UpdateField,
};
static_assert(sizeof(OperationKind) == sizeof(std::uint8_t));

enum class Priority : std::uint8_t {
  Immediate,
  GameplayCritical,
  VisibleSoon,
  Background,
  Maintenance,
};
static_assert(sizeof(Priority) == sizeof(std::uint8_t));

enum class BudgetPolicy : std::uint8_t {
  MustRun,
  CanDefer,
  CanSkipIfSuperseded,
  BudgetedIncremental,
};
static_assert(sizeof(BudgetPolicy) == sizeof(std::uint8_t));

enum class OperationStatus : std::uint8_t {
  Planned,
  InvalidWritePolicy,
  InvalidDomain,
  InvalidFieldAccess,
  HazardConflict,
};
static_assert(sizeof(OperationStatus) == sizeof(std::uint8_t));

enum class OperationFailure : std::uint8_t {
  None,
  InvalidWritePolicyValue,
  ExplicitChunkOutOfRange,
  ReadOnlyWriteMask,
  FieldHazardConflict,
};
static_assert(sizeof(OperationFailure) == sizeof(std::uint8_t));

enum class PlannedExecutionStatus : std::uint8_t {
  Executed,
  PolicyMismatch,
  InvalidPhase,
};
static_assert(sizeof(PlannedExecutionStatus) == sizeof(std::uint8_t));

enum class ExecutionPhaseStatus : std::uint8_t {
  Ready,
  UnsupportedWritePolicy,
};
static_assert(sizeof(ExecutionPhaseStatus) == sizeof(std::uint8_t));

enum class DomainKind : std::uint8_t {
  ExplicitChunks,
  DirtyChunks,
  ActiveChunks,
  ResidentChunks,
};
static_assert(sizeof(DomainKind) == sizeof(std::uint8_t));

class DomainDesc {
 public:
  [[nodiscard]] static auto explicit_chunks(std::span<const ChunkKey> keys)
      -> DomainDesc {
    DomainDesc desc{DomainKind::ExplicitChunks};
    desc.explicit_chunks_.assign(keys.begin(), keys.end());
    std::sort(desc.explicit_chunks_.begin(), desc.explicit_chunks_.end(),
              [](ChunkKey lhs, ChunkKey rhs) { return lhs.value < rhs.value; });
    return desc;
  }

  [[nodiscard]] static constexpr auto dirty_chunks(std::uint32_t flags) noexcept
      -> DomainDesc {
    DomainDesc desc{DomainKind::DirtyChunks};
    desc.mask_ = flags;
    return desc;
  }

  [[nodiscard]] static constexpr auto active_chunks(
      std::uint32_t flags) noexcept -> DomainDesc {
    DomainDesc desc{DomainKind::ActiveChunks};
    desc.mask_ = flags;
    return desc;
  }

  [[nodiscard]] static constexpr auto resident_chunks() noexcept -> DomainDesc {
    return DomainDesc{DomainKind::ResidentChunks};
  }

  [[nodiscard]] constexpr auto kind() const noexcept -> DomainKind {
    return kind_;
  }

  [[nodiscard]] constexpr auto mask() const noexcept -> std::uint32_t {
    return mask_;
  }

  [[nodiscard]] constexpr auto explicit_chunks() const noexcept
      -> std::span<const ChunkKey> {
    return {explicit_chunks_.data(), explicit_chunks_.size()};
  }

 private:
  constexpr explicit DomainDesc(DomainKind kind) noexcept : kind_(kind) {}

  DomainKind kind_;
  std::uint32_t mask_ = 0;
  std::vector<ChunkKey> explicit_chunks_;
};

struct FieldAccessDesc {
  std::uint32_t read_mask = 0;
  std::uint32_t write_mask = 0;
  std::uint32_t dirty_mask = 0;

  friend constexpr bool operator==(FieldAccessDesc lhs,
                                   FieldAccessDesc rhs) noexcept = default;
};

struct QueuedOperation {
  OperationKind kind = OperationKind::UpdateField;
  OpHandle handle{};
  OpId id{};
  DomainDesc domain = DomainDesc::resident_chunks();
  FieldAccessDesc field_access{};
  WritePolicy write_policy = WritePolicy::ReadOnly;
  Priority priority = Priority::GameplayCritical;
  BudgetPolicy budget_policy = BudgetPolicy::MustRun;
  std::source_location source = std::source_location::current();
};

struct OperationAccess {
  WritePolicy write_policy = WritePolicy::ReadOnly;
  DomainKind domain_kind = DomainKind::ResidentChunks;
  std::uint32_t domain_mask = 0;
};

struct PlannedOperation {
  OperationKind kind = OperationKind::UpdateField;
  OpHandle handle{};
  OpId id{};
  OperationAccess access{};
  FieldAccessDesc field_access{};
  WritePolicy write_policy = WritePolicy::ReadOnly;
  Priority priority = Priority::GameplayCritical;
  BudgetPolicy budget_policy = BudgetPolicy::MustRun;
  std::vector<ChunkKey> chunks;
};

class ExecutionPlan {
 public:
  [[nodiscard]] constexpr auto operations() const noexcept
      -> std::span<const PlannedOperation> {
    return {operations_.data(), operations_.size()};
  }

  [[nodiscard]] constexpr bool empty() const noexcept {
    return operations_.empty();
  }

  [[nodiscard]] constexpr auto size() const noexcept -> std::size_t {
    return operations_.size();
  }

 private:
  friend class ExecutionReport;

  std::vector<PlannedOperation> operations_;
};

struct ExecutionPhase {
  std::size_t first_operation = 0;
  std::size_t operation_count = 0;
};

struct ExecutorPhaseRange {
  std::size_t first_operation = 0;
  std::size_t operation_count = 0;
};

[[nodiscard]] constexpr auto executor_phase_range(ExecutionPhase phase) noexcept
    -> ExecutorPhaseRange {
  return ExecutorPhaseRange{
      phase.first_operation,
      phase.operation_count,
  };
}

class ExecutionPhasePlan {
 public:
  [[nodiscard]] constexpr auto phases() const noexcept
      -> std::span<const ExecutionPhase> {
    return {phases_.data(), phases_.size()};
  }

  [[nodiscard]] constexpr auto status() const noexcept -> ExecutionPhaseStatus {
    return status_;
  }

  [[nodiscard]] constexpr bool ok() const noexcept {
    return status_ == ExecutionPhaseStatus::Ready;
  }

  [[nodiscard]] constexpr auto failed_operation_index() const noexcept
      -> std::size_t {
    return failed_operation_index_;
  }

  [[nodiscard]] constexpr auto failed_write_policy() const noexcept
      -> WritePolicy {
    return failed_write_policy_;
  }

 private:
  friend auto plan_parallel_execution_phases(const ExecutionPlan& plan)
      -> ExecutionPhasePlan;

  void reserve(std::size_t size) { phases_.reserve(size); }

  void push_phase(ExecutionPhase phase) { phases_.push_back(phase); }

  std::vector<ExecutionPhase> phases_;
  ExecutionPhaseStatus status_ = ExecutionPhaseStatus::Ready;
  std::size_t failed_operation_index_ = 0;
  WritePolicy failed_write_policy_ = WritePolicy::ReadOnly;
};

struct OperationReport {
  OpHandle handle{};
  OpId id{};
  OperationStatus status = OperationStatus::Planned;
  OperationFailure failure = OperationFailure::None;
  OperationAccess access{};
  FieldAccessDesc field_access{};
  ChunkKey detail_chunk{};
  OpHandle conflict_handle{};
  OpId conflict_id{};
  std::uint32_t conflict_mask = 0;
  bool has_detail_chunk = false;
  bool has_conflict = false;
  std::size_t chunk_count = 0;
  std::source_location source = std::source_location::current();
};

struct PlannedExecutionResult {
  PlannedExecutionStatus status = PlannedExecutionStatus::Executed;
  std::size_t chunk_count = 0;
};

struct SerialPhaseExecutor {
  template <typename Fn>
  auto for_each_operation(ExecutorPhaseRange range, Fn&& fn) const
      -> PlannedExecutionResult {
    return for_each_operation(range.first_operation, range.operation_count,
                              std::forward<Fn>(fn));
  }

  template <typename Fn>
  auto for_each_operation(std::size_t first, std::size_t count, Fn&& fn) const
      -> PlannedExecutionResult {
    auto&& callback = fn;
    const auto end = first + count;
    for (std::size_t i = first; i < end; ++i) {
      auto result = callback(i);
      if (result.status != PlannedExecutionStatus::Executed) {
        return result;
      }
    }
    return PlannedExecutionResult{};
  }
};

class ScopedThreadPhaseExecutor {
 public:
  explicit ScopedThreadPhaseExecutor(std::size_t worker_count) noexcept
      : worker_count_(worker_count == 0 ? 1 : worker_count) {}

  ScopedThreadPhaseExecutor() noexcept
      : ScopedThreadPhaseExecutor(std::thread::hardware_concurrency()) {}

  [[nodiscard]] auto worker_count() const noexcept -> std::size_t {
    return worker_count_;
  }

  template <typename Fn>
  auto for_each_operation(std::size_t first, std::size_t count, Fn&& fn) const
      -> PlannedExecutionResult {
    if (count == 0) {
      return PlannedExecutionResult{};
    }

    const auto thread_count = std::min(worker_count_, count);
    TESS_DIAG_EVENT_VALUE(queued_scoped_thread_dispatch, thread_count);
    std::atomic<std::size_t> next_offset = 0;
    std::vector<PlannedExecutionResult> results(count);
    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    auto&& callback = fn;

    for (std::size_t worker = 0; worker < thread_count; ++worker) {
      threads.emplace_back([&] {
        while (true) {
          const auto offset = next_offset.fetch_add(1);
          if (offset >= count) {
            return;
          }
          results[offset] = callback(first + offset);
        }
      });
    }

    for (auto& thread : threads) {
      thread.join();
    }

    for (const auto result : results) {
      if (result.status != PlannedExecutionStatus::Executed) {
        return result;
      }
    }
    return PlannedExecutionResult{};
  }

 private:
  std::size_t worker_count_ = 1;
};

template <typename Executor, typename Fn>
auto execute_operation_index_range(Executor&& executor,
                                   ExecutorPhaseRange range, Fn&& fn)
    -> PlannedExecutionResult {
  return executor.for_each_operation(
      range.first_operation, range.operation_count, std::forward<Fn>(fn));
}

struct PlannedDirtyRecord {
  ChunkKey chunk{};
  std::uint32_t dirty_mask = 0;
  Box3 bounds{};
};

class PlannedDirtyPartitions;

class PlannedDirtyAccumulator {
 public:
  void reserve(std::size_t count) { records_.reserve(count); }

  void clear() noexcept { records_.clear(); }

  void record(ChunkKey chunk, std::uint32_t dirty_mask, Box3 bounds) {
    if (dirty_mask == 0) {
      return;
    }
    records_.push_back(PlannedDirtyRecord{chunk, dirty_mask, bounds});
  }

  [[nodiscard]] auto records() const noexcept
      -> std::span<const PlannedDirtyRecord> {
    return records_;
  }

 private:
  template <typename World>
  friend auto merge_planned_dirty(World& world,
                                  PlannedDirtyAccumulator& dirty) noexcept
      -> std::size_t;
  friend auto collect_planned_dirty(PlannedDirtyAccumulator& dirty,
                                    PlannedDirtyPartitions& partitions)
      -> std::size_t;

  std::vector<PlannedDirtyRecord> records_;
};

class PlannedDirtyPartitions {
 public:
  void reserve(std::size_t count) { partitions_.reserve(count); }

  void resize(std::size_t count) { partitions_.resize(count); }

  void clear() noexcept { partitions_.clear(); }

  void clear_records() noexcept {
    for (auto& partition : partitions_) {
      partition.clear();
    }
  }

  void reserve_records_per_partition(std::size_t count) {
    records_per_partition_reserve_ = count;
    for (auto& partition : partitions_) {
      partition.reserve(count);
    }
  }

  [[nodiscard]] auto size() const noexcept -> std::size_t {
    return partitions_.size();
  }

  [[nodiscard]] auto partition(std::size_t index) noexcept
      -> PlannedDirtyAccumulator& {
    return partitions_[index];
  }

  [[nodiscard]] auto partition(std::size_t index) const noexcept
      -> const PlannedDirtyAccumulator& {
    return partitions_[index];
  }

  [[nodiscard]] auto partitions() const noexcept
      -> std::span<const PlannedDirtyAccumulator> {
    return partitions_;
  }

 private:
  friend auto collect_planned_dirty(PlannedDirtyAccumulator& dirty,
                                    PlannedDirtyPartitions& partitions)
      -> std::size_t;
  friend class PlannedPhaseExecutionScratch;

  void prepare(std::size_t count) {
    partitions_.resize(count);
    for (auto& partition : partitions_) {
      partition.clear();
      partition.reserve(records_per_partition_reserve_);
    }
  }

  std::vector<PlannedDirtyAccumulator> partitions_;
  std::size_t records_per_partition_reserve_ = 0;
};

class PlannedPhaseExecutionScratch {
 public:
  void reserve_operations(std::size_t count) {
    dirty_partitions_.reserve(count);
    results_.reserve(count);
  }

  void reserve_dirty_records_per_operation(std::size_t count) {
    dirty_partitions_.reserve_records_per_partition(count);
  }

  void reserve_merged_dirty_records(std::size_t count) {
    merged_dirty_.reserve(count);
  }

  void prepare_for_operation_count(std::size_t count) { prepare(count); }

  void clear() noexcept {
    dirty_partitions_.clear_records();
    results_.clear();
    merged_dirty_.clear();
  }

  [[nodiscard]] auto operation_count() const noexcept -> std::size_t {
    return results_.size();
  }

  [[nodiscard]] auto dirty_partitions() const noexcept
      -> std::span<const PlannedDirtyAccumulator> {
    return dirty_partitions_.partitions();
  }

 private:
  template <WritePolicy Policy, typename Executor, typename World, typename Fn>
  friend auto execute_phase_partitioned_dirty_with(
      Executor&& executor, World& world, const ExecutionPlan& plan,
      ExecutionPhase phase, PlannedPhaseExecutionScratch& scratch, Fn&& fn)
      -> PlannedExecutionResult;

  template <typename World>
  friend auto merge_planned_dirty(World& world,
                                  PlannedPhaseExecutionScratch& scratch)
      -> std::size_t;

  void prepare(std::size_t operation_count) {
    dirty_partitions_.prepare(operation_count);
    results_.assign(operation_count, PlannedExecutionResult{});
    merged_dirty_.clear();
  }

  [[nodiscard]] auto dirty_for_operation(std::size_t index) noexcept
      -> PlannedDirtyAccumulator& {
    return dirty_partitions_.partition(index);
  }

  void record_result(std::size_t index, PlannedExecutionResult result) {
    results_[index] = result;
  }

  [[nodiscard]] auto results() const noexcept
      -> std::span<const PlannedExecutionResult> {
    return results_;
  }

  PlannedDirtyPartitions dirty_partitions_;
  std::vector<PlannedExecutionResult> results_;
  PlannedDirtyAccumulator merged_dirty_;
};

class ExecutionReport {
 public:
  [[nodiscard]] constexpr auto operations() const noexcept
      -> std::span<const OperationReport> {
    return {operations_.data(), operations_.size()};
  }

  [[nodiscard]] constexpr auto plan() const noexcept -> const ExecutionPlan& {
    return plan_;
  }

  [[nodiscard]] constexpr auto find(OpHandle handle) const noexcept
      -> const OperationReport* {
    for (const auto& op : operations_) {
      if (op.handle == handle) {
        return &op;
      }
    }
    return nullptr;
  }

  [[nodiscard]] constexpr bool ok() const noexcept {
    return failed_count() == 0;
  }

  [[nodiscard]] constexpr bool failed() const noexcept {
    return failed_count() != 0;
  }

  [[nodiscard]] constexpr auto planned_count() const noexcept -> std::size_t {
    return plan_.size();
  }

  [[nodiscard]] constexpr auto failed_count() const noexcept -> std::size_t {
    std::size_t count = 0;
    for (const auto& op : operations_) {
      if (op.status != OperationStatus::Planned) {
        ++count;
      }
    }
    return count;
  }

 private:
  template <typename World>
  friend auto plan_operations(const World& world,
                              std::span<const QueuedOperation> operations)
      -> ExecutionReport;

  void reserve(std::size_t size) {
    operations_.reserve(size);
    plan_.operations_.reserve(size);
  }

  void push_report(OperationReport report) { operations_.push_back(report); }

  void push_planned(PlannedOperation planned) {
    plan_.operations_.push_back(std::move(planned));
  }

  std::vector<OperationReport> operations_;
  ExecutionPlan plan_;
};

class FrameOps {
 public:
  [[nodiscard]] auto update_field(
      DomainDesc domain, FieldAccessDesc field_access, WritePolicy write_policy,
      Priority priority = Priority::GameplayCritical,
      BudgetPolicy budget_policy = BudgetPolicy::MustRun,
      std::source_location source = std::source_location::current())
      -> OpHandle {
    const auto id = OpId{static_cast<std::uint64_t>(operations_.size())};
    const auto handle = OpHandle{id.value};
    operations_.push_back(QueuedOperation{
        OperationKind::UpdateField,
        handle,
        id,
        std::move(domain),
        field_access,
        write_policy,
        priority,
        budget_policy,
        source,
    });
    return handle;
  }

  [[nodiscard]] auto update_field(
      DomainDesc domain, WritePolicy write_policy,
      Priority priority = Priority::GameplayCritical,
      BudgetPolicy budget_policy = BudgetPolicy::MustRun,
      std::source_location source = std::source_location::current())
      -> OpHandle {
    return update_field(std::move(domain), FieldAccessDesc{}, write_policy,
                        priority, budget_policy, source);
  }

  [[nodiscard]] constexpr auto operations() const noexcept
      -> std::span<const QueuedOperation> {
    return {operations_.data(), operations_.size()};
  }

  [[nodiscard]] constexpr auto operation(OpHandle handle) const noexcept
      -> const QueuedOperation* {
    if (handle.value >= operations_.size()) {
      return nullptr;
    }
    return &operations_[static_cast<std::size_t>(handle.value)];
  }

  [[nodiscard]] constexpr bool empty() const noexcept {
    return operations_.empty();
  }

  [[nodiscard]] constexpr auto size() const noexcept -> std::size_t {
    return operations_.size();
  }

 private:
  std::vector<QueuedOperation> operations_;
};

namespace detail {

[[nodiscard]] constexpr auto operation_access(
    const QueuedOperation& op) noexcept -> OperationAccess {
  return OperationAccess{
      op.write_policy,
      op.domain.kind(),
      op.domain.mask(),
  };
}

[[nodiscard]] constexpr bool is_valid_field_access(
    WritePolicy write_policy, FieldAccessDesc field_access) noexcept {
  if (write_policy == WritePolicy::ReadOnly && field_access.write_mask != 0) {
    return false;
  }
  return true;
}

template <typename World>
[[nodiscard]] auto validate_explicit_chunks(const World& world,
                                            std::span<const ChunkKey> chunks,
                                            ChunkKey& invalid_chunk) noexcept
    -> bool {
  for (const auto key : chunks) {
    if (world.try_chunk(key) == nullptr) {
      invalid_chunk = key;
      return false;
    }
  }
  return true;
}

template <typename World>
[[nodiscard]] auto resident_chunk_keys(const World&) -> std::vector<ChunkKey> {
  std::vector<ChunkKey> chunks;
  chunks.reserve(static_cast<std::size_t>(World::chunk_count));
  for (std::uint64_t key = 0; key < World::chunk_count; ++key) {
    chunks.push_back(ChunkKey{key});
  }
  return chunks;
}

template <typename World>
[[nodiscard]] auto expand_domain(const World& world, const DomainDesc& domain,
                                 std::vector<ChunkKey>& chunks,
                                 ChunkKey& invalid_chunk) -> bool {
  switch (domain.kind()) {
    case DomainKind::ExplicitChunks:
      if (!validate_explicit_chunks(world, domain.explicit_chunks(),
                                    invalid_chunk)) {
        return false;
      }
      chunks.assign(domain.explicit_chunks().begin(),
                    domain.explicit_chunks().end());
      return true;
    case DomainKind::DirtyChunks:
      chunks = world.dirty_chunks(domain.mask());
      return true;
    case DomainKind::ActiveChunks:
      chunks = world.active_chunks(domain.mask());
      return true;
    case DomainKind::ResidentChunks:
      chunks = resident_chunk_keys(world);
      return true;
  }
  return false;
}

[[nodiscard]] constexpr auto hazard_mask(FieldAccessDesc earlier,
                                         FieldAccessDesc later) noexcept
    -> std::uint32_t {
  return (earlier.write_mask & later.write_mask) |
         (earlier.write_mask & later.read_mask) |
         (earlier.read_mask & later.write_mask);
}

[[nodiscard]] constexpr bool chunks_overlap(
    std::span<const ChunkKey> lhs, std::span<const ChunkKey> rhs) noexcept {
  std::size_t lhs_index = 0;
  std::size_t rhs_index = 0;
  while (lhs_index < lhs.size() && rhs_index < rhs.size()) {
    const auto lhs_key = lhs[lhs_index].value;
    const auto rhs_key = rhs[rhs_index].value;
    if (lhs_key == rhs_key) {
      return true;
    }
    if (lhs_key < rhs_key) {
      ++lhs_index;
    } else {
      ++rhs_index;
    }
  }
  return false;
}

[[nodiscard]] constexpr auto find_hazard(
    std::span<const PlannedOperation> earlier_ops,
    const PlannedOperation& later) noexcept -> const PlannedOperation* {
  for (const auto& earlier : earlier_ops) {
    if (hazard_mask(earlier.field_access, later.field_access) == 0) {
      continue;
    }
    if (chunks_overlap(earlier.chunks, later.chunks)) {
      // cppcheck-suppress returnDanglingLifetime
      return &earlier;
    }
  }
  return nullptr;
}

[[nodiscard]] constexpr bool is_parallel_supported_policy(
    WritePolicy policy) noexcept {
  return policy == WritePolicy::ReadOnly ||
         policy == WritePolicy::UniquePerChunk;
}

[[nodiscard]] constexpr bool is_mutating_policy(WritePolicy policy) noexcept {
  return policy != WritePolicy::ReadOnly;
}

[[nodiscard]] constexpr bool parallel_phase_conflict(
    const PlannedOperation& lhs, const PlannedOperation& rhs) noexcept {
  if (!chunks_overlap(lhs.chunks, rhs.chunks)) {
    return false;
  }
  if (is_mutating_policy(lhs.write_policy) ||
      is_mutating_policy(rhs.write_policy)) {
    return true;
  }
  return hazard_mask(lhs.field_access, rhs.field_access) != 0;
}

[[nodiscard]] constexpr auto dirty_axis_end(std::int64_t origin,
                                            std::uint64_t extent) noexcept
    -> std::int64_t {
  return origin + static_cast<std::int64_t>(extent);
}

[[nodiscard]] constexpr auto dirty_min(std::int64_t lhs,
                                       std::int64_t rhs) noexcept
    -> std::int64_t {
  return lhs < rhs ? lhs : rhs;
}

[[nodiscard]] constexpr auto dirty_max(std::int64_t lhs,
                                       std::int64_t rhs) noexcept
    -> std::int64_t {
  return lhs < rhs ? rhs : lhs;
}

[[nodiscard]] constexpr auto dirty_union_extent(std::int64_t origin,
                                                std::int64_t end) noexcept
    -> std::uint64_t {
  return static_cast<std::uint64_t>(end - origin);
}

[[nodiscard]] constexpr auto union_dirty_bounds(Box3 lhs, Box3 rhs) noexcept
    -> Box3 {
  const auto min_x = dirty_min(lhs.origin.x, rhs.origin.x);
  const auto min_y = dirty_min(lhs.origin.y, rhs.origin.y);
  const auto min_z = dirty_min(lhs.origin.z, rhs.origin.z);
  const auto max_x = dirty_max(dirty_axis_end(lhs.origin.x, lhs.extent.x),
                               dirty_axis_end(rhs.origin.x, rhs.extent.x));
  const auto max_y = dirty_max(dirty_axis_end(lhs.origin.y, lhs.extent.y),
                               dirty_axis_end(rhs.origin.y, rhs.extent.y));
  const auto max_z = dirty_max(dirty_axis_end(lhs.origin.z, lhs.extent.z),
                               dirty_axis_end(rhs.origin.z, rhs.extent.z));

  return Box3{
      Coord3{min_x, min_y, min_z},
      Extent3{
          dirty_union_extent(min_x, max_x),
          dirty_union_extent(min_y, max_y),
          dirty_union_extent(min_z, max_z),
      },
  };
}

}  // namespace detail

template <typename World>
[[nodiscard]] auto plan_operations(const World& world,
                                   std::span<const QueuedOperation> operations)
    -> ExecutionReport {
  ExecutionReport report;
  report.reserve(operations.size());

  for (const auto& op : operations) {
    OperationReport op_report{
        op.handle,
        op.id,
        OperationStatus::Planned,
        OperationFailure::None,
        detail::operation_access(op),
        op.field_access,
        {},
        {},
        {},
        0,
        false,
        false,
        0,
        op.source,
    };

    if (!is_valid_write_policy(op.write_policy)) {
      op_report.status = OperationStatus::InvalidWritePolicy;
      op_report.failure = OperationFailure::InvalidWritePolicyValue;
      report.push_report(op_report);
      continue;
    }

    if (!detail::is_valid_field_access(op.write_policy, op.field_access)) {
      op_report.status = OperationStatus::InvalidFieldAccess;
      op_report.failure = OperationFailure::ReadOnlyWriteMask;
      report.push_report(op_report);
      continue;
    }

    PlannedOperation planned{
        op.kind,
        op.handle,
        op.id,
        detail::operation_access(op),
        op.field_access,
        op.write_policy,
        op.priority,
        op.budget_policy,
        {},
    };
    ChunkKey invalid_chunk{};
    if (!detail::expand_domain(world, op.domain, planned.chunks,
                               invalid_chunk)) {
      op_report.status = OperationStatus::InvalidDomain;
      op_report.failure = OperationFailure::ExplicitChunkOutOfRange;
      op_report.detail_chunk = invalid_chunk;
      op_report.has_detail_chunk = true;
      report.push_report(op_report);
      continue;
    }

    if (const auto* conflict =
            detail::find_hazard(report.plan().operations(), planned);
        conflict != nullptr) {
      op_report.status = OperationStatus::HazardConflict;
      op_report.failure = OperationFailure::FieldHazardConflict;
      op_report.conflict_handle = conflict->handle;
      op_report.conflict_id = conflict->id;
      op_report.conflict_mask =
          detail::hazard_mask(conflict->field_access, planned.field_access);
      op_report.has_conflict = true;
      op_report.chunk_count = planned.chunks.size();
      report.push_report(op_report);
      continue;
    }

    op_report.chunk_count = planned.chunks.size();
    report.push_planned(std::move(planned));
    report.push_report(op_report);
  }

  return report;
}

template <typename World>
[[nodiscard]] auto plan_operations(const World& world, const FrameOps& ops)
    -> ExecutionReport {
  return plan_operations(world, ops.operations());
}

[[nodiscard]] constexpr auto planned_chunk_domain(
    const PlannedOperation& operation) noexcept -> ChunkDomain {
  return chunk_domain(std::span<const ChunkKey>{operation.chunks.data(),
                                                operation.chunks.size()});
}

[[nodiscard]] inline auto plan_parallel_execution_phases(
    const ExecutionPlan& plan) -> ExecutionPhasePlan {
  const auto operations = plan.operations();
  auto phases = ExecutionPhasePlan{};
  phases.reserve(operations.size());

  for (std::size_t i = 0; i < operations.size(); ++i) {
    const auto& operation = operations[i];
    if (!detail::is_parallel_supported_policy(operation.write_policy)) {
      phases.status_ = ExecutionPhaseStatus::UnsupportedWritePolicy;
      phases.failed_operation_index_ = i;
      phases.failed_write_policy_ = operation.write_policy;
      return phases;
    }

    if (phases.phases_.empty()) {
      phases.push_phase(ExecutionPhase{i, 1});
      continue;
    }

    auto& phase = phases.phases_.back();
    auto conflicts = false;
    const auto end = phase.first_operation + phase.operation_count;
    for (std::size_t j = phase.first_operation; j < end; ++j) {
      if (detail::parallel_phase_conflict(operations[j], operation)) {
        conflicts = true;
        break;
      }
    }

    if (conflicts) {
      phases.push_phase(ExecutionPhase{i, 1});
    } else {
      ++phase.operation_count;
    }
  }

  return phases;
}

template <typename World>
auto merge_planned_dirty(World& world, PlannedDirtyAccumulator& dirty) noexcept
    -> std::size_t {
  auto& records = dirty.records_;
  std::sort(records.begin(), records.end(),
            [](PlannedDirtyRecord lhs, PlannedDirtyRecord rhs) {
              return lhs.chunk.value < rhs.chunk.value;
            });

  auto merged_count = std::size_t{0};
  for (std::size_t i = 0; i < records.size();) {
    auto chunk = records[i].chunk;
    auto dirty_mask = records[i].dirty_mask;
    auto bounds = records[i].bounds;
    ++i;

    while (i < records.size() && records[i].chunk == chunk) {
      dirty_mask |= records[i].dirty_mask;
      bounds = detail::union_dirty_bounds(bounds, records[i].bounds);
      ++i;
    }

    world.mark_dirty(chunk, dirty_mask, bounds);
    ++merged_count;
  }

  TESS_DIAG_EVENT_VALUE(queued_dirty_merge, merged_count);
  dirty.clear();
  return merged_count;
}

inline auto collect_planned_dirty(PlannedDirtyAccumulator& dirty,
                                  PlannedDirtyPartitions& partitions)
    -> std::size_t {
  std::size_t record_count = 0;
  for (auto& partition : partitions.partitions_) {
    record_count += partition.records_.size();
    dirty.records_.insert(dirty.records_.end(), partition.records_.begin(),
                          partition.records_.end());
    partition.clear();
  }
  TESS_DIAG_EVENT_VALUE(queued_dirty_collect, record_count);
  return record_count;
}

template <typename World>
auto merge_planned_dirty(World& world, PlannedDirtyPartitions& partitions,
                         PlannedDirtyAccumulator& dirty_scratch)
    -> std::size_t {
  dirty_scratch.clear();
  (void)collect_planned_dirty(dirty_scratch, partitions);
  return merge_planned_dirty(world, dirty_scratch);
}

template <typename World>
auto merge_planned_dirty(World& world, PlannedPhaseExecutionScratch& scratch)
    -> std::size_t {
  return merge_planned_dirty(world, scratch.dirty_partitions_,
                             scratch.merged_dirty_);
}

template <WritePolicy Policy>
[[nodiscard]] constexpr bool planned_policy_matches(
    const PlannedOperation& operation) noexcept {
  return operation.write_policy == Policy;
}

template <WritePolicy Policy, typename World>
[[nodiscard]] constexpr auto try_planned_block_ctx(
    World& world, const PlannedOperation& operation) noexcept
    -> std::optional<BlockCtx<World, Policy>> {
  if (!planned_policy_matches<Policy>(operation)) {
    return std::nullopt;
  }
  return block_ctx<Policy>(world, planned_chunk_domain(operation));
}

template <WritePolicy Policy, typename World, typename Fn>
auto execute_planned_operation(World& world, const PlannedOperation& operation,
                               Fn&& fn) -> PlannedExecutionResult {
  auto ctx = try_planned_block_ctx<Policy>(world, operation);
  if (!ctx.has_value()) {
    return PlannedExecutionResult{
        PlannedExecutionStatus::PolicyMismatch,
        0,
    };
  }

  std::size_t chunk_count = 0;
  auto&& callback = fn;
  ctx->for_each_chunk([&](auto view) {
    callback(view);
    if (operation.field_access.dirty_mask != 0) {
      world.mark_dirty(view.key(), operation.field_access.dirty_mask,
                       view.bounds());
    }
    ++chunk_count;
  });

  return PlannedExecutionResult{
      PlannedExecutionStatus::Executed,
      chunk_count,
  };
}

template <WritePolicy Policy, typename World, typename Fn>
auto execute_planned_operation_deferred_dirty(World& world,
                                              const PlannedOperation& operation,
                                              PlannedDirtyAccumulator& dirty,
                                              Fn&& fn)
    -> PlannedExecutionResult {
  auto ctx = try_planned_block_ctx<Policy>(world, operation);
  if (!ctx.has_value()) {
    return PlannedExecutionResult{
        PlannedExecutionStatus::PolicyMismatch,
        0,
    };
  }

  std::size_t chunk_count = 0;
  auto&& callback = fn;
  ctx->for_each_chunk([&](auto view) {
    callback(view);
    dirty.record(view.key(), operation.field_access.dirty_mask, view.bounds());
    ++chunk_count;
  });

  return PlannedExecutionResult{
      PlannedExecutionStatus::Executed,
      chunk_count,
  };
}

template <WritePolicy Policy, typename World, typename Fn>
auto execute_plan(World& world, const ExecutionPlan& plan, Fn&& fn)
    -> PlannedExecutionResult {
  std::size_t chunk_count = 0;
  auto&& callback = fn;
  for (const auto& operation : plan.operations()) {
    auto result = execute_planned_operation<Policy>(world, operation, callback);
    if (result.status != PlannedExecutionStatus::Executed) {
      return result;
    }
    chunk_count += result.chunk_count;
  }
  return PlannedExecutionResult{
      PlannedExecutionStatus::Executed,
      chunk_count,
  };
}

template <WritePolicy Policy, typename World, typename Fn>
auto execute_plan_deferred_dirty(World& world, const ExecutionPlan& plan,
                                 PlannedDirtyAccumulator& dirty, Fn&& fn)
    -> PlannedExecutionResult {
  std::size_t chunk_count = 0;
  auto&& callback = fn;
  for (const auto& operation : plan.operations()) {
    auto result = execute_planned_operation_deferred_dirty<Policy>(
        world, operation, dirty, callback);
    if (result.status != PlannedExecutionStatus::Executed) {
      return result;
    }
    chunk_count += result.chunk_count;
  }
  return PlannedExecutionResult{
      PlannedExecutionStatus::Executed,
      chunk_count,
  };
}

template <WritePolicy Policy, typename Executor, typename World, typename Fn>
auto execute_phase_deferred_dirty_with(Executor&& executor, World& world,
                                       const ExecutionPlan& plan,
                                       ExecutionPhase phase,
                                       PlannedDirtyAccumulator& dirty, Fn&& fn)
    -> PlannedExecutionResult {
  const auto operations = plan.operations();
  if (phase.first_operation > operations.size() ||
      phase.operation_count > operations.size() - phase.first_operation) {
    TESS_DIAG_EVENT(queued_phase_invalid_range);
    return PlannedExecutionResult{
        PlannedExecutionStatus::InvalidPhase,
        0,
    };
  }

  TESS_DIAG_EVENT_VALUE(queued_phase_execute, phase.operation_count);
  std::size_t chunk_count = 0;
  auto&& callback = fn;
  auto result = execute_operation_index_range(
      std::forward<Executor>(executor), executor_phase_range(phase),
      [&](std::size_t index) {
        auto operation_result =
            execute_planned_operation_deferred_dirty<Policy>(
                world, operations[index], dirty, callback);
        if (operation_result.status == PlannedExecutionStatus::Executed) {
          chunk_count += operation_result.chunk_count;
        }
        return operation_result;
      });
  if (result.status != PlannedExecutionStatus::Executed) {
    TESS_DIAG_EVENT(queued_phase_failure);
    result.chunk_count = chunk_count;
    return result;
  }

  return PlannedExecutionResult{
      PlannedExecutionStatus::Executed,
      chunk_count,
  };
}

template <WritePolicy Policy, typename Executor, typename World, typename Fn>
auto execute_phase_partitioned_dirty_with(Executor&& executor, World& world,
                                          const ExecutionPlan& plan,
                                          ExecutionPhase phase,
                                          PlannedPhaseExecutionScratch& scratch,
                                          Fn&& fn) -> PlannedExecutionResult {
  const auto operations = plan.operations();
  if (phase.first_operation > operations.size() ||
      phase.operation_count > operations.size() - phase.first_operation) {
    TESS_DIAG_EVENT(queued_phase_invalid_range);
    return PlannedExecutionResult{
        PlannedExecutionStatus::InvalidPhase,
        0,
    };
  }

  TESS_DIAG_EVENT_VALUE(queued_phase_execute, phase.operation_count);
  TESS_DIAG_EVENT_VALUE(queued_partitioned_phase, phase.operation_count);
  scratch.prepare(phase.operation_count);
  auto&& callback = fn;
  auto result = execute_operation_index_range(
      std::forward<Executor>(executor), executor_phase_range(phase),
      [&](std::size_t index) {
        const auto offset = index - phase.first_operation;
        auto operation_result =
            execute_planned_operation_deferred_dirty<Policy>(
                world, operations[index], scratch.dirty_for_operation(offset),
                callback);
        scratch.record_result(offset, operation_result);
        return operation_result;
      });

  std::size_t chunk_count = 0;
  for (const auto operation_result : scratch.results()) {
    if (operation_result.status != PlannedExecutionStatus::Executed) {
      TESS_DIAG_EVENT(queued_phase_failure);
      return PlannedExecutionResult{
          operation_result.status,
          chunk_count,
      };
    }
    chunk_count += operation_result.chunk_count;
  }

  if (result.status != PlannedExecutionStatus::Executed) {
    TESS_DIAG_EVENT(queued_phase_failure);
    return PlannedExecutionResult{
        result.status,
        chunk_count,
    };
  }

  return PlannedExecutionResult{
      PlannedExecutionStatus::Executed,
      chunk_count,
  };
}

template <WritePolicy Policy, typename World, typename Fn>
auto execute_phase_deferred_dirty(World& world, const ExecutionPlan& plan,
                                  ExecutionPhase phase,
                                  PlannedDirtyAccumulator& dirty, Fn&& fn)
    -> PlannedExecutionResult {
  const SerialPhaseExecutor executor;
  return execute_phase_deferred_dirty_with<Policy>(executor, world, plan, phase,
                                                   dirty, std::forward<Fn>(fn));
}

}  // namespace tess
