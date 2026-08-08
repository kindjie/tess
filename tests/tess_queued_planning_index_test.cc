#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <array>
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

// 16 x 16 chunks. Wider than `index_max_chunks_per_operation`, so a
// whole-domain operation here is one the planner keeps OUT of the index --
// the case that made the index 834x slower than the scan it replaced
// before that bound existed.
using TopDown2D =
    tess::Shape<tess::Extent3{128, 128, 1}, tess::Extent3{8, 8, 1}>;
using Schema = tess::FieldSchema<tess::Field<TerrainTag, std::uint16_t>,
                                 tess::Field<CostTag, float>>;
using World = tess::AlwaysResidentWorld<TopDown2D, Schema>;

constexpr std::size_t ChunkCount = 256;
static_assert(ChunkCount > tess::detail::index_max_chunks_per_operation,
              "the wide-operation cases below need a domain the index "
              "refuses; raise the world size with the bound");

// Sparse chunk sets, for the phase test. Dense ones make almost every pair
// of mutating operations overlap, every phase a singleton, and the whole
// grouping question trivial -- which is exactly how an earlier revision of
// this file stopped detecting a dropped open-phase filter.
[[nodiscard]] auto sparse_chunks(std::mt19937& rng)
    -> std::vector<tess::ChunkKey> {
  // A deliberately tiny chunk universe. The layout that separates "conflicts
  // with the open phase" from "conflicts with any earlier operation" needs
  // one chunk to reappear across a closed phase -- {A}, {B}, {B}, {A} is the
  // shortest such plan -- and over sixteen chunks that pattern is too rare
  // to turn up. Over four it is routine.
  auto count = std::uniform_int_distribution<std::size_t>{1, 2};
  auto pick = std::uniform_int_distribution<std::uint64_t>{0, 3};
  auto chunks = std::vector<tess::ChunkKey>{};
  for (std::size_t i = 0, wanted = count(rng); i < wanted; ++i) {
    chunks.push_back(tess::ChunkKey{pick(rng)});
  }
  return chunks;
}

// Narrow enough to be indexed, drawn from a small corner of the world so
// that sets still collide often. Sizes straddle the index bound so both
// sides of `operation_is_indexable` appear among the sparse operations.
[[nodiscard]] auto random_chunks(std::mt19937& rng)
    -> std::vector<tess::ChunkKey> {
  auto size = std::uniform_int_distribution<std::size_t>{
      1, tess::detail::index_max_chunks_per_operation + 2};
  auto pick = std::uniform_int_distribution<std::uint64_t>{0, 95};
  auto keys = std::vector<std::uint64_t>{};
  for (std::size_t i = 0, wanted = size(rng); i < wanted; ++i) {
    keys.push_back(pick(rng));
  }
  std::sort(keys.begin(), keys.end());
  keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
  auto chunks = std::vector<tess::ChunkKey>{};
  for (const auto key : keys) {
    chunks.push_back(tess::ChunkKey{key});
  }
  return chunks;
}

