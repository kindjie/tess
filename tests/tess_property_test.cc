#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "property_harness.h"

namespace {

namespace property = tess_test::property;

// Residency and schedule ticks had no seeded coverage at all before
// this: their tests drive fixed hand-written sequences, so an
// invariant that only breaks on an unusual interleaving had nothing
// looking for it. Both expose invariants the library already
// computes, which is what makes them worth driving randomly.

struct ResidencyTag {};
using ResidencyShape =
    tess::Shape<tess::Extent3{256, 256, 1}, tess::Extent3{32, 32, 1}>;
using ResidencySchema =
    tess::FieldSchema<tess::Field<ResidencyTag, std::uint32_t>>;
using ResidencyWorld =
    tess::SparseResidentWorld<ResidencyShape, ResidencySchema>;

/// Random ensure/touch/evict/mark-dirty over a budgeted sparse world.
class ResidencyModel {
 public:
  static constexpr std::uint32_t kOperationCount = 12;

  ResidencyModel()
      : world_(tess::ResidencyConfig{kBudgetChunks *
                                     ResidencyWorld::page_byte_size}) {}

  void apply(std::uint32_t op) {
    // Low bits pick the action, high bits pick the chunk, so the
    // operation encoding stays a single integer the shrinker can drop.
    const auto action = op % 4;
    const auto key = tess::ChunkKey{op / 4 % kKeySpace};
    switch (action) {
      case 0: {
        const auto generation_before = world_.residency_generation(key);
        (void)world_.ensure_resident(key);
        const auto generation_after = world_.residency_generation(key);
        if (generation_before != 0 && generation_after != generation_before) {
          reload_without_evict_ = true;
        }
        highest_generation_ = std::max(highest_generation_, generation_after);
        break;
      }
      case 1:
        (void)world_.touch(key);
        break;
      case 2:
        (void)world_.evict(key);
        break;
      default:
        if (world_.is_resident(key)) {
          world_.mark_dirty(
              key, 1U,
              tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}});
        }
        break;
    }
  }

  [[nodiscard]] auto check() const -> std::optional<property::Violation> {
    if (world_.resident_count() > world_.capacity()) {
      return violation("resident_count <= capacity", world_.resident_count(),
                       world_.capacity());
    }
    if (world_.resident_byte_size() > world_.byte_budget()) {
      return violation("resident_byte_size <= byte_budget",
                       world_.resident_byte_size(), world_.byte_budget());
    }
    // A resident chunk always carries a nonzero generation, and an
    // absent one always reports zero: that pairing is what lets a
    // handle detect its own staleness.
    for (const auto key : world_.resident_chunk_keys()) {
      if (world_.residency_generation(key) == 0) {
        return violation("resident chunk has a nonzero generation", key.value,
                         0);
      }
    }
    if (reload_without_evict_) {
      return violation(
          "ensure_resident on a resident chunk keeps its generation", 1, 0);
    }
    return std::nullopt;
  }

 private:
  static constexpr std::size_t kBudgetChunks = 4;
  static constexpr std::uint64_t kKeySpace = 16;

  static auto violation(const char* name, std::uint64_t observed,
                        std::uint64_t bound)
      -> std::optional<property::Violation> {
    std::ostringstream detail;
    detail << "observed " << observed << ", bound " << bound;
    return property::Violation{name, detail.str(), 0};
  }

  ResidencyWorld world_;
  std::uint64_t highest_generation_ = 0;
  bool reload_without_evict_ = false;
};

/// Random tick/notify sequences over a sealed schedule.
class ScheduleModel {
 public:
  static constexpr std::uint32_t kOperationCount = 8;

  ScheduleModel() {
    schedule_.reserve_tasks(3);
    (void)schedule_.add_task(
        {"every", tess::SimPhase::PreUpdate, tess::Cadence::every_tick()},
        every_);
    (void)schedule_.add_task(
        {"dirty", tess::SimPhase::Pathing, tess::Cadence::on_dirty(kMask)},
        dirty_);
    (void)schedule_.add_task(
        {"event", tess::SimPhase::Movement, tess::Cadence::on_event(kMask)},
        event_);
    schedule_.seal();
  }

  void apply(std::uint32_t op) {
    switch (op % 4) {
      case 0: {
        const auto stats = schedule_.run_tick(clock_);
        last_ = stats;
        if (stats.tick <= previous_tick_ && ticked_) {
          tick_regressed_ = true;
        }
        previous_tick_ = stats.tick;
        ticked_ = true;
        break;
      }
      case 1:
        schedule_.notify_dirty(kMask);
        break;
      case 2:
        schedule_.notify_events(kMask);
        break;
      default:
        schedule_.notify_dirty(op & kMask);
        break;
    }
  }

