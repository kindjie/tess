#pragma once

#include <tess/block/block.h>
#include <tess/core/shape.h>
#include <tess/core/tag_identity.h>
#include <tess/diagnostics/diagnostics.h>
#include <tess/diagnostics/trace.h>
#include <tess/ops/phase_executor.h>
#include <tess/storage/world.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <source_location>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace tess {

namespace detail {

struct PlannedWorldStamp {
  std::uintptr_t shape_identity = 0;
  std::uint64_t chunk_limit = 0;
};

template <typename Shape, std::uint64_t ChunkLimit>
[[nodiscard]] inline auto planned_world_stamp() noexcept
    -> const PlannedWorldStamp* {
  static const auto stamp = PlannedWorldStamp{
      tag_identity<Shape>(),
      ChunkLimit,
  };
  return &stamp;
}

template <typename World>
[[nodiscard]] inline auto planned_world_stamp() noexcept
    -> const PlannedWorldStamp* {
  return planned_world_stamp<typename World::shape_type, World::chunk_count>();
}

template <typename World>
[[nodiscard]] inline auto validate_planned_world_stamp(
    const PlannedWorldStamp* stamp) noexcept -> PlannedExecutionStatus {
  const auto* expected = planned_world_stamp<World>();
  if (stamp == expected) {
    return PlannedExecutionStatus::Executed;
  }
  if (stamp == nullptr || stamp->shape_identity != expected->shape_identity) {
    return PlannedExecutionStatus::InvalidShape;
  }
  if (stamp->chunk_limit != expected->chunk_limit) {
    return PlannedExecutionStatus::InvalidChunk;
  }
  return PlannedExecutionStatus::Executed;
}

}  // namespace detail

/** Stable enqueue-order identifier assigned within one `FrameOps` batch. */
struct OpId {
  std::uint64_t value = 0;

  friend constexpr bool operator==(OpId lhs, OpId rhs) noexcept = default;
};

/** Opaque handle used to inspect and receive one queued operation's result. */
struct OpHandle {
  std::uint64_t value = 0;

  friend constexpr bool operator==(OpHandle lhs,
                                   OpHandle rhs) noexcept = default;
};

/** Identifies the work family carried by a queued operation. */
enum class OperationKind : std::uint8_t {
  UpdateField,
};
static_assert(sizeof(OperationKind) == sizeof(std::uint8_t));

/** Expresses scheduler urgency independently of operation order. */
enum class Priority : std::uint8_t {
  Immediate,
  GameplayCritical,
  VisibleSoon,
  Background,
  Maintenance,
};
static_assert(sizeof(Priority) == sizeof(std::uint8_t));

/** Describes whether budget pressure may defer or omit an operation. */
enum class BudgetPolicy : std::uint8_t {
  MustRun,
  CanDefer,
  CanSkipIfSuperseded,
  BudgetedIncremental,
};
static_assert(sizeof(BudgetPolicy) == sizeof(std::uint8_t));

/** Describes planning success or the category of planning rejection. */
enum class OperationStatus : std::uint8_t {
  Planned,
  InvalidIdentity,
  InvalidWritePolicy,
  InvalidDomain,
  InvalidFieldAccess,
  HazardConflict,
};
static_assert(sizeof(OperationStatus) == sizeof(std::uint8_t));

/** Gives the specific reason an operation was rejected during planning. */
enum class OperationFailure : std::uint8_t {
  None,
  NonDenseHandle,
  NonDenseId,
  InvalidWritePolicyValue,
  ExplicitChunkOutOfRange,
  ReadOnlyWriteMask,
  FieldHazardConflict,
};
static_assert(sizeof(OperationFailure) == sizeof(std::uint8_t));

/** Describes whether a parallel phase plan can be executed. */
enum class ExecutionPhaseStatus : std::uint8_t {
  Ready,
  UnsupportedWritePolicy,
};
static_assert(sizeof(ExecutionPhaseStatus) == sizeof(std::uint8_t));

/** Selects how a queued operation expands its chunk domain. */
enum class DomainKind : std::uint8_t {
  ExplicitChunks,
  DirtyChunks,
  ActiveChunks,
  ResidentChunks,
};
static_assert(sizeof(DomainKind) == sizeof(std::uint8_t));

