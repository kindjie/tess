#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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
  static constexpr std::size_t kBudgetChunks = 4;
  // Strictly greater than the capacity in chunks, so a sequence can
  // fill the world and force eviction.
  static constexpr std::uint32_t kKeySpace = 6;
  // Four actions across every key: the key space must exceed capacity
  // or the budget bounds and the LRU eviction path are unreachable, and
  // the invariants below would be asserted against a world that never
  // fills up.
  static constexpr std::uint32_t kOperationCount = 4 * kKeySpace;

  ResidencyModel()
      : world_(tess::ResidencyConfig{kBudgetChunks *
                                     ResidencyWorld::page_byte_size}) {}

  void apply(std::uint32_t op) {
    // Low bits pick the action, high bits pick the chunk, so the
    // operation encoding stays a single integer the shrinker can drop.
    const auto action = op % 4;
    const auto key = tess::ChunkKey{op / 4 % kKeySpace};
    const auto resident_before = world_.resident_count();
    switch (action) {
      case 0: {
        const auto generation_before = world_.residency_generation(key);
        (void)world_.ensure_resident(key);
        const auto generation_after = world_.residency_generation(key);
        if (generation_before.valid() &&
            generation_after != generation_before) {
          reload_without_evict_ = true;
        }
        // A freshly materialized page draws from a monotone clock, so its
        // generation must exceed every generation ever issued. Reusing
        // one would make an evicted chunk's stale handle indistinguish-
        // able from a live one.
        if (!generation_before.valid() &&
            generation_after.value <= highest_generation_) {
          reused_generation_ = generation_after.value;
        }
        highest_generation_ =
            std::max(highest_generation_, generation_after.value);
        // Materializing a chunk in a full world is the LRU eviction path.
        if (!generation_before.valid() &&
            resident_before == world_.capacity()) {
          ++lru_evictions_;
        }
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
              key, tess::DirtyMask{1U},
              tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}});
        }
        break;
    }
    peak_resident_ = std::max(peak_resident_, world_.resident_count());
  }

  /// The most chunks resident at once, so a test can confirm the sweep
  /// reaches the capacity bound instead of assuming it does.
  [[nodiscard]] auto peak_resident() const -> std::size_t {
    return peak_resident_;
  }

  /// How many chunks were dropped to make room, which is the only way
  /// the LRU eviction path runs.
  [[nodiscard]] auto lru_evictions() const -> std::size_t {
    return lru_evictions_;
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
      if (!world_.residency_generation(key).valid()) {
        return violation("resident chunk has a nonzero generation", key.value,
                         0);
      }
    }
    if (reload_without_evict_) {
      return violation(
          "ensure_resident on a resident chunk keeps its generation", 1, 0);
    }
    if (reused_generation_ != 0) {
      return violation("a newly materialized chunk draws an unused generation",
                       reused_generation_, highest_generation_);
    }
    return std::nullopt;
  }

 private:
  static auto violation(const char* name, std::uint64_t observed,
                        std::uint64_t bound)
      -> std::optional<property::Violation> {
    std::ostringstream detail;
    detail << "observed " << observed << ", bound " << bound;
    return property::Violation{name, detail.str(), 0};
  }

  ResidencyWorld world_;
  std::uint64_t highest_generation_ = 0;
  std::uint64_t reused_generation_ = 0;
  std::size_t peak_resident_ = 0;
  std::size_t lru_evictions_ = 0;
  bool reload_without_evict_ = false;
};

/// Random tick/notify sequences over a sealed schedule.
class ScheduleModel {
 public:
  static constexpr std::uint32_t kOperationCount = 8;

  ScheduleModel() {
    every_.runs = &every_runs_;
    dirty_.runs = &dirty_runs_;
    event_.runs = &event_runs_;
    schedule_.reserve_tasks(3);
    (void)schedule_.add_task(
        {"every", tess::SimPhase::PreUpdate, tess::Cadence::every_tick()},
        every_);
    (void)schedule_.add_task({"dirty", tess::SimPhase::Pathing,
                              tess::Cadence::on_dirty(tess::DirtyMask{kMask})},
                             dirty_);
    (void)schedule_.add_task(
        {"event", tess::SimPhase::Movement, tess::Cadence::on_event(kMask)},
        event_);
    schedule_.seal();
  }

