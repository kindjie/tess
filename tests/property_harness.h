// Seeded operation-sequence testing with automatic shrinking
// (redesign section 3.4). A property runs a random sequence of
// operations against a model, checks every invariant after EVERY
// step, and on failure shrinks the sequence to a short failing case
// (see `shrink` for exactly how short) and prints a replay command.
//
// The value is in the shrink and the replay: a 200-step failure
// nobody can reproduce is a bug report nobody acts on. A three-step
// failure with a command that reproduces it is a fix.
#pragma once

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
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
///
/// The model must also be deterministic and must not throw: shrinking
/// replays a sequence many times and keeps a candidate only when it
/// reproduces the same violation, which is meaningless if a replay of
/// the same operations can behave differently.
template <typename Model>
class Property {
 public:
  Property(std::string name, std::uint32_t operation_count)
      : name_(std::move(name)), operation_count_(operation_count) {}

  /// Runs one seed. Returns a shrunk failing sequence, if any: one no
  /// single operation can be removed from (see `shrink`).
  [[nodiscard]] auto run(std::uint64_t seed, std::size_t steps) const
      -> std::optional<std::vector<std::uint32_t>> {
    auto sequence = generate(seed, steps);
    const auto violation = fails(sequence);
    if (!violation.has_value()) {
      return std::nullopt;
    }
    return shrink(std::move(sequence), violation->invariant);
  }

  /// The sequence a seed produces, so a test can assert what the sweep
  /// actually exercises rather than assuming it.
  [[nodiscard]] auto sequence_for(std::uint64_t seed, std::size_t steps) const
      -> std::vector<std::uint32_t> {
    return generate(seed, steps);
  }

  /// Replays an explicit sequence, for a pinned regression.
  [[nodiscard]] auto replay(const std::vector<std::uint32_t>& sequence) const
      -> std::optional<Violation> {
    return fails(sequence);
  }

  /// A command that reproduces a failing sequence, to be run from the
  /// build directory that produced the failure. It deliberately does
  /// not name a build directory: a failure found under dev-werror,
  /// ASan or Release does not reproduce against a `build/dev` binary,
  /// and a command that silently replays the wrong build is worse than
  /// no command at all.
  [[nodiscard]] auto replay_command(
      const std::vector<std::uint32_t>& sequence) const -> std::string {
    std::ostringstream out;
    out << "TESS_PROPERTY_REPLAY=" << format_sequence(sequence)
        << " ctest -R '^" << regex_escape(name_) << "$' --output-on-failure";
    return out.str();
  }

  /// The sequence as the replay variable spells it.
  [[nodiscard]] static auto format_sequence(
      const std::vector<std::uint32_t>& sequence) -> std::string {
    std::ostringstream out;
    for (std::size_t i = 0; i < sequence.size(); ++i) {
      out << (i == 0 ? "" : ",") << sequence[i];
    }
    return out.str();
  }