/** Owns a chunk-domain selector and any explicit keys required by it. */
class DomainDesc {
 public:
  // Explicit chunk keys are stored sorted and deduplicated so a planned
  // operation never visits one chunk twice: repeated keys under
  // UniquePerChunk would otherwise defeat the per-chunk ownership rule
  // that parallel phase planning relies on.
  [[nodiscard]] static auto explicit_chunks(std::span<const ChunkKey> keys)
      -> DomainDesc {
    DomainDesc desc{DomainKind::ExplicitChunks};
    desc.explicit_chunks_.assign(keys.begin(), keys.end());
    std::sort(desc.explicit_chunks_.begin(), desc.explicit_chunks_.end(),
              [](ChunkKey lhs, ChunkKey rhs) { return lhs.value < rhs.value; });
    desc.explicit_chunks_.erase(
        std::unique(desc.explicit_chunks_.begin(), desc.explicit_chunks_.end()),
        desc.explicit_chunks_.end());
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

/** Declares read, write, and resulting dirty-field masks for hazard checks. */
struct FieldAccessDesc {
  std::uint32_t read_mask = 0;
  std::uint32_t write_mask = 0;
  std::uint32_t dirty_mask = 0;

  friend constexpr bool operator==(FieldAccessDesc lhs,
                                   FieldAccessDesc rhs) noexcept = default;
};

/** Captures one operation request before domain expansion and validation. */
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

/** Diagnostic snapshot of an operation's write policy and domain selector. */
struct OperationAccess {
  WritePolicy write_policy = WritePolicy::ReadOnly;
  DomainKind domain_kind = DomainKind::ResidentChunks;
  std::uint32_t domain_mask = 0;
};

/** Describes whether manual checked plan construction accepted every key. */
enum class PlannedOperationCreateStatus : std::uint8_t {
  Created,
  InvalidChunk,
};
static_assert(sizeof(PlannedOperationCreateStatus) == sizeof(std::uint8_t));

struct PlannedOperationCreateResult;
class ExecutionReport;
class ExecutionPhase;

/** A planner-validated operation whose chunk list cannot be mutated. */
class PlannedOperation {
 public:
  OperationKind kind = OperationKind::UpdateField;
  OpHandle handle{};
  OpId id{};
  OperationAccess access{};
  FieldAccessDesc field_access{};
  WritePolicy write_policy = WritePolicy::ReadOnly;
  Priority priority = Priority::GameplayCritical;
  BudgetPolicy budget_policy = BudgetPolicy::MustRun;
  // Enqueue-site capture carried through planning so run-time completions
  // (result channels) can report where the operation came from.
  std::source_location source = std::source_location::current();

  /**
   * Creates a checked operation for manual execution.
   *
   * Keys are validated against `world`, then sorted and deduplicated. An
   * invalid key produces no operation and reports the first offending key.
   */
  template <typename World>
  [[nodiscard]] static auto create(const World& world,
                                   const QueuedOperation& operation,
                                   std::span<const ChunkKey> chunks)
      -> PlannedOperationCreateResult;

  /** Returns the validated chunks in ascending order. */
  [[nodiscard]] constexpr auto chunks() const noexcept
      -> std::span<const ChunkKey> {
    return chunks_;
  }

  /** Checks the O(1) shape/chunk stamp against an execution world. */
  template <typename World>
  [[nodiscard]] auto world_validation_status(
      const World& /*world*/) const noexcept -> PlannedExecutionStatus {
    static_assert(
        std::is_same_v<typename World::residency_type, AlwaysResident>,
        "Queued-op validation requires an AlwaysResidentWorld; sparse "
        "queued-ops support is deferred to a later slice.");
    return detail::validate_planned_world_stamp<World>(world_stamp_);
  }

 private:
  friend class ExecutionReport;
  friend class ExecutionPhase;

  PlannedOperation(const QueuedOperation& operation,
                   std::vector<ChunkKey>&& chunks,
                   const detail::PlannedWorldStamp* world_stamp) noexcept
      : kind(operation.kind),
        handle(operation.handle),
        id(operation.id),
        access(OperationAccess{operation.write_policy, operation.domain.kind(),
                               operation.domain.mask()}),
        field_access(operation.field_access),
        write_policy(operation.write_policy),
        priority(operation.priority),
        budget_policy(operation.budget_policy),
        source(operation.source),
        chunks_(std::move(chunks)),
        world_stamp_(world_stamp) {}

  std::vector<ChunkKey> chunks_;
  const detail::PlannedWorldStamp* world_stamp_ = nullptr;
};

/** Result of checked manual `PlannedOperation` creation. */
struct PlannedOperationCreateResult {
  PlannedOperationCreateStatus status =
      PlannedOperationCreateStatus::InvalidChunk;
  std::optional<PlannedOperation> operation;
  ChunkKey invalid_chunk{};
};

template <typename World>
auto PlannedOperation::create(const World& /*world*/,
                              const QueuedOperation& operation,
                              std::span<const ChunkKey> chunks)
    -> PlannedOperationCreateResult {
  static_assert(
      std::is_same_v<typename World::residency_type, AlwaysResident>,
      "Queued operations require an AlwaysResidentWorld; sparse queued-ops "
      "support is deferred to a later slice.");

  for (const auto key : chunks) {
    if (key.value >= World::chunk_count) {
      return PlannedOperationCreateResult{
          PlannedOperationCreateStatus::InvalidChunk,
          std::nullopt,
          key,
      };
    }
  }

  auto validated = std::vector<ChunkKey>{chunks.begin(), chunks.end()};
  std::sort(validated.begin(), validated.end(),
            [](ChunkKey lhs, ChunkKey rhs) { return lhs.value < rhs.value; });
  validated.erase(std::unique(validated.begin(), validated.end()),
                  validated.end());
  auto planned = PlannedOperation{
      operation,
      std::move(validated),
      detail::planned_world_stamp<World>(),
  };
  return PlannedOperationCreateResult{
      PlannedOperationCreateStatus::Created,
      std::optional<PlannedOperation>{std::move(planned)},
      {},
  };
}

/** Immutable ordered view of the operations accepted by one planning pass. */
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
  friend class ExecutionPhase;

  ExecutionPlan() noexcept = default;
  ExecutionPlan(const ExecutionPlan&) = default;
  ExecutionPlan(ExecutionPlan&&) noexcept = default;

  // generation_ is this plan's capability epoch and must never be copied from
  // another plan; assignment invalidates it through bump_generation instead.
  // cppcheck-suppress operatorEqVarError
  auto operator=(const ExecutionPlan& other) -> ExecutionPlan& {
    if (this != &other) {
      // Expire every issued capability before a potentially throwing vector
      // copy. A failed assignment must not leave an old phase authorized for
      // whatever state the vector copy preserved or partially replaced.
      bump_generation();
      operations_ = other.operations_;
    }
    return *this;
  }

  auto operator=(ExecutionPlan&& other) noexcept -> ExecutionPlan& {
    if (this != &other) {
      operations_ = std::move(other.operations_);
      bump_generation();
    }
    return *this;
  }

  constexpr void bump_generation() noexcept { ++generation_; }

  std::vector<PlannedOperation> operations_;
  std::uint64_t generation_ = 0;
};

/**
 * Planner-issued capability naming one executable range in an exact plan.
 *
 * The bound `ExecutionPlan` must outlive this non-owning token. Copies remain
 * bound to that plan generation and expire if its report is reused or
 * replaced.
 */
class ExecutionPhase {
 public:
  ExecutionPhase(const ExecutionPhase&) noexcept = default;
  ExecutionPhase(ExecutionPhase&&) noexcept = default;
  auto operator=(const ExecutionPhase&) noexcept -> ExecutionPhase& = default;
  auto operator=(ExecutionPhase&&) noexcept -> ExecutionPhase& = default;

  [[nodiscard]] constexpr auto first_operation() const noexcept -> std::size_t {
    return first_operation_;
  }

  [[nodiscard]] constexpr auto operation_count() const noexcept -> std::size_t {
    return operation_count_;
  }

  /** Returns whether this capability was issued for `plan`. */
  [[nodiscard]] constexpr bool belongs_to(
      const ExecutionPlan& plan) const noexcept {
    return plan_ == &plan && plan_generation_ == plan.generation_;
  }

  /** Checks the planner-issued world stamp against an execution world. */
  template <typename World>
  [[nodiscard]] auto world_validation_status(
      const World& /*world*/) const noexcept -> PlannedExecutionStatus {
    static_assert(
        std::is_same_v<typename World::residency_type, AlwaysResident>,
        "Queued-op validation requires an AlwaysResidentWorld; sparse "
        "queued-ops support is deferred to a later slice.");
    return detail::validate_planned_world_stamp<World>(world_stamp_);
  }

  /** Returns whether every operation in this phase uses `Policy`. */
  template <WritePolicy Policy>
  [[nodiscard]] constexpr bool policy_matches() const noexcept {
    static_assert(is_valid_write_policy(Policy));
    return write_policy_mask_ == policy_bit(Policy);
  }

 private:
  friend class ExecutionPhasePlan;

  [[nodiscard]] static constexpr auto policy_bit(WritePolicy policy) noexcept
      -> std::uint8_t {
    return static_cast<std::uint8_t>(std::uint8_t{1}
                                     << static_cast<std::uint8_t>(policy));
  }

  constexpr ExecutionPhase(const ExecutionPlan& plan,
                           std::size_t first_operation,
                           std::size_t operation_count,
                           const PlannedOperation& operation) noexcept
      : plan_(&plan),
        first_operation_(first_operation),
        operation_count_(operation_count),
        plan_generation_(plan.generation_),
        world_stamp_(operation.world_stamp_),
        write_policy_mask_(policy_bit(operation.write_policy)) {}

  constexpr void extend(const PlannedOperation& operation) noexcept {
    ++operation_count_;
    write_policy_mask_ |= policy_bit(operation.write_policy);
  }

  const ExecutionPlan* plan_;
  std::size_t first_operation_;
  std::size_t operation_count_;
  std::uint64_t plan_generation_;
  const detail::PlannedWorldStamp* world_stamp_;
  std::uint8_t write_policy_mask_;
};

/** Converts a queued-operation phase into the executor range vocabulary. */
[[nodiscard]] constexpr auto executor_phase_range(
    const ExecutionPhase& phase) noexcept -> ExecutorPhaseRange {
  return ExecutorPhaseRange{
      phase.first_operation(),
      phase.operation_count(),
  };
}

/** Owns deterministic parallel phases or an unsupported-policy diagnostic. */
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

