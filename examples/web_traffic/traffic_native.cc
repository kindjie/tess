#include <tess/core/config.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <vector>

#include "traffic_model.h"

namespace tess::examples::web_traffic {

namespace {

constexpr std::array kScenarios{
    TrafficScenario::Aligned,
    TrafficScenario::ShuffledCrossing,
    TrafficScenario::Funnel,
    TrafficScenario::MultiGate,
};

struct TickSample {
  int tick = 0;
  double update_us = 0.0;
  double planning_us = 0.0;
  int planning_queries = 0;
  int waits = 0;
  int blocked = 0;
  int arrived = 0;
  int pending = 0;
  int advanced = 0;
  std::uint64_t touched_nodes = 0;
  std::uint64_t heap_pops = 0;
  std::uint64_t neighbor_candidates = 0;
  std::uint64_t passability_checks = 0;
  std::uint64_t reconstructed_nodes = 0;
};

struct CrowdCheckpoint {
  int blocked = 0;
  int arrived = 0;
  std::uint64_t waits = 0;
  std::uint64_t advanced = 0;
  int longest_one_progress = 0;
  std::uint64_t state_hash = 0;
};

struct CrowdExpectation {
  CrowdCheckpoint tick_512;
  CrowdCheckpoint tick_1600;
};

auto parse_scenario(std::string_view name, TrafficScenario& scenario) -> bool {
  for (const auto candidate : kScenarios) {
    if (name == scenario_name(candidate)) {
      scenario = candidate;
      return true;
    }
  }
  return false;
}

auto hash_bytes(const std::uint8_t* bytes, std::size_t size) -> std::uint64_t {
  auto hash = std::uint64_t{1469598103934665603ULL};
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 1099511628211ULL;
  }
  return hash;
}

auto hash_agents(const std::int16_t* values) -> std::uint64_t {
  return hash_bytes(
      reinterpret_cast<const std::uint8_t*>(values),
      static_cast<std::size_t>(traffic_agents) * 2 * sizeof(std::int16_t));
}

auto wall_count(const TrafficModel& model) -> std::size_t {
  auto count = std::size_t{0};
  const auto* terrain = model.terrain();
  for (auto i = std::size_t{0};
       i < static_cast<std::size_t>(traffic_width) * traffic_height; ++i) {
    count += terrain[i] != 0 ? 1 : 0;
  }
  return count;
}

auto expected_wall_count(TrafficScenario scenario) -> std::size_t {
  if (scenario == TrafficScenario::Funnel) {
    return 4U * static_cast<std::size_t>(traffic_height - 24);
  }
  if (scenario == TrafficScenario::MultiGate) {
    return 4U * static_cast<std::size_t>(traffic_height - 8 * 8);
  }
  return 0;
}

constexpr auto crowd_expectation(TrafficScenario scenario) -> CrowdExpectation {
  if (scenario == TrafficScenario::Funnel) {
    return {
        .tick_512 =
            {
                .blocked = 80,
                .arrived = 0,
                .waits = 441,
                .advanced = 458823,
                .longest_one_progress = 0,
                .state_hash = 9342986576464649366ULL,
            },
        .tick_1600 =
            {
                .blocked = 0,
                .arrived = 789,
                .waits = 115579,
                .advanced = 1248733,
                .longest_one_progress = 0,
                .state_hash = 10853025492594229773ULL,
            },
    };
  }
  return {
      .tick_512 =
          {
              .blocked = 16,
              .arrived = 0,
              .waits = 73,
              .advanced = 459191,
              .longest_one_progress = 0,
              .state_hash = 8490615928208071737ULL,
          },
      .tick_1600 =
          {
              .blocked = 0,
              .arrived = 1024,
              .waits = 768,
              .advanced = 1057152,
              .longest_one_progress = 2,
              .state_hash = 8820900079770067587ULL,
          },
  };
}

auto matches_checkpoint(const TrafficModel& model, std::uint64_t waits,
                        std::uint64_t advanced, const CrowdCheckpoint& expected)
    -> bool {
  return model.blocked_agents() == expected.blocked &&
         model.arrived_agents() == expected.arrived &&
         waits == expected.waits && advanced == expected.advanced &&
         model.longest_one_progress_streak() == expected.longest_one_progress &&
         model.agent_state_hash() == expected.state_hash;
}

auto check_crowd_outcome(TrafficScenario scenario) -> bool {
  TrafficModel model{scenario};
  const auto expected = crowd_expectation(scenario);
  auto waits = std::uint64_t{0};
  auto advanced = std::uint64_t{0};
  for (auto tick = 1; tick <= 1600; ++tick) {
    if (model.tick(0.05) < 0.0) {
      return false;
    }
    waits += static_cast<std::uint64_t>(model.movement_waits_last_tick());
    advanced += static_cast<std::uint64_t>(model.advanced_last_tick());
    if (tick == 128 &&
        (model.planning_pending() != 0 || model.max_planning_queries() != 8)) {
      return false;
    }
    if (tick == 512 &&
        !matches_checkpoint(model, waits, advanced, expected.tick_512)) {
      return false;
    }
  }
  return matches_checkpoint(model, waits, advanced, expected.tick_1600);
}

auto check_catalog() -> bool {
  auto previous_terrain_hash = std::uint64_t{0};
  auto previous_agent_hash = std::uint64_t{0};
  for (const auto scenario : kScenarios) {
    TrafficModel model{scenario};
    const auto terrain_hash =
        hash_bytes(model.terrain(),
                   static_cast<std::size_t>(traffic_width) * traffic_height);
    const auto agent_hash = hash_agents(model.current_agents());
    if (scenario != TrafficScenario::ShuffledCrossing &&
        terrain_hash == previous_terrain_hash &&
        agent_hash == previous_agent_hash) {
      std::cerr << "web traffic model: scenarios are not distinct: "
                << scenario_name(scenario) << '\n';
      return false;
    }
    previous_terrain_hash = terrain_hash;
    previous_agent_hash = agent_hash;
  }
  return true;
}

auto check_scenario(TrafficScenario scenario) -> bool {
  TrafficModel first{scenario};
  TrafficModel second{scenario};
  const auto terrain_hash =
      hash_bytes(first.terrain(),
                 static_cast<std::size_t>(traffic_width) * traffic_height);
  const auto agent_hash = hash_agents(first.current_agents());
  if (terrain_hash !=
          hash_bytes(second.terrain(), static_cast<std::size_t>(traffic_width) *
                                           traffic_height) ||
      agent_hash != hash_agents(second.current_agents()) ||
      wall_count(first) != expected_wall_count(scenario) ||
      !first.validate_planner()) {
    std::cerr << "web traffic model: scenario is not deterministic: "
              << scenario_name(scenario) << '\n';
    return false;
  }
  if (first.tick(0.05) < 0.0 || second.tick(0.05) < 0.0 ||
      hash_agents(first.current_agents()) !=
          hash_agents(second.current_agents()) ||
      first.max_planning_queries() != 8 || second.max_planning_queries() != 8 ||
      first.planning_queries_last_tick() != 8 ||
      first.guided_queries_last_tick() !=
          ((scenario == TrafficScenario::Funnel ||
            scenario == TrafficScenario::MultiGate)
               ? 8
               : 0) ||
      first.fixed_ticks_last_call() != 1 ||
      first.planning_pending() != traffic_agents - 8 ||
      second.planning_pending() != traffic_agents - 8) {
    std::cerr << "web traffic model: bounded planning diverged: "
              << scenario_name(scenario) << '\n';
    return false;
  }
  if ((scenario == TrafficScenario::Funnel ||
       scenario == TrafficScenario::MultiGate) &&
      !check_crowd_outcome(scenario)) {
    std::cerr << "web traffic model: crowd outcome diverged: "
              << scenario_name(scenario) << '\n';
    return false;
  }
  return true;
}

template <typename Check>
auto run_checked(Check check, std::string_view success) -> int {
#if TESS_HAS_EXCEPTIONS
  try {
#endif
    if (!check()) {
      return 1;
    }
    std::cout << success << '\n';
#if TESS_HAS_EXCEPTIONS
  } catch (const std::exception& error) {
    std::cerr << "web traffic model: " << error.what() << '\n';
    return 1;
  }
#endif
  return 0;
}

auto run_self_check() -> int {
  return run_checked(
      [] {
        if (!check_catalog()) {
          return false;
        }
        for (const auto scenario : kScenarios) {
          if (!check_scenario(scenario)) {
            return false;
          }
        }
        return true;
      },
      "web traffic model: ok");
}

auto run_catalog_self_check() -> int {
  return run_checked(check_catalog, "web traffic catalog: ok");
}

auto run_scenario_self_check(TrafficScenario scenario) -> int {
  return run_checked([scenario] { return check_scenario(scenario); },
                     "web traffic scenario: ok");
}

auto run_scenario(TrafficScenario scenario, int ticks) -> int {
  TrafficModel model{scenario};
  auto update_us = 0.0;
  auto planning_us = 0.0;
  auto measured_ticks = 0;
  auto total_waits = std::uint64_t{0};
  const auto started = std::chrono::steady_clock::now();
  for (auto tick = 0; tick < ticks; ++tick) {
    const auto update = model.tick(0.05);
    if (update >= 0.0) {
      update_us += update;
      planning_us += model.planning_us();
      ++measured_ticks;
    }
    total_waits += static_cast<std::uint64_t>(model.movement_waits_last_tick());
  }
  const auto elapsed_ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - started)
                              .count();
  const auto divisor = static_cast<double>(std::max(measured_ticks, 1));
  std::cout << "scenario=" << scenario_name(scenario)
            << " shape=" << traffic_width << 'x' << traffic_height
            << " agents=" << traffic_agents << " ticks=" << ticks
            << " update_us=" << update_us / divisor
            << " planning_us=" << planning_us / divisor
            << " waits=" << total_waits << " blocked=" << model.blocked_agents()
            << " arrived=" << model.arrived_agents()
            << " pending=" << model.planning_pending()
            << " one_progress_streak=" << model.one_progress_streak()
            << " longest_one_progress=" << model.longest_one_progress_streak()
            << " max_searches=" << model.max_planning_queries()
            << " elapsed_ms=" << elapsed_ms << '\n';
  return model.max_planning_queries() <= 8 ? 0 : 1;
}

