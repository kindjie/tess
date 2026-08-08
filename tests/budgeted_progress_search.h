// Capacity boundary search for the budgeted-progress benchmark suite
// (docs/planning/budgeted-progress-benchmarks.md, section 9.3).
//
// Flow-stability near the boundary is a noisy binary outcome, not a
// monotone property of load, so the search runs an explicit policy:
// majority-vote probes, geometric bracketing then linear refinement
// down to a terminal resolution, an independent confirmation pass at
// the candidate with step-down on failure, and a reported band whose
// edges cannot invert (the lowest unstable point *above* the
// confirmed-stable point; unstable observations below it are recorded
// as flapping, never as a band edge). Every tested point is retained.
//
// The algorithm is pure logic over probe/confirm callbacks so the
// deterministic tests can script monotone, flapping, and
// confirmation-failing boundaries without running benchmarks.
//
// Harness support only, never a public header.

#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace tess_test::budgeted {

enum class PointKind : std::uint8_t {
  Probe,
  Confirmation,
};

struct SearchPoint {
  std::uint64_t rate = 0;
  PointKind kind = PointKind::Probe;
  bool stable = false;
};

struct CapacityBand {
  std::optional<std::uint64_t> confirmed_stable;
  std::optional<std::uint64_t> lowest_unstable;
};

struct SearchResult {
  std::vector<SearchPoint> points;
  CapacityBand band;
  // Verdict inversions among probes: a stable probe above a rate that
  // probed unstable. Expected near a noisy boundary; reported, never
  // hidden.
  std::uint64_t flapping = 0;
};

struct SearchPolicy {
  std::uint64_t seed_rate = 60;
  std::uint64_t resolution_percent = 2;  // Terminal refinement width.
  std::uint32_t max_doublings = 24;
};

namespace detail {

[[nodiscard]] inline auto resolution_step(std::uint64_t rate,
                                          std::uint64_t percent)
    -> std::uint64_t {
  return std::max<std::uint64_t>(1, (rate * percent) / 100);
}

}  // namespace detail

// `probe(rate) -> bool` is the majority verdict of the probe
// repetitions; `confirm(rate) -> bool` the confirmation cell's
// majority verdict. Both must be deterministic functions of their
// inputs for a given run.
template <typename ProbeFn, typename ConfirmFn>
[[nodiscard]] auto search_capacity(const SearchPolicy& policy, ProbeFn&& probe,
                                   ConfirmFn&& confirm) -> SearchResult {
  SearchResult result;
  auto run_probe = [&](std::uint64_t rate) -> bool {
    const bool stable = probe(rate);
    result.points.push_back({rate, PointKind::Probe, stable});
    return stable;
  };

  // Geometric bracket: find a stable lower bound and an unstable
  // upper bound. A seed that probes unstable halves toward 1 first.
  std::uint64_t low = policy.seed_rate;
  bool low_stable = run_probe(low);
  while (!low_stable && low > 1) {
    low = std::max<std::uint64_t>(1, low / 2);
    low_stable = run_probe(low);
  }
  if (!low_stable) {
    // Nothing sustains even one event per simulation second: no
    // confirmed capacity; the lowest unstable point is what we saw.
    for (const SearchPoint& point : result.points) {
      if (!point.stable) {
        result.band.lowest_unstable =
            result.band.lowest_unstable.has_value()
                ? std::min(*result.band.lowest_unstable, point.rate)
                : point.rate;
      }
    }
    return result;
  }

  std::optional<std::uint64_t> high;
  std::uint64_t candidate = low;
  for (std::uint32_t i = 0; i < policy.max_doublings && !high.has_value();
       ++i) {
    const std::uint64_t next = candidate * 2;
    if (run_probe(next)) {
      candidate = next;
    } else {
      high = next;
    }
  }

  // Linear refinement (bisection) down to the terminal resolution.
  if (high.has_value()) {
    while (*high - candidate >
           detail::resolution_step(candidate, policy.resolution_percent)) {
      const std::uint64_t mid = candidate + (*high - candidate) / 2;
      if (run_probe(mid)) {
        candidate = mid;
      } else {
        high = mid;
      }
    }
  }

  // Confirmation with step-down: a failed confirmation records the
  // point as unstable and moves one resolution unit down; it never
  // re-runs at the same point hoping for a different answer.
  while (candidate >= 1) {
    const bool confirmed = confirm(candidate);
    result.points.push_back({candidate, PointKind::Confirmation, confirmed});
    if (confirmed) {
      result.band.confirmed_stable = candidate;
      break;
    }
    const std::uint64_t step =
        detail::resolution_step(candidate, policy.resolution_percent);
    if (candidate <= step) {
      break;
    }
    candidate -= step;
  }

  // Band edge: the lowest unstable observation strictly above the
  // confirmed-stable point (or the global lowest when nothing
  // confirmed). Unstable points at or below the confirmed rate are
  // flapping evidence, not an edge, so the band cannot invert.
  const std::uint64_t floor_rate = result.band.confirmed_stable.value_or(0);
  for (const SearchPoint& point : result.points) {
    if (point.stable || point.rate <= floor_rate) {
      continue;
    }
    result.band.lowest_unstable =
        result.band.lowest_unstable.has_value()
            ? std::min(*result.band.lowest_unstable, point.rate)
            : point.rate;
  }

  // Flapping: probe-verdict inversions across the tested set.
  for (const SearchPoint& outer : result.points) {
    if (outer.kind != PointKind::Probe || !outer.stable) {
      continue;
    }
    for (const SearchPoint& inner : result.points) {
      if (inner.kind == PointKind::Probe && !inner.stable &&
          inner.rate < outer.rate) {
        ++result.flapping;
        break;
      }
    }
  }
  return result;
}

}  // namespace tess_test::budgeted
