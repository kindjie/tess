#include <benchmark/benchmark.h>
#include <tess/tess.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

void BM_spatial_local_coordination_1000x4(benchmark::State& state) {
  constexpr auto request_count = std::size_t{1000};
  constexpr auto options_per_request = std::size_t{4};
  auto requests = std::vector<tess::LocalMoveRequest>{};
  auto options = std::vector<tess::LocalMoveOption>{};
  requests.reserve(request_count);
  options.reserve(request_count * options_per_request);
  for (std::size_t i = 0; i < request_count; ++i) {
    requests.push_back(tess::LocalMoveRequest{
        .agent = static_cast<std::uint64_t>(request_count - i),
        .from = {static_cast<std::int64_t>(i), 0, 0},
        .priority = static_cast<std::uint32_t>(i % 4),
        .option_offset = options.size(),
        .option_count = options_per_request,
    });
    options.push_back(tess::LocalMoveOption{
        .to = {static_cast<std::int64_t>(i % 64), 1, 0}, .preference = 0});
    options.push_back(tess::LocalMoveOption{
        .to = {static_cast<std::int64_t>(i), 2, 0}, .preference = 1});
    options.push_back(tess::LocalMoveOption{
        .to = {static_cast<std::int64_t>(i), 3, 0}, .preference = 2});
    options.push_back(tess::LocalMoveOption{
        .to = {static_cast<std::int64_t>(i), 4, 0}, .preference = 3});
  }

  tess::LocalCoordinationScratch scratch;
  scratch.reserve(requests.size(), options.size());
  tess::LocalCoordinationResult result;
  for (auto _ : state) {
    result = tess::resolve_local_moves(
        requests, options, [](const auto&, const auto&) { return true; },
        scratch);
    benchmark::DoNotOptimize(result.decisions.data());
    benchmark::ClobberMemory();
  }
  if (result.status != tess::LocalCoordinationStatus::Complete ||
      result.reserved_count != request_count) {
    state.SkipWithError("local coordination did not reserve every request");
  }
  state.counters["requests"] = static_cast<double>(requests.size());
  state.counters["options"] = static_cast<double>(options.size());
}

BENCHMARK(BM_spatial_local_coordination_1000x4)
    ->Name("spatial/local_coordination_1000x4");

}  // namespace