auto write_samples(TrafficScenario scenario, int ticks) -> int {
  TrafficModel model{scenario};
  auto samples = std::vector<TickSample>{};
  samples.reserve(static_cast<std::size_t>(ticks));
  for (auto tick = 0; tick < ticks; ++tick) {
    const auto update = model.tick(0.05);
    if (update < 0.0 || model.fixed_ticks_last_call() != 1) {
      std::cerr << "web traffic model: fixed-tick sampling diverged\n";
      return 1;
    }
    samples.push_back({
        .tick = tick,
        .update_us = update,
        .planning_us = model.planning_us(),
        .planning_queries = model.planning_queries_last_tick(),
        .waits = model.movement_waits_last_tick(),
        .blocked = model.blocked_agents(),
        .arrived = model.arrived_agents(),
        .pending = model.planning_pending(),
        .advanced = model.advanced_last_tick(),
        .touched_nodes = model.planning_touched_nodes_last_tick(),
        .heap_pops = model.planning_heap_pops_last_tick(),
        .neighbor_candidates = model.planning_neighbor_candidates_last_tick(),
        .passability_checks = model.planning_passability_checks_last_tick(),
        .reconstructed_nodes = model.planning_reconstructed_nodes_last_tick(),
    });
  }
  std::cout << "scenario,tick,update_us,planning_us,planning_queries,waits,"
               "blocked,arrived,pending,advanced,touched_nodes,heap_pops,"
               "neighbor_candidates,passability_checks,reconstructed_nodes\n";
  std::cout << std::setprecision(17);
  for (const auto& sample : samples) {
    std::cout << scenario_name(scenario) << ',' << sample.tick << ','
              << sample.update_us << ',' << sample.planning_us << ','
              << sample.planning_queries << ',' << sample.waits << ','
              << sample.blocked << ',' << sample.arrived << ','
              << sample.pending << ',' << sample.advanced << ','
              << sample.touched_nodes << ',' << sample.heap_pops << ','
              << sample.neighbor_candidates << ',' << sample.passability_checks
              << ',' << sample.reconstructed_nodes << '\n';
  }
  return model.max_planning_queries() <= 8 ? 0 : 1;
}