  void push_phase(const ExecutionPlan& plan, std::size_t first_operation,
                  std::size_t operation_count,
                  const PlannedOperation& operation) {
    phases_.push_back(
        ExecutionPhase{plan, first_operation, operation_count, operation});
  }

  void extend_last_phase(const PlannedOperation& operation) {
    phases_.back().extend(operation);
  }

  std::vector<ExecutionPhase> phases_;
  ExecutionPhaseStatus status_ = ExecutionPhaseStatus::Ready;
  std::size_t failed_operation_index_ = 0;
  WritePolicy failed_write_policy_ = WritePolicy::ReadOnly;
};

namespace detail {

[[nodiscard]] constexpr bool execution_phase_valid_for(
    const ExecutionPlan& plan, const ExecutionPhase& phase) noexcept {
  const auto operations = plan.operations();
  const auto first = phase.first_operation();
  const auto count = phase.operation_count();
  return phase.belongs_to(plan) && first <= operations.size() &&
         count <= operations.size() - first;
}

template <WritePolicy Policy, typename World>
[[nodiscard]] auto execution_phase_validation_status(
    const World& world, const ExecutionPlan& plan,
    const ExecutionPhase& phase) noexcept -> PlannedExecutionStatus {
  if (!execution_phase_valid_for(plan, phase)) {
    return PlannedExecutionStatus::InvalidPhase;
  }
  const auto world_status = phase.world_validation_status(world);
  if (world_status != PlannedExecutionStatus::Executed) {
    return world_status;
  }
  if (!phase.template policy_matches<Policy>()) {
    return PlannedExecutionStatus::PolicyMismatch;
  }
  return PlannedExecutionStatus::Executed;
}

inline void record_execution_phase_validation_failure(
    PlannedExecutionStatus status) noexcept {
#if TESS_DIAGNOSTICS_ENABLED
  if (status == PlannedExecutionStatus::InvalidPhase) {
    TESS_DIAG_EVENT(queued_phase_invalid_range);
  } else {
    TESS_DIAG_EVENT(queued_phase_failure);
  }
#else
  (void)status;
#endif
}

}  // namespace detail

/** Records planning outcome, diagnostics, and expanded chunk count. */
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

/** Associates one validated chunk with deferred dirty metadata. */
struct PlannedDirtyRecord {
  ChunkKey chunk{};
  std::uint32_t dirty_mask = 0;
  Box3 bounds{};
};

/** Describes whether deferred dirty recording changed an accumulator. */
enum class PlannedDirtyRecordStatus : std::uint8_t {
  Recorded,
  IgnoredEmptyMask,
  InvalidShape,
  InvalidChunk,
};
static_assert(sizeof(PlannedDirtyRecordStatus) == sizeof(std::uint8_t));

/** Describes whether deferred dirty metadata was safely applied. */
enum class PlannedDirtyMergeStatus : std::uint8_t {
  Merged,
  InvalidShape,
  InvalidChunk,
};
static_assert(sizeof(PlannedDirtyMergeStatus) == sizeof(std::uint8_t));

/** Describes whether dirty partitions were collected without stamp mixing. */
enum class PlannedDirtyCollectStatus : std::uint8_t {
  Collected,
  InvalidShape,
  InvalidChunk,
};
static_assert(sizeof(PlannedDirtyCollectStatus) == sizeof(std::uint8_t));

/** Reports dirty-partition collection status and the transferred records. */
struct PlannedDirtyCollectResult {
  PlannedDirtyCollectStatus status = PlannedDirtyCollectStatus::Collected;
  std::size_t record_count = 0;

  [[nodiscard]] constexpr bool ok() const noexcept {
    return status == PlannedDirtyCollectStatus::Collected;
  }
};

/** Reports dirty-merge status and the number of applied chunks. */
struct PlannedDirtyMergeResult {
  PlannedDirtyMergeStatus status = PlannedDirtyMergeStatus::Merged;
  std::size_t merged_chunk_count = 0;

  [[nodiscard]] constexpr bool ok() const noexcept {
    return status == PlannedDirtyMergeStatus::Merged;
  }
};

class PlannedDirtyAccumulator;
class PlannedPhaseExecutionScratch;

namespace detail {

template <bool BindWorld, WritePolicy Policy, typename World, typename Fn>
auto execute_validated_planned_operation_deferred_dirty(
    World& world, const PlannedOperation& operation,
    PlannedDirtyAccumulator& dirty, Fn&& fn) -> PlannedExecutionResult;

template <typename World>
auto merge_planned_dirty_after_exception(
    World& world, PlannedPhaseExecutionScratch& scratch) noexcept
    -> PlannedDirtyMergeResult;

}  // namespace detail

class PlannedDirtyPartitions;

/** Owns shape-bound deferred dirty records for deterministic later merging. */
class PlannedDirtyAccumulator {
 public:
  void reserve(std::size_t count) { records_.reserve(count); }

  void clear() noexcept {
    records_.clear();
    world_stamp_ = nullptr;
  }

  /** Records a dirty chunk only when it belongs to `world`'s shape. */
  template <typename World>
  auto record(const World& /*world*/, ChunkKey chunk, std::uint32_t dirty_mask,
              Box3 bounds) -> PlannedDirtyRecordStatus {
    static_assert(
        std::is_same_v<typename World::residency_type, AlwaysResident>,
        "Queued-op dirty recording requires an AlwaysResidentWorld; sparse "
        "queued-ops support is deferred to a later slice.");
    if (dirty_mask == 0) {
      return PlannedDirtyRecordStatus::IgnoredEmptyMask;
    }
    if (chunk.value >= World::chunk_count) {
      return PlannedDirtyRecordStatus::InvalidChunk;
    }

    if (world_stamp_ != nullptr) {
      const auto validation =
          detail::validate_planned_world_stamp<World>(world_stamp_);
      if (validation == PlannedExecutionStatus::InvalidShape) {
        return PlannedDirtyRecordStatus::InvalidShape;
      }
      if (validation == PlannedExecutionStatus::InvalidChunk) {
        return PlannedDirtyRecordStatus::InvalidChunk;
      }
    }

    records_.push_back(PlannedDirtyRecord{chunk, dirty_mask, bounds});
    world_stamp_ = detail::planned_world_stamp<World>();
    return PlannedDirtyRecordStatus::Recorded;
  }

  [[nodiscard]] auto records() const noexcept
      -> std::span<const PlannedDirtyRecord> {
    return records_;
  }

  /** Validates this accumulator's O(1) shape/chunk stamp for `world`. */
  template <typename World>
  [[nodiscard]] auto validation_status(const World& /*world*/) const noexcept
      -> PlannedDirtyMergeStatus {
    static_assert(
        std::is_same_v<typename World::residency_type, AlwaysResident>,
        "Queued-op dirty validation requires an AlwaysResidentWorld; sparse "
        "queued-ops support is deferred to a later slice.");
    if (world_stamp_ == nullptr) {
      return PlannedDirtyMergeStatus::Merged;
    }
    const auto validation =
        detail::validate_planned_world_stamp<World>(world_stamp_);
    if (validation == PlannedExecutionStatus::InvalidShape) {
      return PlannedDirtyMergeStatus::InvalidShape;
    }
    if (validation == PlannedExecutionStatus::InvalidChunk) {
      return PlannedDirtyMergeStatus::InvalidChunk;
    }
    return PlannedDirtyMergeStatus::Merged;
  }

