#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <cstdint>

#include "colony_harness.h"

namespace {

namespace colony = tess_test::colony;

// The PR-tier world (redesign section 5): 512x512, the smallest of
// section 3.1's world-size axis, built from the logical 64x64 map at
// scale 8.
using Shape512 =
    tess::Shape<tess::Extent3{512, 512, 1}, tess::Extent3{32, 32, 1}>;
// Chunk-size invariance (section 3.2) reuses the same world extent
// with a different chunk decomposition.
using Shape512Chunk64 =
    tess::Shape<tess::Extent3{512, 512, 1}, tess::Extent3{64, 64, 1}>;

// The narrow schema carries exactly the fields the scenario needs.
using NarrowSchema =
    tess::FieldSchema<tess::Field<colony::PassableTag, bool>,
                      tess::Field<colony::CostTag, std::uint32_t>,
                      tess::Field<colony::OccupancyTag, bool>,
                      tess::Field<colony::ReservationTag, bool>>;

// The wide schema adds payload the scenario never reads, which is
// section 3.1's field-payload-width axis: it must not change results.
struct DecorTag {};
struct HeatTag {};
using WideSchema =
    tess::FieldSchema<tess::Field<colony::PassableTag, bool>,
                      tess::Field<colony::CostTag, std::uint32_t>,
                      tess::Field<colony::OccupancyTag, bool>,
                      tess::Field<colony::ReservationTag, bool>,
                      tess::Field<DecorTag, std::uint16_t>,
                      tess::Field<HeatTag, std::uint32_t>>;

using NarrowColony = colony::Colony<Shape512, NarrowSchema>;
using WideColony = colony::Colony<Shape512, WideSchema>;
using Chunk64Colony = colony::Colony<Shape512Chunk64, NarrowSchema>;

// Churn is on in the shared configuration on purpose: the queued-op
// task is otherwise idle, and every executor comparison below would
// then compare two runs that never dispatched any work.
auto base_config() -> colony::ColonyConfig {
  colony::ColonyConfig config;
  config.agents = 100;
  config.ticks = 40;
  config.churn_period = 8;
  config.churn_chunks = 4;
  return config;
}

void expect_same_outcome(const colony::ColonyRun& left,
                         const colony::ColonyRun& right) {
  EXPECT_EQ(left.final_positions, right.final_positions);
  EXPECT_EQ(left.total_steps, right.total_steps);
  EXPECT_EQ(left.arrivals, right.arrivals);
  EXPECT_EQ(left.sampled_reachability, right.sampled_reachability);
  EXPECT_EQ(left.counters.churn_acked_tiles, right.counters.churn_acked_tiles);
  EXPECT_EQ(left.counters.world_replan_passes,
            right.counters.world_replan_passes);
}

TEST(TessColonyHarness, DrivesTheStackAndMakesProgress) {
  auto config = base_config();

  const auto run = NarrowColony(config).run();

  // The scenario must actually move agents through the production
  // stack rather than idling: a silently inert harness would make
  // every invariance test below vacuous.
  EXPECT_EQ(run.ticks, config.ticks);
  EXPECT_EQ(run.final_positions.size(), config.agents);
  // The placement scan must have satisfied the requested population:
  // a silently smaller colony would weaken every assertion here and
  // in the invariance tests.
  EXPECT_EQ(run.agents_unplaced, 0u);
  // Agents that arrived or exhausted their retries keep no goal. The
  // harness does not reassign, so this is also the signal that a
  // longer run would idle.
  EXPECT_EQ(run.agents_idle_at_end, run.arrivals);
  EXPECT_GT(run.total_steps, 0u);
  EXPECT_GT(run.arrivals, 0u);
  EXPECT_EQ(run.counters.churn_events, 4u);
  // One queued operation per distinct chunk, not one spanning many:
  // the auto-exec task selects its executor by operation count.
  EXPECT_EQ(run.counters.churn_operations, 16u);
  EXPECT_EQ(run.counters.churn_acked_tiles, 16u);
  EXPECT_EQ(run.counters.world_replan_passes, 4u);
  EXPECT_EQ(run.counters.executed_runs, 4u);
  EXPECT_EQ(run.counters.delta_publishes, 4u);
}

TEST(TessColonyHarness, QuietWithoutChurn) {
  auto config = base_config();
  config.churn_period = 0;

  const auto run = NarrowColony(config).run();

  // No terrain edits means no queued work, no topology rebuild, and
  // no world-scoped replan — while agents still move.
  EXPECT_EQ(run.counters.churn_events, 0u);
  EXPECT_EQ(run.counters.churn_operations, 0u);
  EXPECT_EQ(run.counters.world_replan_passes, 0u);
  EXPECT_EQ(run.counters.executed_runs, 0u);
  EXPECT_GT(run.total_steps, 0u);
}

TEST(TessColonyHarness, SerialAndPooledExecutionAgree) {
  auto serial_config = base_config();
  auto pooled_config = base_config();
  pooled_config.worker_count = 2;

  const auto serial = NarrowColony(serial_config).run();
  const auto pooled = NarrowColony(pooled_config).run();

  // Section 3.2's serial == pool gate. The pool_phases assertions
  // keep it honest: equality between two idle tasks would prove
  // nothing about concurrent execution.
  EXPECT_EQ(serial.counters.pool_phases, 0u);
  EXPECT_GT(pooled.counters.pool_phases, 0u);
  EXPECT_EQ(pooled.counters.executed_runs, serial.counters.executed_runs);
  expect_same_outcome(serial, pooled);
}

TEST(TessColonyHarness, WorkerCountDoesNotChangeResults) {
  auto two = base_config();
  two.worker_count = 2;
  auto four = base_config();
  four.worker_count = 4;

  const auto narrow_two = NarrowColony(two).run();
  const auto narrow_four = NarrowColony(four).run();

  EXPECT_GT(narrow_two.counters.pool_phases, 0u);
  EXPECT_GT(narrow_four.counters.pool_phases, 0u);
  expect_same_outcome(narrow_two, narrow_four);
}

TEST(TessColonyHarness, FieldPayloadWidthDoesNotChangeResults) {
  const auto config = base_config();

  const auto narrow = NarrowColony(config).run();
  const auto wide = WideColony(config).run();

  // Two extra fields the scenario never reads must not move a single
  // agent (section 3.1's payload-width axis).
  expect_same_outcome(narrow, wide);
}

TEST(TessColonyHarness, ChunkSizeDoesNotChangeResults) {
  const auto config = base_config();

  const auto chunk32 = NarrowColony(config).run();
  const auto chunk64 = Chunk64Colony(config).run();

  // Section 3.2's chunk-size invariance over identical logical
  // terrain and endpoints.
  expect_same_outcome(chunk32, chunk64);
}

TEST(TessColonyHarness, ClearingRuntimeCachesDoesNotChangeResults) {
  auto warm = base_config();
  auto cold = base_config();
  cold.cold_cache = true;

  const auto warm_run = NarrowColony(warm).run();
  const auto cold_run = NarrowColony(cold).run();

  // Clearing the runtime caches on every world change must not move
  // an agent. This is a smoke check, not section 3.2's cache
  // differential: with distinct goals and the default cache policy,
  // this scenario does not populate the product cache it clears, so
  // a genuine warm-hit-versus-cold-miss comparison needs a
  // repeated-goal workload it does not yet run.
  expect_same_outcome(warm_run, cold_run);
}

TEST(TessColonyHarness, IncrementalTopologyMatchesFreshAfterEveryChurn) {
  auto config = base_config();
  config.verify_fresh_graph_each_churn = true;

  const auto run = NarrowColony(config).run();

  // Section 3.2's incremental == fresh gate checked while it still
  // matters: after each churn event the incrementally updated graph
  // is compared against a freshly built one BEFORE agents move that
  // tick. Comparing only at the end of the run would miss a wrong
  // intermediate graph that later self-heals.
  EXPECT_EQ(run.counters.fresh_graph_comparisons,
            run.counters.world_replan_passes * config.agents);
  EXPECT_GT(run.counters.fresh_graph_comparisons, 0u);
  EXPECT_EQ(run.counters.fresh_graph_mismatches, 0u);
}

TEST(TessColonyHarness, IncrementalTopologyMatchesFreshRebuild) {
  auto incremental = base_config();
  auto fresh = base_config();
  fresh.rebuild_graph_before_sampling = true;

  const auto incremental_run = NarrowColony(incremental).run();
  const auto fresh_run = NarrowColony(fresh).run();

  // The end-of-run half of the differential: the graph updated
  // chunk-by-chunk across four churn events answers exactly like one
  // rebuilt over the same final terrain. The negative half of the
  // sample targets a blocked endpoint, so it exercises endpoint
  // validation rather than disconnected-component reachability; the
  // per-churn test above is the stronger of the two.
  ASSERT_EQ(incremental_run.sampled_reachability.size(),
            2u * incremental.agents);
  const auto reachable =
      static_cast<std::uint8_t>(tess::ReachabilityStatus::Reachable);
  const auto reached =
      std::count(incremental_run.sampled_reachability.begin(),
                 incremental_run.sampled_reachability.end(), reachable);
  EXPECT_GT(reached, 0);
  EXPECT_LT(reached,
            static_cast<long>(incremental_run.sampled_reachability.size()));
  EXPECT_EQ(incremental_run.sampled_reachability,
            fresh_run.sampled_reachability);
}

TEST(TessColonyHarness, FlowIdentitiesHold) {
  const auto config = base_config();

  const auto run = NarrowColony(config).run();

  // Section 3.3's hard checks: no admission is lost, and every
  // admitted goal is terminal or still outstanding, never both.
  // Agents still in flight are expected — outstanding inventory is
  // part of the identity, not a violation.
  EXPECT_TRUE(run.agent_flow.admission_identity_holds())
      << "offered=" << run.agent_flow.offered
      << " admitted=" << run.agent_flow.admitted
      << " rejected=" << run.agent_flow.rejected;
  EXPECT_TRUE(run.agent_flow.retention_identity_holds())
      << "admitted=" << run.agent_flow.admitted
      << " terminal=" << run.agent_flow.terminal()
      << " outstanding=" << run.agent_flow.outstanding_current;
  EXPECT_EQ(run.agent_flow.admitted, config.agents);
  EXPECT_GT(run.agent_flow.inventory_tick_weighted, 0u);
}

// The default stride-8 scan placement seats at most 227 agents on
// the shared map; requesting more must be visible in
// agents_unplaced, never silent truncation, and a denser stride must
// actually seat the large populations the mixed-colony population
// ladder runs (its stage-4 campaign died on exactly this ceiling).
TEST(TessColonyHarness, PlacementStrideControlsSeatingCapacity) {
  auto over_capacity = base_config();
  over_capacity.agents = 500;
  over_capacity.ticks = 1;
  const auto truncated = NarrowColony(over_capacity).run();
  EXPECT_EQ(truncated.agents_unplaced, 500u - 227u);

  auto dense = over_capacity;
  dense.placement_stride = 2;
  const auto seated = NarrowColony(dense).run();
  EXPECT_EQ(seated.agents_unplaced, 0u);
  EXPECT_EQ(seated.final_positions.size(), 500u);
}

// Serial-only scenario golden (section 3.3 scopes goldens to serial
// precisely because pool workers do not aggregate into the caller's
// counter sink). Pinning the outcome catches behavioral drift that
// the invariance tests above cannot: they only compare runs to each
// other, so a change moving every configuration equally would pass
// them all.
TEST(TessColonyHarness, SerialScenarioGolden) {
  const auto config = base_config();

  const auto run = NarrowColony(config).run();

  EXPECT_EQ(run.total_steps, 2838u);
  EXPECT_EQ(run.arrivals, 94u);
  EXPECT_EQ(run.counters.blocked_route_repaths, 0u);
  EXPECT_EQ(run.counters.blocked_retry_exhaustions, 0u);
  EXPECT_EQ(run.agent_flow.admitted, 100u);
  EXPECT_EQ(run.agent_flow.completed, 94u);
  EXPECT_EQ(run.agent_flow.outstanding_current, 6u);
}

// The PIBT tier golden doubles as the dispatch witness: `pibt_passes`
// proves the configuration branch actually ran (an accidentally ignored
// tier cannot pass), and the pinned outcome catches behavioral drift in
// the tier exactly as the serial golden does for baseline. The numbers
// intentionally differ from the baseline golden: PIBT admits off-route
// yields, so wedges that baseline leaves in place resolve.
TEST(TessColonyHarness, PibtTierDispatchesAndPinsItsScenario) {
  auto config = base_config();
  config.movement_tier = colony::ColonyMovementTier::Pibt;

  const auto run = NarrowColony(config).run();

  EXPECT_EQ(run.counters.pibt_passes, 40u);
  // Fewer total steps than the baseline golden's 2838 with the same 94
  // arrivals: ranking-driven stepping takes different (here, fewer)
  // moves on the same fixture, so the tiers demonstrably diverge.
  EXPECT_EQ(run.total_steps, 2566u);
  EXPECT_EQ(run.arrivals, 94u);
  EXPECT_EQ(run.agent_flow.completed, 94u);
  EXPECT_TRUE(run.agent_flow.admission_identity_holds());
  EXPECT_TRUE(run.agent_flow.retention_identity_holds());
}

}  // namespace