auto run_profile_repetitions(TrafficScenario scenario, int ticks,
                             int repetitions) -> int {
  auto checksum = std::uint64_t{0};
  for (auto repetition = 0; repetition < repetitions; ++repetition) {
    TrafficModel model{scenario};
    for (auto tick = 0; tick < ticks; ++tick) {
      if (model.tick(0.05) < 0.0) {
        return 1;
      }
    }
    checksum += static_cast<std::uint64_t>(model.blocked_agents());
    checksum += static_cast<std::uint64_t>(model.arrived_agents());
    checksum += static_cast<std::uint64_t>(model.planning_pending());
    checksum += static_cast<std::uint64_t>(model.max_planning_queries());
  }
  std::cout << "profile scenario=" << scenario_name(scenario)
            << " ticks=" << ticks << " repetitions=" << repetitions
            << " checksum=" << checksum << '\n';
  return 0;
}

}  // namespace

}  // namespace tess::examples::web_traffic

int main(int argc, char** argv) {
  using namespace tess::examples::web_traffic;
  if (argc == 1) {
    return run_self_check();
  }
  if (argc == 2 && std::string_view{argv[1]} == "--self-check-catalog") {
    return run_catalog_self_check();
  }
  if (argc == 3 && std::string_view{argv[1]} == "--self-check") {
    auto scenario = TrafficScenario::Aligned;
    return parse_scenario(argv[2], scenario) ? run_scenario_self_check(scenario)
                                             : 2;
  }
  const auto samples = argc == 6 && std::string_view{argv[5]} == "--samples";
  const auto profiling =
      argc == 7 && std::string_view{argv[5]} == "--profile-repetitions";
  if ((argc != 5 && !samples && !profiling) ||
      std::string_view{argv[1]} != "--scenario" ||
      std::string_view{argv[3]} != "--ticks") {
    std::cerr << "usage: tess_web_traffic_model --scenario "
                 "<aligned|shuffled-crossing|funnel|multi-gate> --ticks N "
                 "[--samples | --profile-repetitions N]\n"
                 "       tess_web_traffic_model --self-check-catalog\n"
                 "       tess_web_traffic_model --self-check "
                 "<aligned|shuffled-crossing|funnel|multi-gate>\n";
    return 2;
  }
  auto scenario = TrafficScenario::Aligned;
  auto ticks = 0;
  const auto text = std::string_view{argv[4]};
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), ticks);
  if (!parse_scenario(argv[2], scenario) || result.ec != std::errc{} ||
      result.ptr != text.data() + text.size() || ticks <= 0) {
    return 2;
  }
  if (profiling) {
    auto repetitions = 0;
    const auto repetitions_text = std::string_view{argv[6]};
    const auto repetitions_result = std::from_chars(
        repetitions_text.data(),
        repetitions_text.data() + repetitions_text.size(), repetitions);
    if (repetitions_result.ec != std::errc{} ||
        repetitions_result.ptr !=
            repetitions_text.data() + repetitions_text.size() ||
        repetitions <= 0) {
      return 2;
    }
    return run_profile_repetitions(scenario, ticks, repetitions);
  }
  return samples ? write_samples(scenario, ticks)
                 : run_scenario(scenario, ticks);
}
