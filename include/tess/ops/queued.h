#pragma once

#include <tess/block/block.h>
#include <tess/core/shape.h>
#include <tess/storage/world.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <source_location>
#include <span>
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
};
static_assert(sizeof(OperationStatus) == sizeof(std::uint8_t));

enum class OperationFailure : std::uint8_t {
  None,
  InvalidWritePolicyValue,
  ExplicitChunkOutOfRange,
  ReadOnlyWriteMask,
};
static_assert(sizeof(OperationFailure) == sizeof(std::uint8_t));

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

struct OperationReport {
  OpHandle handle{};
  OpId id{};
  OperationStatus status = OperationStatus::Planned;
  OperationFailure failure = OperationFailure::None;
  OperationAccess access{};
  FieldAccessDesc field_access{};
  ChunkKey detail_chunk{};
  bool has_detail_chunk = false;
  std::size_t chunk_count = 0;
  std::source_location source = std::source_location::current();
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

  void push_report(OperationReport report) {
    operations_.push_back(std::move(report));
  }

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

}  // namespace tess