// Wider than `index_max_chunks_per_operation`, so the planner keeps it out
// of the index -- the shape a whole-domain selector produces.
[[nodiscard]] auto all_chunks() -> std::vector<tess::ChunkKey> {
  auto chunks = std::vector<tess::ChunkKey>{};
  for (std::size_t chunk = 0; chunk < ChunkCount; ++chunk) {
    chunks.push_back(tess::ChunkKey{static_cast<std::uint64_t>(chunk)});
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

  for (const auto seed : {11u, 29u, 47u, 101u}) {
    auto rng = std::mt19937{seed};
    for (int trial = 0; trial < 16; ++trial) {
      auto accepted = std::vector<tess::PlannedOperation>{};
      accepted.reserve(48);
      auto index = tess::detail::ChunkOperationIndex{};
      auto wide = std::vector<std::uint32_t>{};
      // Some operations are deliberately wider than the index bound, so the
      // mixed case is covered: an indexed candidate must still find a wide
      // conflict, and a wide candidate must fall back to the full scan.
      auto make_wide = std::bernoulli_distribution{0.25};

      for (std::uint32_t op = 0; op < 48; ++op) {
        auto queued = tess::QueuedOperation{};
        queued.handle = tess::OpHandle{op};
        queued.id = tess::OpId{op};
        queued.field_access = random_field_access(rng);
        queued.write_policy = tess::WritePolicy::UniquePerChunk;

        const auto chunks = make_wide(rng) ? all_chunks() : random_chunks(rng);
        auto created = tess::PlannedOperation::create(world, queued, chunks);
        ASSERT_EQ(created.status, tess::PlannedOperationCreateStatus::Created);
        if (!created.operation.has_value()) {
          FAIL() << "creation reported Created without an operation";
          return;
        }
        auto& candidate = *created.operation;

        const auto* linear = tess::detail::find_hazard(
            {accepted.data(), accepted.size()}, candidate);
        const auto* indexed = tess::detail::find_hazard_indexed(
            index, {wide.data(), wide.size()},
            {accepted.data(), accepted.size()}, candidate);
        ASSERT_EQ(linear, indexed)
            << "seed " << seed << " trial " << trial << " op " << op
            << ": indexed lookup blamed a different operation";

        if (linear != nullptr) {
          continue;
        }
        const auto op_index = static_cast<std::uint32_t>(accepted.size());
        if (tess::detail::operation_is_indexable(candidate.chunks())) {
          index.insert(candidate.chunks(), op_index);
        } else {
          wide.push_back(op_index);
        }
        accepted.push_back(std::move(candidate));
      }
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
//
// The operation counts straddle `phase_index_min_operations`: grouping keeps
// the all-pairs comparison below the cutoff, so both branches have to be
// held to the same layout, and a plan that lands exactly on the boundary
// has to pick the indexed one.
//
// Each count runs many plans, not one. An earlier revision ran a single
// plan per count and stopped detecting a dropped open-phase filter: the
// case that separates "conflicts with the open phase" from "conflicts with
// any earlier operation" needs a closed phase behind it, which only turns
// up across enough randomized layouts.
TEST(TessQueuedPlanningIndex, PhaseGroupingMatchesAllPairsScan) {
  static_assert(tess::detail::phase_index_min_operations == 16,
                "the operation counts below are chosen to straddle the "
                "cutoff; update them together with it");
  for (const auto seed : {13u, 31u, 53u, 103u}) {
    auto rng = std::mt19937{seed};
    for (const std::uint32_t op_count : {2u, 8u, 15u, 16u, 17u, 24u, 40u}) {
      for (int trial = 0; trial < 16; ++trial) {
        World world;
        tess::FrameOps ops;
        auto read_only = std::bernoulli_distribution{0.6};
        // A minority of whole-domain operations, so the wide path through
        // grouping is exercised: those are kept out of the index and
        // compared separately, and an indexed candidate must still see
        // them. Without any, that branch is never executed.
        auto make_wide = std::bernoulli_distribution{0.2};
        for (std::uint32_t op = 0; op < op_count; ++op) {
          const auto chunks =
              make_wide(rng) ? all_chunks() : sparse_chunks(rng);
          // Read-only operations never conflict with each other, so a
          // majority of them is what lets a phase hold more than one
          // operation. With dense chunk sets and mostly-mutating policies
          // every phase is a singleton and grouping has nothing to decide.
          auto access = random_field_access(rng);
          if (read_only(rng)) {
            access = tess::FieldAccessDesc{access.read_mask, 0, 0};
          }
          // Parallel phase planning rejects any policy outside these two, and
          // a rejected plan would exercise none of the grouping loop.
          const auto policy = access.write_mask == 0
                                  ? tess::WritePolicy::ReadOnly
                                  : tess::WritePolicy::UniquePerChunk;
          (void)ops.update_field(tess::DomainDesc::explicit_chunks(chunks),
                                 access, policy);
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
            << "seed " << seed << " ops " << op_count << " trial " << trial
            << ": phase count diverged";
        for (std::size_t p = 0; p < expected_first.size(); ++p) {
          EXPECT_EQ(phases.phases()[p].first_operation(), expected_first[p])
              << "seed " << seed << " ops " << op_count << " trial " << trial
              << " phase " << p;
          EXPECT_EQ(phases.phases()[p].operation_count(), expected_count[p])
              << "seed " << seed << " ops " << op_count << " trial " << trial
              << " phase " << p;
        }
      }
    }
  }
}

// The one layout that separates "conflicts with the open phase" from
// "conflicts with any earlier operation", constructed rather than sampled.
// Randomized plans do not reach it often enough to be relied on: dropping
// the open-phase filter survived several thousand random plans.
//
// Chunks {A}, {B}, {B}, {A}, all mutating and pairwise hazard-free so the
// planner accepts every one:
//
//   op0 {A}          opens phase 0
//   op1 {B}          no overlap with op0        -> merges, phase 0 = [0,2)
//   op2 {B}          overlaps op1               -> opens phase 1
//   op3 {A}          no overlap with op2, the only open-phase member
//                    -> merges, phase 1 = [2,4)
//
// op3 does overlap op0, which is in the CLOSED phase 0. A grouping pass
// that consults every earlier operation instead of the open phase alone
// would split op3 into a third phase and serialize work that can run
// together.
TEST(TessQueuedPlanningIndex, ClosedPhaseOverlapDoesNotSplitAPhase) {
  // Two field masks and two chunks give four mutually hazard-free
  // operations; a fifth on either chunk would collide on write masks and be
  // rejected before grouping ever saw it.
  struct Op {
    std::uint64_t chunk;
    std::uint32_t write;
  };
  constexpr auto Pattern = std::array{
      Op{0, DirtyTerrain},
      Op{1, DirtyTerrain},
      Op{1, DirtyCost},
      Op{0, DirtyCost},
  };

  // Run the pattern at both sides of the index cutoff. Below it grouping
  // compares all pairs; at or above it consults the index. The layout must
  // be identical either way, so the pattern is replicated onto disjoint
  // chunk pairs, which never conflict across pairs.
  for (const std::size_t pairs : {std::size_t{1}, std::size_t{8}}) {
    World world;
    tess::FrameOps ops;
    // Interleaved: every pair's step 0, then every pair's step 1, and so
    // on. Emitting the pairs one after another instead would let a pair's
    // step 2 close a phase that a later pair's step 0 then reopens, and the
    // layout would depend on the pair count.
    for (const auto entry : Pattern) {
      for (std::size_t pair = 0; pair < pairs; ++pair) {
        const auto chunks = std::vector<tess::ChunkKey>{
            tess::ChunkKey{entry.chunk + (2 * pair)}};
        (void)ops.update_field(
            tess::DomainDesc::explicit_chunks(chunks),
            tess::FieldAccessDesc{0, entry.write, entry.write},
            tess::WritePolicy::UniquePerChunk);
      }
    }

    const auto report = tess::plan_operations(world, ops.operations());
    const auto& plan = report.plan();
    ASSERT_EQ(plan.operations().size(), pairs * Pattern.size())
        << "pairs " << pairs << ": the planner rejected part of the pattern, "
        << "so grouping never saw the layout under test";

    const auto phases = tess::plan_parallel_execution_phases(plan);
    ASSERT_TRUE(phases.ok());
    // One pair produces exactly the two phases traced above. Eight pairs
    // interleave onto disjoint chunks, which cannot add conflicts, so the
    // count stays at two.
    ASSERT_EQ(phases.phases().size(), 2u)
        << "pairs " << pairs
        << ": an operation overlapping a CLOSED phase split a phase that "
           "should have stayed merged";
    EXPECT_EQ(phases.phases()[0].first_operation(), 0u);
    EXPECT_EQ(phases.phases()[0].operation_count(), 2u * pairs);
    EXPECT_EQ(phases.phases()[1].first_operation(), 2u * pairs);
    EXPECT_EQ(phases.phases()[1].operation_count(), 2u * pairs);
  }
}

}  // namespace