 private:
  friend class PlannedDirtyPartitions;

  template <bool BindWorld, WritePolicy Policy, typename World, typename Fn>
  friend auto detail::execute_validated_planned_operation_deferred_dirty(
      World& world, const PlannedOperation& operation,
      PlannedDirtyAccumulator& dirty, Fn&& fn) -> PlannedExecutionResult;

  template <WritePolicy Policy, typename World, typename Fn>
  friend auto execute_planned_operation_deferred_dirty(
      World& world, const PlannedOperation& operation,
      PlannedDirtyAccumulator& dirty, Fn&& fn) -> PlannedExecutionResult;
  template <typename World>
  friend auto merge_planned_dirty(World& world,
                                  PlannedDirtyAccumulator& dirty) noexcept
      -> PlannedDirtyMergeResult;
  template <typename World>
  friend auto merge_planned_dirty(World& world,
                                  PlannedPhaseExecutionScratch& scratch)
      -> PlannedDirtyMergeResult;
  friend auto collect_planned_dirty(PlannedDirtyAccumulator& dirty,
                                    PlannedDirtyPartitions& partitions)
      -> PlannedDirtyCollectResult;

  template <typename World>
  void bind_validated_world(const World& /*world*/) noexcept {
    if (world_stamp_ == nullptr) {
      world_stamp_ = detail::planned_world_stamp<World>();
    }
  }

  template <typename World>
  void prepare_for_validated_world(const World& /*world*/) noexcept {
    records_.clear();
    world_stamp_ = detail::planned_world_stamp<World>();
  }

  void record_validated(ChunkKey chunk, std::uint32_t dirty_mask, Box3 bounds) {
    if (dirty_mask == 0) {
      return;
    }
    records_.push_back(PlannedDirtyRecord{chunk, dirty_mask, bounds});
  }

  std::vector<PlannedDirtyRecord> records_;
  const detail::PlannedWorldStamp* world_stamp_ = nullptr;
};

template <bool BindWorld, WritePolicy Policy, typename World, typename Fn>
auto detail::execute_validated_planned_operation_deferred_dirty(
    World& world, const PlannedOperation& operation,
    PlannedDirtyAccumulator& dirty, Fn&& fn) -> PlannedExecutionResult {
  if constexpr (BindWorld) {
    if (operation.field_access.dirty_mask != 0) {
      dirty.bind_validated_world(world);
    }
  }
  auto ctx = block_ctx<Policy>(world, chunk_domain(operation.chunks()));

  std::size_t chunk_count = 0;
  auto&& callback = fn;
  ctx.for_each_chunk([&](auto view) {
    dirty.record_validated(view.key(), operation.field_access.dirty_mask,
                           view.bounds());
    callback(view);
    ++chunk_count;
  });

  return PlannedExecutionResult{
      PlannedExecutionStatus::Executed,
      chunk_count,
  };
}

/** Owns isolated dirty accumulators for concurrently executed operations. */
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
      -> PlannedDirtyCollectResult;
  friend class PlannedPhaseExecutionScratch;

  void prepare(std::size_t count) {
    partitions_.resize(count);
    for (auto& partition : partitions_) {
      partition.clear();
      partition.reserve(records_per_partition_reserve_);
    }
  }

  template <typename World>
  void prepare(const World& world, std::size_t count) {
    partitions_.resize(count);
    for (auto& partition : partitions_) {
      partition.prepare_for_validated_world(world);
      partition.reserve(records_per_partition_reserve_);
    }
  }

  std::vector<PlannedDirtyAccumulator> partitions_;
  std::size_t records_per_partition_reserve_ = 0;
};

namespace detail {

// Scratch-owned phase partitions carry no independent world stamp. The
// enclosing scratch object owns one capability stamp, and this record-only
// type cannot be passed to the public dirty-merge APIs for another world.
class PhaseDirtyPartition {
 public:
  void reserve(std::size_t count) { records_.reserve(count); }

  void clear() noexcept { records_.clear(); }

  void record(ChunkKey chunk, std::uint32_t dirty_mask, Box3 bounds) {
    if (dirty_mask != 0) {
      records_.push_back(PlannedDirtyRecord{chunk, dirty_mask, bounds});
    }
  }

  [[nodiscard]] auto records() const noexcept
      -> std::span<const PlannedDirtyRecord> {
    return records_;
  }

 private:
  std::vector<PlannedDirtyRecord> records_;
};

template <WritePolicy Policy, typename World, typename Fn>
auto execute_validated_phase_operation_deferred_dirty(
    World& world, const PlannedOperation& operation, PhaseDirtyPartition& dirty,
    Fn&& fn) -> PlannedExecutionResult {
  auto ctx = block_ctx<Policy>(world, chunk_domain(operation.chunks()));

  std::size_t chunk_count = 0;
  auto&& callback = fn;
  ctx.for_each_chunk([&](auto view) {
    dirty.record(view.key(), operation.field_access.dirty_mask, view.bounds());
    callback(view);
    ++chunk_count;
  });

  return PlannedExecutionResult{
      PlannedExecutionStatus::Executed,
      chunk_count,
  };
}

}  // namespace detail

/** Receives typed completion values for queued execution wrappers. */
template <typename T>
class ResultChannel;

/** Reusable partitions, results, and merge storage for one execution phase. */
class PlannedPhaseExecutionScratch {
 public:
  void reserve_operations(std::size_t count) {
    dirty_partitions_.reserve(count);
    results_.reserve(count);
  }

  void reserve_dirty_records_per_operation(std::size_t count) {
    records_per_partition_reserve_ = count;
    for (auto& partition : dirty_partitions_) {
      partition.reserve(count);
    }
  }

  void reserve_merged_dirty_records(std::size_t count) {
    merged_dirty_.reserve(count);
  }

  void prepare_for_operation_count(std::size_t count) { prepare(count); }

  void clear() noexcept {
    for (auto& partition : dirty_partitions_) {
      partition.clear();
    }
    results_.clear();
    merged_dirty_.clear();
    world_stamp_ = nullptr;
  }

  [[nodiscard]] auto operation_count() const noexcept -> std::size_t {
    return results_.size();
  }

  [[nodiscard]] auto dirty_partitions() const noexcept
      -> std::span<const detail::PhaseDirtyPartition> {
    return dirty_partitions_;
  }

 private:
  template <WritePolicy Policy, typename Executor, typename World, typename Fn>
  friend auto execute_phase_partitioned_dirty_with(
      Executor&& executor, World& world, const ExecutionPlan& plan,
      const ExecutionPhase& phase, PlannedPhaseExecutionScratch& scratch,
      Fn&& fn) -> PlannedExecutionResult;

  template <WritePolicy Policy, typename Executor, typename World, typename T,
            typename Fn>
  friend auto execute_phase_partitioned_dirty_with_results(
      Executor&& executor, World& world, const ExecutionPlan& plan,
      const ExecutionPhase& phase, PlannedPhaseExecutionScratch& scratch,
      ResultChannel<T>& channel, Fn&& fn) -> PlannedExecutionResult;

  template <typename World>
  friend auto merge_planned_dirty(World& world,
                                  PlannedPhaseExecutionScratch& scratch)
      -> PlannedDirtyMergeResult;
  template <typename World>
  friend auto detail::merge_planned_dirty_after_exception(
      World& world, PlannedPhaseExecutionScratch& scratch) noexcept
      -> PlannedDirtyMergeResult;

