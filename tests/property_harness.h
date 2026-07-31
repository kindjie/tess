// Seeded operation-sequence testing with automatic shrinking
// (redesign section 3.4). A property runs a random sequence of
// operations against a model, checks every invariant after EVERY
// step, and on failure shrinks the sequence to a minimal failing
// case and prints a replay command.
//
// The value is in the shrink and the replay: a 200-step failure
// nobody can reproduce is a bug report nobody acts on. A three-step
// failure with a command that reproduces it is a fix.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "grid_map_generators.h"

namespace tess_test::property {

using tess_test::grid_benchmark::SplitMix64;

/// One invariant violation: which invariant, and what it saw.
struct Violation {
  std::string invariant;
  std::string detail;
  std::size_t step = 0;
};

/// A property over a model driven by integer-encoded operations.
///
/// `Model` must provide:
///   void apply(std::uint32_t op)                 — perform one step
///   auto check() const -> std::optional<Violation> — all invariants
/// and be default-constructible, so a shrink can replay from scratch.
template <typename Model>
class Property {
 public:
  Property(std::string name, std::uint32_t operation_count)
      : name_(std::move(name)), operation_count_(operation_count) {}

  /// Runs one seed. Returns the minimal failing sequence, if any.
  [[nodiscard]] auto run(std::uint64_t seed, std::size_t steps) const
      -> std::optional<std::vector<std::uint32_t>> {
    auto sequence = generate(seed, steps);
    if (!fails(sequence).has_value()) {
      return std::nullopt;
    }
    return shrink(std::move(sequence));
  }

  /// Replays an explicit sequence, for a pinned regression.
  [[nodiscard]] auto replay(const std::vector<std::uint32_t>& sequence) const
      -> std::optional<Violation> {
    return fails(sequence);
  }

  /// A command that reproduces a failing sequence.
  [[nodiscard]] auto replay_command(
      const std::vector<std::uint32_t>& sequence) const -> std::string {
    std::ostringstream out;
    out << "TESS_PROPERTY_REPLAY=\"";
    for (std::size_t i = 0; i < sequence.size(); ++i) {
      out << (i == 0 ? "" : ",") << sequence[i];
    }
    out << "\" ctest --test-dir build/dev -R " << name_;
    return out.str();
  }

  /// Human-readable failure report: the violation, the minimal
  /// sequence, and how to reproduce it.
  [[nodiscard]] auto report(const std::vector<std::uint32_t>& sequence) const
      -> std::string {
    const auto violation = fails(sequence);
    std::ostringstream out;
    out << "property " << name_ << " failed\n";
    if (violation.has_value()) {
      out << "  invariant: " << violation->invariant << "\n"
          << "  detail:    " << violation->detail << "\n"
          << "  at step:   " << violation->step << " of " << sequence.size()
          << "\n";
    }
    out << "  sequence:  [";
    for (std::size_t i = 0; i < sequence.size(); ++i) {
      out << (i == 0 ? "" : " ") << sequence[i];
    }
    out << "]\n  replay:    " << replay_command(sequence) << "\n";
    return out.str();
  }

 private:
  [[nodiscard]] auto generate(std::uint64_t seed, std::size_t steps) const
      -> std::vector<std::uint32_t> {
    SplitMix64 rng(seed);
    std::vector<std::uint32_t> sequence;
    sequence.reserve(steps);
    for (std::size_t i = 0; i < steps; ++i) {
      sequence.push_back(
          static_cast<std::uint32_t>(rng.below(operation_count_)));
    }
    return sequence;
  }

  [[nodiscard]] auto fails(const std::vector<std::uint32_t>& sequence) const
      -> std::optional<Violation> {
    Model model;
    for (std::size_t i = 0; i < sequence.size(); ++i) {
      model.apply(sequence[i]);
      auto violation = model.check();
      if (violation.has_value()) {
        violation->step = i;
        return violation;
      }
    }
    return std::nullopt;
  }

  // Delta-debugging: repeatedly try dropping a contiguous run of
  // operations and keep any shorter sequence that still fails. Halves
  // the chunk size when a pass finds nothing, down to single steps, so
  // the result is minimal with respect to single-operation removal.
  [[nodiscard]] auto shrink(std::vector<std::uint32_t> sequence) const
      -> std::vector<std::uint32_t> {
    auto chunk = sequence.size() / 2;
    while (chunk >= 1) {
      bool shrank = false;
      for (std::size_t start = 0; start + chunk <= sequence.size();) {
        auto candidate = sequence;
        candidate.erase(
            candidate.begin() + static_cast<std::ptrdiff_t>(start),
            candidate.begin() + static_cast<std::ptrdiff_t>(start + chunk));
        if (!candidate.empty() && fails(candidate).has_value()) {
          sequence = std::move(candidate);
          shrank = true;
        } else {
          start += chunk;
        }
      }
      if (!shrank) {
        chunk /= 2;
      }
    }
    return sequence;
  }

  std::string name_;
  std::uint32_t operation_count_;
};

/// Reads an explicit replay sequence from the environment, so a
/// printed replay command reproduces a failure without editing code.
[[nodiscard]] inline auto replay_from_environment()
    -> std::optional<std::vector<std::uint32_t>> {
  // MSVC deprecates getenv because the returned pointer aliases a
  // static buffer that a concurrent setenv can invalidate. The value is
  // copied into text below before anything else runs, and a test reads
  // its replay variable once at start-up, so the hazard does not apply.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
  const char* raw = std::getenv("TESS_PROPERTY_REPLAY");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
  if (raw == nullptr) {
    return std::nullopt;
  }
  std::vector<std::uint32_t> sequence;
  std::string text(raw);
  std::size_t start = 0;
  while (start <= text.size()) {
    const auto comma = text.find(',', start);
    const auto piece = text.substr(
        start, comma == std::string::npos ? std::string::npos : comma - start);
    if (!piece.empty()) {
      sequence.push_back(static_cast<std::uint32_t>(std::stoul(piece)));
    }
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
  return sequence;
}

}  // namespace tess_test::property
