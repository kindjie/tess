// Summary derivation and versioned artifact emission for the
// budgeted-progress benchmark suite
// (docs/planning/budgeted-progress-benchmarks.md, sections 11-12).
//
// Percentiles are nearest-rank over explicitly named sample bases and
// are suppressed (emitted as null) below the section 11.4 minimum
// sample counts. The artifact is the suite-specific
// `tess.budgeted_progress.v1` JSON document, hand-assembled by string
// append with no JSON library (the repo convention set by
// tests/tess_counter_golden_probe.cc); inapplicable metric groups are
// omitted entirely, never emitted as zero. SHA-256 is included for the
// frozen-pool/trace identity the design mandates.
//
// Harness support only, never a public header.

#pragma once

#include <tess/diagnostics/diagnostics.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace tess_test::budgeted {

// Section 11.4 minimum sample counts before a percentile publishes.
inline constexpr std::uint64_t kMinSamplesP50 = 20;
inline constexpr std::uint64_t kMinSamplesP95 = 200;
inline constexpr std::uint64_t kMinSamplesP99 = 2'000;
inline constexpr std::uint64_t kMinSamplesP999 = 20'000;

struct PercentileValue {
  bool sufficient = false;
  std::uint64_t value = 0;
};

// One percentile family over one named sample base. `max` publishes
// whenever any sample exists; ranked percentiles publish only at their
// minimum counts.
struct PercentileFamily {
  std::string sample_base;
  std::uint64_t samples = 0;
  PercentileValue p50;
  PercentileValue p95;
  PercentileValue p99;
  PercentileValue p999;
  PercentileValue max;
};

// Nearest-rank percentile: the ceil(q * n)-th smallest, 1-indexed.
[[nodiscard]] inline auto nearest_rank(std::vector<std::uint64_t> sorted,
                                       std::uint64_t numerator,
                                       std::uint64_t denominator)
    -> std::uint64_t {
  const auto n = static_cast<std::uint64_t>(sorted.size());
  std::uint64_t rank = (n * numerator + denominator - 1) / denominator;
  rank = std::max<std::uint64_t>(rank, 1);
  return sorted[static_cast<std::size_t>(rank - 1)];
}

[[nodiscard]] inline auto summarize_family(std::string sample_base,
                                           std::vector<std::uint64_t> samples)
    -> PercentileFamily {
  PercentileFamily family;
  family.sample_base = std::move(sample_base);
  family.samples = static_cast<std::uint64_t>(samples.size());
  if (samples.empty()) {
    return family;
  }
  std::sort(samples.begin(), samples.end());
  family.max = {true, samples.back()};
  if (family.samples >= kMinSamplesP50) {
    family.p50 = {true, nearest_rank(samples, 50, 100)};
  }
  if (family.samples >= kMinSamplesP95) {
    family.p95 = {true, nearest_rank(samples, 95, 100)};
  }
  if (family.samples >= kMinSamplesP99) {
    family.p99 = {true, nearest_rank(samples, 99, 100)};
  }
  if (family.samples >= kMinSamplesP999) {
    family.p999 = {true, nearest_rank(samples, 999, 1000)};
  }
  return family;
}

// Section 11.1 observer-cost calibration, published in every artifact.
struct CalibrationBlock {
  std::string clock_identity;
  PercentileFamily clock_read_cost_ns;
  PercentileFamily empty_controller_loop_ns;
};

// Minimal SHA-256 (FIPS 180-4) for trace/pool identity. Deterministic,
// allocation-free, and verified against the standard test vectors in
// the harness test suite.
class Sha256 {
 public:
  void update(const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const unsigned char*>(data);
    total_bytes_ += size;
    while (size > 0) {
      const std::size_t take = std::min(size, std::size_t{64} - buffer_used_);
      std::memcpy(buffer_.data() + buffer_used_, bytes, take);
      buffer_used_ += take;
      bytes += take;
      size -= take;
      if (buffer_used_ == 64) {
        compress(buffer_.data());
        buffer_used_ = 0;
      }
    }
  }

