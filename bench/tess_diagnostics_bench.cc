#include <benchmark/benchmark.h>
#include <tess/tess.h>

#include <array>
#include <cstdint>
#include <string_view>

// This translation unit exercises the diagnostics primitives directly and so is
// only meaningful in the diagnostics-enabled bench binary. When compiled
// without the gate it is deliberately empty, mirroring the compile-down
// guarantee the benchmarks measure.
#if TESS_DIAGNOSTICS_ENABLED

namespace {

using tess::diagnostics::BufferedWarningSink;
using tess::diagnostics::ScopedTimer;
using tess::diagnostics::ScopedTrace;
using tess::diagnostics::TraceBuffer;
using tess::diagnostics::TraceCategory;
using tess::diagnostics::TraceRecord;
using tess::diagnostics::Warning;
using tess::diagnostics::WarningCategory;

constexpr std::string_view kBenchLabel = "bench";

// Cost of one structured trace record routed through the active buffer.
void diagnostics_trace_record(benchmark::State& state) {
  std::array<TraceRecord, 256> storage{};
  TraceBuffer buffer{storage};
  ScopedTrace scope{buffer};
  std::uint64_t index = 0;
  for (auto _ : state) {
    tess::diagnostics::trace_event(TraceCategory::Planner, kBenchLabel,
                                   index++);
    benchmark::ClobberMemory();
  }
}
BENCHMARK(diagnostics_trace_record)->Name("diagnostics/trace_record");

// Cost of folding one timing sample into a category accumulator.
void diagnostics_record_timing(benchmark::State& state) {
  std::array<TraceRecord, 1> storage{};
  TraceBuffer buffer{storage};
  std::uint64_t nanos = 1;
  for (auto _ : state) {
    buffer.record_timing(TraceCategory::Path, nanos++);
    // Force the accumulator to be materialized so the write is not elided.
    benchmark::DoNotOptimize(buffer.stats(TraceCategory::Path).total_ns);
  }
}
BENCHMARK(diagnostics_record_timing)->Name("diagnostics/record_timing");

// Full ScopedTimer lifecycle: two steady_clock reads plus the record and
// timing-accumulator writes on destruction.
void diagnostics_scoped_timer(benchmark::State& state) {
  std::array<TraceRecord, 256> storage{};
  TraceBuffer buffer{storage};
  ScopedTrace scope{buffer};
  for (auto _ : state) {
    ScopedTimer timer{TraceCategory::Path, kBenchLabel};
    benchmark::DoNotOptimize(timer);
  }
}
BENCHMARK(diagnostics_scoped_timer)->Name("diagnostics/scoped_timer");

// Cost of pushing one warning into a fixed-capacity ring sink.
void diagnostics_warning_sink(benchmark::State& state) {
  BufferedWarningSink<256> sink;
  std::uint64_t index = 0;
  for (auto _ : state) {
    sink.warn(Warning{WarningCategory::General, kBenchLabel, index++});
    benchmark::ClobberMemory();
  }
}
BENCHMARK(diagnostics_warning_sink)->Name("diagnostics/warning_sink");

}  // namespace

#endif  // TESS_DIAGNOSTICS_ENABLED
