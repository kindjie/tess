#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "property_harness.h"

namespace {

namespace property = tess_test::property;

// Queued operations had almost no mixed-kind coverage: the existing
// tests enqueue update_field at 90 call sites and mark_dirty at four,
// and all nine kinds appear together in exactly one test, once. Nothing
// drove a randomized MIX of kinds, so a planner that treated one kind
// differently from another had nothing looking for it.

struct TerrainTag {};
struct CostTag {};

using PropertyShape =
    tess::Shape<tess::Extent3{64, 32, 1}, tess::Extent3{32, 16, 1}>;
using PropertySchema = tess::FieldSchema<tess::Field<TerrainTag, std::uint16_t>,
                                         tess::Field<CostTag, float>>;
using PropertyWorld = tess::AlwaysResidentWorld<PropertyShape, PropertySchema>;

/// Random enqueue/plan sequences over the nine operation kinds.
///
/// Every step enqueues one operation and replans the whole batch, so an
/// invariant that only breaks for a particular ORDER of kinds and
/// policies is exercised, not just a particular operation.
class QueuedPlanModel {
 public:
  // The alphabet has to be able to reach the states the invariants
  // describe. Field-access masks are part of it because a hazard
  // conflict requires intersecting masks: without them the hazard rule
  // below would hold vacuously for every sequence.
  static constexpr std::uint32_t kKinds = 9;
  static constexpr std::uint32_t kPolicies = 4;
  static constexpr std::uint32_t kAccessPatterns = 4;
  static constexpr std::uint32_t kDomains = 3;
  static constexpr std::uint32_t kOperationCount =
      kKinds * kPolicies * kAccessPatterns * kDomains;

  void apply(std::uint32_t op) {
    const auto kind = op % kKinds;
    const auto policy = op / kKinds % kPolicies;
    const auto access = op / (kKinds * kPolicies) % kAccessPatterns;
    const auto domain = op / (kKinds * kPolicies * kAccessPatterns) % kDomains;

    enqueue(static_cast<tess::OperationKind>(kind), write_policy(policy),
            field_access(access), domain_desc(domain));
    plan();
  }

  [[nodiscard]] auto check() const -> std::optional<property::Violation> {
    const auto& rows = report_.operations();
    // One row per input, in input order, with canonical identity.
    if (rows.size() != ops_.operations().size()) {
      return violation("one report row per queued operation", rows.size(),
                       ops_.operations().size());
    }
    for (std::size_t i = 0; i < rows.size(); ++i) {
      if (rows[i].handle.value != i || rows[i].id.value != i) {
        return violation("report identity is the enqueue index",
                         rows[i].handle.value, i);
      }
    }
    if (report_.planned_count() + report_.failed_count() != rows.size()) {
      return violation("planned + failed accounts for every row",
                       report_.planned_count() + report_.failed_count(),
                       rows.size());
    }
    if (report_.plan().size() != report_.planned_count()) {
      return violation("the plan holds exactly the planned operations",
                       report_.plan().size(), report_.planned_count());
    }

    for (const auto& planned : report_.plan().operations()) {
      const auto chunks = planned.chunks();
      for (std::size_t i = 1; i < chunks.size(); ++i) {
        // Ascending AND deduplicated: the per-chunk ownership rule that
        // parallel phases rely on is defeated by a repeated key.
        if (chunks[i - 1].value >= chunks[i].value) {
          return violation("planned chunks ascend without repeats",
                           chunks[i - 1].value, chunks[i].value);
        }
      }
    }

    for (const auto& row : rows) {
      // Only a hazard rejection carries conflict diagnostics; identity,
      // policy, field-access and domain rejections do not.
      if (row.has_conflict &&
          row.status != tess::OperationStatus::HazardConflict) {
        return violation("only a hazard conflict carries conflict diagnostics",
                         static_cast<std::uint64_t>(row.status), 0);
      }
      // A read-only operation that declares a write mask is a
      // contradiction the planner must reject.
      if (row.access.write_policy == tess::WritePolicy::ReadOnly &&
          row.field_access.write_mask != 0 &&
          row.status == tess::OperationStatus::Planned) {
        return violation("a read-only operation cannot declare a write mask",
                         row.field_access.write_mask, 0);
      }
    }

    if (auto phases = check_phases(); phases.has_value()) {
      return phases;
    }
    return check_kind_independence();
  }