  void apply(std::uint32_t op) {
    switch (op % 4) {
      case 0: {
        const auto callbacks_before = total_runs();
        const auto stats = schedule_.run_tick(clock_);
        last_ = stats;
        // An independent tally of callback entries: the schedule's own
        // count of what it ran must match what actually ran.
        if (total_runs() - callbacks_before != stats.tasks_run) {
          callbacks_disagreed_ = true;
        }
        if (stats.tick <= previous_tick_ && ticked_) {
          tick_regressed_ = true;
        }
        previous_tick_ = stats.tick;
        ticked_ = true;
        ++ticks_;
        // An every-tick cadence is due on every tick and nothing here
        // disables it, so its tally must track the tick count exactly.
        if (every_runs_ != ticks_) {
          every_tick_missed_ = true;
        }
        break;
      }
      case 1:
        schedule_.notify_dirty(tess::DirtyMask{kMask});
        break;
      case 2:
        schedule_.notify_events(kMask);
        break;
      default:
        schedule_.notify_dirty(tess::DirtyMask{op & kMask});
        break;
    }
  }

  /// Counters a test can use to confirm the sweep actually ticks and
  /// enters each cadence, instead of passing because nothing ran.
  [[nodiscard]] auto ticks() const -> std::size_t { return ticks_; }
  [[nodiscard]] auto dirty_runs() const -> std::size_t { return dirty_runs_; }
  [[nodiscard]] auto event_runs() const -> std::size_t { return event_runs_; }

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
    if (callbacks_disagreed_) {
      std::ostringstream detail;
      detail << "the schedule reported " << last_.tasks_run
             << " run, but a different number of callbacks were entered";
      return property::Violation{"tasks_run counts the callbacks that ran",
                                 detail.str(), 0};
    }
    if (every_tick_missed_) {
      std::ostringstream detail;
      detail << "an every-tick task ran " << every_runs_ << " times over "
             << ticks_ << " ticks";
      return property::Violation{"an every-tick task runs on every tick",
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

  // Counts through a pointer, so the tally survives however the
  // schedule stores the callable. A task that returns without recording
  // anything gives `tasks_run` no independent witness: every operand of
  // the identity below would come from the same stats struct, and a
  // scheduler that ran the wrong tasks — or none — would still satisfy
  // it.
  struct CountingTask {
    std::size_t* runs = nullptr;

    auto operator()(const tess::ScheduleTaskContext&)
        -> tess::ScheduleTaskResult {
      if (runs != nullptr) {
        ++*runs;
      }
      return {};
    }
  };

  [[nodiscard]] auto total_runs() const -> std::size_t {
    return every_runs_ + dirty_runs_ + event_runs_;
  }

  tess::Schedule schedule_;
  tess::SimClock clock_;
  std::size_t every_runs_ = 0;
  std::size_t dirty_runs_ = 0;
  std::size_t event_runs_ = 0;
  CountingTask every_;
  CountingTask dirty_;
  CountingTask event_;
  tess::ScheduleTickStats last_{};
  std::uint64_t previous_tick_ = 0;
  std::size_t ticks_ = 0;
  bool ticked_ = false;
  bool tick_regressed_ = false;
  bool callbacks_disagreed_ = false;
  bool every_tick_missed_ = false;
};

// Bounded sequences on the pull-request tier (section 3.4); the weekly
// tier can raise both numbers through the environment.
// Pull-request tier defaults. The weekly tier raises both
// through TESS_PROPERTY_SEEDS and TESS_PROPERTY_STEPS; a
// malformed value fails loudly rather than silently running
// the smaller workload and reporting a long-seed pass.
constexpr std::size_t kDefaultSteps = 64;
constexpr std::uint64_t kDefaultSeeds = 24;

template <typename Model>
void run_property() {
  // The name comes from the running test, so the replay command can
  // never name a test that does not exist.
  const property::Property<Model> prop(property::current_test_name(),
                                       Model::kOperationCount);

  // A printed replay command must actually reproduce, so honour it
  // ahead of the seed sweep. An unusable request fails rather than
  // falling through to the sweep, which would report a pass for a run
  // the operator believed was replaying a specific failure.
  const auto budget = property::sweep_budget(kDefaultSeeds, kDefaultSteps);
  if (!budget.error.empty()) {
    FAIL() << budget.error;
  }

  const auto request =
      property::replay_from_environment(Model::kOperationCount);
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

  for (std::uint64_t seed = 1; seed <= budget.seeds; ++seed) {
    const auto failing = prop.run(seed, budget.steps);
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
  const property::Property<BrokenModel> prop(property::current_test_name(),
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
  // Shrunk to two operations, both the offending one. Nothing shorter
  // fails here, so 1-minimality and the true minimum coincide for this
  // model; anything longer means the shrinker gave up early.
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
  const property::Property<BrokenModel> prop(property::current_test_name(),
                                             BrokenModel::kOperationCount);

  // The exact sequence a report would print must fail on replay.
  EXPECT_TRUE(prop.replay({3, 3}).has_value());
  EXPECT_FALSE(prop.replay({3}).has_value());
}

// Sets an environment variable for the duration of a scope, so the
// replay path can be tested through the environment it actually reads
// without leaking a value into the tests that follow.
class ScopedEnvironment {
 public:
  ScopedEnvironment(const char* name, const char* value) : name_(name) {
    set(name_, value);
  }
  ScopedEnvironment(const ScopedEnvironment&) = delete;
  auto operator=(const ScopedEnvironment&) -> ScopedEnvironment& = delete;
  ScopedEnvironment(ScopedEnvironment&&) = delete;
  auto operator=(ScopedEnvironment&&) -> ScopedEnvironment& = delete;
  ~ScopedEnvironment() { set(name_, nullptr); }

 private:
  static void set(const char* name, const char* value) {
#if defined(_MSC_VER)
    // _putenv_s removes the variable when given an empty value.
    (void)_putenv_s(name, value == nullptr ? "" : value);
#else
    if (value == nullptr) {
      (void)unsetenv(name);
    } else {
      (void)setenv(name, value, 1);
    }
#endif
  }

  const char* name_;
};

TEST(TessProperty, ReplayCommandRoundTripsThroughTheEnvironment) {
  const property::Property<BrokenModel> prop(property::current_test_name(),
                                             BrokenModel::kOperationCount);
  const std::vector<std::uint32_t> sequence{3, 3};
  const auto command = prop.replay_command(sequence);

  // Take the value straight out of the printed command: if the command
  // spells the sequence in a form the reader cannot parse back, the
  // report is useless no matter how correct the shrink was.
  const std::string key = "TESS_PROPERTY_REPLAY=";
  const auto begin = command.find(key);
  ASSERT_NE(begin, std::string::npos) << command;
  const auto value_begin = begin + key.size();
  const auto value_end = command.find(' ', value_begin);
  ASSERT_NE(value_end, std::string::npos) << command;
  const auto value = command.substr(value_begin, value_end - value_begin);

  const ScopedEnvironment env("TESS_PROPERTY_REPLAY", value.c_str());
  const auto request =
      property::replay_from_environment(BrokenModel::kOperationCount);
  ASSERT_TRUE(request.present);
  EXPECT_TRUE(request.error.empty()) << request.error;
  EXPECT_EQ(request.sequence, sequence);
  // The recovered sequence must still demonstrate the failure.
  EXPECT_TRUE(prop.replay(request.sequence).has_value());

  // The test name is anchored, so it cannot select a lookalike, and the
  // command names no build directory: a failure found under ASan does
  // not reproduce against a build/dev binary.
  EXPECT_NE(
      command.find(
          "-R '^TessProperty\\.ReplayCommandRoundTripsThroughTheEnvironment$'"),
      std::string::npos)
      << command;
  EXPECT_EQ(command.find("build/dev"), std::string::npos) << command;
}

TEST(TessProperty, TheReplayCommandNamesARegisteredTest) {
  // The regression this pins: the command anchors the property name as
  // a CTest regex, so a name that is not a registered test selects
  // NOTHING — and ctest prints "No tests were found" and exits zero. A
  // reproduction command that silently succeeds while running nothing
  // is the worst possible failure for this tool, and a hand-written
  // name drifts from the test it belongs to as soon as either is
  // renamed.
  const auto name = property::current_test_name();
  EXPECT_EQ(name, "TessProperty.TheReplayCommandNamesARegisteredTest");

  const property::Property<BrokenModel> prop(name,
                                             BrokenModel::kOperationCount);
  // The command escapes regex metacharacters, so the dot separating
  // suite from test appears backslashed.
  std::string escaped;
  for (const char c : name) {
    if (c == '.') {
      escaped.push_back('\\');
    }
    escaped.push_back(c);
  }
  const auto command = prop.replay_command({3, 3});
  EXPECT_NE(command.find("-R '^" + escaped + "$'"), std::string::npos)
      << command;

  // The name must belong to a test gtest actually registered, or the
  // anchored regex matches nothing.
  const auto* unit_test = ::testing::UnitTest::GetInstance();
  bool registered = false;
  for (int suite = 0; suite < unit_test->total_test_suite_count(); ++suite) {
    const auto* test_suite = unit_test->GetTestSuite(suite);
    for (int test = 0; test < test_suite->total_test_count(); ++test) {
      const auto full = std::string(test_suite->name()) + "." +
                        test_suite->GetTestInfo(test)->name();
      if (full == name) {
        registered = true;
      }
    }
  }
  EXPECT_TRUE(registered) << name
                          << " is not a registered test, so its "
                             "replay command would select nothing";
}

TEST(TessProperty, TheSweepBudgetHonoursTheEnvironment) {
  // The weekly tier raises the sweep through these variables. If an
  // override were ignored, or a malformed one silently fell back, the
  // weekly run would report a long-seed pass having run the
  // pull-request workload -- the same silent success the replay parser
  // guards against, one level up.
  // Explicitly UNSET rather than assuming a clean environment: the
  // weekly job exports both variables, so a test that assumed nothing
  // was set would fail there. Running the real weekly workload locally
  // is what surfaced that.
  {
    const ScopedEnvironment seeds("TESS_PROPERTY_SEEDS", nullptr);
    const ScopedEnvironment steps("TESS_PROPERTY_STEPS", nullptr);
    const auto budget = property::sweep_budget(24, 64);
    EXPECT_TRUE(budget.error.empty()) << budget.error;
    EXPECT_EQ(budget.seeds, 24U);
    EXPECT_EQ(budget.steps, 64U);
  }
  {
    const ScopedEnvironment seeds("TESS_PROPERTY_SEEDS", "500");
    const ScopedEnvironment steps("TESS_PROPERTY_STEPS", "256");
    const auto budget = property::sweep_budget(24, 64);
    EXPECT_TRUE(budget.error.empty()) << budget.error;
    EXPECT_EQ(budget.seeds, 500U);
    EXPECT_EQ(budget.steps, 256U);
  }
  // Blank is "unset", so an exported-but-empty variable still runs the
  // default rather than collapsing the sweep.
  {
    const ScopedEnvironment seeds("TESS_PROPERTY_SEEDS", "  ");
    const ScopedEnvironment steps("TESS_PROPERTY_STEPS", nullptr);
    const auto budget = property::sweep_budget(24, 64);
    EXPECT_TRUE(budget.error.empty()) << budget.error;
    EXPECT_EQ(budget.seeds, 24U);
  }
  // Anything else is an error, including zero: a zero-seed sweep runs
  // nothing at all and would pass.
  for (const char* bad : {"0", "-5", "12x", "abc", "99999999999999999999"}) {
    const ScopedEnvironment seeds("TESS_PROPERTY_SEEDS", bad);
    const auto budget = property::sweep_budget(24, 64);
    EXPECT_FALSE(budget.error.empty()) << "accepted '" << bad << "'";
  }
}

TEST(TessProperty, AnUnsetReplayVariableRunsTheSweep) {
  // The dangerous case: a variable exported with no value must not
  // count as a replay request, or every property test would report a
  // pass having run nothing at all.
  for (const char* value : {"", "   ", "\t\n"}) {
    const ScopedEnvironment env("TESS_PROPERTY_REPLAY", value);
    const auto request = property::replay_from_environment(4);
    EXPECT_FALSE(request.present) << "value '" << value << "'";
    EXPECT_TRUE(request.error.empty()) << request.error;
  }
}

TEST(TessProperty, MalformedReplaySequencesAreRejected) {
  // Every one of these was silently accepted, truncated, or thrown as
  // an unhelpful std::stoul error before the parser checked its work.
  for (const char* text : {"1junk", "abc", "-1", "+1", " 1", "3,", ",3", "3,,4",
                           "4294967296", "99999999999999999999"}) {
    const auto request = property::parse_replay_sequence(text, 8);
    EXPECT_TRUE(request.present) << "value '" << text << "'";
    EXPECT_FALSE(request.error.empty())
        << "accepted malformed value '" << text << "'";
    EXPECT_TRUE(request.sequence.empty()) << "value '" << text << "'";
  }

  // An operation the model cannot perform is rejected too: replaying it
  // would drive the model outside the space the sequence came from.
  const auto out_of_range = property::parse_replay_sequence("2,8", 8);
  EXPECT_TRUE(out_of_range.present);
  EXPECT_FALSE(out_of_range.error.empty());

  const auto valid = property::parse_replay_sequence("0,7,3", 8);
  EXPECT_TRUE(valid.present);
  EXPECT_TRUE(valid.error.empty()) << valid.error;
  EXPECT_EQ(valid.sequence, (std::vector<std::uint32_t>{0, 7, 3}));
}

TEST(TessProperty, ResidencyInvariantsHoldUnderRandomSequences) {
  run_property<ResidencyModel>();
}

TEST(TessProperty, ScheduleInvariantsHoldUnderRandomSequences) {
  run_property<ScheduleModel>();
}

TEST(TessProperty, TheResidencySweepReachesCapacityAndEvicts) {
  const auto budget = property::sweep_budget(kDefaultSeeds, kDefaultSteps);
  if (!budget.error.empty()) {
    FAIL() << budget.error;
  }
  // An earlier encoding could only ever address three of six chunks, so
  // a four-chunk world never filled and the capacity, byte-budget and
  // eviction invariants above were asserted against a world that could
  // not violate them. Passing invariants that cannot be reached are
  // indistinguishable from no coverage, so the sweep's reach is now
  // asserted rather than assumed.
  const property::Property<ResidencyModel> prop(
      property::current_test_name(), ResidencyModel::kOperationCount);

  std::size_t peak = 0;
  std::size_t evictions = 0;
  for (std::uint64_t seed = 1; seed <= budget.seeds; ++seed) {
    ResidencyModel model;
    for (const auto op : prop.sequence_for(seed, budget.steps)) {
      model.apply(op);
    }
    peak = std::max(peak, model.peak_resident());
    evictions += model.lru_evictions();
  }

  EXPECT_EQ(peak, ResidencyModel::kBudgetChunks)
      << "the sweep never filled the world, so its capacity and byte-budget "
         "invariants were never actually tested";
  EXPECT_GT(evictions, 0U)
      << "the sweep never evicted, so the generation invariants never saw a "
         "rematerialized chunk";
}

TEST(TessProperty, TheScheduleSweepTicksAndEntersEveryCadence) {
  const auto budget = property::sweep_budget(kDefaultSeeds, kDefaultSteps);
  if (!budget.error.empty()) {
    FAIL() << budget.error;
  }
  // The schedule invariants are all conditioned on having ticked, and
  // the dirty and event cadences only run once something has notified
  // them. If the sweep never reached those paths the invariants would
  // hold over an idle schedule and report a pass.
  const property::Property<ScheduleModel> prop(property::current_test_name(),
                                               ScheduleModel::kOperationCount);

  std::size_t ticks = 0;
  std::size_t dirty = 0;
  std::size_t event = 0;
  for (std::uint64_t seed = 1; seed <= budget.seeds; ++seed) {
    ScheduleModel model;
    for (const auto op : prop.sequence_for(seed, budget.steps)) {
      model.apply(op);
    }
    ticks += model.ticks();
    dirty += model.dirty_runs();
    event += model.event_runs();
  }

  EXPECT_GT(ticks, 0U) << "the sweep never ticked";
  EXPECT_GT(dirty, 0U) << "the on-dirty cadence never ran, so notify_dirty was "
                          "never followed by a tick that observed it";
  EXPECT_GT(event, 0U) << "the on-event cadence never ran, so notify_events "
                          "was never followed by a tick that observed it";
}

}  // namespace