  void prepare(std::size_t operation_count) {
    prepare_partitions(operation_count);
    results_.assign(operation_count, PlannedExecutionResult{});
    merged_dirty_.clear();
    world_stamp_ = nullptr;
  }

  template <typename World>
  void prepare(const World& /*world*/, std::size_t operation_count) {
    prepare_partitions(operation_count);
    results_.assign(operation_count, PlannedExecutionResult{});
    merged_dirty_.clear();
    world_stamp_ = detail::planned_world_stamp<World>();
  }

  [[nodiscard]] auto dirty_for_operation(std::size_t index) noexcept
      -> detail::PhaseDirtyPartition& {
    return dirty_partitions_[index];
  }

  void prepare_partitions(std::size_t operation_count) {
    dirty_partitions_.resize(operation_count);
    for (auto& partition : dirty_partitions_) {
      partition.clear();
      partition.reserve(records_per_partition_reserve_);
    }
  }

  void record_result(std::size_t index, PlannedExecutionResult result) {
    results_[index] = result;
  }

  [[nodiscard]] auto results() const noexcept
      -> std::span<const PlannedExecutionResult> {
    return results_;
  }

  std::vector<detail::PhaseDirtyPartition> dirty_partitions_;
  std::vector<PlannedExecutionResult> results_;
  PlannedDirtyAccumulator merged_dirty_;
  std::size_t records_per_partition_reserve_ = 0;
  const detail::PlannedWorldStamp* world_stamp_ = nullptr;
};

/** Owns ordered planning diagnostics and the corresponding accepted plan. */
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

  // Clears all results while keeping every allocation -- report rows,
  // planned operations, and their chunk lists (parked in a pool) -- so a
  // caller-owned report makes steady-state planning allocation-free
  // (audit 2026-07-11 M4).
  void reset() {
    plan_.bump_generation();
    for (auto& planned : plan_.operations_) {
      planned.chunks_.clear();
      chunk_pool_.push_back(std::move(planned.chunks_));
    }
    plan_.operations_.clear();
    operations_.clear();
  }

 private:
  template <typename World>
  friend auto plan_operations(const World& world,
                              std::span<const QueuedOperation> operations,
                              ExecutionReport& report)
      -> const ExecutionReport&;

  void reserve(std::size_t size) {
    operations_.reserve(size);
    plan_.operations_.reserve(size);
  }

  void push_report(OperationReport report) { operations_.push_back(report); }

  void push_planned(PlannedOperation planned) {
    plan_.operations_.push_back(std::move(planned));
  }

  template <typename World>
  [[nodiscard]] auto make_planned(const QueuedOperation& operation,
                                  std::vector<ChunkKey>&& chunks)
      -> PlannedOperation {
    return PlannedOperation{
        operation,
        std::move(chunks),
        detail::planned_world_stamp<World>(),
    };
  }

  [[nodiscard]] auto acquire_chunks() -> std::vector<ChunkKey> {
    if (chunk_pool_.empty()) {
      return {};
    }
    auto chunks = std::move(chunk_pool_.back());
    chunk_pool_.pop_back();
    return chunks;
  }

  void recycle_chunks(std::vector<ChunkKey>&& chunks) {
    chunks.clear();
    chunk_pool_.push_back(std::move(chunks));
  }

  void recycle_chunks(PlannedOperation&& planned) {
    recycle_chunks(std::move(planned.chunks_));
  }

  std::vector<OperationReport> operations_;
  ExecutionPlan plan_;
  std::vector<std::vector<ChunkKey>> chunk_pool_;
};

/** Collects one frame's operations while assigning stable handles and IDs. */
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

  // Clears queued operations for per-frame reuse while keeping the enqueue
  // vector's capacity, so warm frame loops re-enqueue without allocating.
  // Previously returned handles are invalidated; handle and id assignment
  // restarts at zero on the next enqueue.
  void clear() noexcept { operations_.clear(); }

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