  /// Counters proving the sweep reaches the states these invariants
  /// describe, rather than holding over batches that never conflict.
  [[nodiscard]] auto hazard_conflicts() const -> std::size_t {
    return hazard_conflicts_;
  }
  [[nodiscard]] auto multi_phase_plans() const -> std::size_t {
    return multi_phase_plans_;
  }
  [[nodiscard]] auto unsupported_policy_plans() const -> std::size_t {
    return unsupported_policy_plans_;
  }
  [[nodiscard]] auto kinds_seen() const -> std::uint32_t { return kinds_seen_; }

 private:
  static auto write_policy(std::uint32_t index) -> tess::WritePolicy {
    switch (index) {
      case 0:
        return tess::WritePolicy::ReadOnly;
      case 1:
        return tess::WritePolicy::UniquePerChunk;
      case 2:
        return tess::WritePolicy::UniquePerTile;
      default:
        return tess::WritePolicy::Unsafe;
    }
  }

  // Patterns chosen to produce every hazard relation: read/read (no
  // conflict), write/read, write/write on the same field, and two
  // writes on disjoint fields (no hazard, but still a phase split).
  static auto field_access(std::uint32_t index) -> tess::FieldAccessDesc {
    switch (index) {
      case 0:
        return {};
      case 1:
        return {.read_mask = 0b01, .write_mask = 0, .dirty_mask = 0};
      case 2:
        return {.read_mask = 0, .write_mask = 0b01, .dirty_mask = 0b01};
      default:
        return {.read_mask = 0, .write_mask = 0b10, .dirty_mask = 0b10};
    }
  }

  // Overlapping and disjoint chunk sets, because a hazard needs both an
  // intersecting mask and an overlapping domain.
  static auto domain_desc(std::uint32_t index) -> tess::DomainDesc {
    static constexpr tess::ChunkKey kFirst{0};
    static constexpr tess::ChunkKey kSecond{1};
    switch (index) {
      case 0: {
        const tess::ChunkKey keys[] = {kFirst};
        return tess::DomainDesc::explicit_chunks(keys);
      }
      case 1: {
        const tess::ChunkKey keys[] = {kFirst, kSecond};
        return tess::DomainDesc::explicit_chunks(keys);
      }
      default: {
        const tess::ChunkKey keys[] = {kSecond};
        return tess::DomainDesc::explicit_chunks(keys);
      }
    }
  }

  void enqueue(tess::OperationKind kind, tess::WritePolicy policy,
               tess::FieldAccessDesc access, tess::DomainDesc domain) {
    kinds_seen_ |= 1U << static_cast<std::uint32_t>(kind);
    auto metadata = tess::IntentMetadata{};
    metadata.domain = domain;
    metadata.field_access = access;
    metadata.write_policy = policy;

    switch (kind) {
      case tess::OperationKind::UpdateField:
        (void)ops_.update_field(std::move(domain), access, policy);
        break;
      case tess::OperationKind::QueryPaths:
        (void)ops_.query_paths(
            tess::PathBatchDesc::from(payload(), std::move(metadata)));
        break;
      case tess::OperationKind::QueryNearest:
        (void)ops_.query_nearest(
            tess::NearestBatchDesc::from(payload(), std::move(metadata)));
        break;
      case tess::OperationKind::BuildFieldProduct:
        (void)ops_.build_field_product(
            tess::FieldProductDesc::from(payload(), std::move(metadata)));
        break;
      case tess::OperationKind::MoveEntities:
        (void)ops_.move_entities(
            tess::MoveBatchDesc::from(payload(), std::move(metadata)));
        break;
      case tess::OperationKind::RebuildTopology:
        (void)ops_.rebuild_topology(
            tess::TopologyRebuildDesc{std::move(metadata)});
        break;
      case tess::OperationKind::EnsureResident:
        (void)ops_.ensure_resident(
            tess::ResidencyDesc::from(payload(), std::move(metadata)));
        break;
      case tess::OperationKind::MarkDirty:
        // mark_dirty synthesizes its own metadata from the mask, so it
        // cannot carry an arbitrary access descriptor.
        (void)ops_.mark_dirty(
            tess::MarkDirtyDesc{std::move(domain), access.dirty_mask});
        break;
      default:
        (void)ops_.publish_render_deltas(
            tess::RenderDeltaDesc::from(payload(), std::move(metadata)));
        break;
    }
  }