  // Finalizes on first call; subsequent calls return the cached
  // digest rather than re-padding already-finalized state.
  [[nodiscard]] auto hex_digest() noexcept -> std::string {
    if (!digest_.empty()) {
      return digest_;
    }
    const std::uint64_t bit_length = total_bytes_ * 8;
    const unsigned char pad_byte = 0x80;
    update(&pad_byte, 1);
    const std::array<unsigned char, 64> zeros{};
    while (buffer_used_ != 56) {
      update(zeros.data(),
             buffer_used_ < 56 ? 56 - buffer_used_ : 64 - buffer_used_ + 56);
    }
    std::array<unsigned char, 8> length_bytes{};
    for (int i = 0; i < 8; ++i) {
      length_bytes[static_cast<std::size_t>(i)] =
          static_cast<unsigned char>(bit_length >> (56 - 8 * i));
    }
    update(length_bytes.data(), 8);
    digest_.reserve(64);
    for (const std::uint32_t word : state_) {
      std::array<char, 9> hex{};
      (void)std::snprintf(hex.data(), hex.size(), "%08x", word);
      digest_ += hex.data();
    }
    return digest_;
  }

 private:
  static constexpr std::array<std::uint32_t, 64> kRound = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
      0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
      0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
      0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
      0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
      0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
      0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
      0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
      0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

  [[nodiscard]] static constexpr auto rotr(std::uint32_t value,
                                           int amount) noexcept
      -> std::uint32_t {
    return (value >> amount) | (value << (32 - amount));
  }