// Fills `chunks` in place (clearing it first) so a caller-supplied vector
// keeps its capacity across plans instead of being replaced by a fresh
// allocation per operation (audit 2026-07-11 M4).
template <typename World>
[[nodiscard]] auto expand_domain(const World& world, const DomainDesc& domain,
                                 std::vector<ChunkKey>& chunks,
                                 ChunkKey& invalid_chunk) -> bool {
  chunks.clear();
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
      world.collect_dirty_chunks(domain.mask(), chunks);
      return true;
    case DomainKind::ActiveChunks:
      world.collect_active_chunks(domain.mask(), chunks);
      return true;
    case DomainKind::ResidentChunks:
      chunks.reserve(static_cast<std::size_t>(World::chunk_count));
      for (std::uint64_t key = 0; key < World::chunk_count; ++key) {
        chunks.push_back(ChunkKey{key});
      }
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
    if (chunks_overlap(earlier.chunks(), later.chunks())) {
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
  if (!chunks_overlap(lhs.chunks(), rhs.chunks())) {
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
  // Saturating: an unguarded origin + int64(extent) is UB for huge
  // caller-supplied extents (audit 2026-07-11 C1); share chunk_meta's
  // guarded helper.
  return detail::box_axis_end(origin, extent);
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
  // end >= origin, but a saturated INT64_MAX end paired with a negative
  // origin spans more than int64 can hold, so the subtraction must happen
  // in unsigned space (mirrors chunk_meta's union; audit 2026-07-11 C1).
  return abs_delta(end, origin);
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

/**
 * Validates and expands operations into a caller-owned reusable report.
 * Existing report and chunk-list capacity is retained for warm planning.
 */
template <typename World>
auto plan_operations(const World& world,
                     std::span<const QueuedOperation> operations,
                     ExecutionReport& report) -> const ExecutionReport& {
  // Queued operations are not yet sparse-aware: expand_domain's ResidentChunks
  // case enumerates 0..chunk_count (an OOM on a huge sparse world, and it would
  // yield non-resident keys the executor then writes through), so restrict the
  // whole planner to always-resident worlds until the sparse queued-ops slice
  // ports it. This fails loudly at compile time rather than silently OOMing --
  // matching how every other deferred sparse-unsafe family is guarded.
  static_assert(
      std::is_same_v<typename World::residency_type, AlwaysResident>,
      "Queued operations require an AlwaysResidentWorld; sparse queued-ops "
      "support is deferred to a later slice.");
  report.reset();
  report.reserve(operations.size());

  for (std::size_t op_index = 0; op_index < operations.size(); ++op_index) {
    const auto& op = operations[op_index];
    const auto canonical_handle =
        OpHandle{static_cast<std::uint64_t>(op_index)};
    const auto canonical_id = OpId{static_cast<std::uint64_t>(op_index)};
    OperationReport op_report{
        canonical_handle,
        canonical_id,
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

    if (op.handle != canonical_handle || op.id != canonical_id) {
      op_report.status = OperationStatus::InvalidIdentity;
      op_report.failure = op.handle != canonical_handle
                              ? OperationFailure::NonDenseHandle
                              : OperationFailure::NonDenseId;
      TESS_DIAG_TRACE_VALUE(diagnostics::TraceCategory::Planner,
                            "invalid_identity", op_index);
      report.push_report(op_report);
      continue;
    }

    if (!is_valid_write_policy(op.write_policy)) {
      op_report.status = OperationStatus::InvalidWritePolicy;
      op_report.failure = OperationFailure::InvalidWritePolicyValue;
      TESS_DIAG_TRACE_VALUE(diagnostics::TraceCategory::Planner,
                            "invalid_write_policy", op_index);
      report.push_report(op_report);
      continue;
    }

    if (!detail::is_valid_field_access(op.write_policy, op.field_access)) {
      op_report.status = OperationStatus::InvalidFieldAccess;
      op_report.failure = OperationFailure::ReadOnlyWriteMask;
      TESS_DIAG_TRACE_VALUE(diagnostics::TraceCategory::Planner,
                            "invalid_field_access", op_index);
      report.push_report(op_report);
      continue;
    }

    auto planned_chunks = report.acquire_chunks();
    ChunkKey invalid_chunk{};
    if (!detail::expand_domain(world, op.domain, planned_chunks,
                               invalid_chunk)) {
      op_report.status = OperationStatus::InvalidDomain;
      op_report.failure = OperationFailure::ExplicitChunkOutOfRange;
      op_report.detail_chunk = invalid_chunk;
      op_report.has_detail_chunk = true;
      TESS_DIAG_TRACE_VALUE(diagnostics::TraceCategory::Planner,
                            "invalid_domain", op_index);
      report.recycle_chunks(std::move(planned_chunks));
      report.push_report(op_report);
      continue;
    }

    auto planned =
        report.template make_planned<World>(op, std::move(planned_chunks));

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
      op_report.chunk_count = planned.chunks().size();
      TESS_DIAG_TRACE_VALUE(diagnostics::TraceCategory::Planner, "conflict",
                            op_index);
      report.recycle_chunks(std::move(planned));
      report.push_report(op_report);
      continue;
    }

    op_report.chunk_count = planned.chunks().size();
    TESS_DIAG_TRACE_VALUE(diagnostics::TraceCategory::Planner, "planned",
                          op_index);
    report.push_planned(std::move(planned));
    report.push_report(op_report);
  }

  return report;
}

template <typename World>
/** Validates operations and returns a newly allocated execution report. */
[[nodiscard]] auto plan_operations(const World& world,
                                   std::span<const QueuedOperation> operations)
    -> ExecutionReport {
  ExecutionReport report;
  plan_operations(world, operations, report);
  return report;
}

template <typename World>
/** Validates frame operations into caller-owned reusable report storage. */
auto plan_operations(const World& world, const FrameOps& ops,
                     ExecutionReport& report) -> const ExecutionReport& {
  return plan_operations(world, ops.operations(), report);
}

template <typename World>
/** Validates frame operations and returns a newly allocated report. */
[[nodiscard]] auto plan_operations(const World& world, const FrameOps& ops)
    -> ExecutionReport {
  return plan_operations(world, ops.operations());
}

/** Adapts one immutable planned chunk list to the block-domain API. */
[[nodiscard]] constexpr auto planned_chunk_domain(
    const PlannedOperation& operation) noexcept -> ChunkDomain {
  return chunk_domain(operation.chunks());
}

/** Groups a validated plan into deterministic non-conflicting phases. */
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
      TESS_DIAG_TRACE_VALUE(diagnostics::TraceCategory::Planner,
                            "unsupported_write_policy", i);
      return phases;
    }

    if (phases.phases_.empty()) {
      TESS_DIAG_TRACE_VALUE(diagnostics::TraceCategory::Planner, "new_phase",
                            i);
      phases.push_phase(plan, i, 1, operation);
      continue;
    }

    const auto& phase = phases.phases_.back();
    auto conflicts = false;
    const auto end = phase.first_operation() + phase.operation_count();
    for (std::size_t j = phase.first_operation(); j < end; ++j) {
      if (detail::parallel_phase_conflict(operations[j], operation)) {
        conflicts = true;
        break;
      }
    }

    if (conflicts) {
      TESS_DIAG_TRACE_VALUE(diagnostics::TraceCategory::Planner, "new_phase",
                            i);
      phases.push_phase(plan, i, 1, operation);
    } else {
      TESS_DIAG_TRACE_VALUE(diagnostics::TraceCategory::Planner, "merged", i);
      phases.extend_last_phase(operation);
    }
  }

  return phases;
}

/**
 * Coalesces and applies deferred dirty records after validating their world
 * stamp. Rejected merges leave both the world and accumulator unchanged.
 */
template <typename World>
auto merge_planned_dirty(World& world, PlannedDirtyAccumulator& dirty) noexcept
    -> PlannedDirtyMergeResult {
  static_assert(std::is_same_v<typename World::residency_type, AlwaysResident>,
                "Queued-op dirty merge requires an AlwaysResidentWorld; sparse "
                "queued-ops support is deferred to a later slice.");

  const auto validation = dirty.validation_status(world);
  if (validation != PlannedDirtyMergeStatus::Merged) {
    return PlannedDirtyMergeResult{
        validation,
        0,
    };
  }

  auto& records = dirty.records_;
  std::sort(records.begin(), records.end(),
            [](PlannedDirtyRecord lhs, PlannedDirtyRecord rhs) {
              return lhs.chunk.value < rhs.chunk.value;
            });

  // Sorting makes duplicate records adjacent so they can be coalesced without
  // allocating. This noexcept path cannot replace a callback exception while
  // AutoExec unwinds.
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
  return PlannedDirtyMergeResult{
      PlannedDirtyMergeStatus::Merged,
      merged_count,
  };
}

/** Moves compatible operation partitions into one reusable accumulator. */
inline auto collect_planned_dirty(PlannedDirtyAccumulator& dirty,
                                  PlannedDirtyPartitions& partitions)
    -> PlannedDirtyCollectResult {
  auto* world_stamp = dirty.world_stamp_;
  for (const auto& partition : partitions.partitions_) {
    const auto* partition_stamp = partition.world_stamp_;
    if (partition_stamp == nullptr) {
      continue;
    }
    if (world_stamp == nullptr) {
      world_stamp = partition_stamp;
      continue;
    }
    if (world_stamp->shape_identity != partition_stamp->shape_identity) {
      return PlannedDirtyCollectResult{
          PlannedDirtyCollectStatus::InvalidShape,
          0,
      };
    }
    if (world_stamp->chunk_limit != partition_stamp->chunk_limit) {
      return PlannedDirtyCollectResult{
          PlannedDirtyCollectStatus::InvalidChunk,
          0,
      };
    }
  }

  auto required_capacity = dirty.records_.size();
  auto record_count = std::size_t{0};
  for (const auto& partition : partitions.partitions_) {
    const auto partition_size = partition.records_.size();
    if (partition_size > dirty.records_.max_size() - required_capacity) {
      throw std::length_error{"planned dirty record count exceeds max_size"};
    }
    required_capacity += partition_size;
    record_count += partition_size;
  }
  // Complete the only potentially allocating step before clearing a source;
  // a failed reserve therefore preserves both destination and partitions.
  dirty.records_.reserve(required_capacity);

  for (auto& partition : partitions.partitions_) {
    if (partition.world_stamp_ != nullptr) {
      dirty.world_stamp_ = partition.world_stamp_;
    }
    dirty.records_.insert(dirty.records_.end(), partition.records_.begin(),
                          partition.records_.end());
    partition.clear();
  }
  TESS_DIAG_EVENT_VALUE(queued_dirty_collect, record_count);
  return PlannedDirtyCollectResult{
      PlannedDirtyCollectStatus::Collected,
      record_count,
  };
}

