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

// Deferred dirty recording and merging is a different state machine
// from planning, which is why it is not folded into the planning model:
// planning creates no dirty records at all, so a merge, a zero-mask
// rejection and a coalesced apply are unreachable from an
// enqueue-and-plan sequence. This model drives the accumulator
// directly.

struct TerrainTag {};

using DirtyShape =
    tess::Shape<tess::Extent3{64, 32, 1}, tess::Extent3{16, 16, 1}>;
using DirtySchema = tess::FieldSchema<tess::Field<TerrainTag, std::uint16_t>>;
using DirtyWorld = tess::AlwaysResidentWorld<DirtyShape, DirtySchema>;

/// Random record/merge/collect sequences over a dirty accumulator.
class DirtyMergeModel {
 public:
  // Fewer chunks than records, so records collide and coalescing is
  // reachable; masks include zero, because a zero mask must be ignored
  // rather than recorded and nothing else would exercise that.
  static constexpr std::uint32_t kChunks = 3;
  static constexpr std::uint32_t kMasks = 3;
  static constexpr std::uint32_t kActions = 8;
  static constexpr std::size_t kPartitions = 2;
  static constexpr std::uint32_t kOperationCount = kChunks * kMasks * kActions;

  void apply(std::uint32_t op) {
    const auto chunk = op % kChunks;
    const auto mask_index = op / kChunks % kMasks;
    switch (op / (kChunks * kMasks) % kActions) {
      case 0:
      case 1:
      case 2:
        record(chunk, mask_index);
        break;
      case 3:
        record_out_of_range(mask_index);
        break;
      case 4:
        merge();
        break;
      case 5:
        record_into_partition(chunk, mask_index);
        break;
      case 6:
        collect();
        break;
      default:
        dirty_.clear();
        expected_.clear();
        partitions_.clear_records();
        partitioned_.clear();
        break;
    }
  }

  [[nodiscard]] auto check() const -> std::optional<property::Violation> {
    // A zero mask is never recorded, so the accumulator holds exactly
    // the non-empty records the model handed it.
    if (dirty_.records().size() != expected_.size()) {
      return violation("the accumulator holds every accepted record",
                       dirty_.records().size(), expected_.size());
    }
    for (const auto& record : dirty_.records()) {
      if (record.dirty_mask == 0) {
        return violation("a zero dirty mask is never recorded", 0, 1);
      }
    }
    if (out_of_range_recorded_) {
      return violation("an out-of-range chunk is rejected without recording", 1,
                       0);
    }
    if (merge_miscounted_) {
      return violation(
          "a merge reports the number of DISTINCT chunks it applied", 1, 0);
    }
    if (merge_left_records_) {
      return violation("a successful merge clears the accumulator", 1, 0);
    }
    if (collect_lost_records_) {
      return violation(
          "collection moves every partition record into the accumulator and "
          "empties the partitions",
          1, 0);
    }
    return std::nullopt;
  }

  [[nodiscard]] auto merges() const -> std::size_t { return merges_; }
  [[nodiscard]] auto coalesced_merges() const -> std::size_t {
    return coalesced_merges_;
  }
  [[nodiscard]] auto empty_masks() const -> std::size_t { return empty_masks_; }
  [[nodiscard]] auto out_of_range_rejections() const -> std::size_t {
    return out_of_range_rejections_;
  }
  [[nodiscard]] auto collects_with_records() const -> std::size_t {
    return collects_with_records_;
  }

 private:
  static auto violation(const char* name, std::uint64_t observed,
                        std::uint64_t expected)
      -> std::optional<property::Violation> {
    std::ostringstream detail;
    detail << "observed " << observed << ", expected " << expected;
    return property::Violation{name, detail.str(), 0};
  }

  static auto mask_for(std::uint32_t index) -> std::uint32_t {
    // Index 0 is deliberately zero: the ignore-empty-mask rule is
    // unreachable otherwise.
    switch (index) {
      case 0:
        return 0;
      case 1:
        return 0b01;
      default:
        return 0b10;
    }
  }

