#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstdint>
#include <random>
#include <vector>

// Planning index (audit-2026-08-07 P1): hazard detection and parallel-phase
// grouping consult a chunk-keyed index instead of scanning every accepted
// operation. Both were quadratic in the operation count.
//
// The index is only worth having if it is indistinguishable from the scan it
// replaces, so these tests are differential: they run randomized plans
// through both the indexed path and the original linear predicates and
// require identical answers, including *which* operation is blamed. A test
// that only checked "some conflict was found" would pass even if the index
// blamed the wrong operation, and `conflict_handle` / `conflict_id` are
// reported to callers.
namespace {

struct TerrainTag {};
struct CostTag {};

constexpr std::uint32_t DirtyTerrain = 1u << 0u;
constexpr std::uint32_t DirtyCost = 1u << 1u;

// 4 x 4 chunks: small enough that random chunk sets overlap often.
using TopDown2D =
    tess::Shape<tess::Extent3{128, 64, 1}, tess::Extent3{32, 16, 1}>;
using Schema = tess::FieldSchema<tess::Field<TerrainTag, std::uint16_t>,
                                 tess::Field<CostTag, float>>;
using World = tess::AlwaysResidentWorld<TopDown2D, Schema>;

constexpr std::size_t ChunkCount = 16;

[[nodiscard]] auto random_chunks(std::mt19937& rng)
    -> std::vector<tess::ChunkKey> {
  auto chunks = std::vector<tess::ChunkKey>{};
  auto pick = std::uniform_int_distribution<int>{0, 3};
  for (std::size_t chunk = 0; chunk < ChunkCount; ++chunk) {
    if (pick(rng) == 0) {
      chunks.push_back(tess::ChunkKey{static_cast<std::uint64_t>(chunk)});
    }
  }
  if (chunks.empty()) {
    chunks.push_back(tess::ChunkKey{0});
  }
  return chunks;
}

[[nodiscard]] auto random_field_access(std::mt19937& rng)
    -> tess::FieldAccessDesc {
  auto mask =
      std::uniform_int_distribution<std::uint32_t>{0, DirtyTerrain | DirtyCost};
  const auto reads = mask(rng);
  const auto writes = mask(rng);
  return tess::FieldAccessDesc{reads, writes, writes};
}

// The indexed lookup must return the identical operation the linear scan
// returns -- not merely an operation that also conflicts. The scan reports
// its first match in plan order, so a later-but-also-conflicting answer
// would change `conflict_handle` for callers that pin it.
TEST(TessQueuedPlanningIndex, IndexedHazardMatchesLinearScan) {
  World world;
  auto rng = std::mt19937{20260807};

  for (int trial = 0; trial < 64; ++trial) {
    auto accepted = std::vector<tess::PlannedOperation>{};
    accepted.reserve(48);
    auto index = tess::detail::ChunkOperationIndex{};

    for (std::uint32_t op = 0; op < 48; ++op) {
      auto queued = tess::QueuedOperation{};
      queued.handle = tess::OpHandle{op};
      queued.id = tess::OpId{op};
      queued.field_access = random_field_access(rng);
      queued.write_policy = tess::WritePolicy::UniquePerChunk;

      const auto chunks = random_chunks(rng);
      auto created = tess::PlannedOperation::create(world, queued, chunks);
      ASSERT_EQ(created.status, tess::PlannedOperationCreateStatus::Created);
      auto& candidate = *created.operation;

      const auto* linear = tess::detail::find_hazard(
          {accepted.data(), accepted.size()}, candidate);
      const auto* indexed = tess::detail::find_hazard_indexed(
          index, {accepted.data(), accepted.size()}, candidate);
      ASSERT_EQ(linear, indexed)
          << "trial " << trial << " op " << op
          << ": indexed lookup blamed a different operation";

      if (linear != nullptr) {
        continue;
      }
      index.insert(candidate.chunks(),
                   static_cast<std::uint32_t>(accepted.size()));
      accepted.push_back(std::move(candidate));
    }
  }
}

// `clear` keeps the index's storage for reuse (the caller-owned report
// recycles it every frame), so a stale entry surviving the clear would
// blame an operation that is no longer in the plan.
TEST(TessQueuedPlanningIndex, ClearedIndexReportsNoSharing) {
  auto index = tess::detail::ChunkOperationIndex{};
  const auto chunks =
      std::vector<tess::ChunkKey>{tess::ChunkKey{2}, tess::ChunkKey{5}};
  index.insert(chunks, 0);

  auto visits = 0;
  index.for_each_sharing(chunks, [&](std::uint32_t) { ++visits; });
  ASSERT_EQ(visits, 2);

  index.clear();
  visits = 0;
  index.for_each_sharing(chunks, [&](std::uint32_t) { ++visits; });
  EXPECT_EQ(visits, 0);
}

// Growth rehashes the slot table; every node has to be relinked or lookups
// silently stop finding the operations inserted before the growth.
TEST(TessQueuedPlanningIndex, LookupsSurviveRehash) {
  auto index = tess::detail::ChunkOperationIndex{};
  constexpr std::uint32_t Inserted = 512;
  for (std::uint32_t op = 0; op < Inserted; ++op) {
    const auto chunks =
        std::vector<tess::ChunkKey>{tess::ChunkKey{std::uint64_t{op}}};
    index.insert(chunks, op);
  }

  for (std::uint32_t op = 0; op < Inserted; ++op) {
    const auto chunks =
        std::vector<tess::ChunkKey>{tess::ChunkKey{std::uint64_t{op}}};
    auto found = std::vector<std::uint32_t>{};
    index.for_each_sharing(chunks,
                           [&](std::uint32_t hit) { found.push_back(hit); });
    ASSERT_EQ(found.size(), 1u) << "chunk " << op << " lost across rehash";
    EXPECT_EQ(found.front(), op);
  }
}

// Phase grouping is a second consumer of the index. Its answer is a phase
// layout rather than a pointer, so compare the whole layout against the
// original all-pairs algorithm.
TEST(TessQueuedPlanningIndex, PhaseGroupingMatchesAllPairsScan) {
  auto rng = std::mt19937{20260808};

  for (int trial = 0; trial < 64; ++trial) {
    World world;
    tess::FrameOps ops;
    for (std::uint32_t op = 0; op < 24; ++op) {
      const auto chunks = random_chunks(rng);
      const auto access = random_field_access(rng);
      // Parallel phase planning rejects any policy outside these two, and
      // a rejected plan would exercise none of the grouping loop.
      const auto policy = access.write_mask == 0
                              ? tess::WritePolicy::ReadOnly
                              : tess::WritePolicy::UniquePerChunk;
      (void)ops.update_field(tess::DomainDesc::explicit_chunks(chunks), access,
                             policy);
    }

    const auto report = tess::plan_operations(world, ops.operations());
    const auto& plan = report.plan();
    const auto operations = plan.operations();

    // Reference: the pre-index algorithm, comparing each operation against
    // every member of the open phase.
    auto expected_first = std::vector<std::size_t>{};
    auto expected_count = std::vector<std::size_t>{};
    for (std::size_t i = 0; i < operations.size(); ++i) {
      auto conflicts = expected_first.empty();
      if (!conflicts) {
        const auto first = expected_first.back();
        const auto end = first + expected_count.back();
        for (std::size_t j = first; j < end; ++j) {
          if (tess::detail::parallel_phase_conflict(operations[j],
                                                    operations[i])) {
            conflicts = true;
            break;
          }
        }
      }
      if (conflicts) {
        expected_first.push_back(i);
        expected_count.push_back(1);
      } else {
        ++expected_count.back();
      }
    }

    const auto phases = tess::plan_parallel_execution_phases(plan);
    ASSERT_TRUE(phases.ok());
    ASSERT_EQ(phases.phases().size(), expected_first.size())
        << "trial " << trial << ": phase count diverged";
    for (std::size_t p = 0; p < expected_first.size(); ++p) {
      EXPECT_EQ(phases.phases()[p].first_operation(), expected_first[p])
          << "trial " << trial << " phase " << p;
      EXPECT_EQ(phases.phases()[p].operation_count(), expected_count[p])
          << "trial " << trial << " phase " << p;
    }
  }
}

}  // namespace