template <typename World>
/** Collects partitioned dirty records and applies their merged bounds. */
auto merge_planned_dirty(World& world, PlannedDirtyPartitions& partitions,
                         PlannedDirtyAccumulator& dirty_scratch)
    -> PlannedDirtyMergeResult {
  for (const auto& partition : partitions.partitions()) {
    const auto validation = partition.validation_status(world);
    if (validation != PlannedDirtyMergeStatus::Merged) {
      return PlannedDirtyMergeResult{validation, 0};
    }
  }
  dirty_scratch.clear();
  const auto collected = collect_planned_dirty(dirty_scratch, partitions);
  if (!collected.ok()) {
    return PlannedDirtyMergeResult{
        collected.status == PlannedDirtyCollectStatus::InvalidShape
            ? PlannedDirtyMergeStatus::InvalidShape
            : PlannedDirtyMergeStatus::InvalidChunk,
        0,
    };
  }
  return merge_planned_dirty(world, dirty_scratch);
}

template <typename World>
/** Applies phase dirty records through the scratch object's accumulator. */
auto merge_planned_dirty(World& world, PlannedPhaseExecutionScratch& scratch)
    -> PlannedDirtyMergeResult {
  if (scratch.world_stamp_ == nullptr) {
    return PlannedDirtyMergeResult{
        PlannedDirtyMergeStatus::Merged,
        0,
    };
  }
  const auto validation =
      detail::validate_planned_world_stamp<World>(scratch.world_stamp_);
  if (validation != PlannedExecutionStatus::Executed) {
    return PlannedDirtyMergeResult{
        validation == PlannedExecutionStatus::InvalidShape
            ? PlannedDirtyMergeStatus::InvalidShape
            : PlannedDirtyMergeStatus::InvalidChunk,
        0,
    };
  }

  auto& merged = scratch.merged_dirty_;
  auto record_count = std::size_t{0};
  for (const auto& partition : scratch.dirty_partitions_) {
    const auto partition_size = partition.records().size();
    if (partition_size > merged.records_.max_size() - record_count) {
      throw std::length_error{"planned dirty record count exceeds max_size"};
    }
    record_count += partition_size;
  }
  merged.clear();
  // Reserve before transferring anything, so allocation failure leaves every
  // phase partition available to the caller.
  merged.records_.reserve(record_count);
  merged.world_stamp_ = scratch.world_stamp_;
  for (auto& partition : scratch.dirty_partitions_) {
    const auto records = partition.records();
    merged.records_.insert(merged.records_.end(), records.begin(),
                           records.end());
    partition.clear();
  }
  TESS_DIAG_EVENT_VALUE(queued_dirty_collect, record_count);
  (void)record_count;
  return merge_planned_dirty(world, merged);
}

/** Conservatively applies started phase records while unwinding a callback. */
template <typename World>
auto detail::merge_planned_dirty_after_exception(
    World& world, PlannedPhaseExecutionScratch& scratch) noexcept
    -> PlannedDirtyMergeResult {
  if (scratch.world_stamp_ == nullptr) {
    return PlannedDirtyMergeResult{
        PlannedDirtyMergeStatus::Merged,
        0,
    };
  }
  const auto validation =
      detail::validate_planned_world_stamp<World>(scratch.world_stamp_);
  if (validation != PlannedExecutionStatus::Executed) {
    return PlannedDirtyMergeResult{
        validation == PlannedExecutionStatus::InvalidShape
            ? PlannedDirtyMergeStatus::InvalidShape
            : PlannedDirtyMergeStatus::InvalidChunk,
        0,
    };
  }

  // This cold path must preserve the original callback exception. Coalesce
  // with an allocation-free quadratic scan: exceptions are rare, and normal
  // phase sizes should not dictate whether failed work remains observable.
  auto record_count = std::size_t{0};
  for (const auto& partition : scratch.dirty_partitions_) {
    const auto partition_size = partition.records().size();
    if (partition_size >
        std::numeric_limits<std::size_t>::max() - record_count) {
      record_count = std::numeric_limits<std::size_t>::max();
      break;
    }
    record_count += partition_size;
  }
  auto merged_count = std::size_t{0};
  for (std::size_t partition_index = 0;
       partition_index < scratch.dirty_partitions_.size(); ++partition_index) {
    const auto records = scratch.dirty_partitions_[partition_index].records();
    for (std::size_t record_index = 0; record_index < records.size();
         ++record_index) {
      const auto record = records[record_index];
      auto appeared_earlier = false;
      for (std::size_t earlier_partition = 0;
           earlier_partition <= partition_index && !appeared_earlier;
           ++earlier_partition) {
        const auto earlier_records =
            scratch.dirty_partitions_[earlier_partition].records();
        const auto earlier_count = earlier_partition == partition_index
                                       ? record_index
                                       : earlier_records.size();
        for (std::size_t earlier_index = 0; earlier_index < earlier_count;
             ++earlier_index) {
          if (earlier_records[earlier_index].chunk == record.chunk) {
            appeared_earlier = true;
            break;
          }
        }
      }
      if (appeared_earlier) {
        continue;
      }

      auto dirty_mask = record.dirty_mask;
      auto bounds = record.bounds;
      for (std::size_t later_partition = partition_index;
           later_partition < scratch.dirty_partitions_.size();
           ++later_partition) {
        const auto later_records =
            scratch.dirty_partitions_[later_partition].records();
        const auto first_later = later_partition == partition_index
                                     ? record_index + 1
                                     : std::size_t{0};
        for (std::size_t later_index = first_later;
             later_index < later_records.size(); ++later_index) {
          const auto later = later_records[later_index];
          if (later.chunk == record.chunk) {
            dirty_mask |= later.dirty_mask;
            bounds = detail::union_dirty_bounds(bounds, later.bounds);
          }
        }
      }
      world.mark_dirty(record.chunk, dirty_mask, bounds);
      ++merged_count;
    }
  }
  for (auto& partition : scratch.dirty_partitions_) {
    partition.clear();
  }
  TESS_DIAG_EVENT_VALUE(queued_dirty_collect, record_count);
  TESS_DIAG_EVENT_VALUE(queued_dirty_merge, merged_count);
  (void)record_count;
  (void)merged_count;
  return PlannedDirtyMergeResult{
      PlannedDirtyMergeStatus::Merged,
      merged_count,
  };
}

/** Reports whether a plan's runtime write policy matches `Policy`. */
template <WritePolicy Policy>
[[nodiscard]] constexpr bool planned_policy_matches(
    const PlannedOperation& operation) noexcept {
  return operation.write_policy == Policy;
}

/** Validates a plan's world binding, chunk bound, and write policy in O(1). */
template <WritePolicy Policy, typename World>
[[nodiscard]] auto validate_planned_operation(
    const World& world, const PlannedOperation& operation) noexcept
    -> PlannedExecutionStatus {
  const auto world_status = operation.world_validation_status(world);
  if (world_status != PlannedExecutionStatus::Executed) {
    return world_status;
  }
  if (!planned_policy_matches<Policy>(operation)) {
    return PlannedExecutionStatus::PolicyMismatch;
  }
  return PlannedExecutionStatus::Executed;
}