  auto payload() -> std::span<std::uint32_t> {
    return {payload_.data(), payload_.size()};
  }

  void plan() {
    (void)tess::plan_operations(world_, ops_.operations(), report_);
    phases_ = tess::plan_parallel_execution_phases(report_.plan());

    for (const auto& row : report_.operations()) {
      if (row.status == tess::OperationStatus::HazardConflict) {
        ++hazard_conflicts_;
      }
    }
    if (phases_.ok() && phases_.phases().size() > 1) {
      ++multi_phase_plans_;
    }
    if (!phases_.ok()) {
      ++unsupported_policy_plans_;
    }
  }

  [[nodiscard]] auto check_phases() const
      -> std::optional<property::Violation> {
    const auto& phases = phases_.phases();
    if (phases_.ok()) {
      // A successful phase plan partitions the plan contiguously.
      std::size_t covered = 0;
      for (std::size_t i = 0; i < phases.size(); ++i) {
        if (phases[i].first_operation() != covered) {
          return violation("phases partition the plan contiguously",
                           phases[i].first_operation(), covered);
        }
        covered += phases[i].operation_count();
      }
      if (covered != report_.plan().size()) {
        return violation("phases cover the whole plan", covered,
                         report_.plan().size());
      }
      return std::nullopt;
    }

    // A rejected phase plan keeps the phases it built as a PREFIX, and
    // names the operation and policy that stopped it. Asserting a full
    // partition here would be wrong: only ReadOnly and UniquePerChunk
    // are supported, so any UniquePerTile or Unsafe operation stops
    // planning part-way through by design.
    if (phases_.status() !=
        tess::ExecutionPhaseStatus::UnsupportedWritePolicy) {
      return violation("a failed phase plan reports an unsupported policy",
                       static_cast<std::uint64_t>(phases_.status()), 0);
    }
    std::size_t covered = 0;
    for (const auto& phase : phases) {
      covered += phase.operation_count();
    }
    if (covered > report_.plan().size()) {
      return violation("a failed phase plan covers at most the plan", covered,
                       report_.plan().size());
    }
    if (phases_.failed_operation_index() >= report_.plan().size()) {
      return violation("a failed phase plan names an operation in the plan",
                       phases_.failed_operation_index(), report_.plan().size());
    }
    const auto policy = phases_.failed_write_policy();
    if (policy == tess::WritePolicy::ReadOnly ||
        policy == tess::WritePolicy::UniquePerChunk) {
      return violation("the named policy is genuinely unsupported",
                       static_cast<std::uint64_t>(policy), 0);
    }
    return std::nullopt;
  }

  // The planner never reads OperationKind, and the report does not even
  // carry it. So rewriting every operation's kind must not change a
  // single planning outcome. This is the strongest claim in the model
  // and nothing tested it before.
  [[nodiscard]] auto check_kind_independence() const
      -> std::optional<property::Violation> {
    auto rewritten = std::vector<tess::QueuedOperation>(
        ops_.operations().begin(), ops_.operations().end());
    for (auto& op : rewritten) {
      op.kind = tess::OperationKind::BuildFieldProduct;
    }
    tess::ExecutionReport mirror;
    (void)tess::plan_operations(world_, rewritten, mirror);

    const auto& rows = report_.operations();
    const auto& mirror_rows = mirror.operations();
    if (rows.size() != mirror_rows.size()) {
      return violation("rewriting the kind preserves the row count",
                       mirror_rows.size(), rows.size());
    }
    for (std::size_t i = 0; i < rows.size(); ++i) {
      if (rows[i].status != mirror_rows[i].status ||
          rows[i].failure != mirror_rows[i].failure ||
          rows[i].chunk_count != mirror_rows[i].chunk_count ||
          rows[i].has_conflict != mirror_rows[i].has_conflict ||
          rows[i].conflict_mask != mirror_rows[i].conflict_mask ||
          rows[i].conflict_handle.value !=
              mirror_rows[i].conflict_handle.value) {
        return violation("planning is independent of the operation kind",
                         static_cast<std::uint64_t>(rows[i].status),
                         static_cast<std::uint64_t>(mirror_rows[i].status));
      }
    }
    if (mirror.planned_count() != report_.planned_count()) {
      return violation("rewriting the kind preserves the planned count",
                       mirror.planned_count(), report_.planned_count());
    }
    return std::nullopt;
  }

