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
  // Five policies and five domains: the fifth of each is deliberately
  // invalid, because otherwise the planner's rejection paths for an
  // unknown write policy and an out-of-range chunk are unreachable and
  // the negative-status space goes untested.
  static constexpr std::uint32_t kPolicies = 5;
  static constexpr std::uint32_t kAccessPatterns = 4;
  static constexpr std::uint32_t kDomains = 5;
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
    // `planned_count()` is defined as the plan's size, so comparing the
    // two cannot fail. The claim worth checking is that the plan holds
    // exactly the rows marked Planned, in the same order.
    {
      std::size_t planned_row = 0;
      for (const auto& row : rows) {
        if (row.status != tess::OperationStatus::Planned) {
          continue;
        }
        if (planned_row >= report_.plan().size()) {
          return violation("the plan holds every planned row",
                           report_.plan().size(), planned_row + 1);
        }
        if (report_.plan().operations()[planned_row].handle.value !=
            row.handle.value) {
          return violation(
              "the plan preserves planned-row order",
              report_.plan().operations()[planned_row].handle.value,
              row.handle.value);
        }
        ++planned_row;
      }
      if (planned_row != report_.plan().size()) {
        return violation("the plan holds only planned rows", planned_row,
                         report_.plan().size());
      }
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
  /// Bitset of the OperationStatus values the sweep produced.
  [[nodiscard]] auto statuses_seen() const -> std::uint32_t {
    return statuses_seen_;
  }

 private:
  static auto write_policy(std::uint32_t index) -> tess::WritePolicy {
    switch (index) {
      case 0:
        return tess::WritePolicy::ReadOnly;
      case 1:
        return tess::WritePolicy::UniquePerChunk;
      case 2:
        return tess::WritePolicy::UniquePerTile;
      case 3:
        return tess::WritePolicy::Unsafe;
      default:
        // Outside the enumerated set: the planner must reject it rather
        // than treat it as one of the valid policies. Representable in
        // the underlying type, so the cast is well defined.
        return static_cast<tess::WritePolicy>(9);
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
      case 2: {
        const tess::ChunkKey keys[] = {kSecond};
        return tess::DomainDesc::explicit_chunks(keys);
      }
      case 3: {
        // Unsorted with a duplicate, so the descriptor's documented
        // sort-and-deduplicate normalization is actually exercised
        // rather than handed input that was already normalized.
        const tess::ChunkKey keys[] = {kSecond, kFirst, kSecond};
        return tess::DomainDesc::explicit_chunks(keys);
      }
      default: {
        // Out of range for the shape: reaches the invalid-domain
        // rejection, which no in-range key can.
        const tess::ChunkKey keys[] = {tess::ChunkKey{9999}};
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
        // mark_dirty synthesizes its own metadata: read-only, with the
        // mask copied into dirty_mask and invalidations. The generated
        // policy and read/write masks are therefore DISCARDED for this
        // kind, so the alphabet's kind x policy x access product is not
        // real here. check_kind_independence() compensates by planning
        // a copy with every kind rewritten to MarkDirty, which is the
        // only way MarkDirty meets arbitrary metadata.
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
      statuses_seen_ |= 1U << static_cast<std::uint32_t>(row.status);
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
    for (std::size_t i = 0; i < phases.size(); ++i) {
      if (phases[i].first_operation() != covered) {
        return violation("a failed phase plan's phases stay contiguous",
                         phases[i].first_operation(), covered);
      }
      covered += phases[i].operation_count();
    }
    if (phases_.failed_operation_index() >= report_.plan().size()) {
      return violation("a failed phase plan names an operation in the plan",
                       phases_.failed_operation_index(), report_.plan().size());
    }
    // The phases built so far are exactly the prefix BEFORE the
    // offending operation. "No more than the plan" would also pass for
    // an arbitrary non-prefix grouping, which is not what is promised.
    if (covered != phases_.failed_operation_index()) {
      return violation("the phases are the prefix before the failure", covered,
                       phases_.failed_operation_index());
    }
    // And the reported policy is the offending operation's own policy,
    // not merely some unsupported value.
    const auto policy = phases_.failed_write_policy();
    const auto& offender =
        report_.plan().operations()[phases_.failed_operation_index()];
    if (policy != offender.write_policy) {
      return violation("the reported policy is the offending operation's",
                       static_cast<std::uint64_t>(policy),
                       static_cast<std::uint64_t>(offender.write_policy));
    }
    if (policy == tess::WritePolicy::ReadOnly ||
        policy == tess::WritePolicy::UniquePerChunk) {
      return violation("the named policy is genuinely unsupported",
                       static_cast<std::uint64_t>(policy), 0);
    }
    return std::nullopt;
  }

  // The planner never reads OperationKind, and the report does not
  // carry it, so rewriting every operation's kind must not change any
  // planning DECISION or diagnostic. The kind itself is deliberately
  // preserved on the accepted operation, so this is scoped to the
  // report and the plan's shape rather than claimed as total
  // independence -- that wider claim would be false.
  //
  // This is the strongest property in the model and nothing tested it
  // before.
  [[nodiscard]] auto check_kind_independence() const
      -> std::optional<property::Violation> {
    if (auto drift = compare_rewritten(tess::OperationKind::BuildFieldProduct);
        drift.has_value()) {
      return drift;
    }
    // MarkDirty specifically: the typed enqueue path collapses its
    // metadata, so this is the only route by which the planner sees a
    // MarkDirty operation carrying an arbitrary policy and read/write
    // masks. Without it, a kind-dependent bug in that corner would go
    // unnoticed however many MarkDirty operations the sweep enqueued.
    return compare_rewritten(tess::OperationKind::MarkDirty);
  }

  [[nodiscard]] auto compare_rewritten(tess::OperationKind kind) const
      -> std::optional<property::Violation> {
    auto rewritten = std::vector<tess::QueuedOperation>(
        ops_.operations().begin(), ops_.operations().end());
    for (auto& op : rewritten) {
      op.kind = kind;
    }
    tess::ExecutionReport mirror;
    (void)tess::plan_operations(world_, rewritten, mirror);

    const auto& rows = report_.operations();
    const auto& mirror_rows = mirror.operations();
    if (rows.size() != mirror_rows.size()) {
      return violation("rewriting the kind preserves the row count",
                       mirror_rows.size(), rows.size());
    }
    // Compare every report field except the source location, which
    // records the enqueue call site and is expected to differ.
    for (std::size_t i = 0; i < rows.size(); ++i) {
      const auto& a = rows[i];
      const auto& b = mirror_rows[i];
      const bool same =
          a.handle.value == b.handle.value && a.id.value == b.id.value &&
          a.status == b.status && a.failure == b.failure &&
          a.access.write_policy == b.access.write_policy &&
          a.access.domain_kind == b.access.domain_kind &&
          a.access.domain_mask == b.access.domain_mask &&
          a.field_access == b.field_access && a.backend == b.backend &&
          a.exactness == b.exactness && a.chunk_count == b.chunk_count &&
          a.has_detail_chunk == b.has_detail_chunk &&
          a.detail_chunk.value == b.detail_chunk.value &&
          a.has_conflict == b.has_conflict &&
          a.conflict_mask == b.conflict_mask &&
          a.conflict_handle.value == b.conflict_handle.value &&
          a.conflict_id.value == b.conflict_id.value;
      if (!same) {
        return violation(
            "planning decisions and diagnostics ignore the operation kind",
            static_cast<std::uint64_t>(a.status),
            static_cast<std::uint64_t>(b.status));
      }
    }

    // The accepted operations must expand to the same chunks in the
    // same order, and each must retain the kind it was given -- the one
    // place the kind is intentionally carried through.
    if (mirror.plan().size() != report_.plan().size()) {
      return violation("rewriting the kind preserves the plan size",
                       mirror.plan().size(), report_.plan().size());
    }
    for (std::size_t i = 0; i < report_.plan().size(); ++i) {
      const auto lhs = report_.plan().operations()[i].chunks();
      const auto rhs = mirror.plan().operations()[i].chunks();
      if (lhs.size() != rhs.size()) {
        return violation("rewriting the kind preserves the expanded chunks",
                         rhs.size(), lhs.size());
      }
      for (std::size_t c = 0; c < lhs.size(); ++c) {
        if (lhs[c].value != rhs[c].value) {
          return violation("rewriting the kind preserves the expanded chunks",
                           rhs[c].value, lhs[c].value);
        }
      }
      // Every planned field except the kind must survive the rewrite.
      // Comparing only the report row would miss a planner that used
      // the kind to alter what an accepted operation carries into
      // execution while leaving its row identical.
      const auto& lhs_op = report_.plan().operations()[i];
      const auto& rhs_op = mirror.plan().operations()[i];
      const bool same_planned =
          lhs_op.handle.value == rhs_op.handle.value &&
          lhs_op.id.value == rhs_op.id.value &&
          lhs_op.access.write_policy == rhs_op.access.write_policy &&
          lhs_op.access.domain_kind == rhs_op.access.domain_kind &&
          lhs_op.access.domain_mask == rhs_op.access.domain_mask &&
          lhs_op.field_access == rhs_op.field_access &&
          lhs_op.write_policy == rhs_op.write_policy &&
          lhs_op.priority == rhs_op.priority &&
          lhs_op.budget_policy == rhs_op.budget_policy &&
          lhs_op.payload.data == rhs_op.payload.data &&
          lhs_op.payload.count == rhs_op.payload.count &&
          lhs_op.payload.item_size == rhs_op.payload.item_size &&
          lhs_op.payload.type_identity == rhs_op.payload.type_identity &&
          lhs_op.backend == rhs_op.backend &&
          lhs_op.exactness == rhs_op.exactness;
      if (!same_planned) {
        return violation(
            "an accepted operation carries the same payload and metadata "
            "whatever its kind",
            static_cast<std::uint64_t>(lhs_op.write_policy),
            static_cast<std::uint64_t>(rhs_op.write_policy));
      }
      if (rhs_op.kind != kind) {
        return violation("an accepted operation keeps the kind it was given",
                         static_cast<std::uint64_t>(rhs_op.kind),
                         static_cast<std::uint64_t>(kind));
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
  std::uint32_t statuses_seen_ = 0;
};

constexpr std::size_t kSteps = 24;
constexpr std::uint64_t kSeeds = 24;

TEST(TessQueuedProperty, PlanningInvariantsHoldUnderRandomSequences) {
  const property::Property<QueuedPlanModel> prop(
      property::current_test_name(), QueuedPlanModel::kOperationCount);

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

TEST(TessQueuedProperty, TheSweepReachesEveryPlanningOutcomeItCanReach) {
  // Each of these gates guards an invariant that would otherwise hold
  // vacuously: no conflict means the hazard rule is never exercised, a
  // single phase means the partition rule is trivial, and an
  // all-supported batch never reaches the prefix-and-diagnostics path.
  const property::Property<QueuedPlanModel> prop(
      property::current_test_name(), QueuedPlanModel::kOperationCount);

  std::size_t hazards = 0;
  std::size_t multi_phase = 0;
  std::size_t unsupported = 0;
  std::uint32_t kinds = 0;
  std::uint32_t statuses = 0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    QueuedPlanModel model;
    for (const auto op : prop.sequence_for(seed, kSteps)) {
      model.apply(op);
    }
    hazards += model.hazard_conflicts();
    multi_phase += model.multi_phase_plans();
    unsupported += model.unsupported_policy_plans();
    kinds |= model.kinds_seen();
    statuses |= model.statuses_seen();
  }

  // Every planning outcome this model CAN reach. InvalidIdentity is
  // absent on purpose and the test name says so: FrameOps always
  // assigns dense identities, so only the span overload can produce a
  // non-dense one. That path is covered by the focused test below
  // rather than pretended to be covered here.
  const auto reached = [statuses](tess::OperationStatus status) {
    return (statuses & (1U << static_cast<std::uint32_t>(status))) != 0U;
  };
  EXPECT_TRUE(reached(tess::OperationStatus::Planned));
  EXPECT_TRUE(reached(tess::OperationStatus::InvalidWritePolicy))
      << "no sequence reached an unknown write policy";
  EXPECT_TRUE(reached(tess::OperationStatus::InvalidDomain))
      << "no sequence reached an out-of-range chunk domain";
  EXPECT_TRUE(reached(tess::OperationStatus::InvalidFieldAccess))
      << "no sequence reached a read-only operation with a write mask";
  EXPECT_FALSE(reached(tess::OperationStatus::InvalidIdentity))
      << "FrameOps produced a non-dense identity, which it must never do";

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

TEST(TessQueuedProperty, TheSpanOverloadRejectsNonDenseIdentity) {
  // The random model cannot reach this: FrameOps assigns handle and id
  // from the enqueue index, so every batch it builds is dense by
  // construction. Density is therefore a FrameOps guarantee, and what
  // the planner promises for arbitrary input is the opposite -- that it
  // REJECTS a non-dense batch rather than trusting it. Covered here so
  // the claim is tested rather than assumed.
  PropertyWorld world;
  tess::ExecutionReport report;

  const tess::ChunkKey keys[] = {tess::ChunkKey{0}};
  auto make = [&keys](std::uint64_t handle, std::uint64_t id) {
    tess::QueuedOperation op;
    op.handle = tess::OpHandle{handle};
    op.id = tess::OpId{id};
    op.domain = tess::DomainDesc::explicit_chunks(keys);
    return op;
  };

  const std::vector<tess::QueuedOperation> dense{make(0, 0), make(1, 1)};
  (void)tess::plan_operations(world, dense, report);
  EXPECT_EQ(report.planned_count(), 2U);

  const std::vector<tess::QueuedOperation> bad_handle{make(0, 0), make(7, 1)};
  (void)tess::plan_operations(world, bad_handle, report);
  ASSERT_EQ(report.operations().size(), 2U);
  EXPECT_EQ(report.operations()[1].status,
            tess::OperationStatus::InvalidIdentity);
  EXPECT_EQ(report.operations()[1].failure,
            tess::OperationFailure::NonDenseHandle);
  // The row still reports the canonical index-derived identity.
  EXPECT_EQ(report.operations()[1].handle.value, 1U);

  const std::vector<tess::QueuedOperation> bad_id{make(0, 0), make(1, 7)};
  (void)tess::plan_operations(world, bad_id, report);
  ASSERT_EQ(report.operations().size(), 2U);
  EXPECT_EQ(report.operations()[1].status,
            tess::OperationStatus::InvalidIdentity);
  EXPECT_EQ(report.operations()[1].failure, tess::OperationFailure::NonDenseId);
}

}  // namespace