/** Returns a block context only when plan validation succeeds. */
template <WritePolicy Policy, typename World>
[[nodiscard]] constexpr auto try_planned_block_ctx(
    World& world, const PlannedOperation& operation) noexcept
    -> std::optional<BlockCtx<World, Policy>> {
  static_assert(
      std::is_same_v<typename World::residency_type, AlwaysResident>,
      "Queued-op execution requires an AlwaysResidentWorld; sparse queued-ops "
      "support is deferred to a later slice.");
  if (validate_planned_operation<Policy>(world, operation) !=
      PlannedExecutionStatus::Executed) {
    return std::nullopt;
  }
  return block_ctx<Policy>(world, planned_chunk_domain(operation));
}

/** Executes one validated operation and applies dirty metadata immediately. */
template <WritePolicy Policy, typename World, typename Fn>
auto execute_planned_operation(World& world, const PlannedOperation& operation,
                               Fn&& fn) -> PlannedExecutionResult {
  const auto validation = validate_planned_operation<Policy>(world, operation);
  if (validation != PlannedExecutionStatus::Executed) {
    return PlannedExecutionResult{
        validation,
        0,
    };
  }
  auto ctx = block_ctx<Policy>(world, planned_chunk_domain(operation));

  std::size_t chunk_count = 0;
  auto&& callback = fn;
  ctx.for_each_chunk([&](auto view) {
    if (operation.field_access.dirty_mask != 0) {
      world.mark_dirty(view.key(), operation.field_access.dirty_mask,
                       view.bounds());
    }
    callback(view);
    ++chunk_count;
  });

  return PlannedExecutionResult{
      PlannedExecutionStatus::Executed,
      chunk_count,
  };
}

/** Executes one validated operation while deferring dirty metadata. */
template <WritePolicy Policy, typename World, typename Fn>
auto execute_planned_operation_deferred_dirty(World& world,
                                              const PlannedOperation& operation,
                                              PlannedDirtyAccumulator& dirty,
                                              Fn&& fn)
    -> PlannedExecutionResult {
  const auto validation = validate_planned_operation<Policy>(world, operation);
  if (validation != PlannedExecutionStatus::Executed) {
    return PlannedExecutionResult{
        validation,
        0,
    };
  }
  if (operation.field_access.dirty_mask != 0) {
    const auto dirty_validation = dirty.validation_status(world);
    if (dirty_validation != PlannedDirtyMergeStatus::Merged) {
      return PlannedExecutionResult{
          dirty_validation == PlannedDirtyMergeStatus::InvalidShape
              ? PlannedExecutionStatus::InvalidShape
              : PlannedExecutionStatus::InvalidChunk,
          0,
      };
    }
  }
  return detail::execute_validated_planned_operation_deferred_dirty<true,
                                                                    Policy>(
      world, operation, dirty, std::forward<Fn>(fn));
}

/**
 * Executes a plan in order and stops at its first rejected operation.
 * Earlier writes remain applied and are included in the returned chunk count.
 */
template <WritePolicy Policy, typename World, typename Fn>
auto execute_plan(World& world, const ExecutionPlan& plan, Fn&& fn)
    -> PlannedExecutionResult {
  std::size_t chunk_count = 0;
  auto&& callback = fn;
  for (const auto& operation : plan.operations()) {
    auto result = execute_planned_operation<Policy>(world, operation, callback);
    if (result.status != PlannedExecutionStatus::Executed) {
      return PlannedExecutionResult{
          result.status,
          chunk_count + result.chunk_count,
      };
    }
    chunk_count += result.chunk_count;
  }
  return PlannedExecutionResult{
      PlannedExecutionStatus::Executed,
      chunk_count,
  };
}

/** Executes a plan in order while accumulating dirty metadata for later. */
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
      return PlannedExecutionResult{
          result.status,
          chunk_count + result.chunk_count,
      };
    }
    chunk_count += result.chunk_count;
  }
  return PlannedExecutionResult{
      PlannedExecutionStatus::Executed,
      chunk_count,
  };
}

template <WritePolicy Policy, typename Executor, typename World, typename Fn>
  requires SerialExecutor<Executor>
/** Executes a serial phase without per-operation dirty partitions. */
auto execute_phase_deferred_dirty_with(Executor&& executor, World& world,
                                       const ExecutionPlan& plan,
                                       const ExecutionPhase& phase,
                                       PlannedDirtyAccumulator& dirty, Fn&& fn)
    -> PlannedExecutionResult {
  const auto operations = plan.operations();
  const auto phase_validation =
      detail::execution_phase_validation_status<Policy>(world, plan, phase);
  if (phase_validation != PlannedExecutionStatus::Executed) {
    detail::record_execution_phase_validation_failure(phase_validation);
    return PlannedExecutionResult{
        phase_validation,
        0,
    };
  }
  const auto dirty_validation = dirty.validation_status(world);
  if (dirty_validation != PlannedDirtyMergeStatus::Merged) {
    TESS_DIAG_EVENT(queued_phase_failure);
    return PlannedExecutionResult{
        dirty_validation == PlannedDirtyMergeStatus::InvalidShape
            ? PlannedExecutionStatus::InvalidShape
            : PlannedExecutionStatus::InvalidChunk,
        0,
    };
  }

  TESS_DIAG_EVENT_VALUE(queued_phase_execute, phase.operation_count());
  std::size_t chunk_count = 0;
  auto&& callback = fn;
  auto result = execute_operation_index_range(
      std::forward<Executor>(executor), executor_phase_range(phase),
      [&](std::size_t index) {
        auto operation_result =
            detail::execute_validated_planned_operation_deferred_dirty<true,
                                                                       Policy>(
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

/** Executes a phase with operation-local dirty partitions for concurrency. */
template <WritePolicy Policy, typename Executor, typename World, typename Fn>
auto execute_phase_partitioned_dirty_with(Executor&& executor, World& world,
                                          const ExecutionPlan& plan,
                                          const ExecutionPhase& phase,
                                          PlannedPhaseExecutionScratch& scratch,
                                          Fn&& fn) -> PlannedExecutionResult {
  const auto operations = plan.operations();
  const auto phase_validation =
      detail::execution_phase_validation_status<Policy>(world, plan, phase);
  if (phase_validation != PlannedExecutionStatus::Executed) {
    detail::record_execution_phase_validation_failure(phase_validation);
    return PlannedExecutionResult{
        phase_validation,
        0,
    };
  }

  TESS_DIAG_EVENT_VALUE(queued_phase_execute, phase.operation_count());
  TESS_DIAG_EVENT_VALUE(queued_partitioned_phase, phase.operation_count());
  scratch.prepare(world, phase.operation_count());
  auto&& callback = fn;
  auto result = execute_operation_index_range(
      std::forward<Executor>(executor), executor_phase_range(phase),
      [&](std::size_t index) {
        const auto offset = index - phase.first_operation();
        auto operation_result =
            detail::execute_validated_phase_operation_deferred_dirty<Policy>(
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

/** Executes a phase serially while deferring dirty metadata. */
template <WritePolicy Policy, typename World, typename Fn>
auto execute_phase_deferred_dirty(World& world, const ExecutionPlan& plan,
                                  const ExecutionPhase& phase,
                                  PlannedDirtyAccumulator& dirty, Fn&& fn)
    -> PlannedExecutionResult {
  const SerialPhaseExecutor executor;
  return execute_phase_deferred_dirty_with<Policy>(executor, world, plan, phase,
                                                   dirty, std::forward<Fn>(fn));
}

}  // namespace tess