  static auto violation(const char* name, std::uint64_t observed,
                        std::uint64_t expected)
      -> std::optional<property::Violation> {
    std::ostringstream detail;
    detail << "observed " << observed << ", expected " << expected;
    return property::Violation{name, detail.str(), 0};
  }

  PropertyWorld world_{};
  tess::FrameOps ops_{};
  tess::ExecutionReport report_{};
  tess::ExecutionPhasePlan phases_{};
  std::vector<std::uint32_t> payload_ = std::vector<std::uint32_t>(4, 0);
  std::size_t hazard_conflicts_ = 0;
  std::size_t multi_phase_plans_ = 0;
  std::size_t unsupported_policy_plans_ = 0;
  std::uint32_t kinds_seen_ = 0;
};

constexpr std::size_t kSteps = 24;
constexpr std::uint64_t kSeeds = 24;

TEST(TessQueuedProperty, PlanningInvariantsHoldUnderRandomSequences) {
  const property::Property<QueuedPlanModel> prop(
      "TessQueuedProperty.Planning", QueuedPlanModel::kOperationCount);

  const auto request =
      property::replay_from_environment(QueuedPlanModel::kOperationCount);
  if (request.present) {
    if (!request.error.empty()) {
      FAIL() << request.error;
    }
    const auto violation = prop.replay(request.sequence);
    EXPECT_FALSE(violation.has_value())
        << "replayed sequence still fails: "
        << (violation ? violation->invariant : "");
    return;
  }

  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const auto failing = prop.run(seed, kSteps);
    if (failing.has_value()) {
      FAIL() << "seed " << seed << "\n" << prop.report(*failing);
    }
  }
}

TEST(TessQueuedProperty, TheSweepReachesEveryPlanningOutcome) {
  // Each of these gates guards an invariant that would otherwise hold
  // vacuously: no conflict means the hazard rule is never exercised, a
  // single phase means the partition rule is trivial, and an
  // all-supported batch never reaches the prefix-and-diagnostics path.
  const property::Property<QueuedPlanModel> prop(
      "TessQueuedProperty.Planning", QueuedPlanModel::kOperationCount);

  std::size_t hazards = 0;
  std::size_t multi_phase = 0;
  std::size_t unsupported = 0;
  std::uint32_t kinds = 0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    QueuedPlanModel model;
    for (const auto op : prop.sequence_for(seed, kSteps)) {
      model.apply(op);
    }
    hazards += model.hazard_conflicts();
    multi_phase += model.multi_phase_plans();
    unsupported += model.unsupported_policy_plans();
    kinds |= model.kinds_seen();
  }

  EXPECT_GT(hazards, 0U) << "no sequence produced a hazard conflict, so the "
                            "hazard and conflict-diagnostic rules were never "
                            "actually tested";
  EXPECT_GT(multi_phase, 0U)
      << "every phase plan was a single phase, so the partition rule was "
         "trivially satisfied";
  EXPECT_GT(unsupported, 0U)
      << "no sequence reached an unsupported write policy, so the "
         "prefix-and-diagnostics path was never tested";
  EXPECT_EQ(kinds, (1U << QueuedPlanModel::kKinds) - 1U)
      << "the sweep did not enqueue all nine operation kinds, so the "
         "kind-independence property was only tested over a subset";
}

}  // namespace