  /// Human-readable failure report: the violation, the shrunk
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
    out << "]\n  replay:    " << replay_command(sequence)
        << "\n             (POSIX shell, from the build directory that "
           "produced this failure)\n";
    return out.str();
  }

 private:
  /// Escapes the regex metacharacters a test name can contain, so an
  /// anchored `-R` selects this test and not a lookalike.
  [[nodiscard]] static auto regex_escape(std::string_view text) -> std::string {
    std::string escaped;
    for (const char c : text) {
      if (std::string_view(R"(.[]{}()*+?^$|\)").find(c) !=
          std::string_view::npos) {
        escaped.push_back('\\');
      }
      escaped.push_back(c);
    }
    return escaped;
  }

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
  // operations and keep any shorter sequence that still fails the SAME
  // invariant. Halves the chunk size when a pass finds nothing, down to
  // single steps.
  //
  // The result is 1-minimal — no single operation can be removed from
  // it — which is not the same as globally minimum. Only chunk-aligned
  // runs are considered, so a shorter failing subsequence that straddles
  // an alignment boundary can survive. 1-minimal is what makes a report
  // readable, and claiming more would be a lie.
  //
  // Requiring the same invariant matters: without it a shrink can drift
  // onto a different, easier violation and report a sequence that never
  // demonstrates the failure that was actually found.
  [[nodiscard]] auto shrink(std::vector<std::uint32_t> sequence,
                            const std::string& invariant) const
      -> std::vector<std::uint32_t> {
    auto chunk = sequence.size() / 2;
    while (chunk >= 1) {
      bool shrank = false;
      for (std::size_t start = 0; start + chunk <= sequence.size();) {
        auto candidate = sequence;
        candidate.erase(
            candidate.begin() + static_cast<std::ptrdiff_t>(start),
            candidate.begin() + static_cast<std::ptrdiff_t>(start + chunk));
        const auto violation =
            candidate.empty() ? std::optional<Violation>{} : fails(candidate);
        if (violation.has_value() && violation->invariant == invariant) {
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

/// The outcome of reading an explicit replay request.
///
/// `present` and `error` are separate on purpose. A replay request that
/// cannot be parsed must fail loudly: folding it into "no request" would
/// silently fall back to the seed sweep and report a pass for a run the
/// operator believed was replaying a specific failure.
struct ReplayRequest {
  bool present = false;
  std::vector<std::uint32_t> sequence;
  std::string error;
};

/// Parses a comma-separated replay sequence.
///
/// An empty or blank value counts as no request, so an exported but
/// unset variable still runs the sweep. Anything else must be a
/// complete list of decimal operations below `operation_count`.
[[nodiscard]] inline auto parse_replay_sequence(std::string_view text,
                                                std::uint32_t operation_count)
    -> ReplayRequest {
  ReplayRequest request;
  if (text.find_first_not_of(" \t\r\n") == std::string_view::npos) {
    return request;
  }
  request.present = true;

  const auto reject = [&request](std::string_view field,
                                 std::string_view why) -> ReplayRequest {
    std::ostringstream out;
    out << "TESS_PROPERTY_REPLAY is not a replayable sequence: field '" << field
        << "' " << why;
    request.error = out.str();
    request.sequence.clear();
    return request;
  };

  std::size_t start = 0;
  while (true) {
    const auto comma = text.find(',', start);
    const auto field = text.substr(start, comma == std::string_view::npos
                                              ? std::string_view::npos
                                              : comma - start);
    if (field.empty()) {
      return reject(field, "is empty");
    }
    // from_chars rather than stoul: it does not throw, ignores no
    // leading sign or whitespace, and reports where it stopped, so
    // trailing junk cannot be silently accepted as a valid operation.
    std::uint32_t value = 0;
    const auto* const first = field.data();
    const auto* const last = field.data() + field.size();
    const auto parsed = std::from_chars(first, last, value);
    if (parsed.ec == std::errc::result_out_of_range) {
      return reject(field, "does not fit an operation index");
    }
    if (parsed.ec != std::errc{} || parsed.ptr != last) {
      return reject(field, "is not a decimal number");
    }
    if (value >= operation_count) {
      std::ostringstream why;
      why << "is not below the operation count " << operation_count;
      return reject(field, why.str());
    }
    request.sequence.push_back(value);
    if (comma == std::string_view::npos) {
      break;
    }
    start = comma + 1;
  }
  return request;
}

/// Reads an explicit replay sequence from the environment, so a
/// printed replay command reproduces a failure without editing code.
[[nodiscard]] inline auto replay_from_environment(std::uint32_t operation_count)
    -> ReplayRequest {
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
    return ReplayRequest{};
  }
  // Copied before parsing: the pointer aliases environment storage.
  const std::string text(raw);
  return parse_replay_sequence(text, operation_count);
}

}  // namespace tess_test::property