  void record(std::uint32_t chunk, std::uint32_t mask_index) {
    const auto mask = mask_for(mask_index);
    const auto status = dirty_.record(
        world_, tess::ChunkKey{chunk}, mask,
        tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}});
    if (mask == 0) {
      ++empty_masks_;
      return;
    }
    if (status == tess::PlannedDirtyRecordStatus::Recorded) {
      expected_.push_back(chunk);
    }
  }

  void record_out_of_range(std::uint32_t mask_index) {
    const auto mask = mask_for(mask_index);
    if (mask == 0) {
      return;
    }
    const auto before = dirty_.records().size();
    const auto status = dirty_.record(
        world_, tess::ChunkKey{9999}, mask,
        tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}});
    if (status == tess::PlannedDirtyRecordStatus::Recorded ||
        dirty_.records().size() != before) {
      out_of_range_recorded_ = true;
      return;
    }
    ++out_of_range_rejections_;
  }

  void merge() {
    // The number of DISTINCT chunks among the pending records is what a
    // merge must report -- coalescing means the record count is an
    // upper bound, not the answer.
    auto distinct = expected_;
    std::sort(distinct.begin(), distinct.end());
    distinct.erase(std::unique(distinct.begin(), distinct.end()),
                   distinct.end());
    const auto pending_records = expected_.size();

    const auto result = tess::merge_planned_dirty(world_, dirty_);
    if (!result.ok()) {
      return;
    }
    ++merges_;
    if (result.merged_chunk_count != distinct.size()) {
      merge_miscounted_ = true;
    }
    if (pending_records > distinct.size()) {
      ++coalesced_merges_;
    }
    if (!dirty_.records().empty()) {
      merge_left_records_ = true;
    }
    expected_.clear();
  }

  // Records into one per-operation partition, as a phase execution
  // would. Collection later drains these into the shared accumulator.
  void record_into_partition(std::uint32_t chunk, std::uint32_t mask_index) {
    const auto mask = mask_for(mask_index);
    if (partitions_.size() == 0) {
      partitions_.resize(kPartitions);
    }
    const auto index = chunk % kPartitions;
    const auto status = partitions_.partition(index).record(
        world_, tess::ChunkKey{chunk}, mask,
        tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}});
    if (mask == 0) {
      ++empty_masks_;
      return;
    }
    if (status == tess::PlannedDirtyRecordStatus::Recorded) {
      partitioned_.push_back(chunk);
    }
  }

  // Collection moves records FROM the partitions INTO the shared
  // accumulator -- the opposite direction from the first draft, which
  // the harness caught and shrank to the two-operation sequence 15,51.
  // The record count must be conserved: every partition record arrives,
  // none is lost or duplicated, and the partitions are left empty.
  void collect() {
    if (partitions_.size() == 0) {
      return;
    }
    std::size_t pending = 0;
    for (const auto& partition : partitions_.partitions()) {
      pending += partition.records().size();
    }
    const auto before = dirty_.records().size();
    const auto result = tess::collect_planned_dirty(dirty_, partitions_);
    if (!result.ok()) {
      return;
    }
    ++collects_;
    std::size_t left = 0;
    for (const auto& partition : partitions_.partitions()) {
      left += partition.records().size();
    }
    if (result.record_count != pending ||
        dirty_.records().size() != before + pending || left != 0) {
      collect_lost_records_ = true;
    }
    if (pending != 0) {
      ++collects_with_records_;
    }
    for (const auto chunk : partitioned_) {
      expected_.push_back(chunk);
    }
    partitioned_.clear();
  }

  DirtyWorld world_{};
  tess::PlannedDirtyAccumulator dirty_{};
  tess::PlannedDirtyPartitions partitions_{};
  std::vector<std::uint32_t> expected_;
  std::vector<std::uint32_t> partitioned_;
  std::size_t merges_ = 0;
  std::size_t coalesced_merges_ = 0;
  std::size_t empty_masks_ = 0;
  std::size_t out_of_range_rejections_ = 0;
  std::size_t collects_ = 0;
  std::size_t collects_with_records_ = 0;
  bool out_of_range_recorded_ = false;
  bool merge_miscounted_ = false;
  bool merge_left_records_ = false;
  bool collect_lost_records_ = false;
};

constexpr std::size_t kSteps = 48;
constexpr std::uint64_t kSeeds = 24;

TEST(TessDirtyProperty, MergeInvariantsHoldUnderRandomSequences) {
  const property::Property<DirtyMergeModel> prop(
      property::current_test_name(), DirtyMergeModel::kOperationCount);

  const auto request =
      property::replay_from_environment(DirtyMergeModel::kOperationCount);
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

TEST(TessDirtyProperty, TheSweepReachesEveryMergePath) {
  const property::Property<DirtyMergeModel> prop(
      property::current_test_name(), DirtyMergeModel::kOperationCount);

  std::size_t merges = 0;
  std::size_t coalesced = 0;
  std::size_t empty_masks = 0;
  std::size_t out_of_range = 0;
  std::size_t collects_with_records = 0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    DirtyMergeModel model;
    for (const auto op : prop.sequence_for(seed, kSteps)) {
      model.apply(op);
    }
    merges += model.merges();
    coalesced += model.coalesced_merges();
    empty_masks += model.empty_masks();
    out_of_range += model.out_of_range_rejections();
    collects_with_records += model.collects_with_records();
  }

  EXPECT_GT(merges, 0U) << "the sweep never merged";
  EXPECT_GT(coalesced, 0U)
      << "no merge ever coalesced two records onto one chunk, so the "
         "distinct-chunk count was always just the record count and the "
         "coalescing rule was never tested";
  EXPECT_GT(empty_masks, 0U)
      << "the sweep never offered a zero mask, so the ignore-empty rule was "
         "never exercised";
  EXPECT_GT(out_of_range, 0U)
      << "the sweep never offered an out-of-range chunk";
  EXPECT_GT(collects_with_records, 0U)
      << "every collection ran against EMPTY partitions, so record "
         "conservation was never actually tested";
}

}  // namespace