  [[nodiscard]] auto check() const -> std::optional<property::Violation> {
    if (!ticked_) {
      return std::nullopt;
    }
    // Every due task either ran or was skipped; none is lost.
    if (last_.tasks_run + last_.tasks_skipped != last_.tasks_due) {
      std::ostringstream detail;
      detail << "run " << last_.tasks_run << " + skipped "
             << last_.tasks_skipped << " != due " << last_.tasks_due;
      return property::Violation{"tasks_run + tasks_skipped == tasks_due",
                                 detail.str(), 0};
    }
    if (tick_regressed_) {
      return property::Violation{"tick advances monotonically",
                                 "a tick did not exceed its predecessor", 0};
    }
    return std::nullopt;
  }

 private:
  static constexpr std::uint32_t kMask = 1U << 0U;

  struct CountingTask {
    auto operator()(const tess::ScheduleTaskContext&)
        -> tess::ScheduleTaskResult {
      return {};
    }
  };

  tess::Schedule schedule_;
  tess::SimClock clock_;
  CountingTask every_;
  CountingTask dirty_;
  CountingTask event_;
  tess::ScheduleTickStats last_{};
  std::uint64_t previous_tick_ = 0;
  bool ticked_ = false;
  bool tick_regressed_ = false;
};

// Bounded sequences on the pull-request tier (section 3.4); the weekly
// tier can raise both numbers through the environment.
constexpr std::size_t kSteps = 64;
constexpr std::uint64_t kSeeds = 24;

template <typename Model>
void run_property(const char* name) {
  const property::Property<Model> prop(name, Model::kOperationCount);

  // A printed replay command must actually reproduce, so honour it
  // ahead of the seed sweep.
  if (const auto replay = property::replay_from_environment()) {
    const auto violation = prop.replay(*replay);
    EXPECT_FALSE(violation.has_value())
        << "replayed sequence still fails: "
        << (violation ? violation->invariant : "");
    return;
  }

  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const auto failing = prop.run(seed, kSteps);
    // Guarded rather than asserted on the stream: the report is only
    // reachable when a failure exists, and writing it this way lets
    // static analysis see that too.
    if (failing.has_value()) {
      FAIL() << "seed " << seed << "\n" << prop.report(*failing);
    }
  }
}

// A model that is broken on purpose. Without it, nothing proves the
// harness can detect a violation, shrink it, or produce a replay that
// reproduces — and a property harness that has never failed is
// indistinguishable from one that cannot fail.
class BrokenModel {
 public:
  static constexpr std::uint32_t kOperationCount = 4;

  void apply(std::uint32_t op) {
    if (op == 3) {
      ++threes_;
    }
  }

  // Fails only after operation 3 appears twice, so the minimal
  // failing sequence is exactly two operations and a shrinker that
  // merely truncates cannot find it.
  [[nodiscard]] auto check() const -> std::optional<property::Violation> {
    if (threes_ >= 2) {
      return property::Violation{"fewer than two threes", "seeded defect", 0};
    }
    return std::nullopt;
  }

 private:
  int threes_ = 0;
};

TEST(TessProperty, HarnessDetectsAndShrinksASeededDefect) {
  const property::Property<BrokenModel> prop("TessProperty.Broken",
                                             BrokenModel::kOperationCount);

  // Some seed in a modest sweep must produce two threes in 64 steps.
  std::optional<std::vector<std::uint32_t>> failing;
  for (std::uint64_t seed = 1; seed <= 8 && !failing.has_value(); ++seed) {
    failing = prop.run(seed, 64);
  }

  if (!failing.has_value()) {
    FAIL() << "the harness missed a seeded defect";
  }
  const std::vector<std::uint32_t>& shrunk = *failing;
  // Shrunk to the minimum that still fails: two operations, both the
  // offending one. Anything longer means the shrinker gave up early.
  EXPECT_EQ(shrunk.size(), 2u) << prop.report(shrunk);
  for (const auto op : shrunk) {
    EXPECT_EQ(op, 3u);
  }
  // The reported sequence must actually reproduce, or the replay
  // command in the report is a lie.
  EXPECT_TRUE(prop.replay(shrunk).has_value());
  EXPECT_NE(prop.replay_command(shrunk).find("TESS_PROPERTY_REPLAY"),
            std::string::npos);
}

TEST(TessProperty, ReplayFromEnvironmentReproducesASequence) {
  const property::Property<BrokenModel> prop("TessProperty.Broken",
                                             BrokenModel::kOperationCount);

  // The exact sequence a report would print must fail on replay.
  EXPECT_TRUE(prop.replay({3, 3}).has_value());
  EXPECT_FALSE(prop.replay({3}).has_value());
}

TEST(TessProperty, ResidencyInvariantsHoldUnderRandomSequences) {
  run_property<ResidencyModel>("TessProperty.Residency");
}

TEST(TessProperty, ScheduleInvariantsHoldUnderRandomSequences) {
  run_property<ScheduleModel>("TessProperty.Schedule");
}

}  // namespace