  void compress(const unsigned char* block) noexcept {
    std::array<std::uint32_t, 64> w{};
    for (int i = 0; i < 16; ++i) {
      const auto base = static_cast<std::size_t>(i) * 4;
      w[static_cast<std::size_t>(i)] =
          (static_cast<std::uint32_t>(block[base]) << 24) |
          (static_cast<std::uint32_t>(block[base + 1]) << 16) |
          (static_cast<std::uint32_t>(block[base + 2]) << 8) |
          static_cast<std::uint32_t>(block[base + 3]);
    }
    for (std::size_t i = 16; i < 64; ++i) {
      const std::uint32_t s0 =
          rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const std::uint32_t s1 =
          rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    auto [a, b, c, d, e, f, g, h] = std::array<std::uint32_t, 8>{
        state_[0], state_[1], state_[2], state_[3],
        state_[4], state_[5], state_[6], state_[7]};
    for (std::size_t i = 0; i < 64; ++i) {
      const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const std::uint32_t ch = (e & f) ^ (~e & g);
      const std::uint32_t temp1 = h + s1 + ch + kRound[i] + w[i];
      const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t temp2 = s0 + maj;
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::array<std::uint32_t, 8> state_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372,
                                         0xa54ff53a, 0x510e527f, 0x9b05688c,
                                         0x1f83d9ab, 0x5be0cd19};
  std::array<unsigned char, 64> buffer_{};
  std::size_t buffer_used_ = 0;
  std::uint64_t total_bytes_ = 0;
  std::string digest_;
};

// --- Artifact document -------------------------------------------------

struct RunBlock {
  std::string commit;
  std::string machine_fingerprint;
  std::string compiler;
  std::string bench_flags;
};

struct ExperimentBlock {
  std::string kind;         // e.g. "isolated_saturated"
  std::string scenario_id;  // catalog-anchored cell identity
  std::vector<std::string> workload_refs;
  std::uint64_t seed = 0;
  std::uint32_t frame_hz_num = 60;
  std::uint32_t frame_hz_den = 1;
  std::uint32_t sim_tps = 20;
  std::string sim_speed = "1x";
  std::uint32_t max_ticks_per_frame = 8;
  std::string pacing = "unpaced";
  std::string budget_scope = "frame";
  std::uint64_t budget_ns = 0;
  std::uint64_t settlement_ticks = 0;
  // Arrival-rate cells only: events per simulation second as a
  // rational; zero num means not an arrival cell and the fields are
  // omitted from the artifact.
  std::uint64_t arrival_rate_num = 0;
  std::uint64_t arrival_rate_den = 1;
  std::string executor_kind = "serial";
  std::uint32_t executor_workers = 1;
};

struct TraceBlock {
  std::uint32_t version = 1;
  std::string sha256;
};

// Deadline/age/starvation metrics carried only by demand-limited
// cells; saturated artifacts omit this group entirely (section 12).
struct DeadlineGroup {
  double deadline_success_rate = 0.0;
  PercentileFamily lateness_ticks;
  PercentileFamily oldest_age_ticks;
  std::uint64_t starved_items = 0;
};

struct SummaryBlock {
  std::uint64_t measured_frames = 0;
  std::uint64_t repetitions = 0;
  std::uint64_t useful_completions = 0;
  std::uint64_t consumed_work_units = 0;
  double overshoot_frame_rate = 0.0;
  PercentileFamily frame_elapsed_ns;
  PercentileFamily overshoot_quantum_tail_ns;
  PercentileFamily overshoot_mandatory_ns;
  std::optional<PercentileFamily> frame_start_lag_ns;  // Paced only.
  std::optional<DeadlineGroup> deadlines;  // Demand-limited cells only.
  bool flow_stable_applicable = false;
  bool flow_stable = false;
  std::uint64_t peak_rss_bytes = 0;
  std::optional<std::string> correctness_hash;  // Deterministic runs only.
};

// One demand class's artifact record (design section 12 classes[]).
// Per-class flow attribution beyond these counts arrives with the
// mixed-colony stage; the block carries what the tracker derives
// today so multi-class results are representable, not discarded.
struct ClassArtifact {
  std::string class_id;
  std::uint64_t deadline_allowance_ticks = 0;
  std::uint64_t useful_completions = 0;
  std::uint64_t cohort_admitted = 0;
  double deadline_success_rate = 0.0;
  PercentileFamily lateness_ticks;
  std::uint64_t starved_items = 0;
};

struct Artifact {
  RunBlock run;
  ExperimentBlock experiment;
  TraceBlock trace;
  tess::diagnostics::FlowCounters flow;
  // Demand-limited cells carry one entry per demand class; saturated
  // cells leave this empty and the emitter omits the array.
  std::vector<ClassArtifact> classes;
  SummaryBlock summary;
  CalibrationBlock calibration;
};

// --- JSON emission -----------------------------------------------------

namespace detail {

inline void append_escaped(std::string& out, const std::string& text) {
  out += '"';
  for (const char character : text) {
    const auto code = static_cast<unsigned char>(character);
    if (code < 0x20) {
      std::array<char, 8> escape{};
      (void)std::snprintf(escape.data(), escape.size(), "\\u%04x", code);
      out += escape.data();
      continue;
    }
    if (character == '"' || character == '\\') {
      out += '\\';
    }
    out += character;
  }
  out += '"';
}

inline void append_u64(std::string& out, const char* key, std::uint64_t value,
                       bool trailing_comma = true) {
  out += '"';
  out += key;
  out += "\": ";
  out += std::to_string(value);
  if (trailing_comma) {
    out += ", ";
  }
}

inline void append_double(std::string& out, const char* key, double value,
                          bool trailing_comma = true) {
  std::array<char, 64> buffer{};
  (void)std::snprintf(buffer.data(), buffer.size(), "%.9g", value);
  out += '"';
  out += key;
  out += "\": ";
  out += buffer.data();
  if (trailing_comma) {
    out += ", ";
  }
}

inline void append_string(std::string& out, const char* key,
                          const std::string& value,
                          bool trailing_comma = true) {
  out += '"';
  out += key;
  out += "\": ";
  detail::append_escaped(out, value);
  if (trailing_comma) {
    out += ", ";
  }
}

inline void append_bool(std::string& out, const char* key, bool value,
                        bool trailing_comma = true) {
  out += '"';
  out += key;
  out += "\": ";
  out += value ? "true" : "false";
  if (trailing_comma) {
    out += ", ";
  }
}

inline void append_percentile(std::string& out, const char* key,
                              const PercentileValue& value,
                              bool trailing_comma = true) {
  out += '"';
  out += key;
  out += "\": ";
  out += value.sufficient ? std::to_string(value.value) : "null";
  if (trailing_comma) {
    out += ", ";
  }
}

inline void append_family(std::string& out, const char* key,
                          const PercentileFamily& family,
                          bool trailing_comma = true) {
  out += '"';
  out += key;
  out += "\": {";
  detail::append_string(out, "sample_base", family.sample_base);
  detail::append_u64(out, "samples", family.samples);
  detail::append_percentile(out, "p50", family.p50);
  detail::append_percentile(out, "p95", family.p95);
  detail::append_percentile(out, "p99", family.p99);
  detail::append_percentile(out, "p999", family.p999);
  detail::append_percentile(out, "max", family.max, false);
  out += '}';
  if (trailing_comma) {
    out += ", ";
  }
}

}  // namespace detail

// Serializes the versioned v1 artifact. Additive fields may remain v1;
// semantic changes require v2 (design section 12).
[[nodiscard]] inline auto emit_artifact_json(const Artifact& artifact)
    -> std::string {
  std::string out;
  out.reserve(4096);
  out += "{";
  detail::append_string(out, "schema", "tess.budgeted_progress.v1");
  detail::append_u64(out, "suite_version", 1);

  out += "\"run\": {";
  detail::append_string(out, "commit", artifact.run.commit);
  detail::append_string(out, "machine_fingerprint",
                        artifact.run.machine_fingerprint);
  detail::append_string(out, "compiler", artifact.run.compiler);
  detail::append_string(out, "bench_flags", artifact.run.bench_flags, false);
  out += "}, ";

  const auto& experiment = artifact.experiment;
  out += "\"experiment\": {";
  detail::append_string(out, "kind", experiment.kind);
  detail::append_string(out, "scenario_id", experiment.scenario_id);
  out += "\"workload_refs\": [";
  for (std::size_t i = 0; i < experiment.workload_refs.size(); ++i) {
    detail::append_escaped(out, experiment.workload_refs[i]);
    if (i + 1 < experiment.workload_refs.size()) {
      out += ", ";
    }
  }
  out += "], ";
  detail::append_u64(out, "seed", experiment.seed);
  detail::append_u64(out, "frame_hz_num", experiment.frame_hz_num);
  detail::append_u64(out, "frame_hz_den", experiment.frame_hz_den);
  detail::append_u64(out, "sim_tps", experiment.sim_tps);
  detail::append_string(out, "sim_speed", experiment.sim_speed);
  detail::append_u64(out, "max_ticks_per_frame",
                     experiment.max_ticks_per_frame);
  detail::append_string(out, "pacing", experiment.pacing);
  detail::append_string(out, "budget_scope", experiment.budget_scope);
  detail::append_u64(out, "budget_ns", experiment.budget_ns);
  detail::append_u64(out, "settlement_ticks", experiment.settlement_ticks);
  if (experiment.arrival_rate_num > 0) {
    detail::append_u64(out, "arrival_rate_num", experiment.arrival_rate_num);
    detail::append_u64(out, "arrival_rate_den", experiment.arrival_rate_den);
  }
  out += "\"executor\": {";
  detail::append_string(out, "kind", experiment.executor_kind);
  detail::append_u64(out, "workers", experiment.executor_workers, false);
  out += "}}, ";

  out += "\"trace\": {";
  detail::append_u64(out, "version", artifact.trace.version);
  detail::append_string(out, "sha256", artifact.trace.sha256, false);
  out += "}, ";

  const auto& flow = artifact.flow;
  out += "\"flow\": {";
  detail::append_u64(out, "offered", flow.offered);
  detail::append_u64(out, "admitted", flow.admitted);
  detail::append_u64(out, "rejected", flow.rejected);
  detail::append_u64(out, "coalesced_into_pending",
                     flow.coalesced_into_pending);
  detail::append_u64(out, "completed", flow.completed);
  detail::append_u64(out, "cancelled", flow.cancelled);
  detail::append_u64(out, "superseded", flow.superseded);
  detail::append_u64(out, "stale", flow.stale);
  detail::append_u64(out, "failed", flow.failed);
  detail::append_u64(out, "dropped_after_admission",
                     flow.dropped_after_admission);
  detail::append_u64(out, "offered_work_units", flow.offered_work_units);
  detail::append_u64(out, "consumed_work_units", flow.consumed_work_units);
  detail::append_u64(out, "outstanding_current", flow.outstanding_current);
  detail::append_u64(out, "outstanding_high_water",
                     flow.outstanding_high_water);
  detail::append_u64(out, "inventory_tick_weighted",
                     flow.inventory_tick_weighted);
  detail::append_u64(out, "residence_ticks_accumulated",
                     flow.residence_ticks_accumulated);
  detail::append_u64(out, "oldest_outstanding_age_ticks",
                     flow.oldest_outstanding_age_ticks);
  detail::append_bool(out, "admission_identity_ok",
                      flow.admission_identity_holds());
  detail::append_bool(out, "retention_identity_ok",
                      flow.retention_identity_holds(), false);
  out += "}, ";

  if (!artifact.classes.empty()) {
    out += "\"classes\": [";
    for (std::size_t i = 0; i < artifact.classes.size(); ++i) {
      const ClassArtifact& entry = artifact.classes[i];
      out += "{";
      detail::append_string(out, "class_id", entry.class_id);
      detail::append_u64(out, "deadline_allowance_ticks",
                         entry.deadline_allowance_ticks);
      detail::append_u64(out, "useful_completions", entry.useful_completions);
      detail::append_u64(out, "cohort_admitted", entry.cohort_admitted);
      detail::append_double(out, "deadline_success_rate",
                            entry.deadline_success_rate);
      detail::append_family(out, "lateness_ticks", entry.lateness_ticks);
      detail::append_u64(out, "starved_items", entry.starved_items, false);
      out += "}";
      if (i + 1 < artifact.classes.size()) {
        out += ", ";
      }
    }
    out += "], ";
  }

  const auto& summary = artifact.summary;
  out += "\"summary\": {";
  detail::append_u64(out, "measured_frames", summary.measured_frames);
  detail::append_u64(out, "repetitions", summary.repetitions);
  detail::append_u64(out, "useful_completions", summary.useful_completions);
  detail::append_u64(out, "consumed_work_units", summary.consumed_work_units);
  detail::append_double(out, "overshoot_frame_rate",
                        summary.overshoot_frame_rate);
  detail::append_family(out, "frame_elapsed_ns", summary.frame_elapsed_ns);
  detail::append_family(out, "overshoot_quantum_tail_ns",
                        summary.overshoot_quantum_tail_ns);
  detail::append_family(out, "overshoot_mandatory_ns",
                        summary.overshoot_mandatory_ns);
  if (summary.frame_start_lag_ns.has_value()) {
    detail::append_family(out, "frame_start_lag_ns",
                          *summary.frame_start_lag_ns);
  }
  if (summary.deadlines.has_value()) {
    const auto& deadlines = *summary.deadlines;
    detail::append_double(out, "deadline_success_rate",
                          deadlines.deadline_success_rate);
    detail::append_family(out, "lateness_ticks", deadlines.lateness_ticks);
    detail::append_family(out, "oldest_age_ticks", deadlines.oldest_age_ticks);
    detail::append_u64(out, "starved_items", deadlines.starved_items);
  }
  if (summary.flow_stable_applicable) {
    detail::append_bool(out, "flow_stable", summary.flow_stable);
  }
  out += "\"capacity_band\": null, ";
  detail::append_u64(out, "peak_rss_bytes", summary.peak_rss_bytes);
  out += "\"correctness_hash\": ";
  if (summary.correctness_hash.has_value()) {
    detail::append_escaped(out, *summary.correctness_hash);
  } else {
    out += "null";
  }
  out += "}, ";

  const auto& calibration = artifact.calibration;
  out += "\"calibration\": {";
  detail::append_string(out, "clock_identity", calibration.clock_identity);
  detail::append_family(out, "clock_read_cost_ns",
                        calibration.clock_read_cost_ns);
  detail::append_family(out, "empty_controller_loop_ns",
                        calibration.empty_controller_loop_ns, false);
  out += "}}";
  return out;
}

}  // namespace tess_test::budgeted
